/*
 * Grok voice remote for the Waveshare ESP32-S3-Touch-AMOLED-2.16.
 *
 * Push-to-talk audio goes to the Mac backend over TLS WebSocket. The Mac turns
 * speech into text, the board shows it for confirmation, and only a confirmed
 * command is handed to Grok Bot by the Mac. Protocol: docs/PROTOCOL.md (v2).
 */
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/usb_serial_jtag.h"
#include "esp_codec_dev.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
#include "lwip/sockets.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "cJSON.h"
#include "bsp/esp-bsp.h"
#include "ui.h"

#define SAMPLES 320              // 20 ms at 16 kHz
#define MAX_RECORD_US 29000000
#define CA_MAX 4000
static const char *TAG = "remote";

typedef enum { PHASE_IDLE, PHASE_WAIT_LISTEN, PHASE_LISTENING, PHASE_BUSY } phase_t;
typedef enum { TONE_START, TONE_DONE, TONE_ERROR } tone_t;

static esp_websocket_client_handle_t ws;
static esp_codec_dev_handle_t mic, speaker;
static atomic_int phase = PHASE_IDLE;
static _Atomic int64_t phase_since;  // esp_timer time of the last phase change
static atomic_bool connected, network_started, mic_diagnostic, tone_diagnostic;
static QueueHandle_t actions, tones;
static SemaphoreHandle_t ui_mutex;
#define WIFI_MAX 3
#define DISCOVERY_PORT 8444
static char ssid[WIFI_MAX][33], password[WIFI_MAX][65];
static int wifi_count, wifi_index;
static char uri[256], token[129], headers[180], ca_pem[CA_MAX + 1];
static SemaphoreHandle_t ws_mutex;

static void set_phase(phase_t p)
{
    atomic_store(&phase, p);
    atomic_store(&phase_since, esp_timer_get_time());
}

static phase_t swap_phase(phase_t p)
{
    atomic_store(&phase_since, esp_timer_get_time());
    return (phase_t)atomic_exchange(&phase, p);
}

/* ----------------------------------------------------- LVGL memory -- */

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM
/*
 * All LVGL objects, styles and draw layers live in PSRAM. Internal RAM is kept
 * for Wi-Fi and for the display driver's DMA copies; when it ran out, panel
 * transfers failed with ESP_ERR_NO_MEM and the screen was only partly drawn.
 */
#define LV_CAPS (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }
void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, LV_CAPS);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}
void *lv_realloc_core(void *p, size_t size)
{
    void *r = heap_caps_realloc(p, size, LV_CAPS);
    return r ? r : heap_caps_realloc(p, size, MALLOC_CAP_8BIT);
}
void lv_free_core(void *p) { heap_caps_free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t *mon) { (void)mon; }
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
#endif

/* ------------------------------------------------------------ UI port -- */

static void ui_lock(void) { xSemaphoreTake(ui_mutex, portMAX_DELAY); }
static void ui_unlock(void) { xSemaphoreGive(ui_mutex); }
static void ui_brightness(int percent) { bsp_display_brightness_set(percent); }
static void ui_action(ui_action_t action)
{
    uint8_t value = (uint8_t)action;
    xQueueSend(actions, &value, 0);
}

static void play(tone_t tone)
{
    uint8_t value = (uint8_t)tone;
    xQueueSend(tones, &value, 0);
}

/* ------------------------------------------------------------ backend -- */

static bool send_json(const char *data)
{
    if (!atomic_load(&connected)) return false;
    return esp_websocket_client_send_text(ws, data, strlen(data), pdMS_TO_TICKS(2000)) >= 0;
}

static void go_idle_screen_if_busy(void)
{
    if (swap_phase(PHASE_IDLE) != PHASE_IDLE) ui_show(UI_SCREEN_IDLE);
}

