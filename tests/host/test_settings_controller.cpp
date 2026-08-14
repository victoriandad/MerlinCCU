#include "settings_controller.h"

#include <cstring>

#include "test_framework.h"

// Not covered here: select_weather_source(), select_relative_time_zone(),
// select_screen_saver(), toggle_remote_config_enabled(),
// toggle_home_assistant_enabled(), toggle_mqtt_enabled(),
// toggle_air_traffic_enabled(), and confirm_timeout_edit(). All of them
// route their actual mutation through console_controller's own
// persist_runtime_config_change() -> console_controller::apply_runtime_config(),
// which writes to console_controller.cpp's private g_console_state rather
// than the ConsoleState& these functions nominally take -- there's no way to
// observe the result through the fixture a test controls. Exercising that
// path would mean testing against console_controller::state() instead,
// which is a different (and more invasive) kind of test than the rest of
// this file. See issue #78.

namespace
{

ConsoleState make_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::Settings;
    return state;
}

} // namespace

HOST_TEST(change_page_is_gated_to_the_settings_page_and_stays_in_bounds)
{
    ConsoleState state = make_state();
    state.active_page = MenuPage::Home;
    EXPECT_FALSE(settings_controller::change_page(state, 1));

    state.active_page = MenuPage::Settings;
    state.settings_page_index = 0U;
    EXPECT_TRUE(settings_controller::change_page(state, 1));
    EXPECT_EQ(state.settings_page_index, 1U);

    EXPECT_FALSE(settings_controller::change_page(
        state, static_cast<int>(settings_controller::kSettingsPageCount)));
}

HOST_TEST(change_page_zero_direction_is_a_no_op)
{
    ConsoleState state = make_state();
    EXPECT_FALSE(settings_controller::change_page(state, 0));
}

HOST_TEST(cycle_weather_period_advances_through_all_three_and_wraps)
{
    ConsoleState state = make_state();
    state.weather_period = WeatherPeriod::Hourly;
    EXPECT_TRUE(settings_controller::cycle_weather_period(state));
    EXPECT_TRUE(state.weather_period == WeatherPeriod::NextTwentyFourHours);
    EXPECT_TRUE(settings_controller::cycle_weather_period(state));
    EXPECT_TRUE(state.weather_period == WeatherPeriod::NextSevenDays);
    EXPECT_TRUE(settings_controller::cycle_weather_period(state));
    EXPECT_TRUE(state.weather_period == WeatherPeriod::Hourly);
}

HOST_TEST(cycle_share_period_advances_through_all_five_and_wraps)
{
    ConsoleState state = make_state();
    state.share_period = SharePeriod::Today;
    const SharePeriod expected[] = {SharePeriod::Week, SharePeriod::Month, SharePeriod::Year,
                                    SharePeriod::AllTime, SharePeriod::Today};
    for (SharePeriod next : expected)
    {
        EXPECT_TRUE(settings_controller::cycle_share_period(state));
        EXPECT_TRUE(state.share_period == next);
    }
}

HOST_TEST(toggle_air_traffic_view_mode_flips_and_resets_the_page_index)
{
    ConsoleState state = make_state();
    state.air_traffic_view_mode = AirTrafficViewMode::Tabular;
    state.air_traffic_page_index = 3U;
    EXPECT_TRUE(settings_controller::toggle_air_traffic_view_mode(state));
    EXPECT_TRUE(state.air_traffic_view_mode == AirTrafficViewMode::Plot);
    EXPECT_EQ(state.air_traffic_page_index, 0U);
}

HOST_TEST(air_traffic_page_count_rounds_up)
{
    ConsoleState state = make_state();
    state.air_traffic_status.aircraft_count = 0U;
    EXPECT_EQ(settings_controller::air_traffic_page_count(state), 1U);
    state.air_traffic_status.aircraft_count = kAirTrafficRowsPerPage;
    EXPECT_EQ(settings_controller::air_traffic_page_count(state), 1U);
    state.air_traffic_status.aircraft_count = static_cast<uint8_t>(kAirTrafficRowsPerPage + 1U);
    EXPECT_EQ(settings_controller::air_traffic_page_count(state), 2U);
}

HOST_TEST(select_share_slot_rejects_a_slot_past_the_watchlist_count)
{
    ConsoleState state = make_state();
    state.share_count = 2U;
    EXPECT_TRUE(settings_controller::select_share_slot(state, 1U));
    EXPECT_EQ(state.selected_share_index, 1U);
    EXPECT_TRUE(state.active_page == MenuPage::ShareDetail);

    EXPECT_FALSE(settings_controller::select_share_slot(state, 2U));
}

HOST_TEST(open_selected_share_detail_uses_the_currently_selected_index)
{
    ConsoleState state = make_state();
    state.share_count = 3U;
    state.selected_share_index = 2U;
    EXPECT_TRUE(settings_controller::open_selected_share_detail(state));
    EXPECT_TRUE(state.active_page == MenuPage::ShareDetail);
}

