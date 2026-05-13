#pragma once

#include <cstdint>

#include "console_model.h"

namespace screen_banners
{

/// @brief Draws the shared top banner used by Merlin CCU pages.
/// @param fb Target framebuffer in UI coordinates.
/// @param console_state Console state used to populate the header status area.
/// @param title Null-terminated title text shown in the header banner.
void draw_header_banner(uint8_t* fb, const ConsoleState& console_state, const char* title);

/// @brief Draws the standard Merlin CCU page chrome.
/// @details This is the common banner treatment intended for reuse by future
/// pages so the shared framing and status content stay consistent while the
/// lower screen area remains available for a future scratchpad.
/// @param fb Target framebuffer in UI coordinates.
/// @param console_state Console state used to populate shared status content.
/// @param title Null-terminated title text shown in the header banner.
void draw_standard_banners(uint8_t* fb, const ConsoleState& console_state, const char* title);

} // namespace screen_banners
