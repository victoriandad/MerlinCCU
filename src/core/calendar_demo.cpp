#include "calendar_demo.h"

#include <cstdio>

namespace calendar_demo
{

namespace
{

/// @brief One sample row; the day comes with it.
struct SampleEvent
{
    CalendarOwner owner;
    int8_t day_offset;
    const char* start_time;
    const char* end_time;
    const char* title;
    const char* location;
    const char* reminder;
    const char* attendees;
    const char* description;
};

// clang-format off
constexpr SampleEvent kSamples[] = {
    {CalendarOwner::Owner1, 0, "08:30", "09:00", "Demo standup", "Video call", "10 min popup", "Project group", "Synthetic status sample."},
    {CalendarOwner::Owner2, 0, "09:15", "09:45", "Planning slot", "Room A", "30 min email", "Ops group", "Synthetic planning sample."},
    {CalendarOwner::Owner3, 0, "15:40", "17:00", "Training block", "Activity room", "1 hour popup", "Training lead", "Synthetic training sample."},
    {CalendarOwner::Owner4, 0, "16:30", "17:15", "Practice block", "Activity centre", "45 min popup", "Practice lead", "Synthetic practice sample."},
    {CalendarOwner::Owner1, 0, "19:00", "20:00", "Admin block", "Home base", "None", "Demo group", "End-of-day synthetic sample."},
    {CalendarOwner::Owner2, 1, "10:00", "10:30", "Check-in", "Room B", "1 day email", "Coordinator", "Synthetic check-in notes."},
    {CalendarOwner::Owner3, 1, "13:30", "15:30", "Workshop", "Lab", "1 day popup", "Demo cohort", "Synthetic workshop sample."},
    {CalendarOwner::Owner4, 1, "17:15", "18:30", "Review block", "Room C", "30 min popup", "Reviewer", "Synthetic review sample."},
    {CalendarOwner::Owner1, 2, "08:45", "17:00", "Focus day", "Remote", "None", "Project group", "Synthetic focus-day sample."},
    {CalendarOwner::Owner2, 2, "18:00", "19:00", "Exercise block", "Studio", "1 hour popup", "Class group", "Synthetic exercise sample."},
    {CalendarOwner::Owner3, 3, "11:00", "11:30", "Maintenance slot", "Service desk", "1 day email", "Service team", "Synthetic service sample."},
    {CalendarOwner::Owner4, 3, "14:00", "16:00", "Demo event", "Hall", "2 hours popup", "Demo group", "Synthetic event sample."},
    {CalendarOwner::Owner1, -1, "12:30", "13:00", "Vendor call", "Office", "10 min popup", "Supplier", "Synthetic quote review."},
    {CalendarOwner::Owner2, -1, "18:30", "19:30", "Planning forum", "Meeting room", "30 min popup", "Planning group", "Synthetic planning sample."},
    {CalendarOwner::Owner1, 4, "09:30", "10:15", "Service slot", "Service desk", "1 day email", "Service team", "Synthetic booking sample."},
    {CalendarOwner::Owner3, 5, "16:00", "17:30", "Away activity", "Field site", "2 hours popup", "Activity group", "Synthetic away sample."},
    {CalendarOwner::Owner4, 6, "10:30", "12:00", "Library block", "Resource room", "1 hour popup", "Demo group", "Synthetic library sample."},
    {CalendarOwner::Owner2, 7, "15:00", "16:00", "Review call", "Video call", "30 min popup", "Review group", "Synthetic catch-up sample."},
    {CalendarOwner::Owner1, -14, "08:00", "08:30", "Historic rota", "Demo base", "None", "Ops group", "Historic synthetic sample."},
    {CalendarOwner::Owner2, -7, "11:00", "11:45", "Historic call", "Phone", "15 min popup", "Coordinator", "Historic weekly sample."},
    {CalendarOwner::Owner1, 14, "09:00", "09:30", "Budget check", "Planning desk", "1 day email", "Accounts", "Two-week synthetic sample."},
    {CalendarOwner::Owner4, 13, "16:00", "17:00", "Signup slot", "Community room", "1 day popup", "Coordinator", "Next-fortnight synthetic sample."},
};
// clang-format on

/// @brief Owners for the generated row on a day with no sample, in turn.
struct FillerOwner
{
    CalendarOwner owner;
    const char* title_prefix;
    const char* attendee;
};
constexpr FillerOwner kFillerOwners[] = {
    {CalendarOwner::Owner1, "Owner 1 sample", "Owner 1"},
    {CalendarOwner::Owner2, "Owner 2 sample", "Owner 2"},
    {CalendarOwner::Owner3, "Owner 3 sample", "Owner 3"},
    {CalendarOwner::Owner4, "Owner 4 sample", "Owner 4"},
};
constexpr int kFillerFirstHour = 8;
constexpr int kFillerHourSpread = 10;

void set_event(CalendarEvent& event, CalendarOwner owner, int8_t day_offset, const char* start_time,
               const char* end_time, const char* title, const char* location, const char* reminder,
               const char* attendees, const char* description)
{
    event = {};
    event.owner = owner;
    event.day_offset = day_offset;
    std::snprintf(event.start_time.data(), event.start_time.size(), "%s", start_time);
    std::snprintf(event.end_time.data(), event.end_time.size(), "%s", end_time);
    std::snprintf(event.title.data(), event.title.size(), "%s", title);
    std::snprintf(event.location.data(), event.location.size(), "%s", location);
    std::snprintf(event.reminder.data(), event.reminder.size(), "%s", reminder);
    std::snprintf(event.attendees.data(), event.attendees.size(), "%s", attendees);
    std::snprintf(event.description.data(), event.description.size(), "%s", description);
}

} // namespace

void load_day(int8_t day_offset, std::array<CalendarEvent, kCalendarEventCapacity>& events,
              uint8_t& count)
{
    events = {};
    count = 0U;
    for (const SampleEvent& sample : kSamples)
    {
        if (sample.day_offset != day_offset || count >= events.size())
        {
            continue;
        }
        set_event(events[count++], sample.owner, day_offset, sample.start_time, sample.end_time,
                  sample.title, sample.location, sample.reminder, sample.attendees,
                  sample.description);
    }
    if (count > 0U || day_offset < kCalendarMinDayOffset || day_offset > kCalendarMaxDayOffset)
    {
        return;
    }

    // Every other day in the navigable window gets one generated row, so
    // stepping through the days can be checked before live data exists.
    const int sample_index = day_offset - kCalendarMinDayOffset;
    const FillerOwner& filler = kFillerOwners[static_cast<size_t>(sample_index) %
                                              (sizeof(kFillerOwners) / sizeof(kFillerOwners[0]))];
    const int start_hour = kFillerFirstHour + (sample_index % kFillerHourSpread);
    char start_time[6] = {};
    char end_time[6] = {};
    char title[32] = {};
    std::snprintf(start_time, sizeof(start_time), "%02d:00", start_hour);
    std::snprintf(end_time, sizeof(end_time), "%02d:30", start_hour);
    std::snprintf(title, sizeof(title), "%s %+d", filler.title_prefix, day_offset);
    set_event(events[count++], filler.owner, day_offset, start_time, end_time, title,
              "Calendar test", "None", filler.attendee, "Generated sample for day paging.");
}

} // namespace calendar_demo
