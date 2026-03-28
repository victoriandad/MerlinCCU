#pragma once

#include <cstdint>

#include "console_model.h"

namespace screens {

/// @brief Draws a simple geometry test pattern.
/// @details This is useful when checking orientation, clipping and obvious
/// timing issues on the physical panel.
void draw_demo_screen(uint8_t* fb);

/// @brief Draws a mock Merlin CCU menu screen.
/// @details This is a static layout used for bring-up and UI experimentation.
void draw_dummy_menu_screen(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws a static calibration pattern for timing and visible extents.
/// @details This pattern is intended for photographing the panel so the true
/// usable area, centering, clipping and line stability can be assessed.
void draw_calibration_screen(uint8_t* fb);

}  // namespace screens
