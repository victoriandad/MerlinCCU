#include "status_controller.h"

#include "test_framework.h"

HOST_TEST(set_wifi_status_reports_unchanged_for_an_identical_snapshot)
{
    ConsoleState state = {};
    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connected;
    EXPECT_TRUE(status_controller::set_wifi_status(state, wifi));
    EXPECT_FALSE(status_controller::set_wifi_status(state, wifi)); // identical second call
}

HOST_TEST(set_wifi_status_reports_changed_when_a_field_differs)
{
    ConsoleState state = {};
    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connecting;
    status_controller::set_wifi_status(state, wifi);

    wifi.state = WifiConnectionState::Connected;
    EXPECT_TRUE(status_controller::set_wifi_status(state, wifi));
    EXPECT_TRUE(state.wifi_status.state == WifiConnectionState::Connected);
}

HOST_TEST(set_time_status_change_detection_covers_the_synced_flag)
{
    ConsoleState state = {};
    // state.time_status starts zero-initialized (synced=false), so an
    // identical incoming snapshot is correctly reported as unchanged.
    TimeStatus time_status = {};
    time_status.synced = false;
    EXPECT_FALSE(status_controller::set_time_status(state, time_status));

    time_status.synced = true;
    EXPECT_TRUE(status_controller::set_time_status(state, time_status));
    EXPECT_FALSE(status_controller::set_time_status(state, time_status)); // now stable
}

HOST_TEST(set_mqtt_status_uses_struct_equality_for_change_detection)
{
    ConsoleState state = {};
    MqttStatus mqtt = {};
    mqtt.state = MqttConnectionState::Disabled; // matches the zero-initialized default
    EXPECT_FALSE(status_controller::set_mqtt_status(state, mqtt));

    mqtt.state = MqttConnectionState::Connected;
    EXPECT_TRUE(status_controller::set_mqtt_status(state, mqtt));
    EXPECT_FALSE(status_controller::set_mqtt_status(state, mqtt)); // now stable
}

HOST_TEST(set_home_assistant_status_always_copies_but_excludes_last_success_ms_from_change_detection)
{
    ConsoleState state = {};
    HomeAssistantStatus status = {};
    status.state = HomeAssistantConnectionState::Connected;
    EXPECT_TRUE(status_controller::set_home_assistant_status(state, status));

    // Only weather_last_success_ms differs -- operator== deliberately
    // excludes it, so this must report unchanged...
    status.weather_last_success_ms = 12345U;
    EXPECT_FALSE(status_controller::set_home_assistant_status(state, status));
    // ...but the field must still have been copied through regardless, or a
    // freshness display reading it would never see the update.
    EXPECT_EQ(state.home_assistant_status.weather_last_success_ms, 12345U);
}

HOST_TEST(set_air_traffic_status_always_copies_but_excludes_last_success_ms_from_change_detection)
{
    ConsoleState state = {};
    AirTrafficStatus status = {};
    status.enabled = true;
    EXPECT_TRUE(status_controller::set_air_traffic_status(state, status));

    status.last_success_ms = 999U;
    EXPECT_FALSE(status_controller::set_air_traffic_status(state, status));
    EXPECT_EQ(state.air_traffic_status.last_success_ms, 999U);
}

HOST_TEST(set_share_market_status_always_copies_last_success_ms_but_excludes_it_from_change_detection)
{
    ConsoleState state = {};
    ShareMarketStatus status = {};
    status.configured = true;
    status.share_count = 2U;
    EXPECT_TRUE(status_controller::set_share_market_status(state, status));

    status.last_success_ms = 42U;
    EXPECT_FALSE(status_controller::set_share_market_status(state, status));
    EXPECT_EQ(state.share_data_last_success_ms, 42U);
}

HOST_TEST(set_share_market_status_clamps_selected_index_when_the_watchlist_shrinks)
{
    ConsoleState state = {};
    state.selected_share_index = 4U;

    ShareMarketStatus status = {};
    status.configured = true;
    status.share_count = 2U; // fewer shares than the currently selected index
    EXPECT_TRUE(status_controller::set_share_market_status(state, status));
    EXPECT_EQ(state.selected_share_index, 0U);
}

HOST_TEST(set_keypad_monitor_status_reports_unchanged_for_an_identical_snapshot)
{
    ConsoleState state = {};
    KeypadMonitorStatus keypad = {};
    keypad.active_mask = 0x1U;
    keypad.configured_count = 4U;
    EXPECT_TRUE(status_controller::set_keypad_monitor_status(state, keypad));
    EXPECT_FALSE(status_controller::set_keypad_monitor_status(state, keypad));
}

HOST_TEST(set_main_loop_load_status_change_detection_covers_load_and_sample)
{
    ConsoleState state = {};
    MainLoopLoadStatus status = {};
    status.valid = true;
    status.load_percent = 10U;
    status.sample_ms = 100U;
    EXPECT_TRUE(status_controller::set_main_loop_load_status(state, status));
    EXPECT_FALSE(status_controller::set_main_loop_load_status(state, status));

    status.load_percent = 20U;
    EXPECT_TRUE(status_controller::set_main_loop_load_status(state, status));
}

HOST_TEST(set_heap_status_change_detection_covers_used_and_arena_bytes)
{
    ConsoleState state = {};
    HeapStatus status = {};
    status.valid = true;
    status.used_bytes = 1000U;
    status.arena_bytes = 2000U;
    EXPECT_TRUE(status_controller::set_heap_status(state, status));
    EXPECT_FALSE(status_controller::set_heap_status(state, status));

    status.used_bytes = 1500U;
    EXPECT_TRUE(status_controller::set_heap_status(state, status));
}

HOST_TEST(set_stack_status_change_detection_covers_free_bytes)
{
    ConsoleState state = {};
    StackStatus status = {};
    status.valid = true;
    status.free_bytes = 500U;
    EXPECT_TRUE(status_controller::set_stack_status(state, status));
    EXPECT_FALSE(status_controller::set_stack_status(state, status));

    status.free_bytes = 400U;
    EXPECT_TRUE(status_controller::set_stack_status(state, status));
}

HOST_TEST(set_display_timing_status_change_detection_covers_present_skipped_count)
{
    ConsoleState state = {};
    DisplayTimingStatus status = {};
    status.valid = true;
    status.frame_rate_hz = 30U;
    status.last_rebuild_us = 1000U;
    status.present_skipped_count = 0U;
    EXPECT_TRUE(status_controller::set_display_timing_status(state, status));
    EXPECT_FALSE(status_controller::set_display_timing_status(state, status));

    status.present_skipped_count = 1U;
    EXPECT_TRUE(status_controller::set_display_timing_status(state, status));
}
