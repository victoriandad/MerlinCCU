#pragma once

#include <cstdint>

#include "console_model.h"

/// @brief Renders the Local Conditions page and its history graph.
/// @details Split out of `screens.cpp` like the other page families; shared
/// drawing primitives (empty state, graph plot frame, freshness text) stay in
/// `screens_shared.h`, and reading text and board states come from
/// `local_conditions.h`.
namespace local_conditions_screens
{

/// @brief Draws the Local Conditions body.
/// @details With readings, the values are on the softkeys and the body holds
/// only the footer freshness line. Without, the shared empty state says why
/// and where to look, and the keys are blank (SK-5). `now_ms` is boot uptime,
/// passed in so this file has no Pico SDK dependency.
void draw_local_conditions_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

/// @brief Draws the history graph for the selected local-condition metric.
/// @details With fewer than two history points there is nothing to plot, so
/// the page shows the shared empty state: the board's own state when it has
/// no readings, otherwise that history is still building, with the current
/// reading as the hint.
void draw_local_condition_history_graph(uint8_t* fb, const ConsoleState& console_state,
                                        uint32_t now_ms);

} // namespace local_conditions_screens
