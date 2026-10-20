#pragma once

#include <atomic>
#include <cstdint>

#include "esp_lcd_touch.h"
#include "esp_lcd_panel_io.h"
#include "lvgl.h"
#include "orientation.hpp"
#include "usage_panel_state/display_settings.hpp"
#include "usage_panel_state/page_state.hpp"
#include "usage_panel_state/panel_presentation.hpp"
#include "usage_panel_state/screensaver_usage.hpp"
#include "usage_core/usage_state.hpp"
#include "usage_ota/session.hpp"

namespace usage_panel::amoled {

/**
 * Owns the LVGL object tree and the narrow event boundary to the app loop.
 *
 * LVGL callbacks never mutate application/OTA state directly. They publish
 * page requests or one-shot atomic event flags, which app_main consumes and
 * applies on its own loop. The flags carry no associated payload, so relaxed
 * ordering is sufficient for these event latches.
 */
/** What the Claude desktop app is asking, as the UI shows it. */
struct ClaudePromptState {
    bool show = false;          // a permission decision is pending
    bool working = false;       // a session is generating
    char tool[48]{};
    char hint[192]{};
    uint32_t waiting = 0;
    uint8_t reply = 0;          // claude_reply_t
    uint32_t serial = 0;        // changes with every new prompt
};

/** Button pressed on the Claude permission card. */
enum class ClaudeDecision : uint8_t { None, Allow, Deny };

class DisplayUi {
public:
    /** Takes effect on the next render(). */
    void set_claude(const ClaudePromptState& state) { claude_ = state; }
    /** Consumes one Allow/Deny press from the Claude permission card. */
    ClaudeDecision take_claude_decision();

    /** Initializes display/touch/LVGL and builds the complete object tree. */
    bool begin(ScreenRotation initial_rotation);

    void request_page(Page page) { requested_page_.store(page, std::memory_order_relaxed); }
    bool apply_settings(const DisplaySettings& settings, bool visible, bool save_error = false, bool modified = false);
    SettingsAction take_settings_action()
    {
        return settings_action_.exchange(SettingsAction::None, std::memory_order_relaxed);
    }

    /** Returns the latest page requested by tabs or gestures. */
    Page requested_page() const
    {
        return requested_page_.load(std::memory_order_relaxed);
    }

    /**
     * Renders the current application snapshot and interaction state.
     *
     * Screensaver visibility changes temporarily darken the panel. Brightness
     * is restored asynchronously after the repaint is transferred.
     */
    void render(const UsageModel& model, Page page, bool connected,
                bool has_passkey, uint32_t passkey, uint64_t now_ms,
                const OtaSnapshot& ota, uint32_t confirm_seconds,
                bool show_ota_error, const PanelPresentation& presentation,
                bool input_enabled);

    /** Consumes one pending touch-down event. */
    bool take_touch_down()
    {
        return touch_down_.exchange(false, std::memory_order_relaxed);
    }

    /**
     * Switches the AMOLED on or off. Off sets brightness to zero and sends the
     * panel's display-off command; on restores both and forces a repaint.
     */
    bool set_panel_power(bool on);

    /** Applies display and touch transforms together for a new rotation. */
    bool set_rotation(ScreenRotation rotation);
#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
    void run_memory_probe();
#endif

    /** Consumes one pending on-device OTA confirmation request. */
    bool take_ota_confirmation()
    {
        return ota_confirmation_requested_.exchange(
            false, std::memory_order_relaxed);
    }

    /** Consumes one pending on-device OTA denial request. */
    bool take_ota_denial()
    {
        return ota_denial_requested_.exchange(
            false, std::memory_order_relaxed);
    }

    /**
     * Concentric pixel rings on one canvas: the outer band is the short
     * (5-hour) window and the inner band the weekly window, with the short
     * percentage centred.
     */
    struct RingWidgets {
        lv_obj_t* rings = nullptr;
        lv_obj_t* value = nullptr;
    };

    /** Cells in the overview's weekly usage bar. */
    static constexpr int WEEK_BAR_CELLS = 21;

    /**
     * Widgets for one provider. The overview column (compact) and the detail
     * page share this shape; members a layout does not use stay null.
     */
    struct ProviderWidgets {
        bool compact = false;
        lv_obj_t* panel = nullptr;
        // Overview: link/data state under the pace line, empty while online.
        // Detail: sample age, or the link/data state when not online.
        lv_obj_t* status = nullptr;
        lv_obj_t* name_dot = nullptr;
        RingWidgets ring;
        lv_obj_t* short_reset = nullptr;     // Local clock time of the 5-hour reset.
        lv_obj_t* short_in = nullptr;        // Countdown to the 5-hour reset.
        lv_obj_t* week_value = nullptr;      // Weekly percentage used.
        lv_obj_t* week_reset = nullptr;      // Detail: weekday and time of the weekly reset.
        lv_obj_t* pace_value = nullptr;      // 5-hour usage ahead of elapsed time, or OK.
        lv_obj_t* week_cells[WEEK_BAR_CELLS]{};  // Overview: weekly usage bar.
    };

    struct TabBinding {
        DisplayUi* ui = nullptr;
        uint8_t tab_index = 0;
    };

private:
    struct ScreensaverUsageWidgets {
        // The track moves with the fill: the pair slides half a percentage
        // slot when a number appears, so the ink stays centred in both states.
        lv_obj_t* track = nullptr;
        lv_obj_t* fill = nullptr;
        lv_obj_t* percent = nullptr;
    };

