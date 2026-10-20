#include "display_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "bsp/touch.h"
#include "display_transport.hpp"
#include "performance_probe.hpp"
#include "pet/pet_overview.h"
#include "usage_fonts/usage_fonts.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_lv_adapter.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "usage_panel_state/usage_bar_animation.hpp"
#include "usage_ota/presentation.hpp"

namespace usage_panel::amoled {
namespace {
constexpr char TAG[] = "amoled_ui";
// Unlit AMOLED pixels: black reads as the panel edge and draws no current.
constexpr uint32_t COLOR_BG     = 0x000000;
// True black keeps background pixels unlit during prolonged screensaver use.
constexpr uint32_t COLOR_SAVER_BG = 0x000000;
constexpr uint32_t COLOR_PANEL  = 0x111419;
constexpr uint32_t COLOR_BORDER = 0x1F232B;
constexpr uint32_t COLOR_LINE   = 0x14171D;
constexpr uint32_t COLOR_TRACK  = 0x1A1E26;
// The inner (weekly) ring sits on a darker track so the two rings separate.
constexpr uint32_t COLOR_TRACK_INNER = 0x14171D;
// Provider identity colours: rings, name dots and the navigation underline.
constexpr uint32_t COLOR_CODEX  = 0x7C9CFF;
constexpr uint32_t COLOR_CLAUDE = 0xE8875F;
constexpr uint32_t COLOR_TEXT   = 0xF2F4F7;
constexpr uint32_t COLOR_MUTED  = 0x8B93A1;
constexpr uint32_t COLOR_DIM    = 0x565D68;
constexpr uint32_t COLOR_RESET  = 0xC7CCD4;
constexpr uint32_t COLOR_GOOD   = 0x3ECF8E;
constexpr uint32_t COLOR_WARN   = 0xF5B84C;
constexpr uint32_t COLOR_BAD    = 0xF26D6D;
constexpr uint32_t COLOR_BATTERY_OFF = 0x2C313B;
constexpr uint32_t COLOR_LOW_DIM = 0x8A6A2E;
// Screensaver usage underline colours. A thin underline needs more contrast
// than the 120 px digits to stay legible at viewing distance, so the base fill
// matches COLOR_DIM (the dimmest reliably legible element) while the track is
// dimmer still to keep a visible reference length.
constexpr uint32_t COLOR_SAVER_TRACK = 0x2C313B;
constexpr uint32_t COLOR_SAVER_BASE  = COLOR_DIM;
constexpr uint32_t COLOR_SAVER_WARN  = 0x737B86;
constexpr uint32_t COLOR_SAVER_BAD   = COLOR_MUTED;
constexpr int MARGIN      = 24;
constexpr int CARD_W      = 432;
constexpr int CARD_PAD    = 20;
constexpr int CARD_RADIUS = 16;
constexpr int HEADER_H    = 50;
constexpr int BAR_H_DETAIL   = 16;
// Overview: one pixel-ring column per provider, centred in each half of the
// panel. Y values are relative to the content panel, which starts at CONTENT_Y.
constexpr int OV_COLUMN_CX[2] = {132, 348};
constexpr int OV_CELL         = 6;
constexpr int OV_RING_D       = 168;
constexpr int OV_RING_W       = 18;   // Three cells.
constexpr int OV_INNER_D      = 114;
constexpr int OV_INNER_W      = 12;   // Two cells.
constexpr int OV_RING_CY      = 98;
constexpr int OV_NAME_Y       = OV_RING_CY + OV_RING_D / 2 + 24;
constexpr int OV_RESET_Y      = OV_NAME_Y + 36;
constexpr int OV_RESET_IN_Y   = OV_RESET_Y + 20;
constexpr int OV_WEEK_Y       = OV_RESET_IN_Y + 28;
constexpr int OV_WEEK_BAR_Y   = OV_WEEK_Y + 14;
constexpr int OV_PACE_Y       = OV_WEEK_BAR_Y + 28;
constexpr int OV_ROW_HALF_W   = 84;   // Rows span the ring centre +/- this.
constexpr int OV_BAR_PITCH    = 8;
static_assert(DisplayUi::WEEK_BAR_CELLS * OV_BAR_PITCH <= 2 * OV_ROW_HALF_W);
// Detail: one large pixel ring, then three label/value/note columns.
constexpr int DT_CELL         = 8;
constexpr int DT_RING_D       = 224;
constexpr int DT_RING_W       = 24;   // Three cells.
constexpr int DT_INNER_D      = 160;
constexpr int DT_INNER_W      = 16;   // Two cells.
constexpr int DT_RING_CY      = 124;
constexpr int DT_LABEL_Y      = DT_RING_CY + DT_RING_D / 2 + 26;
constexpr int DT_VALUE_Y      = DT_LABEL_Y + 22;
constexpr int DT_NOTE_Y       = DT_VALUE_Y + 26;
constexpr int DT_COLUMN_X[3]  = {MARGIN, 176, 328};
// Usage windows, in seconds; the elapsed share drives the pace readout.
constexpr uint32_t SHORT_WINDOW_SECONDS = 5 * 3600;
constexpr uint32_t WEEK_WINDOW_SECONDS  = 7 * 86400;
// Usage this many points ahead of elapsed time reads as too fast.
constexpr int PACE_WARN_POINTS = 10;
// Weekly ring colour: the provider colour mixed toward black (255 = pure).
constexpr uint8_t INNER_RING_MIX = 150;
// A stale (disconnected) sample keeps its geometry but loses most of its colour.
constexpr uint8_t STALE_RING_MIX = 90;
constexpr int NAV_H       = 34;
constexpr int NAV_Y       = 436;
constexpr int NAV_GAP     = 6;
constexpr int NAV_BAR_W   = 24;
constexpr int NAV_BAR_H   = 4;
constexpr int NAV_LABEL_GAP = 7;
// The three tabs share the row with flex-grow 1, so their width is exact only
// while the remaining space divides evenly. The shared underline is placed from
// these numbers rather than from resolved geometry, which keeps it independent
// of when the flex pass runs; the assert is what makes that safe.
static_assert((CARD_W - 2 * NAV_GAP) % 3 == 0,
              "navigation tabs must divide the row evenly");
constexpr int NAV_TAB_W   = (CARD_W - 2 * NAV_GAP) / 3;
constexpr int NAV_TAB_PITCH = NAV_TAB_W + NAV_GAP;
constexpr int NAV_BAR_X   = (NAV_TAB_W - NAV_BAR_W) / 2;
constexpr int PAIRING_BOTTOM = 52;
constexpr int SCREEN_W    = 480;
constexpr int SCREEN_H    = 480;
constexpr int BATTERY_GROUP_W = 44;
constexpr int BATTERY_BODY_W = 30;
constexpr int BATTERY_BODY_H = 14;
constexpr int CONTENT_Y   = HEADER_H + 1;
constexpr int CONTENT_H   = NAV_Y - CONTENT_Y;
constexpr int OTA_CARD_Y         = 98;
constexpr int OTA_CARD_H         = 212;
constexpr int OTA_CARD_ROW_Y     = CARD_PAD;
constexpr int OTA_CARD_VALUE_Y   = 74;
constexpr int OTA_CARD_THIRD_ROW_Y = 142;
constexpr int OTA_CARD_HINT_Y    = 172;
constexpr int OTA_BUTTON_Y       = OTA_CARD_Y + OTA_CARD_H + MARGIN;
constexpr int OTA_BUTTON_W       = 204;
constexpr int OTA_BUTTON_H       = 66;
constexpr int OTA_WAIT_TITLE_Y   = 210;
constexpr int OTA_WAIT_DETAIL_Y  = 258;
constexpr int OTA_FAIL_ROW_Y     = 200;
constexpr int OTA_FAIL_DETAIL_Y  = 254;
constexpr int SAVER_CLOCK_Y      = 133;
constexpr int SAVER_CLOCK_H      = 88;   // montserrat_bold_120 line height
constexpr int SAVER_DATE_Y       = 271;
constexpr int SAVER_DATE_H       = 28;
constexpr int SAVER_USAGE_Y      = 320;
constexpr int SAVER_ROW_H        = 16;
constexpr int SAVER_ROW_GAP      = 2;
constexpr int SAVER_BAR_W        = 220;
// A hairline underline loses to the 120 px clock at viewing distance no matter
// how its colour is tuned.
constexpr int SAVER_BAR_H        = 3;
constexpr int SAVER_PCT_GAP      = 12;
// Fits a three-digit percentage without wrapping.
constexpr int SAVER_PCT_W        = 40;
// Both rows share the same centred bar and label positions.
constexpr int SAVER_PCT_SLOT     = SAVER_PCT_GAP + SAVER_PCT_W;
static_assert(SAVER_PCT_SLOT % 2 == 0);  // Halved below, so it must be even.
constexpr int SAVER_BAR_X        = SAVER_PCT_SLOT;
constexpr int SAVER_BAR_ALERT_X  = SAVER_PCT_SLOT / 2;
constexpr int SAVER_PCT_X        = SAVER_BAR_ALERT_X + SAVER_BAR_W + SAVER_PCT_GAP;
constexpr int SAVER_PCT_Y        = 2;
constexpr int SAVER_USAGE_W      = SAVER_BAR_X + SAVER_BAR_W + SAVER_PCT_SLOT;
constexpr int SAVER_USAGE_X      = (SCREEN_W - SAVER_USAGE_W) / 2;
// The row is centred, the resting bar is centred within it, and so is the
// alerting bar-plus-number. The group therefore reads as an underline of the
// clock rather than as a component of its own, in either state.
static_assert(2 * SAVER_USAGE_X + SAVER_USAGE_W == SCREEN_W);
static_assert(2 * (SAVER_USAGE_X + SAVER_BAR_X) + SAVER_BAR_W == SCREEN_W);
static_assert(
    2 * (SAVER_USAGE_X + SAVER_BAR_ALERT_X) + SAVER_BAR_W + SAVER_PCT_GAP +
        SAVER_PCT_W ==
    SCREEN_W);
static_assert(SAVER_PCT_X + SAVER_PCT_W <= SAVER_USAGE_W);
static_assert(SAVER_PCT_Y + 12 <= SAVER_ROW_H);  // 12 px is the font's line height.
// Ink runs from the top of the clock to the middle of the lower bar. Equal
// margins top and bottom mean the block is centred; the wake hint is excluded
// because it fades out and is not part of the resting composition.
constexpr int SAVER_INK_TOP    = SAVER_CLOCK_Y;
constexpr int SAVER_INK_BOTTOM =
    SAVER_USAGE_Y + SAVER_ROW_H + SAVER_ROW_GAP + (SAVER_ROW_H + SAVER_BAR_H) / 2;
static_assert(SAVER_INK_TOP + SAVER_INK_BOTTOM == SCREEN_H);
static_assert(SAVER_CLOCK_Y + SAVER_CLOCK_H < SAVER_DATE_Y);
static_assert(SAVER_DATE_Y + SAVER_DATE_H <= SAVER_USAGE_Y);

void rounder_event(lv_event_t* event)
{
    auto* area = static_cast<lv_area_t*>(lv_event_get_param(event));
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

lv_display_t* start_display(
    ScreenRotation initial_rotation, esp_lcd_touch_handle_t* ret_touch,
    lv_indev_t** ret_input_device, esp_lcd_panel_io_handle_t* ret_panel_io,
    esp_lcd_panel_handle_t* ret_panel)
{
    if (ret_touch != nullptr) {
        *ret_touch = nullptr;
    }
    if (ret_input_device != nullptr) {
        *ret_input_device = nullptr;
    }

    constexpr DisplayBufferPlan plan = make_display_buffer_plan();
    static_assert(
        display_buffer_plan_is_dma_safe(plan),
        "display buffers must be internal and fit the SPI DMA budget");

    esp_lv_adapter_config_t adapter_config{};
    adapter_config.task_stack_size = ESP_LV_ADAPTER_DEFAULT_STACK_SIZE;
    adapter_config.task_priority = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
    adapter_config.task_core_id = ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID;
    adapter_config.tick_period_ms = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS;
    adapter_config.task_min_delay_ms =
        ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS;
    adapter_config.task_max_delay_ms =
        ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS;
    adapter_config.stack_in_psram = false;
    adapter_config.auto_sleep.enable = false;
    adapter_config.auto_sleep.mode =
        ESP_LV_ADAPTER_AUTO_SLEEP_MODE_DISABLED;
    adapter_config.auto_sleep.idle_timeout_ms =
        ESP_LV_ADAPTER_DEFAULT_AUTO_SLEEP_TIMEOUT_MS;
    esp_err_t error = esp_lv_adapter_init(&adapter_config);
    if (error != ESP_OK) {
        ESP_LOGE(
            TAG, "LVGL adapter initialization failed: %s",
            esp_err_to_name(error));
        return nullptr;
    }

    const bsp_display_config_t panel_config{
        .max_transfer_sz = static_cast<int>(plan.transfer_bytes),
    };
    esp_lcd_panel_handle_t panel = nullptr;
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    error = bsp_display_new(&panel_config, &panel, &panel_io);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display initialization failed: %s", esp_err_to_name(error));
        return nullptr;
    }
    error = bsp_display_rotation_set(
        static_cast<bsp_display_rotation_t>(initial_rotation));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "initial display rotation failed: %s",
                 esp_err_to_name(error));
        return nullptr;
    }

