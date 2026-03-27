#pragma once

#include <cstdint>

namespace screens {

/// @brief Draws a simple geometry test pattern.
/// @details This is useful when checking orientation, clipping and obvious
/// timing issues on the physical panel.
void draw_demo_screen(uint8_t* fb);

/// @brief Draws a mock Merlin CCU menu screen.
/// @details This is a static layout used for bring-up and UI experimentation.
void draw_dummy_menu_screen(uint8_t* fb);

}  // namespace screens