    void build_settings(lv_obj_t* screen);
    static void settings_event(lv_event_t* event);
    static void long_press_event(lv_event_t* event);
    void build_header(lv_obj_t* screen);
    void build_overview(lv_obj_t* screen);
    void build_provider_page(lv_obj_t* screen, Provider provider);
    void build_navigation(lv_obj_t* screen);
    void build_screensaver(lv_obj_t* screen);
    void build_pairing_overlay(lv_obj_t* screen);
    void build_ota_overlay(lv_obj_t* screen);
    void show_page(Page page, bool animate);
    void update_provider(ProviderWidgets& widgets, const UsageModel& model,
                         Provider provider, bool connected, uint64_t now_ms,
                         const ClockView& clock);
    void update_screensaver_usage(const ScreensaverUsageView& usage);

    bool set_transition_brightness(int32_t percent);
    static void transition_brightness(void* target, int32_t percent);
    static void transition_refresh_ready(lv_event_t* event);

    static void tab_event(lv_event_t* event);
    static void gesture_event(lv_event_t* event);
    static void touch_down_event(lv_event_t* event);
    static void ota_confirm_event(lv_event_t* event);
    static void ota_deny_event(lv_event_t* event);
#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
    static void probe_refresh_event(lv_event_t* event);
    void start_next_probe_refresh();
#endif

    struct SettingsBinding { DisplayUi* ui; SettingsAction action; };
    SettingsBinding settings_bindings_[9]{};
    std::atomic<SettingsAction> settings_action_{SettingsAction::None};
    std::atomic<bool> settings_visible_{false};
    lv_obj_t* settings_panel_ = nullptr;
    lv_obj_t* settings_brightness_ = nullptr;
    lv_obj_t* settings_buttons_[9]{};
    lv_obj_t* settings_timeout_ = nullptr;
    lv_obj_t* settings_hint_ = nullptr;
    uint8_t brightness_percent_ = 50;  // Serialized by the LVGL lock.
    std::atomic<Page> requested_page_{Page::Overview};
    std::atomic<bool> ota_active_{false};
    std::atomic<bool> ota_confirming_{false};
    std::atomic<bool> ota_confirmation_requested_{false};
    std::atomic<bool> ota_denial_requested_{false};
    std::atomic<bool> touch_down_{false};
    std::atomic<bool> input_enabled_{true};
    lv_display_t* display_ = nullptr;
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;
    bool panel_on_ = true;  // Serialized by the LVGL lock.
    // Access from render() and LVGL callbacks is serialized by the LVGL lock.
    bool transition_refresh_pending_ = false;
    lv_indev_t* input_device_ = nullptr;
    esp_lcd_touch_handle_t touch_ = nullptr;
    ScreenRotation rotation_ = ScreenRotation::Deg0;
    lv_obj_t* connection_label_ = nullptr;
    lv_obj_t* status_dot_ = nullptr;
    lv_obj_t* clock_label_ = nullptr;
    lv_obj_t* battery_group_ = nullptr;
    lv_obj_t* charge_symbol_ = nullptr;
    lv_obj_t* battery_label_ = nullptr;
    lv_obj_t* battery_segments_[4]{};
    lv_obj_t* overview_panel_ = nullptr;
    ProviderWidgets overview_[2]{};
    ProviderWidgets provider_pages_[2]{};
    lv_obj_t* nav_labels_[3]{};
    // One underline shared by the three tabs, slid between them by a translate
    // transform. It is built parked under Page::Overview, so shown_page_ starts
    // there and that page's translation is zero.
    lv_obj_t* nav_indicator_ = nullptr;
    Page shown_page_ = Page::Overview;
    lv_obj_t* screensaver_panel_ = nullptr;
    lv_obj_t* screensaver_content_ = nullptr;
    lv_obj_t* screensaver_time_ = nullptr;
    lv_obj_t* screensaver_weekday_ = nullptr;
    lv_obj_t* screensaver_month_day_ = nullptr;
    lv_obj_t* screensaver_hint_ = nullptr;
    ScreensaverUsageWidgets screensaver_usage_[2]{};
    lv_obj_t* screensaver_low_group_ = nullptr;
    lv_obj_t* screensaver_low_segments_[4]{};
    lv_obj_t* pairing_panel_ = nullptr;
    lv_obj_t* pairing_code_ = nullptr;
    lv_obj_t* ota_panel_ = nullptr;
    lv_obj_t* ota_card_ = nullptr;
    lv_obj_t* ota_card_label_ = nullptr;
    lv_obj_t* ota_card_countdown_ = nullptr;
    lv_obj_t* ota_card_value_ = nullptr;
    lv_obj_t* ota_card_note_ = nullptr;
    lv_obj_t* ota_card_hint_ = nullptr;
    lv_obj_t* ota_title_ = nullptr;
    lv_obj_t* ota_detail_ = nullptr;
    lv_obj_t* ota_progress_ = nullptr;
    lv_obj_t* ota_confirm_button_ = nullptr;
    lv_obj_t* ota_deny_button_ = nullptr;
    lv_obj_t* ota_fail_row_ = nullptr;
    lv_obj_t* ota_fail_title_ = nullptr;
#if CONFIG_WS_USAGE_PANEL_MEMORY_PROBE
    uint32_t probe_refresh_sample_ = 0;
    int64_t probe_refresh_started_us_ = 0;
    bool probe_refresh_active_ = false;
#endif
    TabBinding tab_bindings_[3]{};
    ClaudePromptState claude_{};  // Serialized by the app loop and the LVGL lock.
};

}  // namespace usage_panel::amoled