    esp_lv_adapter_display_config_t display_config{};
    display_config.panel = panel;
    display_config.panel_io = panel_io;
    *ret_panel_io = panel_io;
    *ret_panel = panel;
    display_config.profile.interface = ESP_LV_ADAPTER_PANEL_IF_OTHER;
    display_config.profile.rotation = ESP_LV_ADAPTER_ROTATE_0;
    display_config.profile.hor_res = BSP_LCD_H_RES;
    display_config.profile.ver_res = BSP_LCD_V_RES;
    display_config.profile.buffer_height = plan.buffer_height;
    display_config.profile.use_psram = plan.use_psram;
    display_config.profile.enable_ppa_accel = false;
    display_config.profile.require_double_buffer =
        plan.require_double_buffer;
    display_config.profile.mono_layout =
        ESP_LV_ADAPTER_MONO_LAYOUT_NONE;
    display_config.tear_avoid_mode =
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
    display_config.te_sync.gpio_num = -1;
#if CONFIG_WS_USAGE_PANEL_PERFORMANCE_PROBE
    ESP_ERROR_CHECK(esp_lv_adapter_set_default_display_idf_callback_registration_enabled(false));
#endif
    lv_display_t* display = esp_lv_adapter_register_display(&display_config);
    if (display == nullptr) {
        ESP_LOGE(TAG, "LVGL display registration failed");
        return nullptr;
    }
    lv_display_add_event_cb(
        display, rounder_event, LV_EVENT_INVALIDATE_AREA, nullptr);

    bsp_display_cfg_t touch_board_config{};
    touch_board_config.rotation = ESP_LV_ADAPTER_ROTATE_0;
    touch_board_config.tear_avoid_mode =
        ESP_LV_ADAPTER_TEAR_AVOID_MODE_NONE;
    const auto initial_touch_mapping = touch_mapping(initial_rotation);
    touch_board_config.touch_flags.swap_xy = initial_touch_mapping.swap_xy;
    touch_board_config.touch_flags.mirror_x = initial_touch_mapping.mirror_x;
    touch_board_config.touch_flags.mirror_y = initial_touch_mapping.mirror_y;
    esp_lcd_touch_handle_t touch = nullptr;
    error = bsp_touch_new(&touch_board_config, &touch);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "touch initialization failed: %s", esp_err_to_name(error));
        return nullptr;
    }
    esp_lv_adapter_touch_config_t touch_config{};
    touch_config.disp = display;
    touch_config.handle = touch;
    touch_config.scale.x = 1.0f;
    touch_config.scale.y = 1.0f;
    touch_config.multi_touch.mode =
        ESP_LV_ADAPTER_TOUCH_MODE_SINGLE;
    touch_config.multi_touch.pointers = 1;
    lv_indev_t* input_device = esp_lv_adapter_register_touch(&touch_config);
    if (input_device == nullptr) {
        ESP_LOGE(TAG, "LVGL touch registration failed");
        return nullptr;
    }
    if (ret_touch != nullptr) {
        *ret_touch = touch;
    }
    if (ret_input_device != nullptr) {
        *ret_input_device = input_device;
    }

    error = bsp_display_brightness_init();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display brightness initialization failed: %s",
                 esp_err_to_name(error));
        return nullptr;
    }
    error = bsp_display_brightness_set(
        CONFIG_WS_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT);
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "display brightness setting failed: %s",
                 esp_err_to_name(error));
        return nullptr;
    }
#if CONFIG_WS_USAGE_PANEL_PERFORMANCE_PROBE
    if (!attach_performance_probe(display, panel_io)) return nullptr;
#endif
    error = esp_lv_adapter_start();
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "LVGL task start failed: %s", esp_err_to_name(error));
        return nullptr;
    }
    return display;
}

class DisplayLock {
public:
    DisplayLock() : locked_(esp_lv_adapter_lock(-1) == ESP_OK) {}

    ~DisplayLock()
    {
        if (locked_) {
            esp_lv_adapter_unlock();
        }
    }

    explicit operator bool() const { return locked_; }

    DisplayLock(const DisplayLock&) = delete;
    DisplayLock& operator=(const DisplayLock&) = delete;

private:
    bool locked_;
};

esp_err_t apply_touch_mapping(
    esp_lcd_touch_handle_t touch, const TouchMapping& mapping)
{
    const esp_err_t swap_error =
        esp_lcd_touch_set_swap_xy(touch, mapping.swap_xy);
    const esp_err_t mirror_x_error =
        esp_lcd_touch_set_mirror_x(touch, mapping.mirror_x);
    const esp_err_t mirror_y_error =
        esp_lcd_touch_set_mirror_y(touch, mapping.mirror_y);
    return swap_error != ESP_OK ? swap_error :
           mirror_x_error != ESP_OK ? mirror_x_error : mirror_y_error;
}

size_t provider_index(Provider provider)
{
    return provider == Provider::Codex ? 0 : 1;
}

constexpr uint32_t usage_color_hex(uint8_t percent)
{
    return percent >= 95 ? COLOR_BAD : percent >= 80 ? COLOR_WARN : COLOR_TEXT;
}

static_assert(usage_color_hex(0) == COLOR_TEXT);
static_assert(usage_color_hex(79) == COLOR_TEXT);
static_assert(usage_color_hex(80) == COLOR_WARN);
static_assert(usage_color_hex(94) == COLOR_WARN);
static_assert(usage_color_hex(95) == COLOR_BAD);
static_assert(usage_color_hex(100) == COLOR_BAD);


constexpr uint32_t saver_usage_color_hex(ScreensaverUsageLevel level)
{
    return level == ScreensaverUsageLevel::Bad    ? COLOR_SAVER_BAD
         : level == ScreensaverUsageLevel::Warn   ? COLOR_SAVER_WARN
                                                  : COLOR_SAVER_BASE;
}

// The resting tiers stay below the clock, so the clock keeps being the subject.
// The top tier is allowed to reach it, but never exceed it: at 95% the row is
// showing its number as well, and being noticed is the whole point.
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Base) < COLOR_MUTED);
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Warn) < COLOR_MUTED);
static_assert(saver_usage_color_hex(ScreensaverUsageLevel::Bad) <= COLOR_MUTED);

int saver_fill_width(uint8_t percent)
{
    return (int(percent) * SAVER_BAR_W + 50) / 100;
}

lv_color_t state_color(DisplayState state)
{
    switch (state) {
        case DisplayState::Online:
        case DisplayState::Partial:
            return lv_color_hex(COLOR_MUTED);
        case DisplayState::NoData:
        case DisplayState::Offline:
        default:
            return lv_color_hex(COLOR_DIM);
    }
}

