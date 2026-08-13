#pragma once

#include <array>
#include <cstdint>

#include "alert_ordering.h"
#include "console_model.h"
#include "input.h"

/// @brief Alert state synchronisation, split out of console_controller.cpp --
/// issue #44 (staged, following the pinter_controller.cpp precedent).
/// @details Every function here takes the console's ConsoleState explicitly
/// rather than reaching for a hidden global, matching the pattern already
/// used by the display split (issue #45) and pinter_controller.h.
namespace alert_controller
{

/// @brief Rebuilds currently active alerts from live subsystem conditions.
void sync(ConsoleState& console_state);

/// @brief Returns the number of alert list pages required for the current queue.
uint8_t page_count(const ConsoleState& console_state);

/// @brief Sorts active alerts from newest to oldest for list-page mapping.
void build_display_indices(const ConsoleState& console_state,
                           std::array<uint8_t, kActiveAlertCapacity>& out_indices,
                           uint8_t* out_count);

/// @brief Formats one two-line alert softkey label using summary and occurred time.
const char* build_softkey_label(SoftKeyId key, const ActiveAlert& alert);

/// @brief Summarizes the active queue for lamp/annunciator purposes, without
/// acknowledging it -- see alert_ordering::summarize().
alert_ordering::AnnunciationSummary annunciation_summary(const ConsoleState& console_state);

/// @brief Opens the alert list from the current page when alerts exist.
bool open_list_page(ConsoleState& console_state);

/// @brief Opens one alert-detail page from the currently visible list page slot.
bool open_detail_from_slot(ConsoleState& console_state, uint8_t page_slot);

/// @brief Removes one alert from the active queue and compacts trailing entries.
void erase_active_alert(ConsoleState& console_state, uint8_t index);

/// @brief Suppresses one alert code so it will not re-trigger until cleared.
/// @details Takes the raw ActiveAlert::code index rather than an enum: the
/// underlying AlertCode enum is a private implementation detail of this module.
void suppress_alert_code(uint8_t alert_code_index);

/// @brief Resets alert history and failure-sample counters, e.g. on device reset.
void reset();

} // namespace alert_controller
