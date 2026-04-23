#pragma once

#include <cstdint>

namespace screensaver_starfield
{

/// @brief Initializes the starfield screensaver state.
void init();

/// @brief Advances the starfield screensaver and renders one frame.
void step_and_render(uint8_t* fb);

} // namespace screensaver_starfield
