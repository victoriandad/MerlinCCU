#include "calendar_controller.h"

#include <cstdio>
#include <cstring>

#include "test_framework.h"

namespace
{

ConsoleState make_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::Calendar;
    state.calendar_owner = CalendarOwner::Combined;
    state.calendar_day_offset = 0;
    return state;
}

CalendarEvent make_event(CalendarOwner owner, int8_t day_offset, const char* title)
{
    CalendarEvent event = {};
    event.owner = owner;
    event.day_offset = day_offset;
    std::snprintf(event.title.data(), event.title.size(), "%s", title);
    std::snprintf(event.start_time.data(), event.start_time.size(), "09:00");
    return event;
}

} // namespace

HOST_TEST(owner_selection_text_returns_combined_by_default)
{
    ConsoleState state = make_state();
    EXPECT_TRUE(std::strcmp(calendar_controller::owner_selection_text(state), "Combined") == 0);
}

HOST_TEST(filters_are_default_requires_combined_owner_and_zero_offset)
{
    ConsoleState state = make_state();
    EXPECT_TRUE(calendar_controller::filters_are_default(state));

    state.calendar_owner = CalendarOwner::Owner2;
    EXPECT_FALSE(calendar_controller::filters_are_default(state));

    state.calendar_owner = CalendarOwner::Combined;
    state.calendar_day_offset = 1;
    EXPECT_FALSE(calendar_controller::filters_are_default(state));
}

HOST_TEST(cycle_owner_advances_and_wraps_back_to_combined)
{
    ConsoleState state = make_state();
    EXPECT_TRUE(calendar_controller::cycle_owner(state));
    EXPECT_TRUE(state.calendar_owner == CalendarOwner::Owner1);

    // Cycle all the way around back to Combined.
    for (int i = 0; i < 8; ++i)
    {
        calendar_controller::cycle_owner(state);
    }
    EXPECT_TRUE(state.calendar_owner == CalendarOwner::Combined);
}

HOST_TEST(reset_filters_restores_combined_today_and_reports_whether_it_changed_anything)
{
    ConsoleState state = make_state();
    EXPECT_FALSE(calendar_controller::reset_filters(state)); // already default

    state.calendar_owner = CalendarOwner::Owner3;
    state.calendar_day_offset = 5;
    EXPECT_TRUE(calendar_controller::reset_filters(state));
    EXPECT_TRUE(state.calendar_owner == CalendarOwner::Combined);
    EXPECT_EQ(state.calendar_day_offset, 0);
}

HOST_TEST(change_day_is_gated_to_the_calendar_page)
{
    ConsoleState state = make_state();
    state.active_page = MenuPage::Home;
    EXPECT_FALSE(calendar_controller::change_day(state, 1));

    state.active_page = MenuPage::Calendar;
    EXPECT_TRUE(calendar_controller::change_day(state, 1));
    EXPECT_EQ(state.calendar_day_offset, 1);
}

HOST_TEST(change_day_stays_within_the_supported_window)
{
    ConsoleState state = make_state();
    state.calendar_day_offset = static_cast<int8_t>(kCalendarMaxDayOffset);
    EXPECT_FALSE(calendar_controller::change_day(state, 1));
    EXPECT_EQ(state.calendar_day_offset, kCalendarMaxDayOffset);

    state.calendar_day_offset = static_cast<int8_t>(kCalendarMinDayOffset);
    EXPECT_FALSE(calendar_controller::change_day(state, -1));
    EXPECT_EQ(state.calendar_day_offset, kCalendarMinDayOffset);
}

HOST_TEST(event_matches_filter_combined_owner_matches_any_owner_on_the_right_day)
{
    ConsoleState state = make_state();
    state.calendar_owner = CalendarOwner::Combined;
    state.calendar_day_offset = 0;

    const CalendarEvent today_owner2 = make_event(CalendarOwner::Owner2, 0, "Standup");
    const CalendarEvent tomorrow_owner2 = make_event(CalendarOwner::Owner2, 1, "Standup");
    EXPECT_TRUE(calendar_controller::event_matches_filter(state, today_owner2));
    EXPECT_FALSE(calendar_controller::event_matches_filter(state, tomorrow_owner2));
}

HOST_TEST(event_matches_filter_a_specific_owner_only_matches_that_owner)
{
    ConsoleState state = make_state();
    state.calendar_owner = CalendarOwner::Owner1;
    state.calendar_day_offset = 0;

    const CalendarEvent owner1_event = make_event(CalendarOwner::Owner1, 0, "Meeting");
    const CalendarEvent owner2_event = make_event(CalendarOwner::Owner2, 0, "Meeting");
    EXPECT_TRUE(calendar_controller::event_matches_filter(state, owner1_event));
    EXPECT_FALSE(calendar_controller::event_matches_filter(state, owner2_event));
}

HOST_TEST(open_detail_from_slot_finds_the_nth_matching_event_under_the_active_filter)
{
    ConsoleState state = make_state();
    state.calendar_owner = CalendarOwner::Combined;
    state.calendar_day_offset = 0;
    state.calendar_event_count = 3U;
    state.calendar_events[0] = make_event(CalendarOwner::Owner1, 1, "Not today"); // filtered out
    state.calendar_events[1] = make_event(CalendarOwner::Owner1, 0, "First today");
    state.calendar_events[2] = make_event(CalendarOwner::Owner2, 0, "Second today");

    EXPECT_TRUE(calendar_controller::open_detail_from_slot(state, 0U));
    EXPECT_EQ(state.selected_calendar_event_index, 1U);
    EXPECT_TRUE(state.active_page == MenuPage::CalendarDetail);

    EXPECT_TRUE(calendar_controller::open_detail_from_slot(state, 1U));
    EXPECT_EQ(state.selected_calendar_event_index, 2U);
}

HOST_TEST(open_detail_from_slot_rejects_a_slot_with_no_matching_event)
{
    ConsoleState state = make_state();
    state.calendar_event_count = 0U;
    EXPECT_FALSE(calendar_controller::open_detail_from_slot(state, 0U));
}
