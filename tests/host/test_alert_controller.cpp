#include "alert_controller.h"

#include <cstring>

#include "test_framework.h"

namespace
{

// A fully "healthy" baseline: nothing configured/enabled, so none of
// sync()'s threshold- or config-gated conditions can fire. Individual tests
// perturb only the fields their alert cares about, keeping the rest
// deliberately inert so a test can't accidentally trip an unrelated alert.
ConsoleState make_healthy_state()
{
    // Alert counters and history are module state; start each test from a
    // reset so tests do not depend on what ran before them.
    alert_controller::reset();
    ConsoleState state = {};
    state.wifi_status.state = WifiConnectionState::Connected;
    state.time_status.synced = true;
    return state;
}

} // namespace

HOST_TEST(sync_reports_no_alerts_for_a_fully_healthy_state)
{
    ConsoleState state = make_healthy_state();
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
}

HOST_TEST(sync_raises_wifi_disconnected_immediately_no_threshold)
{
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
}

HOST_TEST(sync_clears_wifi_disconnected_once_reconnected)
{
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);

    state.wifi_status.state = WifiConnectionState::Connected;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
}

HOST_TEST(sync_raises_wifi_auth_failed_immediately)
{
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::AuthFailed;
    alert_controller::sync(state);
    // AuthFailed is also not Connected, so WifiDisconnected fires alongside
    // WifiAuthFailed -- both conditions are genuinely true at once.
    EXPECT_EQ(state.alert_count, 2U);
}

HOST_TEST(sync_time_not_synced_needs_five_consecutive_unsynced_samples)
{
    ConsoleState state = make_healthy_state();
    state.time_status.synced = false;
    for (int i = 0; i < 4; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 0U); // below the retry threshold

    alert_controller::sync(state); // 5th consecutive sample
    EXPECT_EQ(state.alert_count, 1U);
}

HOST_TEST(sync_time_not_synced_counter_resets_once_synced_again)
{
    ConsoleState state = make_healthy_state();
    state.time_status.synced = false;
    for (int i = 0; i < 5; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 1U);

    state.time_status.synced = true;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);

    // Confirms the failure counter itself was reset, not just the alert:
    // three more unsynced samples (below threshold) should not re-raise it.
    state.time_status.synced = false;
    for (int i = 0; i < 3; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 0U);
}

HOST_TEST(sync_raises_share_data_unavailable_when_configured_but_invalid_with_an_error)
{
    ConsoleState state = make_healthy_state();
    state.share_data_configured = true;
    state.share_data_valid = false;
    state.share_data_last_error = -1;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
}

HOST_TEST(sync_does_not_raise_share_data_unavailable_when_not_configured)
{
    ConsoleState state = make_healthy_state();
    state.share_data_configured = false;
    state.share_data_valid = false;
    state.share_data_last_error = -1;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
}

HOST_TEST(page_count_rounds_up_by_nine_per_page)
{
    ConsoleState state = make_healthy_state();
    state.alert_count = 0U;
    EXPECT_EQ(alert_controller::page_count(state), 1U);
    state.alert_count = 9U;
    EXPECT_EQ(alert_controller::page_count(state), 1U);
    state.alert_count = 10U;
    EXPECT_EQ(alert_controller::page_count(state), 2U);
}

HOST_TEST(erase_active_alert_compacts_trailing_entries)
{
    ConsoleState state = make_healthy_state();
    state.alert_count = 3U;
    state.active_alerts[0].code = 10U;
    state.active_alerts[1].code = 20U;
    state.active_alerts[2].code = 30U;

    alert_controller::erase_active_alert(state, 0U);

    EXPECT_EQ(state.alert_count, 2U);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].code), 20U);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[1].code), 30U);
}

HOST_TEST(erase_active_alert_out_of_range_index_is_a_no_op)
{
    ConsoleState state = make_healthy_state();
    state.alert_count = 1U;
    state.active_alerts[0].code = 5U;
    alert_controller::erase_active_alert(state, 3U);
    EXPECT_EQ(state.alert_count, 1U);
}

HOST_TEST(suppress_alert_code_prevents_the_condition_reappearing_until_it_clears)
{
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);

    const uint8_t code = state.active_alerts[0].code;
    alert_controller::suppress_alert_code(code);
    alert_controller::erase_active_alert(state, 0U);
    EXPECT_EQ(state.alert_count, 0U);

    // Condition is still active: without suppression this would re-add the
    // alert, but suppression holds until the condition itself clears.
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);

    // Once the underlying condition clears and re-triggers, suppression lifts.
    state.wifi_status.state = WifiConnectionState::Connected;
    alert_controller::sync(state);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
}

