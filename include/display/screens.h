#pragma once

#include <cstdint>

#include "console_model.h"

namespace screens
{

/// @brief Draws a simple geometry test pattern.
/// @details This is useful when checking orientation, clipping and obvious
/// timing issues on the physical panel.
void draw_demo_screen(uint8_t* fb);

/// @brief Draws the current Merlin CCU menu page and contextual softkeys.
/// @details This is the main UI surface used for menu bring-up and navigation.
/// `now_ms` is the caller's boot-uptime sample (`to_ms_since_boot`), threaded
/// down to whichever page family's data-freshness text needs it -- passed in
/// rather than read by the render layer itself so screens.cpp and its
/// sibling page-family files have no Pico SDK dependency and are
/// host-testable (issue #71).
void draw_menu_screen(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

/// @brief Draws a static calibration pattern for timing and visible extents.
/// @details This pattern is intended for photographing the panel so the true
/// usable area, centering, clipping and line stability can be assessed.
void draw_calibration_screen(uint8_t* fb);

} // namespace screens
