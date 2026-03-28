#pragma once

#include <cstdint>

#include "hardware/pio.h"

namespace display {

/// @brief Initializes the panel scanout engine.
/// @details This sets up the PIO state machine and the DMA loop that repeatedly
/// streams the prepared raster buffer to the display.
void init(PIO pio, uint sm, uint offset, uint pin_base);

/// @brief Updates the active PIO clock divider at runtime.
/// @details This is useful for timing sweeps while keeping the display content static.
void set_clkdiv(float clkdiv);

/// @brief Queues one UI framebuffer for display.
/// @details The caller supplies a portrait-oriented UI framebuffer. This module
/// converts it into the panel's native electrical scan order and arranges for
/// the new raster to be adopted at a safe frame boundary.
void present(const uint8_t* ui_fb);

}  // namespace display
