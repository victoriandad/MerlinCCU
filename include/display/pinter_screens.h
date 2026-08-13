#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Pinter brewing-assistant pages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (paged
/// navigation arrows, centred text) stay there in `screens_shared.h`; this
/// file owns only the Pinter-page-specific formatting and layout.
namespace pinter_screens
{

/// @brief Draws only non-data navigation affordances for Pinter pages.
void draw_pinter_page(uint8_t* fb, const ConsoleState& console_state);

} // namespace pinter_screens
