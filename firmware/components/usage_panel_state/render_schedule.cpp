#include "usage_panel_state/render_schedule.hpp"

#include <cstring>

namespace usage_panel {

bool TimedRenderKey::operator==(const TimedRenderKey& other) const
{
    for (size_t i = 0; i < kProviderCount; ++i) {
        if (states[i] != other.states[i] ||
            std::strcmp(overview_countdowns[i], other.overview_countdowns[i]) != 0) {
            return false;
        }
    }
    return page == other.page &&
           std::strcmp(short_countdown, other.short_countdown) == 0 &&
           std::strcmp(week_countdown, other.week_countdown) == 0 &&
           presentation == other.presentation;
}

TimedRenderKey make_timed_render_key(
    const UsageModel& model, Page page, bool connected, uint64_t now_ms,
    const PanelPresentation& presentation)
{
    TimedRenderKey key{};
    key.page = page;
    key.presentation = presentation;

    // Every provider remains visible in the overview and screensaver, even
    // when the screensaver was entered from a single-provider detail page.
    if (page == Page::Overview || presentation.screensaver.active) {
        for (size_t i = 0; i < kProviderCount; ++i) {
            const Provider provider = kProviders[i];
            key.states[i] = model.display_state(provider, connected);
            // Non-online cards display status text instead of a countdown.
            // Single-window providers only ever publish partial samples, so
            // for them a partial card counts as online.
            const bool single_window =
                provider_window_labels(provider).week_compact == nullptr;
            const bool counts_down =
                key.states[i] == DisplayState::Online ||
                (single_window && key.states[i] == DisplayState::Partial);
            if (presentation.screensaver.active || !counts_down) continue;
            const auto& window = model.snapshot(provider).short_window;
            format_countdown(window.has_reset, window.reset_at,
                model.estimated_epoch(provider, now_ms),
                key.overview_countdowns[i], sizeof(key.overview_countdowns[i]));
        }
        return key;
    }

    const Provider provider = provider_for_page(page);
    key.states[0] = model.display_state(provider, connected);

    const auto& snapshot = model.snapshot(provider);
    if (!snapshot.has_valid_data) {
        return key;
    }

    const uint32_t epoch = model.estimated_epoch(provider, now_ms);
    format_countdown(
        snapshot.short_window.present && snapshot.short_window.has_reset,
        snapshot.short_window.reset_at, epoch,
        key.short_countdown, sizeof(key.short_countdown));
    format_countdown(
        snapshot.week_window.present && snapshot.week_window.has_reset,
        snapshot.week_window.reset_at, epoch,
        key.week_countdown, sizeof(key.week_countdown));
    return key;
}

}  // namespace usage_panel
