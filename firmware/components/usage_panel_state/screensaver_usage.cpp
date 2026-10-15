#include "usage_panel_state/screensaver_usage.hpp"


namespace usage_panel {
namespace {
constexpr uint8_t WARN_PERCENT = 80;
constexpr uint8_t BAD_PERCENT  = 95;
}  // namespace

bool ScreensaverUsageRow::operator==(const ScreensaverUsageRow& other) const
{
    return has_data == other.has_data && percent == other.percent &&
           show_percent == other.show_percent && level == other.level;
}

bool ScreensaverUsageView::operator==(const ScreensaverUsageView& other) const
{
    for (size_t i = 0; i < kProviderCount; ++i) {
        if (!(rows[i] == other.rows[i])) return false;
    }
    return offline == other.offline;
}

ScreensaverUsageLevel screensaver_usage_level(uint8_t percent)
{
    if (percent >= BAD_PERCENT) return ScreensaverUsageLevel::Bad;
    if (percent >= WARN_PERCENT) return ScreensaverUsageLevel::Warn;
    return ScreensaverUsageLevel::Base;
}

ScreensaverUsageRow make_screensaver_usage_row(const UsageModel& model, Provider provider, bool connected)
{
    ScreensaverUsageRow row{};
    const DisplayState state =
        model.display_state(provider, connected);
    if (state != DisplayState::Online && state != DisplayState::Partial) {
        return row;
    }

    // Only windows from the last valid publication participate in selection.
    const ProviderSnapshot& snapshot = model.snapshot(provider);
    const UsageWindow* fill_window = nullptr;
    if (snapshot.latest_short_present) {
        fill_window = &snapshot.short_window;
    } else if (snapshot.latest_week_present) {
        fill_window = &snapshot.week_window;
    }
    if (fill_window == nullptr) return row;

    row.has_data = true;
    row.percent = fill_window->used_percent;
    row.level = screensaver_usage_level(row.percent);
    row.show_percent = row.percent >= WARN_PERCENT;
    return row;
}

ScreensaverUsageView make_screensaver_usage_view(const UsageModel& model, bool connected)
{
    ScreensaverUsageView view{};
    bool any_percent = false;
    bool any_data = false;
    for (Provider provider : kProviders) {
        ScreensaverUsageRow& row = view.rows[provider_index(provider)];
        row = make_screensaver_usage_row(model, provider, connected);
        any_percent = any_percent || row.show_percent;
        any_data = any_data || row.has_data;
    }

    // Showing every label keeps the shared columns aligned during an alert.
    if (any_percent) {
        for (ScreensaverUsageRow& row : view.rows) {
            if (row.has_data) row.show_percent = true;
        }
    }

    view.offline = !any_data;
    return view;
}

}  // namespace usage_panel
