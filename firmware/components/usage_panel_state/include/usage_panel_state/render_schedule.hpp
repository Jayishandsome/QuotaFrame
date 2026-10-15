#pragma once

#include <cstdint>

#include "usage_panel_state/page_state.hpp"
#include "usage_panel_state/panel_presentation.hpp"
#include "usage_core/usage_state.hpp"

namespace usage_panel {

/**
 * Visible state used to decide whether the AMOLED actually needs a redraw.
 *
 * Time is polled independently, but only changes to provider state/countdowns
 * or panel presentation (clock, battery, screensaver) alter this key. This
 * keeps freshness accurate without refreshing the display every loop tick.
 */
struct TimedRenderKey {
    Page page = Page::Overview;
    // Overview and screensaver: one state per provider in kProviders order.
    // Detail pages: states[0] is the shown provider and the rest stay NoData.
    DisplayState states[kProviderCount]{};
    char overview_countdowns[kProviderCount][16]{};  // Short-window labels.
    char short_countdown[16]{};
    char week_countdown[16]{};
    PanelPresentation presentation{};

    bool operator==(const TimedRenderKey& other) const;
    bool operator!=(const TimedRenderKey& other) const
    {
        return !(*this == other);
    }
};

/** Builds the time-sensitive render key for the current page and panel state. */
TimedRenderKey make_timed_render_key(
    const UsageModel& model, Page page, bool connected, uint64_t now_ms,
    const PanelPresentation& presentation = PanelPresentation{});

}  // namespace usage_panel
