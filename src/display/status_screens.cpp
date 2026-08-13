#include "status_screens.h"

#include <algorithm>
#include <cstdio>
#include <cstdint>

#include "config_manager.h"
#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace status_screens
{

namespace
{

/// @brief Returns the terse test-state label shown on status-oriented screens.
const char* test_state_text(SystemTestState state)
{
    switch (state)
    {
    case SystemTestState::Idle:
        return "IDLE";
    case SystemTestState::Running:
        return "RUN";
    case SystemTestState::Passed:
        return "PASS";
    case SystemTestState::Failed:
        return "FAIL";
    }

    return "?";
}

/// @brief Returns the Wi-Fi state label sized to fit the one-line status panel.
const char* wifi_state_text(WifiConnectionState state)
{
    switch (state)
    {
    case WifiConnectionState::Disabled:
        return "Disabled";
    case WifiConnectionState::Unconfigured:
        return "Unconfig";
    case WifiConnectionState::Initializing:
        return "Init";
    case WifiConnectionState::Scanning:
        return "Scan";
    case WifiConnectionState::Connecting:
        return "Connect";
    case WifiConnectionState::WaitingForIp:
        return "DHCP";
    case WifiConnectionState::Connected:
        return "Up";
    case WifiConnectionState::AuthFailed:
        return "Bad auth";
    case WifiConnectionState::NoNetwork:
        return "No net";
    case WifiConnectionState::ConnectFailed:
        return "Fail";
    case WifiConnectionState::Error:
        return "Error";
    }

    return "?";
}

/// @brief Returns the MQTT state label used on the condensed status page.
const char* mqtt_state_text(MqttConnectionState state)
{
    switch (state)
    {
    case MqttConnectionState::Disabled:
        return "Disabled";
    case MqttConnectionState::Unconfigured:
        return "Unconfig";
    case MqttConnectionState::WaitingForWifi:
        return "Wait wifi";
    case MqttConnectionState::Resolving:
        return "Resolve";
    case MqttConnectionState::Connecting:
        return "Connect";
    case MqttConnectionState::Connected:
        return "Up";
    case MqttConnectionState::AuthFailed:
        return "Auth";
    case MqttConnectionState::Error:
        return "Error";
    }

    return "?";
}

/// @brief Returns the optional environment-board health label for diagnostics.
const char* environment_sensor_health_text(
    environment_sensor_manager::EnvironmentSensorHealth health)
{
    switch (health)
    {
    case environment_sensor_manager::EnvironmentSensorHealth::Disabled:
        return "Disabled";
    case environment_sensor_manager::EnvironmentSensorHealth::BusReady:
        return "Bus ready";
    case environment_sensor_manager::EnvironmentSensorHealth::BoardMissing:
        return "Missing";
    case environment_sensor_manager::EnvironmentSensorHealth::Partial:
        return "Partial";
    case environment_sensor_manager::EnvironmentSensorHealth::BoardDetected:
        return "Detected";
    case environment_sensor_manager::EnvironmentSensorHealth::Fault:
        return "Fault";
    }

    return "?";
}

/// @brief Formats configured I2C pins into one compact status-page value.
void build_environment_bus_text(const environment_sensor_manager::EnvironmentSensorStatus& status,
                                char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    if (!status.enabled)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    std::snprintf(out, out_size, "I2C%d SDA%d SCL%d", static_cast<int>(status.i2c_bus),
                  static_cast<int>(status.sda_gpio), static_cast<int>(status.scl_gpio));
}

/// @brief Formats the detected Waveshare device addresses without overflowing the row.
void build_environment_address_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    out[0] = '\0';
    if (!status.enabled || status.detected_device_count == 0U)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    size_t used = 0U;
    for (const environment_sensor_manager::EnvironmentSensorPresence& presence : status.devices)
    {
        if (!presence.detected)
        {
            continue;
        }

        const int written =
            std::snprintf(out + used, out_size - used, "%s%s%02X", used == 0U ? "" : ",",
                          used == 0U ? "0x" : "", static_cast<unsigned>(presence.i2c_address));
        if (written <= 0)
        {
            break;
        }

        const size_t write_size = static_cast<size_t>(written);
        if (write_size >= (out_size - used))
        {
            out[out_size - 1U] = '\0';
            break;
        }
        used += write_size;
    }
}

/// @brief Returns the detected BME pressure-sensor variant for the status page.
const char* environment_bme_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status)
{
    if (!status.enabled)
    {
        return "-";
    }

    switch (status.bme_variant)
    {
    case environment_sensor_manager::EnvironmentBmeVariant::NotChecked:
        return "UNKNOWN";
    case environment_sensor_manager::EnvironmentBmeVariant::Missing:
        return "MISSING";
    case environment_sensor_manager::EnvironmentBmeVariant::Bme280:
        return "BME280";
    case environment_sensor_manager::EnvironmentBmeVariant::Bme680:
        return "BME680";
    case environment_sensor_manager::EnvironmentBmeVariant::Unknown:
        return "UNKNOWN";
    case environment_sensor_manager::EnvironmentBmeVariant::Fault:
        return "FAULT";
    }

    return "UNKNOWN";
}

