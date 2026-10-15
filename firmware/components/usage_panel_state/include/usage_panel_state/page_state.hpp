#pragma once

#include <cstdint>

#include "usage_core/usage_state.hpp"
#include "usage_ota/session.hpp"

namespace usage_panel {

/** Overview followed by one detail page per provider, in kProviders order. */
enum class Page : uint8_t { Overview, Codex, Claude, Gemini, Grok };

/** Number of Page values, including Overview. */
constexpr size_t kPageCount = kProviderCount + 1;
enum class SwipeDirection : uint8_t { Previous, Next };
enum class TouchAction : uint8_t { Navigate, Confirm, Deny, Ignore };
enum class OtaTouchControl : uint8_t { None, Confirm, Deny };

const char* page_name(Page page);
/** Returns the page showing one provider's details. */
Page page_for_provider(Provider provider);
/** Returns the provider shown by a detail page; Overview maps to Codex. */
Provider provider_for_page(Page page);
Page page_from_tab(uint8_t tab_index, Page fallback);
Page page_after_swipe(Page page, SwipeDirection direction);
TouchAction route_touch(OtaPhase phase, OtaTouchControl control);

}  // namespace usage_panel
