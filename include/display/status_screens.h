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
void draw_status_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace status_screens