HOST_TEST(reset_clears_failure_counters_so_a_fresh_threshold_run_is_needed)
{
    ConsoleState state = make_healthy_state();
    state.time_status.synced = false;
    for (int i = 0; i < 5; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 1U);

    alert_controller::reset();
    state.alert_count = 0U;
    state.active_alerts = {};

    // Still unsynced, but the failure counter was reset -- needs another
    // full run at the threshold before the alert reappears.
    for (int i = 0; i < 4; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 0U);
}

HOST_TEST(open_list_page_remembers_the_caller_page_for_ignore_to_return_to)
{
    ConsoleState state = make_healthy_state();
    state.active_page = MenuPage::Weather;
    EXPECT_TRUE(alert_controller::open_list_page(state));
    EXPECT_TRUE(state.active_page == MenuPage::AlertList);
    EXPECT_TRUE(state.alert_parent_page == MenuPage::Weather);
}

HOST_TEST(open_detail_from_slot_rejects_a_slot_past_the_active_alert_count)
{
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state); // exactly one active alert
    EXPECT_EQ(state.alert_count, 1U);

    EXPECT_TRUE(alert_controller::open_detail_from_slot(state, 0U));
    EXPECT_TRUE(state.active_page == MenuPage::AlertDetail);

    state.active_page = MenuPage::AlertList;
    EXPECT_FALSE(alert_controller::open_detail_from_slot(state, 5U));
}

namespace
{

constexpr int kAlertRetryThreshold = 5;

void sync_times(ConsoleState& state, int times)
{
    for (int i = 0; i < times; ++i)
    {
        alert_controller::sync(state);
    }
}

} // namespace

HOST_TEST(sync_raises_weather_unavailable_when_open_meteo_keeps_failing)
{
    alert_controller::reset();
    ConsoleState state = make_healthy_state();
    state.weather_source = WeatherSource::OpenMeteo;
    state.wifi_status.internet_reachable = true;
    state.home_assistant_status.state = HomeAssistantConnectionState::Error;
    state.home_assistant_status.last_error = -5;

    // One failed sample is not enough to raise it.
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);

    for (int i = 0; i < 5; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_TRUE(std::strcmp(state.active_alerts[0].summary.data(), "WEATHER") == 0);

    // A good fetch clears it.
    std::strncpy(state.home_assistant_status.weather_condition.data(), "cloudy",
                 state.home_assistant_status.weather_condition.size());
    state.home_assistant_status.state = HomeAssistantConnectionState::Connected;
    state.home_assistant_status.last_error = 0;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
}

namespace
{

/// @brief Drops Wi-Fi and syncs, then restores it and syncs, `times` times.
void flap_wifi(ConsoleState& state, int times)
{
    for (int i = 0; i < times; ++i)
    {
        state.wifi_status.state = WifiConnectionState::Disabled;
        alert_controller::sync(state);
        state.wifi_status.state = WifiConnectionState::Connected;
        alert_controller::sync(state);
    }
}

} // namespace

HOST_TEST(a_recurring_alert_raises_its_count_on_one_entry)
{
    alert_controller::reset();
    ConsoleState state = make_healthy_state();
    flap_wifi(state, 2);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    alert_controller::sync(state); // staying down is not a new occurrence

    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count), 3U);
    EXPECT_TRUE(std::strstr(alert_controller::build_softkey_label(SoftKeyId::Left1,
                                                                  state.active_alerts[0]),
                            " x3]") != nullptr);
}

HOST_TEST(a_first_occurrence_shows_no_count)
{
    alert_controller::reset();
    ConsoleState state = make_healthy_state();
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);

    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count), 1U);
    EXPECT_TRUE(std::strcmp(alert_controller::build_softkey_label(SoftKeyId::Left1,
                                                                  state.active_alerts[0]),
                            "NETWORK\n[--:--]") == 0);
}

HOST_TEST(accept_restarts_the_occurrence_count)
{
    alert_controller::reset();
    ConsoleState state = make_healthy_state();
    flap_wifi(state, 3);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count), 4U);

    alert_controller::accept_alert(state, 0U);
    EXPECT_EQ(state.alert_count, 0U);
    alert_controller::sync(state); // still down: accepted alerts stay hidden
    EXPECT_EQ(state.alert_count, 0U);

    // The accepted episode ends and a new one starts: the first since ACCEPT.
    flap_wifi(state, 1);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count), 1U);

    flap_wifi(state, 1);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count), 2U);
}

HOST_TEST(the_occurrence_count_stops_at_its_display_limit)
{
    alert_controller::reset();
    ConsoleState state = make_healthy_state();
    flap_wifi(state, 150);
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    EXPECT_EQ(static_cast<unsigned>(state.active_alerts[0].occurrence_count),
              static_cast<unsigned>(kMaxAlertOccurrenceCount));
}

