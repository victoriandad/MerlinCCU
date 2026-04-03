#pragma once

#include <cstdint>

#include "console_model.h"

namespace screen_banners {

/// @brief Draws the shared top banner used by Merlin CCU pages.
/// @param fb Target framebuffer in UI coordinates.
/// @param title Null-terminated title text shown in the header banner.
void draw_header_banner(uint8_t* fb, const char* title);

/// @brief Draws the shared footer banner used by Merlin CCU pages.
/// @details The footer currently shows the synced NTP time when available and
/// the current internet reachability icon derived from the Wi-Fi state.
/// @param fb Target framebuffer in UI coordinates.
/// @param console_state Console state used to populate footer status content.
void draw_footer_banner(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws the standard Merlin CCU page chrome.
/// @details This is the common banner treatment intended for reuse by future
/// pages so the shared framing and status content stay consistent.
/// @param fb Target framebuffer in UI coordinates.
/// @param console_state Console state used to populate shared status content.
/// @param title Null-terminated title text shown in the header banner.
void draw_standard_banners(uint8_t* fb, const ConsoleState& console_state, const char* title);

}  // namespace screen_banners