/// @brief Draws one proportional resource bar with a label and compact value.
void draw_resource_bar(uint8_t* fb, int x, int y, int width, int height, size_t used_bytes,
                       size_t total_bytes, const char* label, const char* value_text)
{
    framebuffer::draw_rect(fb, x, y, width, height, true);
    const size_t clamped_total = (total_bytes == 0U) ? 1U : total_bytes;
    const size_t clamped_used = std::min(used_bytes, clamped_total);
    const int inner_width = std::max(0, width - 2);
    const int inner_height = std::max(0, height - 2);
    const int fill_width = static_cast<int>((clamped_used * static_cast<size_t>(inner_width)) /
                                            clamped_total);
    if (fill_width > 0 && inner_height > 0)
    {
        framebuffer::fill_rect(fb, x + 1, y + 1, fill_width, inner_height, true);
    }
    framebuffer::draw_text(fb, x + 2, y - 10, label, true, fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, x + width - framebuffer::measure_text(value_text,
                                                                      fonts::FontFace::Font5x7, 1),
                           y - 10, value_text, true, fonts::FontFace::Font5x7, 1);
}

/// @brief Formats byte counts as rounded-up KiB for compact resource rows.
void build_kib_text(size_t bytes, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const size_t kib = (bytes + 1023U) / 1024U;
    std::snprintf(out, out_size, "%zuK", kib);
}

/// @brief Returns the REST status label used by status subpages.
const char* home_assistant_rest_state_text(const ConsoleState& console_state,
                                           const RuntimeConfig& config)
{
    if (!config.home_assistant_enabled)
    {
        return "Disabled";
    }
    if (console_state.weather_source == WeatherSource::HomeAssistant)
    {
        return screens::home_assistant_state_text(console_state.home_assistant_status.state);
    }
    if (config.home_assistant_host[0] != '\0' && config.home_assistant_token[0] != '\0')
    {
        return "Enabled";
    }

    return "Unconfig";
}

/// @brief Draws the Status selector page.
void draw_status_selector_page(uint8_t* fb)
{
    (void)fb;
}

/// @brief Draws the high-level system overview status page.
void draw_status_overview_page(uint8_t* fb, const ConsoleState& console_state)
{
    char alert_count_text[8] = {};
    std::snprintf(alert_count_text, sizeof(alert_count_text), "%u",
                  static_cast<unsigned>(console_state.alert_count));

    const screens::DetailRow rows[] = {
        {"TIME",
         console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--"},
        {"WIFI", wifi_state_text(console_state.wifi_status.state)},
        {"IP ADDRESS", console_state.wifi_status.ip_address[0]
                           ? console_state.wifi_status.ip_address.data()
                           : "-"},
        {"HA MQTT", mqtt_state_text(console_state.mqtt_status.state)},
        {"ENV SENSOR",
         environment_sensor_health_text(console_state.environment_sensor_status.health)},
        {"ALERTS", alert_count_text},
        {"TEST", test_state_text(console_state.test_state)},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), 34, 13);
}

