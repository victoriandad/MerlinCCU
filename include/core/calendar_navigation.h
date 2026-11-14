#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure calendar filtering/navigation/selection rules shared between the
/// firmware and host tests (issue #49). Covers owner-filter cycling, day-offset
/// bounds, and translating a visible Calendar softkey slot into a backing event
/// index. Knows nothing about `ConsoleState` or softkey routing --
/// console_controller.cpp owns the live selection and calls into this for the
/// decision logic, the same split established for Pinter scheduling (#48).
namespace calendar_navigation
{

/// @brief Returns whether one event should be visible under the given owner/day filter.
bool event_matches_filter(const CalendarEvent& event, CalendarOwner owner, int8_t day_offset);

/// @brief Returns the next shared-calendar owner filter in the Calendar page cycle.
CalendarOwner next_owner(CalendarOwner owner);

/// @brief Returns whether the given filters represent the default combined-today view.
bool filters_are_default(CalendarOwner owner, int8_t day_offset);

/// @brief Returns the backing event index for one visible Calendar softkey slot
/// under the given owner/day filter, or `events.size()` if the slot is out of range.
uint8_t event_index_for_visible_slot(const std::array<CalendarEvent, kCalendarEventCapacity>& events,
                                     uint8_t event_count, CalendarOwner owner, int8_t day_offset,
                                     uint8_t visible_slot);

} // namespace calendar_navigation