void style_panel(lv_obj_t* panel, uint32_t background = COLOR_PANEL,
                 int radius = CARD_RADIUS)
{
    lv_obj_set_style_bg_color(panel, lv_color_hex(background), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(panel, radius, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t* make_label(lv_obj_t* parent, const char* text, int x, int y,
                     const lv_font_t* font, uint32_t color = COLOR_TEXT)
{
    lv_obj_t* label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

void style_bar(lv_obj_t* bar, int height)
{
    lv_bar_set_range(bar, 0, 100);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_TRACK), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COLOR_TEXT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, height / 2, LV_PART_INDICATOR);
}

/** Animation callback for the shared navigation underline. */
void nav_indicator_slide(void* target, int32_t translate_x)
{
    lv_obj_set_style_translate_x(
        static_cast<lv_obj_t*>(target), translate_x, 0);
}

uint32_t provider_color(Provider provider)
{
    return provider == Provider::Codex ? COLOR_CODEX : COLOR_CLAUDE;
}

/** Ring colour for a usage percentage: provider colour until the alert tiers. */
constexpr uint32_t ring_color_hex(uint8_t percent, uint32_t accent)
{
    return percent >= 95 ? COLOR_BAD : percent >= 80 ? COLOR_WARN : accent;
}

lv_color_t mix_with_black(uint32_t color, uint8_t amount)
{
    return lv_color_mix(lv_color_hex(color), lv_color_black(), amount);
}

/**
 * Concentric usage rings drawn as a grid of square cells, each band lit
 * clockwise from 12 o'clock.
 *
 * Both bands share one opaque canvas: separate canvases would overlap and the
 * inner one's black corners would cover the outer band. The buffer is painted
 * only when a value or colour changes; LVGL then blits it like any image, so a
 * steady screen costs no per-cell drawing.
 */
struct PixelRingBand {
    int inner_radius = 0;
    int outer_radius = 0;
    int32_t value = 0;
    // Percentage of the window already elapsed, drawn as white cells; -1 hides it.
    int32_t marker = -1;
    uint32_t lit = 0;
    uint32_t track = 0;
};

struct PixelRings {
    uint16_t* pixels = nullptr;  // size * size, owned for the program lifetime.
    int size = 0;                // Canvas edge in pixels.
    int cell = 0;                // Grid pitch in pixels.
    int gap = 0;                 // Unlit pixels between neighbouring cells.
    PixelRingBand bands[2];      // RING_OUTER and RING_INNER.
};

constexpr int RING_OUTER = 0;
constexpr int RING_INNER = 1;

// One ring pair per provider on the overview and one per detail page.
constexpr size_t MAX_PIXEL_RINGS = 4;
PixelRings pixel_rings[MAX_PIXEL_RINGS];
size_t pixel_ring_count = 0;

void paint_pixel_rings(const PixelRings& rings)
{
    std::fill(rings.pixels, rings.pixels + rings.size * rings.size, uint16_t{0});
    const int center = rings.size / 2;
    const int steps = rings.bands[RING_OUTER].outer_radius / rings.cell + 1;
    const int block = rings.cell - rings.gap;
    for (int row = -steps; row <= steps; ++row) {
        for (int column = -steps; column <= steps; ++column) {
            const int dx = column * rings.cell;
            const int dy = row * rings.cell;
            const float distance = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            const PixelRingBand* band = nullptr;
            for (const PixelRingBand& candidate : rings.bands) {
                // Strict outer bound: a cell exactly on the radius would only
                // appear on the axes and read as a stray tick.
                if (distance >= candidate.inner_radius &&
                    distance < candidate.outer_radius) band = &candidate;
            }
            if (band == nullptr) continue;
            // Clockwise angle from 12 o'clock, in [0, 360).
            float degrees = std::atan2(static_cast<float>(dx), static_cast<float>(-dy)) *
                            57.2957795f;
            if (degrees < 0) degrees += 360.0f;
            const bool lit = band->value > 0 &&
                degrees < 3.6f * static_cast<float>(band->value);
            uint32_t hex = lit ? band->lit : band->track;
            if (band->marker >= 0) {
                // One cell wide at the band's mid radius, whatever its size.
                const float half_width = 0.5f * 57.2957795f * rings.cell /
                    (0.5f * (band->inner_radius + band->outer_radius));
                float offset = std::fabs(degrees - 3.6f * static_cast<float>(band->marker));
                if (offset > 180.0f) offset = 360.0f - offset;
                if (offset <= half_width) hex = 0xFFFFFF;
            }
            const uint16_t color = lv_color_to_u16(lv_color_hex(hex));
            const int left = center + dx - rings.cell / 2 + rings.gap / 2;
            const int top = center + dy - rings.cell / 2 + rings.gap / 2;
            for (int y = std::max(top, 0); y < std::min(top + block, rings.size); ++y) {
                for (int x = std::max(left, 0); x < std::min(left + block, rings.size); ++x) {
                    rings.pixels[y * rings.size + x] = color;
                }
            }
        }
    }
}

/**
 * Creates a ring pair centred on (center_x, center_y). Each band is given by
 * the outer diameter and width of its cell centres; `cell` is the grid pitch.
 */
lv_obj_t* make_rings(lv_obj_t* parent, int center_x, int center_y, int cell,
                     int outer_d, int outer_w, uint32_t outer_track,
                     int inner_d, int inner_w, uint32_t inner_track)
{
    if (pixel_ring_count >= MAX_PIXEL_RINGS) {
        ESP_LOGE(TAG, "pixel ring pool exhausted");
        return nullptr;
    }
    PixelRings& rings = pixel_rings[pixel_ring_count++];
    rings.cell = cell;
    rings.gap = cell >= 8 ? 2 : 1;
    rings.size = outer_d + cell;
    const auto band = [](int diameter, int width, uint32_t track) {
        PixelRingBand result;
        result.outer_radius = diameter / 2;
        result.inner_radius = result.outer_radius - width;
        result.lit = track;
        result.track = track;
        return result;
    };
    rings.bands[RING_OUTER] = band(outer_d, outer_w, outer_track);
    rings.bands[RING_INNER] = band(inner_d, inner_w, inner_track);
    rings.pixels = static_cast<uint16_t*>(heap_caps_malloc(
        static_cast<size_t>(rings.size) * rings.size * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM));
    if (rings.pixels == nullptr) {
        ESP_LOGE(TAG, "pixel ring buffer allocation failed");
        return nullptr;
    }
    lv_obj_t* canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(canvas, rings.pixels, rings.size, rings.size,
                         LV_COLOR_FORMAT_RGB565);
    lv_obj_set_pos(canvas, center_x - rings.size / 2, center_y - rings.size / 2);
    lv_obj_remove_flag(canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(canvas, &rings);
    paint_pixel_rings(rings);
    return canvas;
}

/**
 * Sets one band's percentage, lit colour and elapsed-time marker (-1 hides
 * it); repaints only on change.
 */
void set_ring_band(lv_obj_t* canvas, int band_index, int32_t value,
                   lv_color_t color, int32_t marker = -1)
{
    auto* rings = static_cast<PixelRings*>(lv_obj_get_user_data(canvas));
    if (rings == nullptr) return;
    PixelRingBand& band = rings->bands[band_index];
    const uint32_t hex = lv_color_to_u32(color) & 0xFFFFFF;
    if (band.value == value && band.lit == hex && band.marker == marker) return;
    band.value = value;
    band.lit = hex;
    band.marker = marker;
    paint_pixel_rings(*rings);
    lv_obj_invalidate(canvas);
}

/** Creates a fixed-width, centred label whose line box is centred on y. */
lv_obj_t* make_centered_label(lv_obj_t* parent, const char* text,
                              int center_x, int center_y, int width,
                              const lv_font_t* font, uint32_t color)
{
    lv_obj_t* label = make_label(
        parent, text, center_x - width / 2,
        center_y - lv_font_get_line_height(font) / 2, font, color);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_CLIP);
    return label;
}

lv_obj_t* make_dot(lv_obj_t* parent, int x, int y, int diameter, uint32_t color)
{
    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_remove_style_all(dot);
    lv_obj_set_pos(dot, x, y);
    lv_obj_set_size(dot, diameter, diameter);
    lv_obj_set_style_radius(dot, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    return dot;
}

/** Formats a percentage, or "--" when the window carries no current value. */
void set_percent_text(lv_obj_t* label, bool present, uint8_t percent)
{
    if (present) lv_label_set_text_fmt(label, "%u%%", percent);
    else lv_label_set_text(label, "--");
}

constexpr const char* WEEKDAYS[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

/**
 * Formats when a window resets in panel-local time ("22:59", or "FRI 10:00"
 * with `with_weekday`). The local time is derived from the header clock plus
 * the remaining seconds, so it needs no time zone. Writes "--:--" while the
 * clock is unset or the reset has passed.
 */
void format_reset_clock(const ClockView& clock, uint32_t now_epoch,
                        uint32_t reset_at, bool with_weekday,
                        char* output, size_t output_size)
{
    const char* time = clock.time;
    if (!clock.available || reset_at <= now_epoch ||
        time[0] < '0' || time[0] > '2' || time[2] != ':') {
        snprintf(output, output_size, "--:--");
        return;
    }
    const int now_minutes = ((time[0] - '0') * 10 + (time[1] - '0')) * 60 +
                            (time[3] - '0') * 10 + (time[4] - '0');
    const uint32_t total = static_cast<uint32_t>(now_minutes) +
                           (reset_at - now_epoch + 30) / 60;
    const unsigned minute_of_day = total % 1440;
    const unsigned hour = minute_of_day / 60;
    const unsigned minute = minute_of_day % 60;
    int weekday = -1;
    for (int i = 0; i < 7; ++i) {
        if (std::strncmp(clock.weekday, WEEKDAYS[i], 3) == 0) weekday = i;
    }
    if (with_weekday && weekday >= 0) {
        snprintf(output, output_size, "%s %02u:%02u",
                 WEEKDAYS[(weekday + total / 1440) % 7], hour, minute);
    } else {
        snprintf(output, output_size, "%02u:%02u", hour, minute);
    }
}

/** Returns the elapsed share of a window, 0-100, or -1 without a reset time. */
int32_t elapsed_percent(const UsageWindow& window, uint32_t window_seconds,
                        uint32_t now_epoch)
{
    if (!window.has_reset || now_epoch == 0) return -1;
    const uint32_t remaining =
        window.reset_at > now_epoch ? window.reset_at - now_epoch : 0;
    if (remaining >= window_seconds) return 0;
    return static_cast<int32_t>(
        (uint64_t(window_seconds - remaining) * 100) / window_seconds);
}
}  // namespace

bool DisplayUi::begin(ScreenRotation initial_rotation)
{
    display_ = start_display(
        initial_rotation, &touch_, &input_device_, &panel_io_, &panel_);
    if (display_ == nullptr || input_device_ == nullptr || touch_ == nullptr) {
        ESP_LOGE(TAG, "BSP display initialization failed");
        return false;
    }
    rotation_ = initial_rotation;
    brightness_percent_ = CONFIG_WS_USAGE_PANEL_DISPLAY_BRIGHTNESS_PERCENT;
    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock");
        return false;
    }

    lv_obj_t* screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(screen, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screen, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(screen, touch_down_event, LV_EVENT_PRESSED, this);
    // Listen at the input-device level as well: a press targeting a card,
    // label, or navigation button is not guaranteed to bubble to the screen.
    lv_indev_add_event_cb(
        input_device_, touch_down_event, LV_EVENT_PRESSED, this);

    lv_indev_set_long_press_time(input_device_, 800);
    lv_indev_add_event_cb(input_device_, long_press_event, LV_EVENT_LONG_PRESSED, this);
    build_header(screen);
    build_overview(screen);
    build_provider_page(screen, Provider::Codex);
    build_provider_page(screen, Provider::Claude);
    build_navigation(screen);
    build_screensaver(screen);
    build_settings(screen);
    build_pairing_overlay(screen);
    build_ota_overlay(screen);
    show_page(Page::Overview, /*animate=*/false);
    lv_display_add_event_cb(
        display_, transition_refresh_ready, LV_EVENT_REFR_READY, this);

    return true;
}

bool DisplayUi::set_panel_power(bool on)
{
    DisplayLock lock;
    if (!lock) return false;
    if (on == panel_on_) return true;
    lv_anim_delete(this, transition_brightness);
    transition_refresh_pending_ = false;
    if (!on) {
        pet_overview_set_active(false);  // nothing to animate on a dark panel
        set_transition_brightness(0);
        // Not every panel driver implements display off; zero brightness
        // already darkens the panel, so the result is only logged.
        const esp_err_t error = esp_lcd_panel_disp_on_off(panel_, false);
        if (error != ESP_OK) {
            ESP_LOGW(TAG, "panel display-off failed: %s", esp_err_to_name(error));
        }
    } else {
        esp_lcd_panel_disp_on_off(panel_, true);
        lv_obj_invalidate(lv_screen_active());
        set_transition_brightness(brightness_percent_);
    }
    panel_on_ = on;
    return true;
}

bool DisplayUi::set_rotation(ScreenRotation rotation)
{
    if (display_ == nullptr || input_device_ == nullptr || touch_ == nullptr) {
        ESP_LOGE(TAG, "cannot rotate before display initialization");
        return false;
    }
    if (rotation == rotation_) {
        return true;
    }

    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock for rotation");
        return false;
    }

    const auto previous_mapping = touch_mapping(rotation_);
    const auto mapping = touch_mapping(rotation);
    const esp_err_t display_error = bsp_display_rotation_set(
        static_cast<bsp_display_rotation_t>(rotation));
    if (display_error != ESP_OK) {
        ESP_LOGE(TAG, "display rotation failed: %s",
                 esp_err_to_name(display_error));
        return false;
    }

    const esp_err_t touch_error = apply_touch_mapping(touch_, mapping);
    if (touch_error != ESP_OK) {
        const esp_err_t rollback_display_error = bsp_display_rotation_set(
            static_cast<bsp_display_rotation_t>(rotation_));
        const esp_err_t rollback_touch_error =
            apply_touch_mapping(touch_, previous_mapping);
        ESP_LOGE(TAG, "touch rotation failed: %s",
                 esp_err_to_name(touch_error));
        if (rollback_display_error != ESP_OK ||
            rollback_touch_error != ESP_OK) {
            ESP_LOGE(TAG, "rotation rollback failed: display=%s touch=%s",
                     esp_err_to_name(rollback_display_error),
                     esp_err_to_name(rollback_touch_error));
        }
        return false;
    }

    rotation_ = rotation;
    lv_obj_invalidate(lv_screen_active());
    return true;
}

#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
void DisplayUi::run_memory_probe()
{
    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock for memory probe");
        return;
    }

    constexpr uint32_t internal_caps =
        MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const size_t internal_free_before =
        heap_caps_get_free_size(internal_caps);
    const size_t largest_before =
        heap_caps_get_largest_free_block(internal_caps);
    lv_mem_monitor_t monitor_before{};
    lv_mem_monitor(&monitor_before);

    lv_obj_t* probe = lv_obj_create(lv_screen_active());
    lv_obj_add_flag(probe, LV_OBJ_FLAG_HIDDEN);
    for (uint32_t index = 0; index < 32; ++index) {
        lv_obj_t* label = lv_label_create(probe);
        lv_label_set_text_fmt(label, "PROBE %lu",
                              static_cast<unsigned long>(index));
    }
    for (uint32_t index = 0; index < 32; ++index) {
        lv_obj_t* button = lv_button_create(probe);
        lv_obj_set_size(button, 96, 40);
    }

    const size_t internal_free_after =
        heap_caps_get_free_size(internal_caps);
    const size_t largest_after =
        heap_caps_get_largest_free_block(internal_caps);
    lv_mem_monitor_t monitor_after{};
    lv_mem_monitor(&monitor_after);
    ESP_LOGI(
        TAG,
        "MEMORY_PROBE stress internal_free_before=%u internal_free_after=%u "
        "largest_before=%u largest_after=%u lvgl_used_before=%u "
        "lvgl_used_after=%u",
        static_cast<unsigned>(internal_free_before),
        static_cast<unsigned>(internal_free_after),
        static_cast<unsigned>(largest_before),
        static_cast<unsigned>(largest_after),
        static_cast<unsigned>(monitor_before.total_size -
                              monitor_before.free_size),
        static_cast<unsigned>(monitor_after.total_size -
                              monitor_after.free_size));

    lv_obj_delete(probe);
    lv_display_remove_event_cb_with_user_data(display_, probe_refresh_event, this);
    probe_refresh_sample_ = 0;
    probe_refresh_active_ = true;
    lv_display_add_event_cb(
        display_, probe_refresh_event, LV_EVENT_REFR_READY, this);
    start_next_probe_refresh();
}

void DisplayUi::start_next_probe_refresh()
{
    if (probe_refresh_sample_ >= 90) {
        probe_refresh_active_ = false;
        show_page(Page::Overview, /*animate=*/false);
        lv_obj_invalidate(lv_screen_active());
        return;
    }

    constexpr Page pages[] = {Page::Overview, Page::Codex, Page::Claude};
    const uint32_t page_index = probe_refresh_sample_ % 3;
    // The probe times forced full repaints; an underline animation would add
    // invalid areas to the very refreshes it is measuring.
    show_page(pages[page_index], /*animate=*/false);
    probe_refresh_started_us_ = esp_timer_get_time();
    lv_obj_invalidate(lv_screen_active());
}

void DisplayUi::probe_refresh_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (!ui->probe_refresh_active_) {
        return;
    }

    const uint64_t elapsed_us = static_cast<uint64_t>(
        esp_timer_get_time() - ui->probe_refresh_started_us_);
    const uint32_t sample = ui->probe_refresh_sample_;
    ESP_LOGI(
        TAG, "MEMORY_PROBE refresh cycle=%u page=%u elapsed_us=%llu",
        static_cast<unsigned>(sample / 3),
        static_cast<unsigned>(sample % 3),
        static_cast<unsigned long long>(elapsed_us));
    ++ui->probe_refresh_sample_;
    ui->start_next_probe_refresh();
}
#endif