/// @brief Draws network and Home Assistant connectivity state.
void draw_status_connectivity_page(uint8_t* fb, const ConsoleState& console_state)
{
    const RuntimeConfig& config = config_manager::settings();
    char rtt_text[16] = {};
    if (console_state.wifi_status.internet_rtt_ms >= 0)
    {
        std::snprintf(rtt_text, sizeof(rtt_text), "%dms",
                      console_state.wifi_status.internet_rtt_ms);
    }
    else
    {
        std::snprintf(rtt_text, sizeof(rtt_text), "-");
    }

    const char* internet_text = console_state.wifi_status.internet_reachable ? "Up" : "Down";
    if (console_state.wifi_status.internet_probe_pending)
    {
        internet_text = "Probe";
    }

    const screens::DetailRow rows[] = {
        {"TIME SYNC", console_state.time_status.synced ? "Synced" : "No sync"},
        {"WIFI", wifi_state_text(console_state.wifi_status.state)},
        {"SSID", console_state.wifi_status.credentials_present
                     ? console_state.wifi_status.ssid.data()
                     : "-"},
        {"IP ADDRESS", console_state.wifi_status.ip_address[0]
                           ? console_state.wifi_status.ip_address.data()
                           : "-"},
        {"INTERNET", internet_text},
        {"RTT", rtt_text},
        {"HA REST", home_assistant_rest_state_text(console_state, config)},
        {"HA MQTT", mqtt_state_text(console_state.mqtt_status.state)},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), 34, 13);
}

/// @brief Draws fixed-memory and foreground-loop resource usage.
void draw_status_resources_page(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr size_t kConsoleStateBytes = sizeof(ConsoleState);
    constexpr size_t kUiFramebufferBytes = static_cast<size_t>(kUiFbSize) * 2U;
    const ImageFootprintStatus& footprint = console_state.image_footprint_status;
    const size_t static_ram = footprint.static_ram_bytes;
    const size_t total_ram = footprint.total_ram_bytes;
    const size_t program_flash = footprint.program_flash_bytes;
    const size_t flash_budget = footprint.flash_budget_bytes;
    const size_t reserved_flash = footprint.reserved_flash_bytes;

    char flash_value_text[24] = {};
    char ram_value_text[24] = {};
    char program_flash_text[16] = {};
    char flash_budget_text[16] = {};
    char reserved_flash_text[16] = {};
    char static_ram_text[16] = {};
    char ram_budget_text[16] = {};
    char console_text[16] = {};
    char framebuffer_text[16] = {};
    char loop_load_text[16] = {};
    char loop_sample_text[16] = {};
    char frame_rate_text[16] = {};
    char rebuild_time_text[16] = {};
    char present_skipped_text[16] = {};
    char heap_text[16] = {};
    char stack_text[16] = {};
    build_kib_text(program_flash, program_flash_text, sizeof(program_flash_text));
    build_kib_text(flash_budget, flash_budget_text, sizeof(flash_budget_text));
    build_kib_text(reserved_flash, reserved_flash_text, sizeof(reserved_flash_text));
    build_kib_text(static_ram, static_ram_text, sizeof(static_ram_text));
    build_kib_text(total_ram, ram_budget_text, sizeof(ram_budget_text));
    build_kib_text(kConsoleStateBytes, console_text, sizeof(console_text));
    build_kib_text(kUiFramebufferBytes, framebuffer_text, sizeof(framebuffer_text));
    std::snprintf(flash_value_text, sizeof(flash_value_text), "%s/%s", program_flash_text,
                  flash_budget_text);
    std::snprintf(ram_value_text, sizeof(ram_value_text), "%s/%s", static_ram_text,
                  ram_budget_text);
    if (console_state.heap_status.valid)
    {
        char heap_used_text[16] = {};
        build_kib_text(console_state.heap_status.used_bytes, heap_used_text,
                       sizeof(heap_used_text));
        if (console_state.heap_status.arena_bytes > 0U)
        {
            char heap_arena_text[16] = {};
            build_kib_text(console_state.heap_status.arena_bytes, heap_arena_text,
                           sizeof(heap_arena_text));
            std::snprintf(heap_text, sizeof(heap_text), "%s/%s", heap_used_text, heap_arena_text);
        }
        else
        {
            std::snprintf(heap_text, sizeof(heap_text), "%s", heap_used_text);
        }
    }
    else
    {
        std::snprintf(heap_text, sizeof(heap_text), "-");
    }
    if (console_state.stack_status.valid)
    {
        // Core-0's stack is only 4KB (PICO_STACK_SIZE) -- shown as exact
        // bytes, not KiB-rounded, since build_kib_text's ceiling rounding
        // would mask a near-overflow (e.g. 100 bytes free rounding up to a
        // reassuring-looking "1K").
        std::snprintf(stack_text, sizeof(stack_text), "%luB",
                      static_cast<unsigned long>(console_state.stack_status.free_bytes));
    }
    else
    {
        std::snprintf(stack_text, sizeof(stack_text), "-");
    }
    if (console_state.main_loop_load_status.valid)
    {
        std::snprintf(loop_load_text, sizeof(loop_load_text), "%u%%",
                      static_cast<unsigned>(console_state.main_loop_load_status.load_percent));
        std::snprintf(loop_sample_text, sizeof(loop_sample_text), "%ums",
                      static_cast<unsigned>(console_state.main_loop_load_status.sample_ms));
    }
    else
    {
        std::snprintf(loop_load_text, sizeof(loop_load_text), "-");
        std::snprintf(loop_sample_text, sizeof(loop_sample_text), "-");
    }
    if (console_state.display_timing_status.valid)
    {
        std::snprintf(frame_rate_text, sizeof(frame_rate_text), "%uHz",
                      static_cast<unsigned>(console_state.display_timing_status.frame_rate_hz));
        std::snprintf(rebuild_time_text, sizeof(rebuild_time_text), "%luus",
                      static_cast<unsigned long>(console_state.display_timing_status.last_rebuild_us));
    }
    else
    {
        std::snprintf(frame_rate_text, sizeof(frame_rate_text), "-");
        std::snprintf(rebuild_time_text, sizeof(rebuild_time_text), "-");
    }
    std::snprintf(present_skipped_text, sizeof(present_skipped_text), "%lu",
                  static_cast<unsigned long>(
                      console_state.display_timing_status.present_skipped_count));

    draw_resource_bar(fb, 54, 46, 160, 20, program_flash, flash_budget, "PROGRAM FLASH",
                      flash_value_text);
    draw_resource_bar(fb, 54, 96, 160, 20, static_ram, total_ram, "STATIC RAM", ram_value_text);

    const screens::DetailRow rows[] = {
        {"PROG FLASH", flash_value_text},
        {"RESERVED", reserved_flash_text},
        {"STATIC RAM", ram_value_text},
        {"CONSOLE", console_text},
        {"UI BUFFERS", framebuffer_text},
        {"LOOP LOAD", loop_load_text},
        {"LOOP SAMPLE", loop_sample_text},
        {"FRAME RATE", frame_rate_text},
        {"RASTER BUILD", rebuild_time_text},
        {"PRESENT SKIP", present_skipped_text},
        {"HEAP LIVE", heap_text},
        {"STACK FREE", stack_text},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), 136, 13);
}