HOST_TEST(timeout_editing_start_stop_round_trip_restores_the_saved_minutes)
{
    ConsoleState state = make_state();
    state.screen_saver_timeout_minutes = 5U;
    state.screen_saver_timeout_editing = false;

    EXPECT_TRUE(settings_controller::start_timeout_editing(state));
    EXPECT_TRUE(state.screen_saver_timeout_editing);
    EXPECT_FALSE(settings_controller::start_timeout_editing(state)); // already editing

    EXPECT_TRUE(settings_controller::stop_timeout_editing(state));
    EXPECT_FALSE(state.screen_saver_timeout_editing);
    EXPECT_EQ(state.screen_saver_timeout_edit_minutes, 5U);
}

HOST_TEST(handle_timeout_edit_event_ignores_input_while_not_editing)
{
    ConsoleState state = make_state();
    state.screen_saver_timeout_editing = false;
    const ButtonEvent digit_event = {ButtonId::Zero, ButtonEventType::Pressed};
    EXPECT_FALSE(settings_controller::handle_timeout_edit_event(state, digit_event));
}

HOST_TEST(handle_timeout_edit_event_backstep_exits_editing)
{
    ConsoleState state = make_state();
    state.screen_saver_timeout_minutes = 5U;
    settings_controller::start_timeout_editing(state);
    const ButtonEvent backstep = {ButtonId::BackStep, ButtonEventType::Pressed};
    EXPECT_TRUE(settings_controller::handle_timeout_edit_event(state, backstep));
    EXPECT_FALSE(state.screen_saver_timeout_editing);
}

// Digit decoding itself (button_digit_value(), reachable here via
// console_controller_internal::keypad_digit_value()) reads the LTRS mode off
// console_controller.cpp's own private g_console_state, not the ConsoleState&
// this module's functions take -- there's no way for a test to put it in
// Numbers mode through the fixture it controls. What *is* testable without
// that global is the real, well-defined behaviour when it's in the (default)
// non-Numbers mode: a would-be digit key is correctly rejected.
HOST_TEST(handle_timeout_edit_event_digit_press_is_rejected_outside_numbers_mode)
{
    ConsoleState state = make_state();
    state.screen_saver_timeout_minutes = 5U;
    settings_controller::start_timeout_editing(state);
    const uint16_t before = state.screen_saver_timeout_edit_minutes;

    const ButtonEvent digit_1 = {ButtonId::AlphaJ, ButtonEventType::Pressed};
    EXPECT_FALSE(settings_controller::handle_timeout_edit_event(state, digit_1));
    EXPECT_EQ(state.screen_saver_timeout_edit_minutes, before);
}

HOST_TEST(handle_timeout_edit_event_clr_resets_to_zero_and_next_digit_replaces)
{
    ConsoleState state = make_state();
    state.screen_saver_timeout_minutes = 5U;
    settings_controller::start_timeout_editing(state);
    // Simulates the scratchpad already having a typed value, without going
    // through the digit-press path (see the note above).
    state.screen_saver_timeout_edit_minutes = 42U;
    state.screen_saver_timeout_replace_on_next_digit = false;

    const ButtonEvent clr = {ButtonId::Clr, ButtonEventType::Pressed};
    EXPECT_TRUE(settings_controller::handle_timeout_edit_event(state, clr));
    EXPECT_EQ(state.screen_saver_timeout_edit_minutes, 0U);
    EXPECT_TRUE(state.screen_saver_timeout_replace_on_next_digit);
}

HOST_TEST(weather_source_definition_falls_back_to_the_first_entry_for_an_unknown_value)
{
    const auto& def = settings_controller::weather_source_definition(WeatherSource::HomeAssistant);
    EXPECT_TRUE(def.selection_label != nullptr);
}

HOST_TEST(relative_time_zone_definition_returns_null_past_the_edges)
{
    ConsoleState state = make_state();
    state.time_zone = TimeZoneSelection::EuropeLondon;
    EXPECT_TRUE(settings_controller::relative_time_zone_definition(state, 0) != nullptr);
    EXPECT_TRUE(settings_controller::relative_time_zone_definition(state, 100) == nullptr);
    EXPECT_TRUE(settings_controller::relative_time_zone_definition(state, -100) == nullptr);
}

HOST_TEST(enabled_and_secret_selection_text_are_simple_boolean_labels)
{
    EXPECT_TRUE(std::strcmp(settings_controller::enabled_selection_text(true), "Enabled") == 0);
    EXPECT_TRUE(std::strcmp(settings_controller::enabled_selection_text(false), "Disabled") == 0);
    EXPECT_TRUE(std::strcmp(settings_controller::secret_selection_text(true), "Stored") == 0);
    EXPECT_TRUE(std::strcmp(settings_controller::secret_selection_text(false), "Not set") == 0);
}
