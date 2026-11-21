#include "cursor_navigation.h"

#include "test_framework.h"

// Every page's cursor behaviour is also exercised through real key presses
// in test_page_coverage.cpp, which checks that each arrow shows exactly when
// its key acts. These cases pin down the bounds.

namespace
{

ConsoleState state_on(MenuPage page)
{
    ConsoleState state = {};
    state.active_page = page;
    return state;
}

} // namespace

HOST_TEST(cursor_keys_do_nothing_on_a_page_without_a_range)
{
    ConsoleState state = state_on(MenuPage::Home);
    EXPECT_FALSE(cursor_navigation::can_move(state, -1));
    EXPECT_FALSE(cursor_navigation::can_move(state, 1));
    EXPECT_FALSE(cursor_navigation::move(state, 1));
}

HOST_TEST(settings_pages_stop_at_both_ends)
{
    ConsoleState state = state_on(MenuPage::Settings);
    EXPECT_FALSE(cursor_navigation::move(state, -1));
    for (uint8_t i = 1U; i < kSettingsPageCount; ++i)
    {
        EXPECT_TRUE(cursor_navigation::move(state, 1));
    }
    EXPECT_FALSE(cursor_navigation::move(state, 1));
    EXPECT_EQ(state.settings_page_index, static_cast<uint8_t>(kSettingsPageCount - 1U));
    EXPECT_FALSE(cursor_navigation::move(state, 0));
}

HOST_TEST(time_zone_pages_stop_at_both_ends)
{
    ConsoleState state = state_on(MenuPage::TimeZoneSettings);
    EXPECT_FALSE(cursor_navigation::move(state, -1));
    for (uint8_t i = 1U; i < kTimeZonePageCount; ++i)
    {
        EXPECT_TRUE(cursor_navigation::move(state, 1));
    }
    EXPECT_FALSE(cursor_navigation::move(state, 1));
    EXPECT_EQ(state.time_zone_page_index, static_cast<uint8_t>(kTimeZonePageCount - 1U));
}

HOST_TEST(calendar_day_steps_within_the_preview_window)
{
    ConsoleState state = state_on(MenuPage::Calendar);
    EXPECT_TRUE(cursor_navigation::move(state, 1));
    EXPECT_EQ(state.calendar_day_offset, 1);

    state.calendar_day_offset = static_cast<int8_t>(kCalendarMaxDayOffset);
    EXPECT_FALSE(cursor_navigation::move(state, 1));
    EXPECT_TRUE(cursor_navigation::can_move(state, -1));

    state.calendar_day_offset = static_cast<int8_t>(kCalendarMinDayOffset);
    EXPECT_FALSE(cursor_navigation::move(state, -1));
    EXPECT_EQ(state.calendar_day_offset, kCalendarMinDayOffset);
}

HOST_TEST(header_page_position_shows_only_for_a_list_of_more_than_one_page)
{
    unsigned page = 0U;
    unsigned count = 0U;

    ConsoleState alerts = state_on(MenuPage::AlertList);
    alerts.alert_count = 1U;
    EXPECT_FALSE(cursor_navigation::list_page_position(alerts, page, count));

    alerts.alert_count = static_cast<uint8_t>(kAlertsPerPage + 1U);
    alerts.alert_list_page_index = 1U;
    EXPECT_TRUE(cursor_navigation::list_page_position(alerts, page, count));
    EXPECT_EQ(page, 2U);
    EXPECT_EQ(count, 2U);

    // A stepped value is not a list page.
    ConsoleState calendar = state_on(MenuPage::Calendar);
    EXPECT_FALSE(cursor_navigation::list_page_position(calendar, page, count));
}
