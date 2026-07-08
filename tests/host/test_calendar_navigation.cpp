#include "calendar_navigation.h"

#include "test_framework.h"

namespace
{

CalendarEvent make_event(CalendarOwner owner, int8_t day_offset, const char* title = "Event")
{
    CalendarEvent event = {};
    event.owner = owner;
    event.day_offset = day_offset;
    std::snprintf(event.title.data(), event.title.size(), "%s", title);
    return event;
}

} // namespace

HOST_TEST(event_matches_filter_rejects_an_empty_title_slot)
{
    CalendarEvent event = make_event(CalendarOwner::Combined, 0, "");
    EXPECT_FALSE(calendar_navigation::event_matches_filter(event, CalendarOwner::Combined, 0));
}

HOST_TEST(event_matches_filter_requires_the_day_offset_to_match)
{
    const CalendarEvent event = make_event(CalendarOwner::Combined, 3);
    EXPECT_TRUE(calendar_navigation::event_matches_filter(event, CalendarOwner::Combined, 3));
    EXPECT_FALSE(calendar_navigation::event_matches_filter(event, CalendarOwner::Combined, 4));
}

HOST_TEST(event_matches_filter_combined_owner_matches_any_event_owner)
{
    const CalendarEvent event = make_event(CalendarOwner::Owner3, 0);
    EXPECT_TRUE(calendar_navigation::event_matches_filter(event, CalendarOwner::Combined, 0));
}

HOST_TEST(event_matches_filter_a_specific_owner_only_matches_the_same_owner)
{
    const CalendarEvent event = make_event(CalendarOwner::Owner3, 0);
    EXPECT_TRUE(calendar_navigation::event_matches_filter(event, CalendarOwner::Owner3, 0));
    EXPECT_FALSE(calendar_navigation::event_matches_filter(event, CalendarOwner::Owner2, 0));
}

HOST_TEST(next_owner_cycles_through_every_owner_and_wraps_to_combined)
{
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Combined) == CalendarOwner::Owner1);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner1) == CalendarOwner::Owner2);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner2) == CalendarOwner::Owner3);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner3) == CalendarOwner::Owner4);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner4) == CalendarOwner::Owner5);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner5) == CalendarOwner::Owner6);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner6) == CalendarOwner::Owner7);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner7) == CalendarOwner::Owner8);
    EXPECT_TRUE(calendar_navigation::next_owner(CalendarOwner::Owner8) == CalendarOwner::Combined);
}

HOST_TEST(filters_are_default_is_true_only_for_combined_owner_and_zero_offset)
{
    EXPECT_TRUE(calendar_navigation::filters_are_default(CalendarOwner::Combined, 0));
    EXPECT_FALSE(calendar_navigation::filters_are_default(CalendarOwner::Combined, 1));
    EXPECT_FALSE(calendar_navigation::filters_are_default(CalendarOwner::Owner1, 0));
}

HOST_TEST(step_day_offset_is_a_no_op_for_zero_direction)
{
    int8_t offset = 5;
    EXPECT_FALSE(calendar_navigation::step_day_offset(0, 5, offset));
    EXPECT_EQ(offset, 5);
}

HOST_TEST(step_day_offset_moves_within_the_supported_window)
{
    int8_t offset = 0;
    EXPECT_TRUE(calendar_navigation::step_day_offset(1, 0, offset));
    EXPECT_EQ(offset, 1);

    EXPECT_TRUE(calendar_navigation::step_day_offset(-1, 1, offset));
    EXPECT_EQ(offset, 0);
}

HOST_TEST(step_day_offset_rejects_moves_outside_the_supported_window)
{
    int8_t offset = 99;
    EXPECT_FALSE(calendar_navigation::step_day_offset(1, static_cast<int8_t>(kCalendarMaxDayOffset),
                                                      offset));
    EXPECT_EQ(offset, 99); // unchanged

    EXPECT_FALSE(calendar_navigation::step_day_offset(-1, static_cast<int8_t>(kCalendarMinDayOffset),
                                                      offset));
    EXPECT_EQ(offset, 99); // unchanged
}

HOST_TEST(step_day_offset_allows_reaching_the_exact_boundary_values)
{
    int8_t offset = 0;
    EXPECT_TRUE(calendar_navigation::step_day_offset(
        kCalendarMaxDayOffset - 1, 1, offset)); // 1 + (max-1) == max
    EXPECT_EQ(offset, static_cast<int8_t>(kCalendarMaxDayOffset));
}

HOST_TEST(event_index_for_visible_slot_skips_non_matching_events)
{
    std::array<CalendarEvent, kCalendarEventCapacity> events = {};
    events[0] = make_event(CalendarOwner::Owner1, 0, "Owner1 today");
    events[1] = make_event(CalendarOwner::Owner2, 0, "Owner2 today");
    events[2] = make_event(CalendarOwner::Owner1, 1, "Owner1 tomorrow");

    // Filtered to Owner1/day 0: only index 0 should be visible, at visible slot 0.
    EXPECT_EQ(calendar_navigation::event_index_for_visible_slot(events, 3, CalendarOwner::Owner1, 0, 0U),
             0U);
    // Slot 1 has no second match under this filter -> out-of-range sentinel.
    EXPECT_EQ(calendar_navigation::event_index_for_visible_slot(events, 3, CalendarOwner::Owner1, 0, 1U),
             static_cast<uint8_t>(events.size()));
}

HOST_TEST(event_index_for_visible_slot_maps_slots_in_order_under_the_combined_filter)
{
    std::array<CalendarEvent, kCalendarEventCapacity> events = {};
    events[0] = make_event(CalendarOwner::Owner1, 0, "First");
    events[1] = make_event(CalendarOwner::Owner2, 0, "Second");
    events[2] = make_event(CalendarOwner::Owner3, 1, "Different day");

    EXPECT_EQ(
        calendar_navigation::event_index_for_visible_slot(events, 3, CalendarOwner::Combined, 0, 0U),
        0U);
    EXPECT_EQ(
        calendar_navigation::event_index_for_visible_slot(events, 3, CalendarOwner::Combined, 0, 1U),
        1U);
    // Index 2 is filtered out (different day), so slot 2 is out of range.
    EXPECT_EQ(
        calendar_navigation::event_index_for_visible_slot(events, 3, CalendarOwner::Combined, 0, 2U),
        static_cast<uint8_t>(events.size()));
}

HOST_TEST(event_index_for_visible_slot_clamps_event_count_to_the_array_size)
{
    std::array<CalendarEvent, kCalendarEventCapacity> events = {};
    events[0] = make_event(CalendarOwner::Combined, 0, "Only real event");

    // An event_count larger than the backing array must not run past its bounds.
    EXPECT_EQ(calendar_navigation::event_index_for_visible_slot(
                 events, static_cast<uint8_t>(events.size() + 10U), CalendarOwner::Combined, 0, 0U),
             0U);
}
