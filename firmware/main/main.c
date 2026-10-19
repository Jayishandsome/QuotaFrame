/*
 * Grok voice remote for the Waveshare ESP32-S3-Touch-AMOLED-2.16.
 *
 * Push-to-talk audio goes to the Mac backend over TLS WebSocket. The Mac turns
 * speech into text, the board shows it for confirmation, and only a confirmed
 * command is handed to Grok Bot by the Mac. Protocol: docs/PROTOCOL.md (v2).
 */
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_websocket_client.h"
#include "esp_wifi.h"
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
static atomic_bool connected, network_started, mic_diagnostic, tone_diagnostic;
static QueueHandle_t actions, tones;
static SemaphoreHandle_t ui_mutex;
static char ssid[33], password[65], uri[256], token[129], headers[180], ca_pem[CA_MAX + 1];

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
    if (atomic_exchange(&phase, PHASE_IDLE) != PHASE_IDLE) ui_show(UI_SCREEN_IDLE);
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
            atomic_compare_exchange_strong(&phase, &expected, PHASE_LISTENING);
        } else if (!strcmp(v, "idle")) {
            go_idle_screen_if_busy();
        } else if (!strcmp(v, "transcribing")) {
            atomic_store(&phase, PHASE_BUSY);
            ui_show(UI_SCREEN_THINK);
        } else if (!strcmp(v, "sending")) {
            atomic_store(&phase, PHASE_BUSY);
            ui_show(UI_SCREEN_SENDING);
        }
    } else if (!strcmp(t, "confirm") && cJSON_IsString(text)) {
        atomic_store(&phase, PHASE_BUSY);
        ui_show_confirm(text->valuestring);
    } else if (!strcmp(t, "sent")) {
        atomic_store(&phase, PHASE_IDLE);
        ui_show_done(cJSON_IsString(text) ? text->valuestring : "");
        play(TONE_DONE);
    } else if (!strcmp(t, "error")) {
        const cJSON *reason = cJSON_GetObjectItem(json, "reason");
        const char *r = cJSON_IsString(reason) ? reason->valuestring : "";
        atomic_store(&phase, PHASE_IDLE);
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
        if (atomic_exchange(&phase, PHASE_IDLE) != PHASE_IDLE) {
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
    atomic_store(&phase, PHASE_WAIT_LISTEN);
    ui_show(UI_SCREEN_LISTEN);
    play(TONE_START);
    send_json("{\"type\":\"listen.start\"}");
    *since = esp_timer_get_time();
}

static void stop_listening(void)
{
    atomic_store(&phase, PHASE_BUSY);
    esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    ui_show(UI_SCREEN_THINK);
    if (!send_json("{\"type\":\"listen.stop\"}")) {
        atomic_store(&phase, PHASE_IDLE);
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
            atomic_store(&phase, PHASE_IDLE);
            esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
            ui_show(UI_SCREEN_IDLE);
        }
        break;
    case UI_ACT_SEND:
    case UI_ACT_RETRY:
        if (send_json(action == UI_ACT_SEND ? "{\"type\":\"confirm.send\"}" : "{\"type\":\"retry\"}")) {
            atomic_store(&phase, PHASE_BUSY);
            ui_show(UI_SCREEN_SENDING);
        } else {
            atomic_store(&phase, PHASE_IDLE);
            ui_show_error(UI_ERR_OFFLINE);
        }
        break;
    case UI_ACT_REDO:
        atomic_store(&phase, PHASE_IDLE);
        send_json("{\"type\":\"confirm.cancel\"}");
        start_listening(since);
        break;
    case UI_ACT_TIMEOUT:
        send_json("{\"type\":\"confirm.cancel\"}");
        atomic_store(&phase, PHASE_IDLE);
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
        if (p == PHASE_WAIT_LISTEN || p == PHASE_LISTENING) {
            ui_set_level((int)fmin(1000.0, sqrt(frame / SAMPLES) * 1000.0 / 2500.0));
            if (esp_timer_get_time() - since >= MAX_RECORD_US) {
                stop_listening();
            } else if (p == PHASE_LISTENING &&
                       esp_websocket_client_send_bin(ws, (const char *)mono, sizeof(mono), pdMS_TO_TICKS(2000)) < 0) {
                atomic_store(&phase, PHASE_IDLE);
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

static void battery_task(void *arg)
{
    (void)arg;
    i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
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
    bool ok = get_string(nvs, "ssid", ssid, sizeof(ssid)) && get_string(nvs, "password", password, sizeof(password)) &&
              get_string(nvs, "uri", uri, sizeof(uri)) && get_string(nvs, "token", token, sizeof(token)) &&
              get_string(nvs, "ca", ca_pem, sizeof(ca_pem));
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

static void network_task(void *arg)
{
    (void)arg;
    // Certificate validity checks need a real clock; never disable TLS verification.
    esp_sntp_config_t time_config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    ESP_ERROR_CHECK(esp_netif_sntp_init(&time_config));
    while (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(30000)) != ESP_OK) ESP_LOGW(TAG, "Waiting for network time");
    snprintf(headers, sizeof(headers), "Authorization: Bearer %s\r\n", token);
    esp_websocket_client_config_t cfg = {.uri = uri, .headers = headers, .cert_pem = ca_pem,
        .buffer_size = 2048, .reconnect_timeout_ms = 5000, .network_timeout_ms = 5000,
        .ping_interval_sec = 20};
    ws = esp_websocket_client_init(&cfg);
    ESP_ERROR_CHECK(esp_websocket_register_events(ws, WEBSOCKET_EVENT_ANY, websocket_event, NULL));
    ESP_ERROR_CHECK(esp_websocket_client_start(ws));
    vTaskDelete(NULL);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)data;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        ui_set_link(false);
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Wi-Fi connected");
        if (!atomic_exchange(&network_started, true)) xTaskCreate(network_task, "network", 6144, NULL, 4, NULL);
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
    assert(actions && tones && ui_mutex);
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
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event, NULL));
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, ssid, strlen(ssid));
    memcpy(wifi.sta.password, password, strlen(password));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_MIN_MODEM));
}