void DisplayUi::build_header(lv_obj_t* screen)
{
    clock_label_ = make_label(
        screen, "--:--", MARGIN, 18,
        &pixel_press_16, COLOR_TEXT);

    lv_obj_t* status_group = lv_obj_create(screen);
    lv_obj_set_pos(status_group, 222, 13);
    lv_obj_set_size(status_group, 110, 28);
    lv_obj_set_style_bg_opa(status_group, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(status_group, 0, 0);
    lv_obj_set_style_pad_all(status_group, 0, 0);
    lv_obj_remove_flag(status_group, LV_OBJ_FLAG_SCROLLABLE);

    status_dot_ = lv_obj_create(status_group);
    lv_obj_set_pos(status_dot_, 0, 10);
    lv_obj_set_size(status_dot_, 8, 8);
    lv_obj_set_style_radius(status_dot_, 0, 0);
    lv_obj_set_style_bg_color(status_dot_, lv_color_hex(COLOR_MUTED), 0);
    lv_obj_set_style_bg_opa(status_dot_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(status_dot_, 0, 0);
    lv_obj_set_style_pad_all(status_dot_, 0, 0);
    lv_obj_remove_flag(status_dot_, LV_OBJ_FLAG_SCROLLABLE);

    connection_label_ = make_label(
        status_group, "WAITING", 16, 8,
        &pixel_silk_16, COLOR_RESET);
    lv_obj_set_width(connection_label_, 94);

    // Measured charge, or USB when no cell is attached.
    battery_label_ = make_label(
        screen, "--", SCREEN_W - MARGIN - BATTERY_GROUP_W - 62, 21,
        &pixel_silk_16, COLOR_RESET);
    lv_obj_set_width(battery_label_, 56);
    lv_obj_set_style_text_align(battery_label_, LV_TEXT_ALIGN_RIGHT, 0);

    battery_group_ = lv_obj_create(screen);
    lv_obj_set_pos(battery_group_, SCREEN_W - MARGIN - BATTERY_GROUP_W, 13);
    lv_obj_set_size(battery_group_, BATTERY_GROUP_W, 28);
    lv_obj_set_style_bg_opa(battery_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(battery_group_, 0, 0);
    lv_obj_set_style_pad_all(battery_group_, 0, 0);
    lv_obj_remove_flag(battery_group_, LV_OBJ_FLAG_SCROLLABLE);

    charge_symbol_ = make_label(
        battery_group_, LV_SYMBOL_CHARGE, 0, 6,
        &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(charge_symbol_, 7);
    lv_obj_set_style_text_align(charge_symbol_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_opa(charge_symbol_, LV_OPA_TRANSP, 0);

    lv_obj_t* body = lv_obj_create(battery_group_);
    lv_obj_set_pos(body, 12, 7);
    lv_obj_set_size(body, BATTERY_BODY_W, BATTERY_BODY_H);
    lv_obj_set_style_bg_opa(body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(body, 1, 0);
    lv_obj_set_style_border_color(body, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_radius(body, 3, 0);
    lv_obj_set_style_pad_all(body, 0, 0);
    lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < 4; ++i) {
        battery_segments_[i] = lv_obj_create(body);
        lv_obj_set_pos(battery_segments_[i], 2 + i * 6, 2);
        lv_obj_set_size(battery_segments_[i], 5, 8);
        lv_obj_set_style_border_width(battery_segments_[i], 0, 0);
        lv_obj_set_style_radius(battery_segments_[i], 1, 0);
        lv_obj_set_style_pad_all(battery_segments_[i], 0, 0);
        lv_obj_remove_flag(battery_segments_[i], LV_OBJ_FLAG_SCROLLABLE);
    }
    lv_obj_t* terminal = lv_obj_create(battery_group_);
    lv_obj_set_pos(terminal, 42, 11);
    lv_obj_set_size(terminal, 2, 6);
    lv_obj_set_style_bg_color(terminal, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_opa(terminal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(terminal, 0, 0);
    lv_obj_set_style_radius(terminal, 1, 0);
    lv_obj_set_style_pad_all(terminal, 0, 0);
    lv_obj_remove_flag(terminal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* divider = lv_obj_create(screen);
    lv_obj_set_pos(divider, MARGIN, HEADER_H);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);
}

void DisplayUi::build_screensaver(lv_obj_t* screen)
{
    screensaver_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(screensaver_panel_, 0, 0);
    lv_obj_set_size(screensaver_panel_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(
        screensaver_panel_, lv_color_hex(COLOR_SAVER_BG), 0);
    lv_obj_set_style_bg_opa(screensaver_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(screensaver_panel_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_panel_, 0, 0);
    lv_obj_remove_flag(screensaver_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(
        screensaver_panel_, touch_down_event, LV_EVENT_PRESSED, this);

    screensaver_content_ = lv_obj_create(screensaver_panel_);
    lv_obj_set_pos(screensaver_content_, 0, 0);
    lv_obj_set_size(screensaver_content_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_opa(screensaver_content_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screensaver_content_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_content_, 0, 0);
    lv_obj_remove_flag(screensaver_content_, LV_OBJ_FLAG_SCROLLABLE);

    screensaver_time_ = make_label(
        screensaver_content_, "--:--", 0, SAVER_CLOCK_Y,
        &montserrat_bold_120, COLOR_MUTED);
    lv_obj_set_width(screensaver_time_, SCREEN_W);
    lv_obj_set_style_text_align(screensaver_time_, LV_TEXT_ALIGN_CENTER, 0);

    lv_obj_t* date_row = lv_obj_create(screensaver_content_);
    lv_obj_set_pos(date_row, 0, SAVER_DATE_Y);
    lv_obj_set_size(date_row, SCREEN_W, SAVER_DATE_H);
    lv_obj_set_style_bg_opa(date_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(date_row, 0, 0);
    lv_obj_set_style_pad_all(date_row, 0, 0);
    lv_obj_set_style_pad_column(date_row, 12, 0);
    lv_obj_set_flex_flow(date_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        date_row, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(date_row, LV_OBJ_FLAG_SCROLLABLE);
    screensaver_weekday_ = make_label(
        date_row, "---", 0, 0, &montserrat_semibold_20, COLOR_DIM);
    lv_obj_set_style_text_letter_space(screensaver_weekday_, 2, 0);
    lv_obj_t* date_dot = lv_obj_create(date_row);
    lv_obj_set_size(date_dot, 4, 4);
    lv_obj_set_style_radius(date_dot, 2, 0);
    lv_obj_set_style_bg_color(date_dot, lv_color_hex(COLOR_DIM), 0);
    lv_obj_set_style_bg_opa(date_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(date_dot, 0, 0);
    lv_obj_set_style_pad_all(date_dot, 0, 0);
    screensaver_month_day_ = make_label(
        date_row, "-- --", 0, 0, &montserrat_semibold_20, COLOR_DIM);
    lv_obj_set_style_text_letter_space(screensaver_month_day_, 2, 0);

    for (int i = 0; i < 2; ++i) {
        lv_obj_t* row = lv_obj_create(screensaver_content_);
        lv_obj_set_pos(
            row, SAVER_USAGE_X,
            SAVER_USAGE_Y + i * (SAVER_ROW_H + SAVER_ROW_GAP));
        lv_obj_set_size(row, SAVER_USAGE_W, SAVER_ROW_H);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(row, 0, 0);
        lv_obj_set_style_pad_all(row, 0, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t* track = lv_obj_create(row);
        lv_obj_set_pos(track, SAVER_BAR_X, (SAVER_ROW_H - SAVER_BAR_H) / 2);
        lv_obj_set_size(track, SAVER_BAR_W, SAVER_BAR_H);
        lv_obj_set_style_bg_color(track, lv_color_hex(COLOR_SAVER_TRACK), 0);
        lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(track, 0, 0);
        lv_obj_set_style_radius(track, SAVER_BAR_H / 2, 0);
        lv_obj_set_style_pad_all(track, 0, 0);
        lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
        screensaver_usage_[i].track = track;

        lv_obj_t* fill = lv_obj_create(row);
        lv_obj_set_pos(fill, SAVER_BAR_X, (SAVER_ROW_H - SAVER_BAR_H) / 2);
        lv_obj_set_size(fill, SAVER_BAR_W, SAVER_BAR_H);
        lv_obj_set_style_bg_color(fill, lv_color_hex(COLOR_SAVER_BASE), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(fill, 0, 0);
        lv_obj_set_style_radius(fill, SAVER_BAR_H / 2, 0);
        lv_obj_set_style_pad_all(fill, 0, 0);
        lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(fill, LV_OBJ_FLAG_HIDDEN);
        screensaver_usage_[i].fill = fill;

        lv_obj_t* percent = make_label(
            row, "", SAVER_PCT_X, SAVER_PCT_Y,
            &montserrat_semibold_13, COLOR_SAVER_BASE);
        lv_obj_set_width(percent, SAVER_PCT_W);
        // Clip rather than wrap, so no future glyph change can push a second
        // line into the row below.
        lv_label_set_long_mode(percent, LV_LABEL_LONG_MODE_CLIP);
        lv_obj_set_style_text_letter_space(percent, 0, 0);
        lv_obj_add_flag(percent, LV_OBJ_FLAG_HIDDEN);
        screensaver_usage_[i].percent = percent;
    }

    screensaver_hint_ = make_label(
        screensaver_content_, "TAP OR SHAKE TO WAKE", 0, 433,
        &montserrat_semibold_13, COLOR_BATTERY_OFF);
    lv_obj_set_width(screensaver_hint_, SCREEN_W);
    lv_obj_set_style_text_align(screensaver_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(screensaver_hint_, 1, 0);

    screensaver_low_group_ = lv_obj_create(screensaver_content_);
    lv_obj_set_pos(screensaver_low_group_, 382, 18);
    lv_obj_set_size(screensaver_low_group_, 74, 22);
    lv_obj_set_style_bg_opa(screensaver_low_group_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(screensaver_low_group_, 0, 0);
    lv_obj_set_style_pad_all(screensaver_low_group_, 0, 0);
    lv_obj_remove_flag(screensaver_low_group_, LV_OBJ_FLAG_SCROLLABLE);
    make_label(
        screensaver_low_group_, "LOW", 0, 4,
        &montserrat_semibold_13, COLOR_LOW_DIM);
    lv_obj_t* low_body = lv_obj_create(screensaver_low_group_);
    lv_obj_set_pos(low_body, 42, 4);
    lv_obj_set_size(low_body, 30, 14);
    lv_obj_set_style_bg_opa(low_body, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(low_body, 1, 0);
    lv_obj_set_style_border_color(low_body, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_radius(low_body, 2, 0);
    lv_obj_set_style_pad_all(low_body, 0, 0);
    for (int i = 0; i < 4; ++i) {
        screensaver_low_segments_[i] = lv_obj_create(low_body);
        lv_obj_set_pos(screensaver_low_segments_[i], 2 + i * 6, 2);
        lv_obj_set_size(screensaver_low_segments_[i], 5, 8);
        lv_obj_set_style_border_width(screensaver_low_segments_[i], 0, 0);
        lv_obj_set_style_radius(screensaver_low_segments_[i], 1, 0);
        lv_obj_set_style_pad_all(screensaver_low_segments_[i], 0, 0);
    }
    lv_obj_t* low_terminal = lv_obj_create(screensaver_low_group_);
    lv_obj_set_pos(low_terminal, 72, 8);
    lv_obj_set_size(low_terminal, 2, 6);
    lv_obj_set_style_bg_color(low_terminal, lv_color_hex(0x3A4048), 0);
    lv_obj_set_style_bg_opa(low_terminal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(low_terminal, 0, 0);
    lv_obj_set_style_radius(low_terminal, 1, 0);
    lv_obj_set_style_pad_all(low_terminal, 0, 0);
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::build_overview(lv_obj_t* screen)
{
    overview_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(overview_panel_, 0, CONTENT_Y);
    lv_obj_set_size(overview_panel_, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(overview_panel_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(overview_panel_, 0, 0);
    lv_obj_set_style_pad_all(overview_panel_, 0, 0);
    lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        overview_panel_, gesture_event, LV_EVENT_GESTURE, this);

    // The flying-orb pet reacts to both quotas; a card per provider below it.
    pet_overview_create(overview_panel_);
}

namespace {
/** Condenses one provider's snapshot into what the pet page shows. */
pet_provider_t pet_provider_view(const UsageModel& model, Provider provider,
                                 bool connected, uint64_t now_ms)
{
    pet_provider_t view{};
    const auto state = model.display_state(provider, connected);
    const auto& snapshot = model.snapshot(provider);
    const bool partial = snapshot.source_state == SourceState::Partial;
    view.state = state == DisplayState::Online ? PET_ONLINE
        : state == DisplayState::Partial ? PET_PARTIAL
        : state == DisplayState::Offline ? PET_OFFLINE : PET_NO_DATA;
    view.short_present = snapshot.has_valid_data &&
        snapshot.short_window.present && (!partial || snapshot.latest_short_present);
    view.week_present = snapshot.has_valid_data &&
        snapshot.week_window.present && (!partial || snapshot.latest_week_present);
    const uint8_t short_used = std::min<uint8_t>(snapshot.short_window.used_percent, 100);
    view.short_left = static_cast<uint8_t>(100 - short_used);
    view.week_used = std::min<uint8_t>(snapshot.week_window.used_percent, 100);
    const uint32_t epoch =
        snapshot.has_valid_data ? model.estimated_epoch(provider, now_ms) : 0;
    if (view.short_present) {
        format_countdown(snapshot.short_window.has_reset, snapshot.short_window.reset_at,
                         epoch, view.countdown, sizeof(view.countdown));
        const int32_t elapsed =
            elapsed_percent(snapshot.short_window, SHORT_WINDOW_SECONDS, epoch);
        const bool fresh = state == DisplayState::Online || state == DisplayState::Partial;
        view.pace_known = fresh && elapsed >= 0;
        view.pace = static_cast<int16_t>(view.pace_known ? int32_t{short_used} - elapsed : 0);
    }
    return view;
}
}  // namespace

void DisplayUi::build_provider_page(lv_obj_t* screen, Provider provider)
{
    const size_t index = provider_index(provider);
    const uint32_t accent = provider_color(provider);
    ProviderWidgets& widgets = provider_pages_[index];
    widgets.panel = lv_obj_create(screen);
    lv_obj_set_pos(widgets.panel, 0, CONTENT_Y);
    lv_obj_set_size(widgets.panel, SCREEN_W, CONTENT_H);
    lv_obj_set_style_bg_opa(widgets.panel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(widgets.panel, 0, 0);
    lv_obj_set_style_pad_all(widgets.panel, 0, 0);
    lv_obj_remove_flag(widgets.panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(widgets.panel, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(
        widgets.panel, gesture_event, LV_EVENT_GESTURE, this);

    const int cx = SCREEN_W / 2;
    widgets.ring.rings = make_rings(
        widgets.panel, cx, DT_RING_CY, DT_CELL,
        DT_RING_D, DT_RING_W, COLOR_TRACK,
        DT_INNER_D, DT_INNER_W, COLOR_TRACK_INNER);
    make_centered_label(
        widgets.panel, provider == Provider::Codex ? "CODEX" : "CLAUDE",
        cx, DT_RING_CY - 40, DT_INNER_D - 50, &pixel_silk_16, accent);
    widgets.ring.value = make_centered_label(
        widgets.panel, "--", cx, DT_RING_CY + 2, DT_INNER_D - 40,
        &pixel_press_40, COLOR_DIM);
    make_centered_label(
        widgets.panel, "5H LEFT", cx, DT_RING_CY + 38, DT_INNER_D - 50,
        &pixel_silk_16, COLOR_MUTED);

    // Three columns: label, value, note.
    const char* labels[3] = {"5H RESET", "WEEK", "PACE"};
    lv_obj_t** values[3] = {&widgets.short_reset, &widgets.week_value,
                            &widgets.pace_value};
    lv_obj_t** notes[3] = {&widgets.short_in, &widgets.week_reset,
                           &widgets.status};
    for (int column = 0; column < 3; ++column) {
        const int x = DT_COLUMN_X[column];
        make_label(widgets.panel, labels[column], x, DT_LABEL_Y,
                   &pixel_silk_16, COLOR_MUTED);
        *values[column] = make_label(widgets.panel, "--", x, DT_VALUE_Y,
                                     &pixel_press_16, COLOR_DIM);
        *notes[column] = make_label(widgets.panel, "", x, DT_NOTE_Y,
                                    &pixel_silk_16, COLOR_DIM);
    }
}

void DisplayUi::build_navigation(lv_obj_t* screen)
{
    lv_obj_t* navigation = lv_obj_create(screen);
    lv_obj_set_pos(navigation, MARGIN, NAV_Y);
    lv_obj_set_size(navigation, CARD_W, NAV_H);
    lv_obj_set_style_bg_opa(navigation, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(navigation, 0, 0);
    lv_obj_set_style_pad_all(navigation, 0, 0);
    lv_obj_set_style_pad_column(navigation, NAV_GAP, 0);
    lv_obj_set_flex_flow(navigation, LV_FLEX_FLOW_ROW);
    lv_obj_remove_flag(navigation, LV_OBJ_FLAG_SCROLLABLE);

    const char* labels[] = {"ALL", "CODEX", "CLAUDE"};
    for (size_t i = 0; i < 3; ++i) {
        tab_bindings_[i] =
            TabBinding{this, static_cast<uint8_t>(i)};
        lv_obj_t* tab = lv_button_create(navigation);
        lv_obj_set_size(tab, 0, NAV_H);
        lv_obj_set_flex_grow(tab, 1);
        lv_obj_set_style_radius(tab, 0, 0);
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, 0);
        // Keep the theme's pressed fill off too, so a touch never paints a
        // block behind the label.
        lv_obj_set_style_bg_opa(tab, LV_OPA_TRANSP, LV_STATE_PRESSED);
        lv_obj_set_style_shadow_width(tab, 0, 0);
        lv_obj_set_style_border_width(tab, 0, 0);
        lv_obj_set_style_pad_all(tab, 0, 0);
        // Reserve the shared underline's height plus its label gap as bottom
        // padding so the label baseline stays a fixed distance above it.
        lv_obj_set_style_pad_bottom(tab, NAV_LABEL_GAP + NAV_BAR_H, 0);
        lv_obj_set_flex_flow(tab, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(
            tab, LV_FLEX_ALIGN_END,
            LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_remove_flag(tab, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(
            tab, tab_event, LV_EVENT_CLICKED, &tab_bindings_[i]);

        nav_labels_[i] =
            make_label(
                tab, labels[i], 0, 0,
                &pixel_silk_16, COLOR_DIM);
        lv_obj_set_style_text_letter_space(nav_labels_[i], 1, 0);
    }

    // Selection is shown by an underline, never by a filled block: the
    // navigation must not outshine the data above it. One underline is shared
    // by all three tabs so a page change can slide it instead of blinking it
    // from one tab to the next. It ignores the row layout and is placed by
    // hand; only its translation is animated, which keeps the transform out of
    // the flex pass.
    nav_indicator_ = lv_obj_create(navigation);
    lv_obj_add_flag(nav_indicator_, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_size(nav_indicator_, NAV_BAR_W, NAV_BAR_H);
    lv_obj_set_style_radius(nav_indicator_, 0, 0);
    lv_obj_set_style_bg_color(nav_indicator_, lv_color_hex(COLOR_RESET), 0);
    lv_obj_set_style_bg_opa(nav_indicator_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(nav_indicator_, 0, 0);
    lv_obj_set_style_pad_all(nav_indicator_, 0, 0);
    lv_obj_remove_flag(nav_indicator_, LV_OBJ_FLAG_SCROLLABLE);

    // Parked under the first tab; every other page is reached by translating
    // one whole tab pitch, so Page::Overview needs no transform at all.
    lv_obj_set_pos(nav_indicator_, NAV_BAR_X, NAV_H - NAV_BAR_H);
}

void DisplayUi::build_settings(lv_obj_t* screen)
{
    settings_panel_ = lv_obj_create(screen);
    lv_obj_remove_style_all(settings_panel_);
    lv_obj_set_pos(settings_panel_, 0, HEADER_H + 1);
    lv_obj_set_size(settings_panel_, SCREEN_W, SCREEN_H - HEADER_H - 1);
    lv_obj_set_style_bg_color(settings_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(settings_panel_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_SCROLLABLE);
    auto* title = make_label(settings_panel_, "BRIGHTNESS", MARGIN, 24,
                            &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    settings_brightness_ = make_label(settings_panel_, "", MARGIN, 58, &lv_font_montserrat_48);
    lv_obj_t* divider = lv_obj_create(settings_panel_);
    lv_obj_remove_style_all(divider);
    lv_obj_set_pos(divider, MARGIN, 158);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    title = make_label(settings_panel_, "AUTO CLOCK", MARGIN, 180,
                       &lv_font_montserrat_16, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    settings_timeout_ = make_label(settings_panel_, "AFTER IDLE / MIN", MARGIN, 207,
                                   &lv_font_montserrat_14, COLOR_MUTED);
    settings_hint_ = make_label(settings_panel_, "", MARGIN, 322,
                               &montserrat_semibold_13, COLOR_TEXT);
    const char* labels[] = {"-", "+", "OFF", "1", "5", "10", "30", "SAVE", "CANCEL"};
    const SettingsAction actions[] = {SettingsAction::Dimmer, SettingsAction::Brighter,
        SettingsAction::TimeoutOff, SettingsAction::Timeout1, SettingsAction::Timeout5,
        SettingsAction::Timeout10, SettingsAction::Timeout30, SettingsAction::Save, SettingsAction::Cancel};
    const int x[] = {288, 376, 24, 112, 200, 288, 376, 250, MARGIN};
    const int y[] = {58, 58, 244, 244, 244, 244, 244, 349, 349};
    for (size_t i = 0; i < 9; ++i) {
        lv_obj_t* button = lv_button_create(settings_panel_);
        settings_buttons_[i] = button;
        lv_obj_remove_style_all(button);
        lv_obj_set_pos(button, x[i], y[i]);
        lv_obj_set_size(button, i < 7 ? 80 : 206, i < 2 ? 80 : 64);
        style_panel(button);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_MUTED), 0);
        lv_obj_set_style_text_font(button, i < 2 ? &lv_font_montserrat_30 : &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_letter_space(button, 1, 0);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_TRACK), LV_STATE_PRESSED);
        lv_obj_set_style_border_color(button, lv_color_hex(COLOR_MUTED), LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_TEXT), LV_STATE_CHECKED);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_BG), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_RESET), LV_STATE_CHECKED | LV_STATE_PRESSED);
        lv_obj_set_style_bg_color(button, lv_color_hex(COLOR_PANEL), LV_STATE_DISABLED);
        lv_obj_set_style_text_color(button, lv_color_hex(COLOR_DIM), LV_STATE_DISABLED);
        if (i == 7) lv_obj_add_state(button, LV_STATE_CHECKED);
        settings_bindings_[i] = {this, actions[i]};
        lv_obj_add_event_cb(button, settings_event, LV_EVENT_CLICKED, &settings_bindings_[i]);
        lv_obj_t* label = lv_label_create(button);
        lv_label_set_text(label, labels[i]);
        lv_obj_center(label);
    }
    lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
}

bool DisplayUi::apply_settings(const DisplaySettings& settings, bool visible, bool save_error, bool modified)
{
    DisplayLock lock;
    if (!lock) return false;
    const bool brightness_changed = brightness_percent_ != settings.brightness;
    if (brightness_changed) {
        lv_anim_delete(this, transition_brightness);
        if (!transition_refresh_pending_ && !set_transition_brightness(settings.brightness))
            return false;
        brightness_percent_ = settings.brightness;
    }
    const bool was_visible = settings_visible_.exchange(visible, std::memory_order_relaxed);
    lv_label_set_text_fmt(settings_brightness_, "%u%%", settings.brightness);
    lv_obj_set_state(settings_buttons_[0], LV_STATE_DISABLED, settings.brightness <= 1);
    lv_obj_set_state(settings_buttons_[1], LV_STATE_DISABLED, settings.brightness >= 100);
    lv_obj_set_state(settings_buttons_[7], LV_STATE_DISABLED, !modified && !save_error);
    bool standard_timeout = false;
    for (size_t i = 0; i < 5; ++i) {
        const bool selected = settings.clock_timeout_seconds == CLOCK_TIMEOUT_CHOICES[i];
        lv_obj_set_state(settings_buttons_[i + 2], LV_STATE_CHECKED, selected);
        standard_timeout = standard_timeout || selected;
    }
    if (standard_timeout) lv_label_set_text(settings_timeout_, "AFTER IDLE / MIN");
    else lv_label_set_text_fmt(settings_timeout_, "AFTER IDLE / MIN - CURRENT %lu SEC",
                              static_cast<unsigned long>(settings.clock_timeout_seconds));
    lv_label_set_text(settings_hint_, save_error ? "SAVE FAILED - TRY AGAIN" : "");
    if (visible && !was_visible) {
        lv_obj_remove_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(settings_panel_);
    } else if (!visible) lv_obj_add_flag(settings_panel_, LV_OBJ_FLAG_HIDDEN);
    return true;
}

void DisplayUi::settings_event(lv_event_t* event)
{
    auto* binding = static_cast<SettingsBinding*>(lv_event_get_user_data(event));
    if (binding->ui->settings_visible_.load(std::memory_order_relaxed))
        binding->ui->settings_action_.store(binding->action, std::memory_order_relaxed);
}

void DisplayUi::long_press_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (!ui->settings_visible_.load(std::memory_order_relaxed) &&
        !ui->ota_active_.load(std::memory_order_relaxed) &&
        ui->input_enabled_.load(std::memory_order_relaxed)) {
        ui->settings_action_.store(SettingsAction::Open, std::memory_order_relaxed);
        lv_indev_wait_release(ui->input_device_);
    }
}

void DisplayUi::build_pairing_overlay(lv_obj_t* screen)
{
    pairing_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(pairing_panel_, 0, HEADER_H + 1);
    lv_obj_set_size(
        pairing_panel_, SCREEN_W,
        SCREEN_H - (HEADER_H + 1) - PAIRING_BOTTOM);
    lv_obj_set_style_bg_color(pairing_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(pairing_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pairing_panel_, 0, 0);
    lv_obj_set_style_pad_all(pairing_panel_, 0, 0);
    lv_obj_remove_flag(pairing_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* pairing_card = lv_obj_create(pairing_panel_);
    lv_obj_set_size(pairing_card, 380, 200);
    style_panel(pairing_card, COLOR_PANEL, 18);
    lv_obj_set_style_border_color(
        pairing_card, lv_color_hex(0x2C313B), 0);
    lv_obj_center(pairing_card);

    lv_obj_t* title =
        make_label(
            pairing_card, "PAIRING", 0, 22,
            &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(title, 380);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    pairing_code_ =
        make_label(
            pairing_card, "000000", 0, 56,
            &lv_font_montserrat_30, COLOR_TEXT);
    lv_obj_set_width(pairing_code_, 380);
    lv_obj_set_style_text_align(pairing_code_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(pairing_code_, 10, 0);
    lv_obj_t* instruction =
        make_label(
            pairing_card, "Enter this code on your computer", 0, 119,
            &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(instruction, 380);
    lv_obj_set_style_text_align(instruction, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t* supporting =
        make_label(
            pairing_card, "The panel will connect after confirmation", 0, 157,
            &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_width(supporting, 380);
    lv_obj_set_style_text_align(supporting, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::build_ota_overlay(lv_obj_t* screen)
{
    ota_panel_ = lv_obj_create(screen);
    lv_obj_set_pos(ota_panel_, 0, 0);
    lv_obj_set_size(ota_panel_, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(ota_panel_, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(ota_panel_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ota_panel_, 0, 0);
    lv_obj_set_style_pad_all(ota_panel_, 0, 0);
    lv_obj_remove_flag(ota_panel_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* header = make_label(
        ota_panel_, "FIRMWARE", MARGIN, 18,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(header, 2, 0);
    lv_obj_t* divider = lv_obj_create(ota_panel_);
    lv_obj_set_pos(divider, MARGIN, HEADER_H);
    lv_obj_set_size(divider, CARD_W, 1);
    lv_obj_set_style_bg_color(divider, lv_color_hex(COLOR_LINE), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(divider, 0, 0);
    lv_obj_set_style_pad_all(divider, 0, 0);
    lv_obj_remove_flag(divider, LV_OBJ_FLAG_SCROLLABLE);

    // Confirming and receiving share one card: only the text inside and
    // which secondary row (countdown/note vs. progress bar/hint) is shown
    // changes between the two phases.
    ota_card_ = lv_obj_create(ota_panel_);
    lv_obj_set_pos(ota_card_, MARGIN, OTA_CARD_Y);
    lv_obj_set_size(ota_card_, CARD_W, OTA_CARD_H);
    style_panel(ota_card_);

    ota_card_label_ = make_label(
        ota_card_, "", CARD_PAD, OTA_CARD_ROW_Y,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_style_text_letter_space(ota_card_label_, 1, 0);

    ota_card_countdown_ = make_label(
        ota_card_, "", CARD_PAD, OTA_CARD_ROW_Y,
        &lv_font_montserrat_14, COLOR_RESET);
    lv_obj_set_width(ota_card_countdown_, CARD_W - 2 * CARD_PAD);
    lv_obj_set_style_text_align(ota_card_countdown_, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_letter_space(ota_card_countdown_, 1, 0);

    ota_card_value_ = make_label(
        ota_card_, "", 0, OTA_CARD_VALUE_Y,
        &lv_font_montserrat_48, COLOR_TEXT);
    lv_obj_set_width(ota_card_value_, CARD_W);
    lv_label_set_long_mode(ota_card_value_, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(ota_card_value_, LV_TEXT_ALIGN_CENTER, 0);

    ota_card_note_ = make_label(
        ota_card_, "AUTO-CANCELS", 0, OTA_CARD_THIRD_ROW_Y,
        &lv_font_montserrat_12, COLOR_DIM);
    lv_obj_set_width(ota_card_note_, CARD_W);
    lv_obj_set_style_text_align(ota_card_note_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_card_note_, 1, 0);

    ota_progress_ = lv_bar_create(ota_card_);
    lv_obj_set_pos(ota_progress_, CARD_PAD, OTA_CARD_THIRD_ROW_Y);
    lv_obj_set_size(ota_progress_, CARD_W - 2 * CARD_PAD, BAR_H_DETAIL);
    style_bar(ota_progress_, BAR_H_DETAIL);
    lv_bar_set_value(ota_progress_, 0, LV_ANIM_OFF);

    ota_card_hint_ = make_label(
        ota_card_, "DO NOT POWER OFF", 0, OTA_CARD_HINT_Y,
        &lv_font_montserrat_14, COLOR_TEXT);
    lv_obj_set_width(ota_card_hint_, CARD_W);
    lv_obj_set_style_text_align(ota_card_hint_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_card_hint_, 1, 0);

    ota_confirm_button_ = lv_button_create(ota_panel_);
    lv_obj_set_pos(ota_confirm_button_, MARGIN, OTA_BUTTON_Y);
    lv_obj_set_size(ota_confirm_button_, OTA_BUTTON_W, OTA_BUTTON_H);
    lv_obj_set_style_bg_color(
        ota_confirm_button_, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_radius(ota_confirm_button_, 14, 0);
    lv_obj_add_event_cb(
        ota_confirm_button_, ota_confirm_event, LV_EVENT_CLICKED, this);
    lv_obj_t* label = make_label(
        ota_confirm_button_, "CONFIRM", 0, 0,
        &lv_font_montserrat_16, COLOR_BG);
    lv_obj_center(label);

    ota_deny_button_ = lv_button_create(ota_panel_);
    lv_obj_set_pos(
        ota_deny_button_, MARGIN + OTA_BUTTON_W + MARGIN, OTA_BUTTON_Y);
    lv_obj_set_size(ota_deny_button_, OTA_BUTTON_W, OTA_BUTTON_H);
    lv_obj_set_style_bg_color(ota_deny_button_, lv_color_hex(COLOR_PANEL), 0);
    lv_obj_set_style_border_width(ota_deny_button_, 1, 0);
    lv_obj_set_style_border_color(
        ota_deny_button_, lv_color_hex(COLOR_BORDER), 0);
    lv_obj_set_style_radius(ota_deny_button_, 14, 0);
    lv_obj_add_event_cb(
        ota_deny_button_, ota_deny_event, LV_EVENT_CLICKED, this);
    label = make_label(
        ota_deny_button_, "CANCEL", 0, 0,
        &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_center(label);

    // Verifying/rebooting: plain centered status text, no card.
    ota_title_ = make_label(
        ota_panel_, "", 0, OTA_WAIT_TITLE_Y,
        &lv_font_montserrat_30, COLOR_TEXT);
    lv_obj_set_width(ota_title_, SCREEN_W);
    lv_obj_set_style_text_align(ota_title_, LV_TEXT_ALIGN_CENTER, 0);

    ota_detail_ = make_label(
        ota_panel_, "", 0, OTA_WAIT_DETAIL_Y,
        &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(ota_detail_, SCREEN_W);
    lv_obj_set_style_text_align(ota_detail_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(ota_detail_, 1, 0);

    // Failed: a status dot beside the headline, sized to its content and
    // centered as a unit rather than assuming a fixed text width.
    ota_fail_row_ = lv_obj_create(ota_panel_);
    lv_obj_set_size(ota_fail_row_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(ota_fail_row_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ota_fail_row_, 0, 0);
    lv_obj_set_style_pad_all(ota_fail_row_, 0, 0);
    lv_obj_set_style_pad_column(ota_fail_row_, 12, 0);
    lv_obj_set_flex_flow(ota_fail_row_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        ota_fail_row_, LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(ota_fail_row_, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(ota_fail_row_, LV_ALIGN_TOP_MID, 0, OTA_FAIL_ROW_Y);

    lv_obj_t* fail_dot = lv_obj_create(ota_fail_row_);
    lv_obj_set_size(fail_dot, 14, 14);
    lv_obj_set_style_radius(fail_dot, 7, 0);
    lv_obj_set_style_bg_color(fail_dot, lv_color_hex(COLOR_BAD), 0);
    lv_obj_set_style_bg_opa(fail_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(fail_dot, 0, 0);
    lv_obj_remove_flag(fail_dot, LV_OBJ_FLAG_SCROLLABLE);

    ota_fail_title_ = make_label(
        ota_fail_row_, "UPDATE FAILED", 0, 0,
        &lv_font_montserrat_34, COLOR_BAD);

    lv_obj_add_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
}

void DisplayUi::show_page(Page page, bool animate)
{
    lv_obj_add_flag(overview_panel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provider_pages_[0].panel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(provider_pages_[1].panel, LV_OBJ_FLAG_HIDDEN);
    if (page == Page::Overview) {
        lv_obj_remove_flag(overview_panel_, LV_OBJ_FLAG_HIDDEN);
    } else {
        const Provider provider =
            page == Page::Codex ? Provider::Codex : Provider::Claude;
        lv_obj_remove_flag(
            provider_pages_[provider_index(provider)].panel,
            LV_OBJ_FLAG_HIDDEN);
    }

    const size_t selected = static_cast<size_t>(page);
    lv_obj_set_style_bg_color(
        nav_indicator_,
        lv_color_hex(page == Page::Codex ? COLOR_CODEX
                     : page == Page::Claude ? COLOR_CLAUDE : COLOR_RESET), 0);
    for (size_t i = 0; i < 3; ++i) {
        lv_obj_set_style_text_color(
            nav_labels_[i],
            lv_color_hex(i == selected ? COLOR_RESET : COLOR_DIM), 0);
    }

    // render() calls this on every refresh, so the underline only moves when
    // the page actually changed; otherwise a steady screen would restart the
    // slide once per second.
    if (page != shown_page_) {
        const int32_t target =
            static_cast<int32_t>(selected) * NAV_TAB_PITCH;
        lv_anim_delete(nav_indicator_, nav_indicator_slide);
        if (animate) {
            lv_anim_t slide;
            lv_anim_init(&slide);
            lv_anim_set_var(&slide, nav_indicator_);
            lv_anim_set_exec_cb(&slide, nav_indicator_slide);
            lv_anim_set_values(
                &slide,
                lv_obj_get_style_translate_x(nav_indicator_, LV_PART_MAIN),
                target);
            lv_anim_set_duration(&slide, UI_ANIM_MS);
            lv_anim_set_path_cb(&slide, lv_anim_path_ease_out);
            lv_anim_start(&slide);
        } else {
            nav_indicator_slide(nav_indicator_, target);
        }
        shown_page_ = page;
    }
}

void DisplayUi::update_screensaver_usage(const ScreensaverUsageView& usage)
{
    // Both rows share a horizontal position when either label is visible.
    const bool any_percent =
        usage.rows[0].show_percent || usage.rows[1].show_percent;
    const int bar_x = any_percent ? SAVER_BAR_ALERT_X : SAVER_BAR_X;

    for (int i = 0; i < 2; ++i) {
        const ScreensaverUsageRow& row = usage.rows[i];
        const ScreensaverUsageWidgets& widgets = screensaver_usage_[i];
        const lv_color_t color =
            lv_color_hex(saver_usage_color_hex(row.level));
        lv_obj_set_x(widgets.track, bar_x);
        lv_obj_set_x(widgets.fill, bar_x);
        const int width = row.has_data ? saver_fill_width(row.percent) : 0;
        if (width > 0) {
            lv_obj_set_width(widgets.fill, width);
            lv_obj_set_style_bg_color(widgets.fill, color, 0);
            lv_obj_remove_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.fill, LV_OBJ_FLAG_HIDDEN);
        }
        if (row.show_percent) {
            lv_label_set_text_fmt(widgets.percent, "%u%%", row.percent);
            lv_obj_set_style_text_color(widgets.percent, color, 0);
            lv_obj_remove_flag(widgets.percent, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(widgets.percent, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

void DisplayUi::update_provider(
    ProviderWidgets& widgets, const UsageModel& model,
    Provider provider, bool connected, uint64_t now_ms,
    const ClockView& clock)
{
    const auto state = model.display_state(provider, connected);
    const auto& snapshot = model.snapshot(provider);
    const bool partial = snapshot.source_state == SourceState::Partial;
    const bool fresh = state == DisplayState::Online || state == DisplayState::Partial;
    const bool short_present = snapshot.has_valid_data &&
        snapshot.short_window.present && (!partial || snapshot.latest_short_present);
    const bool week_present = snapshot.has_valid_data &&
        snapshot.week_window.present && (!partial || snapshot.latest_week_present);
    const uint32_t epoch =
        snapshot.has_valid_data ? model.estimated_epoch(provider, now_ms) : 0;
    const uint32_t accent = provider_color(provider);
    const uint8_t short_pct = snapshot.short_window.used_percent;
    const uint8_t week_pct = snapshot.week_window.used_percent;
    const int32_t short_elapsed = short_present
        ? elapsed_percent(snapshot.short_window, SHORT_WINDOW_SECONDS, epoch) : -1;
    const lv_color_t text = lv_color_hex(fresh ? COLOR_TEXT : COLOR_DIM);

    // Rings: severity tiers override the provider colour; a stale sample
    // keeps its cells but fades them so it cannot pass for live data. The
    // white marker shows how much of the 5-hour window has elapsed.
    const uint8_t stale_mix = fresh ? uint8_t{LV_OPA_COVER} : STALE_RING_MIX;
    set_ring_band(
        widgets.ring.rings, RING_OUTER, short_present ? short_pct : 0,
        mix_with_black(ring_color_hex(short_pct, accent), stale_mix),
        fresh ? short_elapsed : -1);
    set_ring_band(
        widgets.ring.rings, RING_INNER, week_present ? week_pct : 0,
        mix_with_black(ring_color_hex(week_pct, accent),
                       fresh ? INNER_RING_MIX : STALE_RING_MIX / 2));

    // The centre shows what is left of the 5-hour window.
    set_percent_text(widgets.ring.value, short_present, 100 - short_pct);
    lv_obj_set_style_text_color(
        widgets.ring.value,
        lv_color_hex(!short_present || !fresh ? COLOR_DIM
                     : ring_color_hex(short_pct, COLOR_TEXT)), 0);
    if (widgets.name_dot != nullptr) {
        lv_obj_set_style_bg_color(
            widgets.name_dot, lv_color_hex(fresh ? accent : COLOR_DIM), 0);
    }

    char text_buffer[24]{};
    if (short_present) {
        format_reset_clock(clock, epoch, snapshot.short_window.reset_at, false,
                           text_buffer, sizeof(text_buffer));
        lv_label_set_text(widgets.short_reset, text_buffer);
        char countdown[16]{};
        format_countdown(snapshot.short_window.has_reset,
            snapshot.short_window.reset_at, epoch, countdown, sizeof(countdown));
        lv_label_set_text_fmt(widgets.short_in, "in %s", countdown);
    } else {
        lv_label_set_text(widgets.short_reset, "--");
        lv_label_set_text(widgets.short_in, "");
    }
    lv_obj_set_style_text_color(widgets.short_reset, short_present ? text
                                : lv_color_hex(COLOR_DIM), 0);

    set_percent_text(widgets.week_value, week_present, week_pct);
    lv_obj_set_style_text_color(
        widgets.week_value,
        lv_color_hex(!week_present || !fresh ? COLOR_DIM : usage_color_hex(week_pct)), 0);

    // Pace: 5-hour usage compared with the elapsed share of the window.
    const bool pace_known = fresh && short_present && short_elapsed >= 0;
    const int32_t pace = pace_known ? int32_t{short_pct} - short_elapsed : 0;
    const bool too_fast = pace_known && pace > PACE_WARN_POINTS;
    const lv_color_t pace_color = lv_color_hex(
        !pace_known ? COLOR_DIM : too_fast ? COLOR_WARN : COLOR_GOOD);

    if (widgets.compact) {
        for (int cell = 0; cell < WEEK_BAR_CELLS; ++cell) {
            const bool lit = week_present &&
                cell < (int(week_pct) * WEEK_BAR_CELLS + 50) / 100;
            lv_obj_set_style_bg_color(
                widgets.week_cells[cell],
                lit ? mix_with_black(ring_color_hex(week_pct, accent),
                                     fresh ? uint8_t{LV_OPA_COVER} : STALE_RING_MIX)
                    : lv_color_hex(COLOR_TRACK), 0);
        }
        if (state != DisplayState::Online) {
            lv_label_set_text(widgets.pace_value, display_state_name(state));
            lv_obj_set_style_text_color(widgets.pace_value, state_color(state), 0);
        } else if (!pace_known) {
            lv_label_set_text(widgets.pace_value, "");
        } else if (too_fast) {
            lv_label_set_text_fmt(widgets.pace_value, "FAST +%ld%%",
                                  static_cast<long>(pace));
            lv_obj_set_style_text_color(widgets.pace_value, pace_color, 0);
        } else {
            lv_label_set_text(widgets.pace_value, "ON PACE");
            lv_obj_set_style_text_color(widgets.pace_value, pace_color, 0);
        }
        return;
    }

    if (week_present) {
        format_reset_clock(clock, epoch, snapshot.week_window.reset_at, true,
                           text_buffer, sizeof(text_buffer));
        lv_label_set_text(widgets.week_reset, text_buffer);
    } else {
        lv_label_set_text(widgets.week_reset, "");
    }
    if (!pace_known) {
        lv_label_set_text(widgets.pace_value, "--");
    } else if (pace > 0) {
        lv_label_set_text_fmt(widgets.pace_value, "+%ld%%", static_cast<long>(pace));
    } else {
        lv_label_set_text(widgets.pace_value, "OK");
    }
    lv_obj_set_style_text_color(widgets.pace_value, pace_color, 0);

    if (state == DisplayState::Online) {
        const uint32_t age = epoch > snapshot.sampled_at ? epoch - snapshot.sampled_at : 0;
        if (age < 60) {
            lv_label_set_text_fmt(widgets.status, "%lus ago",
                static_cast<unsigned long>(age));
        } else {
            lv_label_set_text_fmt(widgets.status, "%lum ago",
                static_cast<unsigned long>(age / 60));
        }
        lv_obj_set_style_text_color(widgets.status, lv_color_hex(COLOR_DIM), 0);
    } else {
        lv_label_set_text(widgets.status, display_state_name(state));
        lv_obj_set_style_text_color(widgets.status, state_color(state), 0);
    }
}

bool DisplayUi::set_transition_brightness(int32_t percent)
{
    const uint8_t level = static_cast<uint8_t>(percent * 255 / 100);
    // CO5300 QSPI encodes brightness register 0x51 with opcode 0x02
    // and an 8-bit address shift.
    constexpr int brightness_command = (0x02 << 24) | (0x51 << 8);
    const esp_err_t error = esp_lcd_panel_io_tx_param(
        panel_io_, brightness_command, &level, sizeof(level));
    if (error != ESP_OK) {
        ESP_LOGE(TAG, "transition brightness failed: %s", esp_err_to_name(error));
        return false;
    }
    return true;
}

void DisplayUi::transition_brightness(void* target, int32_t percent)
{
    static_cast<DisplayUi*>(target)->set_transition_brightness(percent);
}

void DisplayUi::transition_refresh_ready(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (!ui->transition_refresh_pending_) return;
    ui->transition_refresh_pending_ = false;

    // REFR_READY can precede the final DMA completion. lv_anim_start() applies
    // zero brightness synchronously; the SPI parameter command waits for queued
    // pixel transfers to finish before the fade can illuminate the new frame.
    lv_anim_t fade;
    lv_anim_init(&fade);
    lv_anim_set_var(&fade, ui);
    lv_anim_set_exec_cb(&fade, transition_brightness);
    lv_anim_set_values(&fade, 0, ui->brightness_percent_);
    lv_anim_set_duration(&fade, 120);
    lv_anim_set_path_cb(&fade, lv_anim_path_ease_out);
    if (lv_anim_start(&fade) == nullptr) {
        ui->set_transition_brightness(ui->brightness_percent_);
    }
}

void DisplayUi::render(
    const UsageModel& model, Page page, bool connected,
    bool has_passkey, uint32_t passkey, uint64_t now_ms,
    const OtaSnapshot& ota, uint32_t confirm_seconds,
    bool show_ota_error, const PanelPresentation& presentation,
    bool input_enabled)
{
    DisplayLock lock;
    if (!lock) {
        ESP_LOGE(TAG, "failed to acquire display lock for render");
        return;
    }

    const bool ota_visible = ota.phase != OtaPhase::Idle || show_ota_error;
    const bool screensaver_visible =
        presentation.screensaver.active && !has_passkey && !ota_visible;
    const bool was_screensaver_visible =
        !lv_obj_has_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
    if (screensaver_visible != was_screensaver_visible) {
        // Cancel an active fade before concealing the next screensaver repaint.
        lv_anim_delete(this, transition_brightness);
        transition_refresh_pending_ = set_transition_brightness(0);
    }
    input_enabled_.store(input_enabled && !has_passkey, std::memory_order_relaxed);
    pet_overview_set_active(
        panel_on_ && !ota_visible && !screensaver_visible && page == Page::Overview &&
        !(settings_visible_.load(std::memory_order_relaxed) && !has_passkey));
    ota_active_.store(ota.phase != OtaPhase::Idle, std::memory_order_relaxed);
    ota_confirming_.store(
        ota.phase == OtaPhase::Confirming, std::memory_order_relaxed);
    if (ota_visible) {
        lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ota_panel_);

        lv_obj_add_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_countdown_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_note_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_progress_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_card_hint_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_confirm_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_deny_button_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_title_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ota_fail_row_, LV_OBJ_FLAG_HIDDEN);

        if (show_ota_error) {
            const OtaErrorPresentation message =
                ota_error_presentation(ota.error);
            lv_obj_remove_flag(ota_fail_row_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ota_detail_, OTA_FAIL_DETAIL_Y);
            lv_label_set_text(ota_fail_title_, message.title);
            lv_label_set_text(ota_detail_, message.detail);
        } else if (ota.phase == OtaPhase::Confirming) {
            lv_obj_remove_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_countdown_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_note_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_confirm_button_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_deny_button_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ota_card_label_, "NEW VERSION AVAILABLE");
            lv_label_set_text_fmt(
                ota_card_countdown_, "%luS",
                static_cast<unsigned long>(confirm_seconds));
            lv_label_set_text(ota_card_value_, ota.version.c_str());
        } else if (ota.phase == OtaPhase::Receiving) {
            lv_obj_remove_flag(ota_card_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_progress_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_card_hint_, LV_OBJ_FLAG_HIDDEN);
            lv_label_set_text(ota_card_label_, "UPDATING FIRMWARE");
            const uint8_t percent = ota_progress_percent(ota.offset, ota.size);
            lv_label_set_text_fmt(ota_card_value_, "%u%%", percent);
            lv_bar_set_value(ota_progress_, percent, LV_ANIM_OFF);
        } else {
            lv_obj_remove_flag(ota_title_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(ota_detail_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(ota_detail_, OTA_WAIT_DETAIL_Y);
            lv_label_set_text(
                ota_title_, ota.phase == OtaPhase::Verifying
                    ? "VERIFYING" : "REBOOTING");
            lv_label_set_text(ota_detail_, "PLEASE WAIT");
        }
        return;
    }
    lv_obj_add_flag(ota_panel_, LV_OBJ_FLAG_HIDDEN);
    const bool settings_visible = settings_visible_.load(std::memory_order_relaxed) && !has_passkey;
    lv_label_set_text(clock_label_, settings_visible ? "DISPLAY" : presentation.clock.time);
    lv_obj_set_style_text_color(
        clock_label_,
        lv_color_hex(settings_visible || presentation.clock.available ? COLOR_TEXT : COLOR_DIM), 0);

    const bool battery_available = presentation.battery.available;
    const int visible_segments = battery_available
        ? static_cast<int>(presentation.battery.band) : 0;
    const uint32_t battery_color =
        presentation.battery.low_on_battery ? COLOR_WARN : COLOR_RESET;
    lv_obj_set_style_opa(
        charge_symbol_, presentation.battery.charging || presentation.battery.usb_only
            ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    if (battery_available) {
        lv_label_set_text_fmt(battery_label_, "%u%%", presentation.battery.percent);
    } else {
        lv_label_set_text(battery_label_, presentation.battery.usb_only ? "USB" : "--");
    }
    lv_obj_set_style_text_color(
        battery_label_, lv_color_hex(battery_available || presentation.battery.usb_only
                                     ? battery_color : COLOR_DIM), 0);
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_style_bg_color(
            battery_segments_[i],
            lv_color_hex(i < visible_segments
                ? battery_color : COLOR_BATTERY_OFF), 0);
        lv_obj_set_style_bg_opa(battery_segments_[i], LV_OPA_COVER, 0);
    }

    if (settings_visible) {
        lv_label_set_text(connection_label_, connected ? "LINKED" : "WAITING");
        lv_obj_set_style_bg_color(status_dot_, lv_color_hex(connected ? COLOR_GOOD : COLOR_MUTED), 0);
        lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    if (screensaver_visible) {
        lv_label_set_text(screensaver_time_, presentation.clock.time);
        lv_label_set_text(
            screensaver_weekday_, presentation.clock.available
                ? presentation.clock.weekday : "---");
        lv_label_set_text(
            screensaver_month_day_, presentation.clock.available
                ? presentation.clock.month_day : "-- --");
        lv_obj_set_style_text_color(
            screensaver_time_,
            lv_color_hex(presentation.clock.available ? COLOR_MUTED : COLOR_DIM),
            0);
        const ScreensaverUsageView usage = make_screensaver_usage_view(model, connected);
        update_screensaver_usage(usage);
        lv_label_set_text(
            screensaver_hint_,
            usage.offline ? (connected ? "DATA UNAVAILABLE" : "OFFLINE")
                          : "TAP OR SHAKE TO WAKE");
        lv_obj_set_style_opa(
            screensaver_hint_, usage.offline ? static_cast<lv_opa_t>(LV_OPA_COVER)
                : presentation.screensaver.hint_opacity, 0);
        lv_obj_set_style_translate_x(
            screensaver_content_, presentation.screensaver.offset_x, 0);
        lv_obj_set_style_translate_y(
            screensaver_content_, presentation.screensaver.offset_y, 0);
        if (presentation.battery.low_on_battery) {
            lv_obj_remove_flag(screensaver_low_group_, LV_OBJ_FLAG_HIDDEN);
            const int low_segments = static_cast<int>(presentation.battery.band);
            for (int i = 0; i < 4; ++i) {
                lv_obj_set_style_bg_color(
                    screensaver_low_segments_[i],
                    lv_color_hex(i < low_segments
                        ? COLOR_LOW_DIM : COLOR_LINE), 0);
                lv_obj_set_style_bg_opa(
                    screensaver_low_segments_[i], LV_OPA_COVER, 0);
            }
        } else {
            lv_obj_add_flag(screensaver_low_group_, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(screensaver_panel_);
        return;
    }
    lv_obj_add_flag(screensaver_panel_, LV_OBJ_FLAG_HIDDEN);
    show_page(page, /*animate=*/true);
    pet_view_t pet_view{};
    pet_view.connected = connected;
    pet_view.provider[0] = pet_provider_view(model, Provider::Codex, connected, now_ms);
    pet_view.provider[1] = pet_provider_view(model, Provider::Claude, connected, now_ms);
    pet_overview_update(&pet_view);
    for (Provider provider : {Provider::Codex, Provider::Claude}) {
        const size_t index = provider_index(provider);
        update_provider(
            provider_pages_[index], model, provider, connected, now_ms,
            presentation.clock);
    }

    if (has_passkey) {
        lv_label_set_text(connection_label_, "PAIRING");
        lv_label_set_text_fmt(
            pairing_code_, "%06lu", static_cast<unsigned long>(passkey));
        lv_obj_remove_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(pairing_panel_);
    } else {
        lv_label_set_text(connection_label_, connected ? "LINKED" : "WAITING");
        lv_obj_add_flag(pairing_panel_, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(
        status_dot_,
        lv_color_hex(
            has_passkey ? COLOR_WARN : connected ? COLOR_GOOD : COLOR_MUTED),
        0);
}

void DisplayUi::tab_event(lv_event_t* event)
{
    auto* binding =
        static_cast<TabBinding*>(lv_event_get_user_data(event));
    if (binding->ui->settings_visible_.load(std::memory_order_relaxed) ||
        binding->ui->ota_active_.load(std::memory_order_relaxed) ||
        !binding->ui->input_enabled_.load(std::memory_order_relaxed)) return;
    binding->ui->requested_page_.store(
        page_from_tab(
            binding->tab_index,
            binding->ui->requested_page_.load(std::memory_order_relaxed)),
        std::memory_order_relaxed);
}

void DisplayUi::gesture_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->settings_visible_.load(std::memory_order_relaxed) ||
        ui->ota_active_.load(std::memory_order_relaxed) ||
        !ui->input_enabled_.load(std::memory_order_relaxed)) return;
    lv_indev_t* indev = lv_indev_active();
    if (indev == nullptr) {
        return;
    }
    const lv_dir_t direction = lv_indev_get_gesture_dir(indev);
    TouchDirection detected_direction;
    switch (direction) {
    case LV_DIR_LEFT:
        detected_direction = TouchDirection::Left;
        break;
    case LV_DIR_RIGHT:
        detected_direction = TouchDirection::Right;
        break;
    case LV_DIR_TOP:
        detected_direction = TouchDirection::Up;
        break;
    case LV_DIR_BOTTOM:
        detected_direction = TouchDirection::Down;
        break;
    default:
        return;
    }

    const auto swipe = navigation_swipe(detected_direction);
    if (!swipe.has_value()) {
        return;
    }

    const Page current =
        ui->requested_page_.load(std::memory_order_relaxed);
    ui->requested_page_.store(
        page_after_swipe(current, *swipe), std::memory_order_relaxed);
    lv_indev_wait_release(indev);
}

void DisplayUi::touch_down_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    ui->touch_down_.store(true, std::memory_order_relaxed);
}

void DisplayUi::ota_confirm_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->ota_confirming_.load(std::memory_order_relaxed)) {
        ui->ota_confirmation_requested_.store(true, std::memory_order_relaxed);
    }
}

void DisplayUi::ota_deny_event(lv_event_t* event)
{
    auto* ui = static_cast<DisplayUi*>(lv_event_get_user_data(event));
    if (ui->ota_confirming_.load(std::memory_order_relaxed)) {
        ui->ota_denial_requested_.store(true, std::memory_order_relaxed);
    }
}

}  // namespace usage_panel::amoled