HOST_TEST(the_alert_list_holds_every_alert_code_at_once)
{
    ConsoleState state = make_healthy_state();
    state.alert_count = 0U;
    for (size_t code = 0U; code < kAlertCodeCount; ++code)
    {
        ActiveAlert& alert = state.active_alerts[state.alert_count++];
        alert.code = static_cast<uint8_t>(code);
    }
    EXPECT_EQ(static_cast<size_t>(state.alert_count), kActiveAlertCapacity);
    EXPECT_EQ(alert_controller::page_count(state),
              static_cast<uint8_t>((kAlertCodeCount + kAlertsPerPage - 1U) / kAlertsPerPage));
}

namespace
{

// Bench figures from a Pico 2 W at 150 MHz, clkdiv 2.51 (issue #144).
constexpr uint16_t kBenchNominalFrameRateHz = 297U;
constexpr uint16_t kBenchSlowFrameRateHz = 150U;

/// Delivers one new one-second display timing sample, as the main loop does.
void feed_display_timing(ConsoleState& state, uint16_t frame_rate_hz)
{
    state.display_timing_status.valid = true;
    state.display_timing_status.frame_rate_hz = frame_rate_hz;
    state.display_timing_status.nominal_frame_rate_hz = kBenchNominalFrameRateHz;
    ++state.display_timing_status.sample_sequence;
    alert_controller::sync(state);
}

bool has_alert(const ConsoleState& state, AlertCode code)
{
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (state.active_alerts[i].code == static_cast<uint8_t>(code))
        {
            return true;
        }
    }
    return false;
}

} // namespace

HOST_TEST(display_lag_needs_five_consecutive_slow_samples)
{
    ConsoleState state = make_healthy_state();
    for (int i = 0; i < 4; ++i)
    {
        feed_display_timing(state, kBenchSlowFrameRateHz);
    }
    EXPECT_FALSE(has_alert(state, AlertCode::DisplayPipelineLag));

    feed_display_timing(state, kBenchSlowFrameRateHz);
    EXPECT_TRUE(has_alert(state, AlertCode::DisplayPipelineLag));
}

HOST_TEST(display_lag_ignores_a_single_slow_sample)
{
    ConsoleState state = make_healthy_state();
    feed_display_timing(state, kBenchSlowFrameRateHz);
    // Many state changes inside one sample window must not count as more samples.
    for (int i = 0; i < 10; ++i)
    {
        alert_controller::sync(state);
    }
    feed_display_timing(state, kBenchNominalFrameRateHz);
    for (int i = 0; i < 4; ++i)
    {
        feed_display_timing(state, kBenchSlowFrameRateHz);
    }
    EXPECT_FALSE(has_alert(state, AlertCode::DisplayPipelineLag));
}

HOST_TEST(display_lag_clears_once_the_frame_rate_recovers)
{
    ConsoleState state = make_healthy_state();
    for (int i = 0; i < 5; ++i)
    {
        feed_display_timing(state, kBenchSlowFrameRateHz);
    }
    EXPECT_TRUE(has_alert(state, AlertCode::DisplayPipelineLag));
    const ActiveAlert& alert = state.active_alerts[0];
    EXPECT_TRUE(std::strstr(alert.detail.data(), "150 Hz, expected 297 Hz") != nullptr);

    feed_display_timing(state, kBenchNominalFrameRateHz);
    EXPECT_FALSE(has_alert(state, AlertCode::DisplayPipelineLag));
}

HOST_TEST(display_lag_needs_a_valid_sample_with_a_known_nominal_rate)
{
    ConsoleState state = make_healthy_state();
    for (int i = 0; i < 5; ++i)
    {
        state.display_timing_status.nominal_frame_rate_hz = 0U;
        state.display_timing_status.frame_rate_hz = 0U;
        state.display_timing_status.valid = true;
        ++state.display_timing_status.sample_sequence;
        alert_controller::sync(state);
    }
    EXPECT_FALSE(has_alert(state, AlertCode::DisplayPipelineLag));
}

HOST_TEST(list_page_count_rounds_up_and_never_returns_zero)
{
    EXPECT_EQ(list_page_count(0U, 8U), 1U);
    EXPECT_EQ(list_page_count(1U, 8U), 1U);
    EXPECT_EQ(list_page_count(8U, 8U), 1U);
    EXPECT_EQ(list_page_count(9U, 8U), 2U);
    EXPECT_EQ(list_page_count(35U, 8U), 5U);
    EXPECT_EQ(list_page_count(5U, 0U), 1U);
    EXPECT_EQ(kTimeZonePageCount,
              list_page_count(kTimeZoneSelectionCount, kTimeZoneOptionsPerPage));
}
