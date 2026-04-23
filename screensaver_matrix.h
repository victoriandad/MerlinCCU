#pragma once

#include <cstdint>

namespace screensaver_matrix
{

/// @brief Initializes the falling-glyph Matrix screensaver state.
void init();

/// @brief Advances the Matrix screensaver and renders one frame.
void step_and_render(uint8_t* fb);

} // namespace screensaver_matrix
