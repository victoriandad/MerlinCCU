#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Status root and status readout pages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, centred text, graph helpers) stay there; this file owns only the
/// Status-page-specific formatting and layout.
namespace status_screens
{

/// @brief Draws whichever Status page is currently active.
/// @details `now_ms` is the caller's boot-uptime sample (`to_ms_since_boot`)
/// used for the Sensors/Integrations pages' data-freshness text -- passed in
/// rather than read here so this file has no Pico SDK dependency (issue
/// #71).
void draw_status_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

} // namespace status_screens
