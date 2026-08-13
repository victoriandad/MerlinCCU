#pragma once

#include <cstdint>

#include "console_model.h"
#include "input.h"

/// @brief Calendar owner filtering and day navigation, split out of
/// console_controller.cpp -- issue #44 (staged, following the
/// pinter_controller.h/alert_controller.h precedent).
/// @details Every function here takes the console's ConsoleState explicitly
/// rather than reaching for a hidden global, matching the pattern already
/// used by the display split (issue #45).
namespace calendar_controller
{

/// @brief Returns the currently selected shared-calendar owner label.
const char* owner_selection_text(const ConsoleState& console_state);

/// @brief Returns whether one event belongs in the active Calendar page filter.
bool event_matches_filter(const ConsoleState& console_state, const CalendarEvent& event);

/// @brief Formats one calendar event for the surrounding softkey labels.
const char* build_event_softkey_label(SoftKeyId key, const CalendarEvent& event);

/// @brief Advances the Calendar owner filter without touching persisted config.
bool cycle_owner(ConsoleState& console_state);

/// @brief Returns whether the Calendar filters are showing the default view.
bool filters_are_default(const ConsoleState& console_state);

/// @brief Restores the Calendar page to the default combined-today view.
bool reset_filters(ConsoleState& console_state);

/// @brief Moves the Calendar page day selection within a bounded preview window.
bool change_day(ConsoleState& console_state, int direction);

/// @brief Opens the detail page for one visible calendar event slot.
bool open_detail_from_slot(ConsoleState& console_state, uint8_t visible_slot);

} // namespace calendar_controller