/// @brief Draws local sensor health and readings.
void draw_status_sensors_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    char sensor_bus_text[24] = {};
    build_environment_bus_text(console_state.environment_sensor_status, sensor_bus_text,
                               sizeof(sensor_bus_text));
    char sensor_addresses_text[24] = {};
    build_environment_address_text(console_state.environment_sensor_status, sensor_addresses_text,
                                   sizeof(sensor_addresses_text));
    char sensor_temperature_text[16] = {};
    screens::build_environment_temperature_text(console_state.environment_sensor_status,
                                                sensor_temperature_text,
                                                sizeof(sensor_temperature_text));
    char sensor_humidity_text[16] = {};
    screens::build_environment_humidity_text(console_state.environment_sensor_status,
                                             sensor_humidity_text, sizeof(sensor_humidity_text));
    char sensor_pressure_text[16] = {};
    screens::build_environment_pressure_text(console_state.environment_sensor_status,
                                             sensor_pressure_text, sizeof(sensor_pressure_text));
    char sensor_scan_text[16] = {};
    if (console_state.environment_sensor_status.enabled &&
        console_state.environment_sensor_status.last_scan_ms > 0U)
    {
        // True elapsed age (issue #16's stale-data display policy), not the
        // raw boot-uptime timestamp this used to show -- that read like an
        // ever-growing "age" even seconds after a fresh scan.
        const uint32_t scan_ms = console_state.environment_sensor_status.last_scan_ms;
        screens::build_elapsed_time_text(now_ms > scan_ms ? now_ms - scan_ms : 0U, sensor_scan_text,
                                         sizeof(sensor_scan_text));
    }
    else
    {
        std::snprintf(sensor_scan_text, sizeof(sensor_scan_text), "-");
    }
    char sensor_error_text[16] = {};
    if (console_state.environment_sensor_status.enabled)
    {
        std::snprintf(sensor_error_text, sizeof(sensor_error_text), "%d",
                      console_state.environment_sensor_status.last_error);
    }
    else
    {
        std::snprintf(sensor_error_text, sizeof(sensor_error_text), "-");
    }

    const screens::DetailRow rows[] = {
        {"ENV SENSOR", environment_sensor_health_text(console_state.environment_sensor_status.health)},
        {"ENV BUS", sensor_bus_text},
        {"ENV BME", environment_bme_text(console_state.environment_sensor_status)},
        {"ENV TEMP", sensor_temperature_text},
        {"ENV HUM", sensor_humidity_text},
        {"ENV PRESS", sensor_pressure_text},
        {"ENV ADDR", sensor_addresses_text},
        {"ENV SCAN", sensor_scan_text},
        {"ENV ERR", sensor_error_text},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), 34, 13);
}

