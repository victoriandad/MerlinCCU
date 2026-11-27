#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Sample calendar rows for the Calendar page until live data lands
/// (#8).
/// @details The page holds only the day on screen (kCalendarEventCapacity
/// rows), and loads a day when it is shown. Home Assistant is to supply each
/// day the same way, so no unit keeps weeks of events in RAM.
namespace calendar_demo
{

/// @brief Fills `events` with the sample rows for `day_offset` (0 is today),
/// in time order, up to the capacity, and sets `count`.
void load_day(int8_t day_offset, std::array<CalendarEvent, kCalendarEventCapacity>& events,
              uint8_t& count);

} // namespace calendar_demo