static void apply_history(cJSON *json)
{
    static ui_history_item_t items[UI_HISTORY_SHOW];
    cJSON *list = cJSON_GetObjectItem(json, "items");
    cJSON *total = cJSON_GetObjectItem(json, "total");
    int count = 0;
    cJSON *item;
    cJSON_ArrayForEach(item, list) {
        if (count == UI_HISTORY_SHOW) break;
        cJSON *t = cJSON_GetObjectItem(item, "time"), *x = cJSON_GetObjectItem(item, "text");
        if (!cJSON_IsString(t) || !cJSON_IsString(x)) continue;
        snprintf(items[count].time, sizeof(items[count].time), "%s", t->valuestring);
        snprintf(items[count].text, sizeof(items[count].text), "%s", x->valuestring);
        items[count].ok = cJSON_IsTrue(cJSON_GetObjectItem(item, "ok"));
        ++count;
    }
    ui_set_history(items, count, cJSON_IsNumber(total) ? total->valueint : count);
}

static void received(const char *data, size_t len)
{
    cJSON *json = cJSON_ParseWithLength(data, len);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return; }
    const cJSON *type = cJSON_GetObjectItem(json, "type");
    const char *t = cJSON_IsString(type) ? type->valuestring : "";
    const cJSON *value = cJSON_GetObjectItem(json, "value");
    const cJSON *text = cJSON_GetObjectItem(json, "text");
    const char *v = cJSON_IsString(value) ? value->valuestring : "";
    if (!strcmp(t, "state")) {
        if (!strcmp(v, "listening")) {
            int expected = PHASE_WAIT_LISTEN;
            if (atomic_compare_exchange_strong(&phase, &expected, PHASE_LISTENING))
                atomic_store(&phase_since, esp_timer_get_time());
        } else if (!strcmp(v, "idle")) {
            go_idle_screen_if_busy();
        } else if (!strcmp(v, "transcribing")) {
            set_phase(PHASE_BUSY);
            ui_show(UI_SCREEN_THINK);
        } else if (!strcmp(v, "sending")) {
            set_phase(PHASE_BUSY);
            ui_show(UI_SCREEN_SENDING);
        }
    } else if (!strcmp(t, "confirm") && cJSON_IsString(text)) {
        set_phase(PHASE_BUSY);
        ui_show_confirm(text->valuestring);
    } else if (!strcmp(t, "sent")) {
        set_phase(PHASE_IDLE);
        ui_show_done(cJSON_IsString(text) ? text->valuestring : "");
        play(TONE_DONE);
    } else if (!strcmp(t, "error")) {
        const cJSON *reason = cJSON_GetObjectItem(json, "reason");
        const char *r = cJSON_IsString(reason) ? reason->valuestring : "";
        set_phase(PHASE_IDLE);
        ui_show_error(!strcmp(r, "unclear") ? UI_ERR_UNCLEAR :
                      !strcmp(r, "not_configured") ? UI_ERR_SETUP :
                      !strcmp(r, "stt_unavailable") ? UI_ERR_STT : UI_ERR_SEND);
        play(TONE_ERROR);
    } else if (!strcmp(t, "history")) {
        apply_history(json);
    }
    cJSON_Delete(json);
}

static void websocket_event(void *arg, esp_event_base_t base, int32_t id, void *event)
{
    (void)arg; (void)base;
    static char message[4096];
    static size_t used, total;
    esp_websocket_event_data_t *data = event;
    if (id == WEBSOCKET_EVENT_CONNECTED) {
        used = total = 0;
        atomic_store(&connected, true);
        ui_set_link(true);
        ESP_LOGI(TAG, "Backend TLS connected");
    } else if (id == WEBSOCKET_EVENT_DISCONNECTED || id == WEBSOCKET_EVENT_ERROR) {
        bool was = atomic_exchange(&connected, false);
        used = total = 0;
        if (was) ESP_LOGW(TAG, "Backend disconnected");
        ui_set_link(false);
        if (swap_phase(PHASE_IDLE) != PHASE_IDLE) {
            ui_show_error(UI_ERR_OFFLINE);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
        }
    } else if (id == WEBSOCKET_EVENT_DATA) {
        // Text only; continuation frames (op 0) extend the current message.
        if (data->op_code != 1 && data->op_code != 0) return;
        if (data->payload_offset == 0 && data->op_code == 1) used = total = 0;
        if (data->payload_offset == 0) total = used + data->payload_len;
        if (data->data_len < 0 || total >= sizeof(message) || used + data->data_len >= sizeof(message)) {
            used = total = 0;
            return;
        }
        memcpy(message + used, data->data_ptr, data->data_len);
        used += data->data_len;
        if (data->fin && used == total) {
            message[used] = 0;
            received(message, used);
            used = total = 0;
        }
    }
}

