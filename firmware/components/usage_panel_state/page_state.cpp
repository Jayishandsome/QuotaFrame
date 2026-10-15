#include "usage_panel_state/page_state.hpp"

namespace usage_panel {

TouchAction route_touch(OtaPhase phase, OtaTouchControl control)
{
    if (phase == OtaPhase::Idle) return TouchAction::Navigate;
    if (phase == OtaPhase::Confirming) {
        if (control == OtaTouchControl::Confirm) return TouchAction::Confirm;
        if (control == OtaTouchControl::Deny) return TouchAction::Deny;
    }
    return TouchAction::Ignore;
}

const char* page_name(Page page)
{
    switch (page) {
    case Page::Codex: return "codex";
    case Page::Claude: return "claude";
    case Page::Gemini: return "gemini";
    case Page::Grok: return "grok";
    default: return "overview";
    }
}

Page page_for_provider(Provider provider)
{
    return static_cast<Page>(provider_index(provider) + 1);
}

Provider provider_for_page(Page page)
{
    const size_t index = static_cast<size_t>(page);
    return index >= 1 && index < kPageCount ? kProviders[index - 1]
                                            : Provider::Codex;
}

Page page_from_tab(uint8_t tab_index, Page fallback)
{
    return tab_index < kPageCount ? static_cast<Page>(tab_index) : fallback;
}

Page page_after_swipe(Page page, SwipeDirection direction)
{
    const size_t index = static_cast<size_t>(page) % kPageCount;
    const size_t next = direction == SwipeDirection::Next
        ? (index + 1) % kPageCount
        : (index + kPageCount - 1) % kPageCount;
    return static_cast<Page>(next);
}

}  // namespace usage_panel
