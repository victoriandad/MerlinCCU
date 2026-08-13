#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Shares watchlist and share-detail pages (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, centred text, graph plotting) stay there in `screens_shared.h`; this
/// file owns only the Shares-page-specific formatting and layout.
namespace shares_screens
{

/// @brief Draws the share watchlist page.
void draw_shares_page(uint8_t* fb, const ConsoleState& console_state);

/// @brief Draws one watched share's detail page.
/// @details `now_ms` is the caller's boot-uptime sample (`to_ms_since_boot`)
/// used for the data-freshness text -- passed in rather than read here so
/// this file has no Pico SDK dependency (issue #71).
void draw_share_detail_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

} // namespace shares_screens