/* ------------------------------------------------------------- actions -- */

static void start_listening(int64_t *since)
{
    if (!atomic_load(&connected)) {
        ui_show_error(UI_ERR_OFFLINE);
        play(TONE_ERROR);
        return;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    set_phase(PHASE_WAIT_LISTEN);
    ui_show(UI_SCREEN_LISTEN);
    play(TONE_START);
    send_json("{\"type\":\"listen.start\"}");
    *since = esp_timer_get_time();
}

static void stop_listening(void)
{
    set_phase(PHASE_BUSY);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ui_show(UI_SCREEN_THINK);
    if (!send_json("{\"type\":\"listen.stop\"}")) {
        set_phase(PHASE_IDLE);
        ui_show_error(UI_ERR_OFFLINE);
    }
}

static void handle_action(ui_action_t action, int64_t *since)
{
    int p = atomic_load(&phase);
    switch (action) {
    case UI_ACT_TAP:
        if (p == PHASE_WAIT_LISTEN || p == PHASE_LISTENING) stop_listening();
        else if (p == PHASE_IDLE) start_listening(since);
        break;
    case UI_ACT_CANCEL:
        if (p == PHASE_WAIT_LISTEN || p == PHASE_LISTENING) {
            send_json("{\"type\":\"listen.cancel\"}");
            set_phase(PHASE_IDLE);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
            ui_show(UI_SCREEN_IDLE);
        }
        break;
    case UI_ACT_SEND:
    case UI_ACT_RETRY:
        if (send_json(action == UI_ACT_SEND ? "{\"type\":\"confirm.send\"}" : "{\"type\":\"retry\"}")) {
            set_phase(PHASE_BUSY);
            ui_show(UI_SCREEN_SENDING);
        } else {
            set_phase(PHASE_IDLE);
            ui_show_error(UI_ERR_OFFLINE);
        }
        break;
    case UI_ACT_REDO:
        set_phase(PHASE_IDLE);
        send_json("{\"type\":\"confirm.cancel\"}");
        start_listening(since);
        break;
    case UI_ACT_TIMEOUT:
        send_json("{\"type\":\"confirm.cancel\"}");
        set_phase(PHASE_IDLE);
        break;
    }
}

/* --------------------------------------------------------------- audio -- */

static void audio_task(void *arg)
{
    (void)arg;
    static int16_t stereo[SAMPLES * 2], mono[SAMPLES];
    int64_t since = 0;
    double energy = 0;
    unsigned diagnostic_frames = 0;
    while (true) {
        // Keep RX drained while idle so old audio is never sent at the next turn.
        if (esp_codec_dev_read(mic, stereo, sizeof(stereo)) != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        double frame = 0;
        // ES7210 stereo capture: use the first physical microphone.
        for (int i = 0; i < SAMPLES; ++i) {
            mono[i] = stereo[2 * i];
            frame += (double)mono[i] * mono[i];
        }
        if (atomic_load(&mic_diagnostic)) {
            energy += frame;
            if (++diagnostic_frames == 50) {
                ESP_LOGI(TAG, "SELFTEST microphone: 16000 samples, RMS=%.1f", sqrt(energy / 16000));
                energy = 0;
                diagnostic_frames = 0;
                atomic_store(&mic_diagnostic, false);
            }
        }
        uint8_t action;
        while (xQueueReceive(actions, &action, 0) == pdTRUE) handle_action((ui_action_t)action, &since);

        int p = atomic_load(&phase);
        int64_t waited = esp_timer_get_time() - atomic_load(&phase_since);
        if ((p == PHASE_BUSY && waited > 120000000) || (p == PHASE_WAIT_LISTEN && waited > 5000000)) {
            // The Mac never answered: don't leave the board stuck on a busy screen.
            set_phase(PHASE_IDLE);
            ui_show_error(UI_ERR_OFFLINE);
            continue;
        }
        if (p == PHASE_WAIT_LISTEN || p == PHASE_LISTENING) {
            ui_set_level((int)fmin(1000.0, sqrt(frame / SAMPLES) * 1000.0 / 2500.0));
            if (esp_timer_get_time() - since >= MAX_RECORD_US) {
                stop_listening();
            } else if (p == PHASE_LISTENING &&
                       esp_websocket_client_send_bin(ws, (const char *)mono, sizeof(mono), pdMS_TO_TICKS(2000)) < 0) {
                set_phase(PHASE_IDLE);
                ui_show_error(UI_ERR_OFFLINE);
            }
        }
    }
}

static void tone(int16_t *out, float hz, int ms, float gain)
{
    int frames = ms / 20;
    for (int f = 0; f < frames; ++f) {
        for (int i = 0; i < SAMPLES; ++i) {
            int n = f * SAMPLES + i, total = frames * SAMPLES;
            float env = fminf(1.0f, fminf(n / 160.0f, (total - n) / 480.0f));  // soft attack/release
            int16_t s = (int16_t)(gain * env * sinf(2 * 3.14159265f * hz * n / 16000));
            out[2 * i] = out[2 * i + 1] = s;
        }
        esp_codec_dev_write(speaker, out, SAMPLES * 4);
    }
}

static void tone_task(void *arg)
{
    (void)arg;
    static int16_t out[SAMPLES * 2];
    while (true) {
        uint8_t t;
        if (xQueueReceive(tones, &t, pdMS_TO_TICKS(100)) != pdTRUE) {
            if (atomic_exchange(&tone_diagnostic, false)) {
                tone(out, 660, 200, 2000);
                ESP_LOGI(TAG, "SELFTEST speaker: 200ms tone sent to codec");
            }
            continue;
        }
        if (t == TONE_START) tone(out, 880, 80, 1800);
        else if (t == TONE_DONE) { tone(out, 660, 80, 1800); tone(out, 990, 140, 1800); }
        else tone(out, 220, 200, 2200);
    }
}

/* ------------------------------------------------------------- battery -- */

#define AXP2101_ADDR 0x34
static i2c_master_dev_handle_t pmu;

static int pmu_read(uint8_t reg)
{
    uint8_t value;
    if (i2c_master_transmit_receive(pmu, &reg, 1, &value, 1, 50) != ESP_OK) return -1;
    return value;
}

static void pmu_write(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    i2c_master_transmit(pmu, buf, 2, 50);
}

static void pmu_update_bits(uint8_t reg, uint8_t mask, uint8_t bits)
{
    int v = pmu_read(reg);
    if (v >= 0) pmu_write(reg, (uint8_t)((v & ~mask) | bits));
}

/* ---------------------------------------------------------------- clock -- */

/*
 * PCF85063 real-time clock (0x51) keeps local time while the board is off or
 * away from Wi-Fi. Boot: system clock <- RTC. After network time: RTC <- clock.
 */
#define RTC_ADDR 0x51
static i2c_master_dev_handle_t rtc;

static int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0x0F); }
static uint8_t to_bcd(int v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void rtc_load(void)
{
    uint8_t reg = 0x04, r[7];
    if (i2c_master_transmit_receive(rtc, &reg, 1, r, sizeof(r), 100) != ESP_OK) return;
    if (r[0] & 0x80) {
        ESP_LOGW(TAG, "Clock chip lost its time; waiting for network time");
        return;
    }
    struct tm tm = {.tm_sec = from_bcd(r[0] & 0x7F), .tm_min = from_bcd(r[1] & 0x7F),
                    .tm_hour = from_bcd(r[2] & 0x3F), .tm_mday = from_bcd(r[3] & 0x3F),
                    .tm_mon = from_bcd(r[5] & 0x1F) - 1, .tm_year = from_bcd(r[6]) + 100, .tm_isdst = -1};
    if (tm.tm_year < 124 || time(NULL) > 1700000000) return;  // invalid, or network time already set
    struct timeval now = {.tv_sec = mktime(&tm)};
    settimeofday(&now, NULL);
    ESP_LOGI(TAG, "Clock set from the clock chip");
}

static void rtc_save(void)
{
    if (!rtc) return;
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    uint8_t reg = 0x00, control;
    if (i2c_master_transmit_receive(rtc, &reg, 1, &control, 1, 100) != ESP_OK) return;
    // Stop the oscillator while writing so all fields belong to one instant; 24-hour mode.
    uint8_t stop[2] = {0x00, (uint8_t)((control | 0x20) & ~0x02)};
    uint8_t calendar[8] = {0x04, to_bcd(tm.tm_sec), to_bcd(tm.tm_min), to_bcd(tm.tm_hour), to_bcd(tm.tm_mday),
                           (uint8_t)tm.tm_wday, to_bcd(tm.tm_mon + 1), to_bcd(tm.tm_year - 100)};
    uint8_t resume[2] = {0x00, (uint8_t)(control & ~(0x20 | 0x02))};
    i2c_master_transmit(rtc, stop, 2, 100);
    i2c_master_transmit(rtc, calendar, sizeof(calendar), 100);
    i2c_master_transmit(rtc, resume, 2, 100);
}

static void battery_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
    i2c_device_config_t rtc_cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = RTC_ADDR,
                                   .scl_speed_hz = 400000};
    if (bus && i2c_master_bus_add_device(bus, &rtc_cfg, &rtc) == ESP_OK) {
        uint8_t cap[2] = {0x00, 0x01};  // 12.5 pF crystal load, clock running, 24-hour
        if (i2c_master_transmit(rtc, cap, 2, 100) == ESP_OK) rtc_load();
        else rtc = NULL;
    }
    i2c_device_config_t cfg = {.dev_addr_length = I2C_ADDR_BIT_LEN_7, .device_address = AXP2101_ADDR,
                               .scl_speed_hz = 400000};
    if (!bus || i2c_master_bus_add_device(bus, &cfg, &pmu) != ESP_OK || pmu_read(0x00) < 0) {
        ESP_LOGW(TAG, "Power chip not found; battery level hidden");
        vTaskDelete(NULL);
    }
    pmu_update_bits(0x68, 0x01, 0x01);  // battery detection
    pmu_update_bits(0x30, 0x01, 0x01);  // battery voltage ADC
    pmu_update_bits(0x62, 0x1F, 0x0B);  // charge current 500 mA (1000 mAh cell)
    pmu_update_bits(0x16, 0x07, 0x03);  // USB input limit 1000 mA
    int log_ticks = 0;
    while (true) {
        int s1 = pmu_read(0x00), s2 = pmu_read(0x01);
        if (s1 >= 0 && s2 >= 0) {
            bool vbus = (s1 & 0x20) && !(s2 & 0x08);
            bool present = s1 & 0x08;
            int hi = pmu_read(0x34), lo = pmu_read(0x35);
            int mv = (hi >= 0 && lo >= 0) ? ((hi & 0x1F) << 8 | lo) : 0;
            if (mv < 2500) present = false;
            bool charging = present && ((s2 >> 5) & 0x07) == 0x01;
            int pct = present ? pmu_read(0xA4) : -1;
            ui_set_battery(pct > 100 ? 100 : pct, charging, vbus);
            static const char *const STATES[] = {"trickle", "precharge", "constant-current",
                                                 "constant-voltage", "full", "not-charging"};
            int state = s2 & 0x07;
            if (log_ticks % 6 == 0) {
                ESP_LOGI(TAG, "battery: %d%% %dmV usb=%d charge=%s", pct, mv, vbus,
                         state < 6 ? STATES[state] : "unknown");
            }
        }
        if (++log_ticks % 6 == 1) {
            ESP_LOGI(TAG, "heap: internal %u free, largest DMA block %u, PSRAM %u free",
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL),
                     (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* -------------------------------------------------------------- input -- */

static void button_task(void *arg)
{
    (void)arg;
    gpio_config_t cfg = {.pin_bit_mask = 1ULL << 18, .mode = GPIO_MODE_INPUT, .pull_up_en = GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&cfg));
    int last = 1, stable = 1, count = 0;
    while (true) {
        int now = gpio_get_level(18);
        count = now == last ? count + 1 : 0;
        if (count == 3 && now != stable) {
            stable = now;
            if (!now) ui_key();
        }
        last = now;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* ---------------------------------------------------------- settings -- */

static bool get_string(nvs_handle_t nvs, const char *key, char *buf, size_t size)
{
    return nvs_get_str(nvs, key, buf, &size) == ESP_OK;
}

static bool load_config(void)
{
    nvs_handle_t nvs;
    if (nvs_open("butler", NVS_READONLY, &nvs) != ESP_OK) return false;
    bool ok = get_string(nvs, "ssid", ssid[0], sizeof(ssid[0])) &&
              get_string(nvs, "password", password[0], sizeof(password[0])) &&
              get_string(nvs, "uri", uri, sizeof(uri)) && get_string(nvs, "token", token, sizeof(token)) &&
              get_string(nvs, "ca", ca_pem, sizeof(ca_pem));
    wifi_count = ok ? 1 : 0;
    // Optional extra networks (e.g. the phone's hotspot), tried in turn.
    for (int i = 1; ok && i < WIFI_MAX; ++i) {
        char ks[8], kp[12];
        snprintf(ks, sizeof(ks), "ssid%d", i + 1);
        snprintf(kp, sizeof(kp), "password%d", i + 1);
        if (get_string(nvs, ks, ssid[wifi_count], sizeof(ssid[0])) && ssid[wifi_count][0] &&
            get_string(nvs, kp, password[wifi_count], sizeof(password[0]))) {
            ++wifi_count;
        }
    }
    nvs_close(nvs);
    return ok;
}

static int provision(const char *line)
{
    static const char *keys[] = {"wifi_ssid", "wifi_password", "backend_uri", "device_token", "backend_ca"};
    static const char *nvs_keys[] = {"ssid", "password", "uri", "token", "ca"};
    static const size_t limits[] = {32, 64, 255, 128, CA_MAX};
    cJSON *json = cJSON_Parse(line);
    if (!cJSON_IsObject(json)) { cJSON_Delete(json); return 0; }
    if (cJSON_IsTrue(cJSON_GetObjectItem(json, "self_test")) && !atomic_load(&connected)) {
        atomic_store(&mic_diagnostic, true);
        atomic_store(&tone_diagnostic, true);
        cJSON_Delete(json);
        return 2;  // Diagnostic doesn't write settings or reboot.
    }
    const char *values[5];
    for (int i = 0; i < 5; ++i) {
        cJSON *value = cJSON_GetObjectItem(json, keys[i]);
        if (!cJSON_IsString(value) || strlen(value->valuestring) > limits[i]) { cJSON_Delete(json); return 0; }
        values[i] = value->valuestring;
    }
    bool ok = values[0][0] && !strncmp(values[2], "wss://", 6) && strlen(values[3]) >= 32 &&
              strstr(values[4], "-----BEGIN CERTIFICATE-----") == values[4];
    for (const char *p = values[3]; *p; ++p) if (*p < 33 || *p > 126) ok = false;
    nvs_handle_t nvs;
    if (ok && nvs_open("butler", NVS_READWRITE, &nvs) == ESP_OK) {
        for (int i = 0; i < 5; ++i) if (nvs_set_str(nvs, nvs_keys[i], values[i]) != ESP_OK) ok = false;
        for (int n = 2; ok && n <= WIFI_MAX; ++n) {
            char js[16], jp[20], ks[8], kp[12];
            snprintf(js, sizeof(js), "wifi%d_ssid", n);
            snprintf(jp, sizeof(jp), "wifi%d_password", n);
            snprintf(ks, sizeof(ks), "ssid%d", n);
            snprintf(kp, sizeof(kp), "password%d", n);
            cJSON *s = cJSON_GetObjectItem(json, js), *pw = cJSON_GetObjectItem(json, jp);
            if (cJSON_IsString(s) && s->valuestring[0] && strlen(s->valuestring) <= 32 &&
                cJSON_IsString(pw) && strlen(pw->valuestring) <= 64) {
                if (nvs_set_str(nvs, ks, s->valuestring) != ESP_OK || nvs_set_str(nvs, kp, pw->valuestring) != ESP_OK)
                    ok = false;
            } else {
                nvs_erase_key(nvs, ks);
                nvs_erase_key(nvs, kp);
            }
        }
        if (ok) ok = nvs_commit(nvs) == ESP_OK;
        nvs_close(nvs);
    } else {
        ok = false;
    }
    cJSON_Delete(json);
    return ok ? 1 : 0;
}

static void usb_task(void *arg)
{
    (void)arg;
    static char line[6144];
    size_t used = 0;
    while (true) {
        uint8_t byte;
        if (usb_serial_jtag_read_bytes(&byte, 1, pdMS_TO_TICKS(100)) <= 0) continue;
        if (byte == '\n') {
            line[used] = 0;
            int result = provision(line);
            if (result == 1) {
                ESP_LOGI(TAG, "PROVISIONED; restarting");
                memset(line, 0, sizeof(line));
                vTaskDelay(pdMS_TO_TICKS(200));
                esp_restart();
            } else if (result == 0) {
                ESP_LOGW(TAG, "Invalid configuration (contents redacted)");
            }
            memset(line, 0, sizeof(line));
            used = 0;
        } else if (byte != '\r') {
            if (used < sizeof(line) - 1) line[used++] = (char)byte;
            else { memset(line, 0, sizeof(line)); used = 0; }
        }
    }
}

/* ------------------------------------------------------------- network -- */

/*
 * The Mac backend broadcasts {"service":"grok-voice-remote","port":8443} on UDP
 * 8444 every few seconds. When the board is not connected and hears it from a
 * new address (e.g. on the phone hotspot), it reconnects there. The pinned
 * certificate still has to match, so a fake announcement gets nowhere.
 */
static void discovery_task(void *arg)
{
    (void)arg;
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    struct sockaddr_in local = {.sin_family = AF_INET, .sin_port = htons(DISCOVERY_PORT),
                                .sin_addr.s_addr = htonl(INADDR_ANY)};
    struct timeval timeout = {.tv_sec = 5};
    if (sock < 0 || bind(sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        ESP_LOGW(TAG, "Mac discovery unavailable");
        if (sock >= 0) close(sock);
        vTaskDelete(NULL);
    }
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    int64_t offline_since = esp_timer_get_time();
    char packet[128];
    while (true) {
        struct sockaddr_in from;
        socklen_t len = sizeof(from);
        int n = recvfrom(sock, packet, sizeof(packet) - 1, 0, (struct sockaddr *)&from, &len);
        if (atomic_load(&connected)) offline_since = esp_timer_get_time();
        if (n <= 0) continue;
        packet[n] = 0;
        cJSON *json = cJSON_Parse(packet);
        cJSON *service = cJSON_GetObjectItem(json, "service"), *port = cJSON_GetObjectItem(json, "port");
        if (cJSON_IsString(service) && !strcmp(service->valuestring, "grok-voice-remote") &&
            cJSON_IsNumber(port) && port->valueint > 0 && port->valueint < 65536 &&
            !atomic_load(&connected) && esp_timer_get_time() - offline_since > 3000000) {
            char host[16], found[64];
            inet_ntoa_r(from.sin_addr, host, sizeof(host));
            snprintf(found, sizeof(found), "wss://%s:%d/ws", host, port->valueint);
            xSemaphoreTake(ws_mutex, portMAX_DELAY);
            if (strcmp(found, uri)) {
                ESP_LOGI(TAG, "Mac found at %s", host);
                snprintf(uri, sizeof(uri), "%s", found);
                if (ws) {
                    esp_websocket_client_stop(ws);
                    esp_websocket_client_set_uri(ws, uri);
                    esp_websocket_client_start(ws);
                }
            }
            xSemaphoreGive(ws_mutex);
            offline_since = esp_timer_get_time();
        }
        cJSON_Delete(json);
    }
}

static void network_task(void *arg)
{
    (void)arg;
    // Certificate validity checks need a real clock; never disable TLS verification.
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    // TLS needs a real date: network time, or the clock chip's time if the
    // network (e.g. a hotspot without internet) can't provide it.
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(15000)) != ESP_OK) {
        if (time(NULL) > 1700000000) break;
        ESP_LOGW(TAG, "Waiting for network time");
    }
    if (esp_netif_sntp_sync_wait(0) == ESP_OK) rtc_save();
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
    xSemaphoreTake(ws_mutex, portMAX_DELAY);
    // The Mac's address changes between networks, so the certificate is pinned
    // instead of matched by name: only the Mac holding config/server.key passes.
    esp_websocket_client_config_t cfg = {.uri = uri, .headers = headers, .cert_pem = ca_pem,
        .skip_cert_common_name_check = true,
        .buffer_size = 2048, .reconnect_timeout_ms = 5000, .network_timeout_ms = 5000,
        .ping_interval_sec = 20};
    ws = esp_websocket_client_init(&cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, websocket_event, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(ws));
    xSemaphoreGive(ws_mutex);
    vTaskDelete(NULL);
}

static esp_timer_handle_t reconnect_timer;
static int reconnect_delay_s = 2;
static void use_network(int index)
{
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, ssid[index], strlen(ssid[index]));
    memcpy(wifi.sta.password, password[index], strlen(password[index]));
    esp_wifi_set_config(WIFI_IF_STA, &wifi);
}

static void reconnect(void *arg)
{
    (void)arg;
    use_network(wifi_index);
    esp_wifi_connect();
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "Wi-Fi: %d saved network(s)", wifi_count);
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        // Back off when the home network is out of reach: every attempt scans
        // all channels, which costs a lot of battery if repeated nonstop.
        // Try the next saved network; wait longer only after all of them failed.
        const wifi_event_sta_disconnected_t *info = data;
        ESP_LOGW(TAG, "Wi-Fi network %d of %d not joined (reason %d)", wifi_index + 1, wifi_count,
                 info ? info->reason : -1);
        ui_set_link(false);
        wifi_index = (wifi_index + 1) % wifi_count;
        int delay = wifi_index == 0 ? reconnect_delay_s : 1;
        if (wifi_index == 0) reconnect_delay_s = reconnect_delay_s >= 60 ? 120 : reconnect_delay_s * 2;
        esp_timer_start_once(reconnect_timer, (uint64_t)delay * 1000000);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        reconnect_delay_s = 2;
        ESP_LOGI(TAG, "Wi-Fi connected (saved network %d of %d)", wifi_index + 1, wifi_count);
        if (!atomic_exchange(&network_started, true)) {
            xTaskCreate(network_task, "network", 6144, NULL, 4, NULL);
            xTaskCreate(discovery_task, "discovery", 4096, NULL, 3, NULL);
        }
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    // Never log credentials or enable WebSocket debug headers.
    esp_log_level_set("transport_ws", ESP_LOG_NONE);
    esp_log_level_set("websocket_client", ESP_LOG_NONE);
    setenv("TZ", "HKT-8", 1);
    tzset();

    actions = xQueueCreate(8, 1);
    tones = xQueueCreate(4, 1);
    ui_mutex = xSemaphoreCreateMutex();
    ws_mutex = xSemaphoreCreateMutex();
    assert(actions && tones && ui_mutex && ws_mutex);
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 1024, .rx_buffer_size = 4096};
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb));

    lv_display_t *display = bsp_display_start();
    assert(display);
    bsp_display_brightness_set(80);
    static const ui_port_t port = {ui_action, ui_brightness, ui_lock, ui_unlock};
    bsp_display_lock((uint32_t)-1);
    ui_init(&port);
    bsp_display_unlock();

    xTaskCreate(usb_task, "usb_config", 4096, NULL, 3, NULL);
    xTaskCreate(button_task, "button", 2048, NULL, 3, NULL);
    xTaskCreate(battery_task, "battery", 3072, NULL, 2, NULL);
    mic = bsp_audio_codec_microphone_init();
    speaker = bsp_audio_codec_speaker_init();
    assert(mic && speaker);
    esp_codec_dev_sample_info_t format = {.sample_rate = 16000, .channel = 2, .bits_per_sample = 16};
    ESP_ERROR_CHECK(esp_codec_dev_open(speaker, &format));
    ESP_ERROR_CHECK(esp_codec_dev_open(mic, &format));
    ESP_ERROR_CHECK(esp_codec_dev_set_in_gain(mic, 24));
    ESP_ERROR_CHECK(esp_codec_dev_set_out_vol(speaker, 45));
    xTaskCreate(audio_task, "audio", 8192, NULL, 5, NULL);
    xTaskCreate(tone_task, "tone", 4096, NULL, 4, NULL);

    if (!load_config()) {
        ui_show(UI_SCREEN_SETUP);
        ESP_LOGI(TAG, "USB configuration required");
        return;
    }
    ui_show(UI_SCREEN_IDLE);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    const esp_timer_create_args_t retry = {.callback = reconnect, .name = "wifi_retry"};
    ESP_ERROR_CHECK(esp_timer_create(&retry, &reconnect_timer));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    use_network(0);
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
}
