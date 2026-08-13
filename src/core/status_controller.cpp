#include "status_controller.h"

#include <array>
#include <cstdio>

#include "settings_controller.h"

namespace status_controller
{

namespace
{

/// @brief Compares environment sensor snapshots without relying on structure padding.
bool environment_sensor_status_matches(
    const environment_sensor_manager::EnvironmentSensorStatus& lhs,
    const environment_sensor_manager::EnvironmentSensorStatus& rhs)
{
    if (lhs.enabled !=                                  rhs.enabled ||
        lhs.board !=                                    rhs.board ||
        lhs.health !=                                   rhs.health ||
        lhs.detected_device_count !=                    rhs.detected_device_count ||
        lhs.last_scan_ms !=                             rhs.last_scan_ms ||
        lhs.successful_scan_count !=                    rhs.successful_scan_count ||
        lhs.failed_scan_count !=                        rhs.failed_scan_count ||
        lhs.last_error !=                               rhs.last_error ||
        lhs.i2c_bus !=                                  rhs.i2c_bus ||
        lhs.sda_gpio !=                                 rhs.sda_gpio ||
        lhs.scl_gpio !=                                 rhs.scl_gpio ||
        lhs.baudrate_hz !=                              rhs.baudrate_hz ||
        lhs.bme_variant !=                              rhs.bme_variant ||
        lhs.bme_chip_id !=                              rhs.bme_chip_id ||
        lhs.bme_last_error !=                           rhs.bme_last_error ||
        lhs.bme_reading_valid !=                        rhs.bme_reading_valid ||
        lhs.bme_temperature_centi_celsius !=            rhs.bme_temperature_centi_celsius ||
        lhs.bme_pressure_pa !=                          rhs.bme_pressure_pa ||
        lhs.bme_humidity_milli_percent !=               rhs.bme_humidity_milli_percent ||
        lhs.bme_last_read_ms !=                         rhs.bme_last_read_ms ||
        lhs.bme_read_error !=                           rhs.bme_read_error ||
        lhs.bme_history_count !=                        rhs.bme_history_count ||
        lhs.bme_temperature_history_centi_celsius !=    rhs.bme_temperature_history_centi_celsius ||
        lhs.bme_pressure_history_deci_hpa !=            rhs.bme_pressure_history_deci_hpa ||
        lhs.bme_humidity_history_centi_percent !=       rhs.bme_humidity_history_centi_percent ||
        lhs.air_quality_raw_valid !=                    rhs.air_quality_raw_valid ||
        lhs.air_quality_raw_signal !=                   rhs.air_quality_raw_signal ||
        lhs.air_quality_baseline_raw_signal !=          rhs.air_quality_baseline_raw_signal ||
        lhs.air_quality_score_valid !=                  rhs.air_quality_score_valid ||
        lhs.air_quality_score !=                        rhs.air_quality_score ||
        lhs.air_quality_last_read_ms !=                 rhs.air_quality_last_read_ms ||
        lhs.air_quality_read_error !=                   rhs.air_quality_read_error ||
        lhs.air_quality_history_count !=                rhs.air_quality_history_count ||
        lhs.air_quality_history_score !=                rhs.air_quality_history_score)
    {
        return false;
    }

    for (size_t index = 0; index < lhs.devices.size(); ++index)
    {
        if (lhs.devices[index].device !=        rhs.devices[index].device ||
            lhs.devices[index].i2c_address !=   rhs.devices[index].i2c_address ||
            lhs.devices[index].detected !=      rhs.devices[index].detected)
        {
            return false;
        }
    }

    return true;
}

/// @brief Formats the list of currently active keypad panel pins.
void build_active_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                 std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;