/// @brief Draws external integration state.
void draw_status_integrations_page(uint8_t* fb, const ConsoleState& console_state,
                                   uint32_t now_ms)
{
    const RuntimeConfig& config = config_manager::settings();
    char http_text[12] = {};
    if (console_state.home_assistant_status.last_http_status > 0)
    {
        std::snprintf(http_text, sizeof(http_text), "%d",
                      console_state.home_assistant_status.last_http_status);
    }
    else
    {
        std::snprintf(http_text, sizeof(http_text), "-");
    }

    char share_http_text[12] = {};
    if (console_state.share_data_last_http_status > 0)
    {
        std::snprintf(share_http_text, sizeof(share_http_text), "%d",
                      console_state.share_data_last_http_status);
    }
    else
    {
        std::snprintf(share_http_text, sizeof(share_http_text), "-");
    }

    // Issue #16's stale-data display policy: same helper and "DEMO vs
    // freshness" split used by the Weather/Shares/Air Traffic pages, so this
    // summary page reads consistently with the pages it's summarizing.
    char weather_data_text[16] = {};
    screens::build_data_freshness_text(
        console_state.home_assistant_status.state == HomeAssistantConnectionState::Connected,
        console_state.home_assistant_status.weather_last_success_ms, now_ms,
        4U * 5U * 60U * 1000U, weather_data_text, sizeof(weather_data_text));

    char share_data_text[16] = {};
    if (console_state.share_data_last_success_ms == 0U)
    {
        std::snprintf(share_data_text, sizeof(share_data_text), "DEMO");
    }
    else
    {
        screens::build_data_freshness_text(console_state.share_data_valid,
                                           console_state.share_data_last_success_ms, now_ms,
                                           4U * 5U * 60U * 1000U, share_data_text,
                                           sizeof(share_data_text));
    }

    const screens::DetailRow rows[] = {
        {"HA REST", home_assistant_rest_state_text(console_state, config)},
        {"HA HOST", config.home_assistant_host[0] != '\0' ? config.home_assistant_host.data() : "-"},
        {"WX FETCH", screens::weather_fetch_state_text(console_state.home_assistant_status.state)},
        {"WX HOST", console_state.home_assistant_status.host[0]
                        ? console_state.home_assistant_status.host.data()
                        : "-"},
        {"WX DATA", weather_data_text},
        {"HTTP", http_text},
        {"SHARES", console_state.share_data_configured ? "Configured" : "Unconfig"},
        {"SHARE DATA", share_data_text},
        {"SHARE HTTP", share_http_text},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), 34, 13);
}

} // namespace

void draw_status_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    switch (console_state.active_page)
    {
    case MenuPage::Status:
        draw_status_selector_page(fb);
        break;
    case MenuPage::StatusOverview:
        draw_status_overview_page(fb, console_state);
        break;
    case MenuPage::StatusConnectivity:
        draw_status_connectivity_page(fb, console_state);
        break;
    case MenuPage::StatusResources:
        draw_status_resources_page(fb, console_state);
        break;
    case MenuPage::StatusSensors:
        draw_status_sensors_page(fb, console_state, now_ms);
        break;
    case MenuPage::StatusIntegrations:
        draw_status_integrations_page(fb, console_state, now_ms);
        break;
    default:
        break;
    }
}

} // namespace status_screens
