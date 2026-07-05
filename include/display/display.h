#pragma once

#include <cstdint>

#include "hardware/pio.h"

namespace display
{

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
/// the new raster to be adopted at a safe frame boundary. If a previously
/// queued raster has not yet been adopted, this call is skipped rather than
/// risking a torn buffer handed to DMA mid-swap — see `present_skipped_count()`.
void present(const uint8_t* ui_fb);

/// @brief Returns the number of physical frame-boundary interrupts observed.
/// @details Increments once per real panel frame, driven by the DMA control
/// channel's IRQ. Exists to measure actual scanout frame rate on hardware
/// rather than assume it — see docs/greyscale-investigation.md's open
/// questions and docs/multicore-raster-regen-design.md.
uint32_t frame_count();

/// @brief Returns how long the most recent raster rebuild took, in microseconds.
/// @details Measures `rebuild_raster_from_fb` inside `present()`. Used to
/// estimate whether a per-frame raster regeneration scheme (temporal
/// dithering, dirty-line redraw) could fit inside one physical frame period.
uint32_t last_rebuild_us();

/// @brief Returns how many `present()` calls were skipped because a
/// previously queued raster had not yet been adopted.
/// @details A high count relative to `frame_count()` means callers are
/// presenting faster than the panel can adopt frames.
uint32_t present_skipped_count();

} // namespace display
