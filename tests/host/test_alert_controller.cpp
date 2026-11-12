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

ConsoleState make_failing_air_traffic_state()
{
    ConsoleState state = make_healthy_state();
    state.air_traffic_status.enabled = true;
    state.air_traffic_status.configured = true;
    state.air_traffic_status.data_valid = false;
    state.air_traffic_status.last_http_status = 503;
    return state;
}

void sync_times(ConsoleState& state, int times)
{
    for (int i = 0; i < times; ++i)
    {
        alert_controller::sync(state);
    }
}

} // namespace

HOST_TEST(sync_raises_air_traffic_unavailable_after_repeated_failures)
{
    alert_controller::reset();
    ConsoleState state = make_failing_air_traffic_state();
    sync_times(state, kAlertRetryThreshold - 1);
    EXPECT_EQ(state.alert_count, 0U);
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_TRUE(std::strcmp(state.active_alerts[0].summary.data(), "ADS-B") == 0);
}

HOST_TEST(sync_does_not_raise_air_traffic_unavailable_when_disabled_or_unconfigured)
{
    alert_controller::reset();
    ConsoleState disabled = make_failing_air_traffic_state();
    disabled.air_traffic_status.enabled = false;
    sync_times(disabled, kAlertRetryThreshold * 2);
    EXPECT_EQ(disabled.alert_count, 0U);

    alert_controller::reset();
    ConsoleState unconfigured = make_failing_air_traffic_state();
    unconfigured.air_traffic_status.configured = false;
    sync_times(unconfigured, kAlertRetryThreshold * 2);
    EXPECT_EQ(unconfigured.alert_count, 0U);
}

HOST_TEST(sync_air_traffic_unavailable_needs_consecutive_failures_and_clears_on_valid_data)
{
    alert_controller::reset();
    ConsoleState state = make_failing_air_traffic_state();
    sync_times(state, kAlertRetryThreshold - 1);

    // One good fetch resets the run, so a single later failure does not alert.
    state.air_traffic_status.data_valid = true;
    alert_controller::sync(state);
    state.air_traffic_status.data_valid = false;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);

    sync_times(state, kAlertRetryThreshold);
    EXPECT_EQ(state.alert_count, 1U);
    state.air_traffic_status.data_valid = true;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
}