    // These helpers flatten the electrical keypad snapshot into compact text so
    // the debug page can show bench-oriented panel-pin numbers directly.
    for (const auto& line : keypad_status.lines)
    {
        if (!line.configured || !line.active)
        {
            continue;
        }

        const int kWritten = std::snprintf(
            out_text.data() + used,
            out_text.size() - used,
            "%s%u",
            (used == 0) ? "" : " ",
            static_cast<unsigned>(line.panel_pin));

        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
}

/// @brief Formats the panel-pin list for the current probe hit mask.
void build_probe_hit_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                    std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;
    for (size_t i = 0; i < keypad_status.lines.size(); ++i)
    {
        if ((keypad_status.probe_hit_mask & (1U << i)) == 0)
        {
            continue;
        }

        const int kWritten = std::snprintf(out_text.data() + used, out_text.size() - used, "%s%u",
                                           (used == 0) ? "" : " ",
                                           static_cast<unsigned>(keypad_status.lines[i].panel_pin));
        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
}

} // namespace

bool set_wifi_status(ConsoleState& console_state, const WifiStatus& wifi_status)
{
    // These setters short-circuit unchanged snapshots so the UI does not redraw
    // every loop when the subsystem state is stable.
    const bool kChanged =
        console_state.wifi_status.state != wifi_status.state ||
        console_state.wifi_status.credentials_present != wifi_status.credentials_present ||
        console_state.wifi_status.internet_reachable != wifi_status.internet_reachable ||
        console_state.wifi_status.internet_probe_pending != wifi_status.internet_probe_pending ||
        console_state.wifi_status.last_error != wifi_status.last_error ||
        console_state.wifi_status.link_status != wifi_status.link_status ||
        console_state.wifi_status.internet_rtt_ms != wifi_status.internet_rtt_ms ||
        console_state.wifi_status.auth_mode != wifi_status.auth_mode ||
        console_state.wifi_status.mac_address != wifi_status.mac_address ||
        console_state.wifi_status.ssid != wifi_status.ssid ||
        console_state.wifi_status.ip_address != wifi_status.ip_address;

    if (!kChanged)
    {
        return false;
    }

    console_state.wifi_status = wifi_status;
    return true;
}

bool set_time_status(ConsoleState& console_state, const TimeStatus& time_status)
{
    const bool kChanged = console_state.time_status.synced != time_status.synced ||
                          console_state.time_status.time_text != time_status.time_text ||
                          console_state.time_status.date_text != time_status.date_text ||
                          console_state.time_status.local_epoch_day !=
                              time_status.local_epoch_day ||
                          console_state.time_status.weekday_index != time_status.weekday_index;

    if (!kChanged)
    {
        return false;
    }

    console_state.time_status = time_status;
    return true;
}

bool set_home_assistant_status(ConsoleState& console_state,
                               const HomeAssistantStatus& home_assistant_status)
{
    // operator== deliberately excludes weather_last_success_ms (see its
    // declaration) so that field alone doesn't count as a UI-visible change --
    // but it must still be copied into console_state every call, or it would
    // never reach the page that reads it on ticks where nothing else changed.
    const bool changed = !(console_state.home_assistant_status == home_assistant_status);
    console_state.home_assistant_status = home_assistant_status;
    return changed;
}

bool set_mqtt_status(ConsoleState& console_state, const MqttStatus& mqtt_status)
{
    if (console_state.mqtt_status == mqtt_status)
    {
        return false;
    }

    console_state.mqtt_status = mqtt_status;
    return true;
}

bool set_air_traffic_status(ConsoleState& console_state,
                            const AirTrafficStatus& air_traffic_status)
{
    // See set_home_assistant_status() above: last_success_ms is deliberately
    // excluded from operator== but must still always be copied through.
    const bool changed = !(console_state.air_traffic_status == air_traffic_status);
    console_state.air_traffic_status = air_traffic_status;
    return changed;
}

bool set_share_market_status(ConsoleState& console_state,
                             const ShareMarketStatus& share_market_status)
{
    const bool kChanged =
        console_state.share_data_configured != share_market_status.configured ||
        console_state.share_data_valid != share_market_status.data_valid ||
        console_state.share_data_last_error != share_market_status.last_error ||
        console_state.share_data_last_http_status != share_market_status.last_http_status ||
        console_state.share_count != share_market_status.share_count ||
        console_state.watched_shares != share_market_status.watched_shares;

    // last_success_ms is deliberately excluded from kChanged above (see its
    // declaration) but must still always be copied through, or it would never
    // reach the page that reads it on ticks where nothing else changed.
    console_state.share_data_last_success_ms = share_market_status.last_success_ms;

    if (!kChanged)
    {
        return false;
    }

    console_state.share_data_configured = share_market_status.configured;
    console_state.share_data_valid = share_market_status.data_valid;
    console_state.share_data_last_error = share_market_status.last_error;
    console_state.share_data_last_http_status = share_market_status.last_http_status;
    console_state.share_count = share_market_status.share_count;
    console_state.watched_shares = share_market_status.watched_shares;
    if (console_state.selected_share_index >= console_state.share_count)
    {
        console_state.selected_share_index = 0U;
    }
    return true;
}

bool set_environment_sensor_status(
    ConsoleState& console_state,
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status)
{
    if (environment_sensor_status_matches(console_state.environment_sensor_status,
                                          environment_sensor_status))
    {
        return false;
    }

    console_state.environment_sensor_status = environment_sensor_status;
    return true;
}

bool set_keypad_monitor_status(ConsoleState& console_state, const KeypadMonitorStatus& keypad_status)
{
    std::array<char, 48> active_panel_pins = {};
    std::array<char, 48> probe_hit_panel_pins = {};
    std::array<char, 24> pressed_key_name = {};

    // Build the display strings up front so the change detection compares the
    // exact text the diagnostics page will eventually render.
    build_active_panel_pin_text(keypad_status, active_panel_pins);
    build_probe_hit_panel_pin_text(keypad_status, probe_hit_panel_pins);
    std::snprintf(pressed_key_name.data(), pressed_key_name.size(), "%s",
                  settings_controller::decoded_pressed_key(keypad_status));

    const bool kChanged =
        console_state.keypad_debug_status.active_mask != keypad_status.active_mask ||
        console_state.keypad_debug_status.configured_count != keypad_status.configured_count ||
        console_state.keypad_debug_status.active_count != keypad_status.active_count ||
        console_state.keypad_debug_status.pressed_key_name != pressed_key_name ||
        console_state.keypad_debug_status.active_panel_pins != active_panel_pins ||
        console_state.keypad_debug_status.probe_drive_panel_pin !=
            keypad_status.probe_drive_panel_pin ||
        console_state.keypad_debug_status.probe_hit_mask != keypad_status.probe_hit_mask ||
        console_state.keypad_debug_status.probe_hit_count != keypad_status.probe_hit_count ||
        console_state.keypad_debug_status.probe_hit_panel_pins != probe_hit_panel_pins;

    if (!kChanged)
    {
        return false;
    }

    console_state.keypad_debug_status.active_mask = keypad_status.active_mask;
    console_state.keypad_debug_status.configured_count = keypad_status.configured_count;
    console_state.keypad_debug_status.active_count = keypad_status.active_count;
    console_state.keypad_debug_status.pressed_key_name = pressed_key_name;
    console_state.keypad_debug_status.active_panel_pins = active_panel_pins;
    console_state.keypad_debug_status.probe_drive_panel_pin = keypad_status.probe_drive_panel_pin;
    console_state.keypad_debug_status.probe_hit_mask = keypad_status.probe_hit_mask;
    console_state.keypad_debug_status.probe_hit_count = keypad_status.probe_hit_count;
    console_state.keypad_debug_status.probe_hit_panel_pins = probe_hit_panel_pins;
    return true;
}

bool set_main_loop_load_status(ConsoleState& console_state, const MainLoopLoadStatus& status)
{
    if (console_state.main_loop_load_status.valid == status.valid &&
        console_state.main_loop_load_status.load_percent == status.load_percent &&
        console_state.main_loop_load_status.sample_ms == status.sample_ms)
    {
        return false;
    }

    console_state.main_loop_load_status = status;
    return true;
}

bool set_heap_status(ConsoleState& console_state, const HeapStatus& status)
{
    if (console_state.heap_status.valid == status.valid &&
        console_state.heap_status.used_bytes == status.used_bytes &&
        console_state.heap_status.arena_bytes == status.arena_bytes)
    {
        return false;
    }

    console_state.heap_status = status;
    return true;
}

bool set_stack_status(ConsoleState& console_state, const StackStatus& status)
{
    if (console_state.stack_status.valid == status.valid &&
        console_state.stack_status.free_bytes == status.free_bytes)
    {
        return false;
    }

    console_state.stack_status = status;
    return true;
}

bool set_display_timing_status(ConsoleState& console_state, const DisplayTimingStatus& status)
{
    if (console_state.display_timing_status.valid == status.valid &&
        console_state.display_timing_status.frame_rate_hz == status.frame_rate_hz &&
        console_state.display_timing_status.last_rebuild_us == status.last_rebuild_us &&
        console_state.display_timing_status.present_skipped_count ==
            status.present_skipped_count)
    {
        return false;
    }

    console_state.display_timing_status = status;
    return true;
}

} // namespace status_controller
