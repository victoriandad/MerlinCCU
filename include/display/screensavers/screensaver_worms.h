#pragma once

#include <cstdint>

namespace screensaver_worms
{

/// @brief Initializes the wandering worms screensaver state.
void init();

/// @brief Advances the worms screensaver and renders one frame.
void step_and_render(uint8_t* fb);

} // namespace screensaver_worms
