#include "calendar_navigation.h"

#include <algorithm>

namespace calendar_navigation
{

bool event_matches_filter(const CalendarEvent& event, CalendarOwner owner, int8_t day_offset)
{
    if (event.title[0] == '\0' || event.day_offset != day_offset)
    {
        return false;
    }

    return owner == CalendarOwner::Combined || event.owner == owner;
}

CalendarOwner next_owner(CalendarOwner owner)
{
    switch (owner)
    {
    case CalendarOwner::Combined:
        return CalendarOwner::Owner1;
    case CalendarOwner::Owner1:
        return CalendarOwner::Owner2;
    case CalendarOwner::Owner2:
        return CalendarOwner::Owner3;
    case CalendarOwner::Owner3:
        return CalendarOwner::Owner4;
    case CalendarOwner::Owner4:
        return CalendarOwner::Owner5;
    case CalendarOwner::Owner5:
        return CalendarOwner::Owner6;
    case CalendarOwner::Owner6:
        return CalendarOwner::Owner7;
    case CalendarOwner::Owner7:
        return CalendarOwner::Owner8;
    case CalendarOwner::Owner8:
        return CalendarOwner::Combined;
    }

    return CalendarOwner::Combined;
}

bool filters_are_default(CalendarOwner owner, int8_t day_offset)
{
    return owner == CalendarOwner::Combined && day_offset == 0;
}

bool step_day_offset(int direction, int8_t current_offset, int8_t& out_offset)
{
    if (direction == 0)
    {
        return false;
    }

    const int target = static_cast<int>(current_offset) + direction;
    if (target < kCalendarMinDayOffset || target > kCalendarMaxDayOffset)
    {
        return false;
    }

    out_offset = static_cast<int8_t>(target);
    return true;
}

uint8_t event_index_for_visible_slot(const std::array<CalendarEvent, kCalendarEventCapacity>& events,
                                     uint8_t event_count, CalendarOwner owner, int8_t day_offset,
                                     uint8_t visible_slot)
{
    uint8_t visible_index = 0U;
    const uint8_t clamped_count = std::min(event_count, static_cast<uint8_t>(events.size()));
    for (uint8_t i = 0U; i < clamped_count; ++i)
    {
        if (!event_matches_filter(events[i], owner, day_offset))
        {
            continue;
        }
        if (visible_index == visible_slot)
        {
            return i;
        }
        ++visible_index;
    }

    return static_cast<uint8_t>(events.size());
}

} // namespace calendar_navigation
