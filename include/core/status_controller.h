#pragma once

#include "console_model.h"
#include "input.h"

/// @brief Status snapshot ingestion, split out of console_controller.cpp --
/// issue #44 (staged, following the pinter_controller.h/alert_controller.h/
/// calendar_controller.h/settings_controller.h precedent).
/// @details Every function here takes the console's ConsoleState explicitly,
/// copies in the given snapshot with change detection, and returns whether
/// anything UI-visible changed. It deliberately does NOT trigger a softkey
/// rebuild itself -- console_controller.cpp's public set_*_status() wrappers
/// own that side effect, since update_softkeys_from_state() is part of this
/// file's private dispatch, not something a split-out module can reach.
namespace status_controller
{

/// @brief Updates the cached Wi-Fi snapshot in the console model.
bool set_wifi_status(ConsoleState& console_state, const WifiStatus& wifi_status);

/// @brief Updates the cached time snapshot in the console model.
bool set_time_status(ConsoleState& console_state, const TimeStatus& time_status);

/// @brief Updates the cached Home Assistant snapshot in the console model.
bool set_home_assistant_status(ConsoleState& console_state,
                               const HomeAssistantStatus& home_assistant_status);

/// @brief Updates the cached MQTT snapshot in the console model.
bool set_mqtt_status(ConsoleState& console_state, const MqttStatus& mqtt_status);

/// @brief Updates the cached local air-traffic snapshot in the console model.
bool set_air_traffic_status(ConsoleState& console_state,
                            const AirTrafficStatus& air_traffic_status);

/// @brief Updates the cached share market-data snapshot in the console model.
bool set_share_market_status(ConsoleState& console_state,
                             const ShareMarketStatus& share_market_status);

/// @brief Updates the cached environment sensor discovery snapshot in the console model.
bool set_environment_sensor_status(
    ConsoleState& console_state,
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status);

/// @brief Updates the keypad diagnostics snapshot shown by the UI.
bool set_keypad_monitor_status(ConsoleState& console_state, const KeypadMonitorStatus& keypad_status);

/// @brief Updates foreground main-loop load telemetry shown by Resources status.
bool set_main_loop_load_status(ConsoleState& console_state, const MainLoopLoadStatus& status);

/// @brief Updates live heap usage telemetry shown by Resources status.
bool set_heap_status(ConsoleState& console_state, const HeapStatus& status);

/// @brief Updates worst-case core-0 stack headroom telemetry shown by Resources status.
bool set_stack_status(ConsoleState& console_state, const StackStatus& status);

/// @brief Updates panel scanout timing telemetry shown by Resources status.
bool set_display_timing_status(ConsoleState& console_state, const DisplayTimingStatus& status);

} // namespace status_controller
