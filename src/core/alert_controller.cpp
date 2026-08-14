#include "alert_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "alert_ordering.h"
#include "config_manager.h"
#include "console_controller_internal.h"
#include "pico/error.h"

namespace alert_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

uint32_t g_alert_sequence_counter = 1U;
uint32_t g_alert_acknowledged_sequence = 0U;
uint8_t g_home_assistant_connect_failures = 0U;
uint8_t g_weather_refresh_failures = 0U;
uint8_t g_mqtt_connect_failures = 0U;
uint8_t g_time_unsynced_samples = 0U;
uint8_t g_keypad_fault_samples = 0U;
constexpr uint8_t kAlertRetryThreshold = 5U;
constexpr float kFreezingTemperatureAlertCelsius = 0.0F;
constexpr float kHighTemperatureAlertCelsius = 30.0F;
constexpr float kHighWindAlertMph = 40.0F;
constexpr uint32_t kStormLowPressurePa = 98000U;
constexpr uint32_t kStormRapidPressureFallPa = 200U;
constexpr uint8_t kStormPressureMinimumSamples = 6U;

enum class AlertCode : uint8_t
{
    WifiDisconnected = 0,
    WifiAuthFailed,
    TimeNotSynced,
    HomeAssistantOffline,
    HomeAssistantUnauthorized,
    HomeAssistantEntityMissing,
    WeatherUnavailable,
    WeatherProviderWarning,
    WeatherTemperatureWarning,
    WeatherWindWarning,
    MqttOffline,
    KeypadLineFault,
    EnvironmentSensorFault,
    LocalPressureStormWarning,
    DisplayPipelineLag,
    ShareDataUnavailable,
    PinterScheduleConflict,
    Count,
};

std::array<bool, static_cast<size_t>(AlertCode::Count)> g_alert_suppressed = {};
std::array<bool, static_cast<size_t>(AlertCode::Count)> g_alert_was_active = {};

constexpr size_t alert_code_index(AlertCode code)
{
    return static_cast<size_t>(code);
}

/// @brief Finds an alert by code in the active queue.
int find_alert_by_code(const ConsoleState& console_state, AlertCode code)
{
    for (uint8_t i = 0U; i < console_state.alert_count; ++i)
    {
        if (console_state.active_alerts[i].code == static_cast<uint8_t>(code))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// @brief Adds or updates one alert condition in the active queue.
void set_alert_condition(ConsoleState& console_state, AlertCode code, bool active,
                         AlertSeverity severity, const char* summary, const char* detail)
{
    const size_t code_idx = alert_code_index(code);
    if (!active)
    {
        g_alert_was_active[code_idx] = false;
        g_alert_suppressed[code_idx] = false;
        const int existing = find_alert_by_code(console_state, code);
        if (existing >= 0)
        {
            erase_active_alert(console_state, static_cast<uint8_t>(existing));
        }
        return;
    }

    g_alert_was_active[code_idx] = true;
    if (g_alert_suppressed[code_idx])
    {
        return;
    }

    const int existing = find_alert_by_code(console_state, code);
    if (existing >= 0)
    {
        ActiveAlert& alert = console_state.active_alerts[static_cast<size_t>(existing)];
        alert.severity = severity;
        std::snprintf(alert.summary.data(), alert.summary.size(), "%s", summary);
        std::snprintf(alert.detail.data(), alert.detail.size(), "%s", detail);
        // Existing active alerts keep their original arrival sequence/time so
        // periodic state re-evaluation does not retrigger annunciation.
        return;
    }

    if (console_state.alert_count >= console_state.active_alerts.size())
    {
        erase_active_alert(console_state, 0U);
    }

    ActiveAlert& alert = console_state.active_alerts[console_state.alert_count++];
    alert.severity = severity;
    alert.code = static_cast<uint8_t>(code);
    alert.sequence = g_alert_sequence_counter++;
    std::snprintf(alert.occurred_time_text.data(), alert.occurred_time_text.size(), "%s",
                  console_state.time_status.synced ? console_state.time_status.time_text.data()
                                                   : "--:--");
    std::snprintf(alert.summary.data(), alert.summary.size(), "%s", summary);
    std::snprintf(alert.detail.data(), alert.detail.size(), "%s", detail);
}

/// @brief Adds one optional temperature value to a min/max range.
void include_temperature_alert_value(bool valid, float value_celsius, bool& have_range,
                                     float& min_celsius, float& max_celsius)
{
    if (!valid)
    {
        return;
    }

    if (!have_range)
    {
        min_celsius = value_celsius;
        max_celsius = value_celsius;
        have_range = true;
        return;
    }

    min_celsius = std::min(min_celsius, value_celsius);
    max_celsius = std::max(max_celsius, value_celsius);
}

/// @brief Builds the combined current/forecast temperature range used by weather alerts.
bool weather_temperature_alert_range(const WeatherMetrics& metrics, float& min_celsius,
                                     float& max_celsius)
{
    bool have_range = false;
    include_temperature_alert_value(metrics.current_temperature_celsius_valid,
                                    metrics.current_temperature_celsius, have_range, min_celsius,
                                    max_celsius);
    include_temperature_alert_value(metrics.forecast_min_temperature_celsius_valid,
                                    metrics.forecast_min_temperature_celsius, have_range,
                                    min_celsius, max_celsius);
    include_temperature_alert_value(metrics.forecast_max_temperature_celsius_valid,
                                    metrics.forecast_max_temperature_celsius, have_range,
                                    min_celsius, max_celsius);
    return have_range;
}

/// @brief Builds the maximum current/forecast wind value used by weather alerts.
bool weather_wind_alert_maximum(const WeatherMetrics& metrics, float& max_wind_mph)
{
    bool have_wind = false;
    if (metrics.current_wind_speed_mph_valid)
    {
        max_wind_mph = metrics.current_wind_speed_mph;
        have_wind = true;
    }
    if (metrics.forecast_max_wind_speed_mph_valid &&
        (!have_wind || metrics.forecast_max_wind_speed_mph > max_wind_mph))
    {
        max_wind_mph = metrics.forecast_max_wind_speed_mph;
        have_wind = true;
    }

    return have_wind;
}

/// @brief Formats the operator-facing temperature alert detail.
void build_weather_temperature_alert_detail(float min_celsius, float max_celsius, char* out,
                                            size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const int min_celsius_rounded = static_cast<int>(std::lround(min_celsius));
    const int max_celsius_rounded = static_cast<int>(std::lround(max_celsius));
    const bool freezing = min_celsius <= kFreezingTemperatureAlertCelsius;
    const bool hot = max_celsius >= kHighTemperatureAlertCelsius;

    if (freezing && hot)
    {
        std::snprintf(out, out_size,
                      "Weather temperature thresholds are active.\nLowest: %d C.\nHighest: %d C.",
                      min_celsius_rounded, max_celsius_rounded);
    }
    else if (freezing)
    {
        std::snprintf(out, out_size,
                      "Weather source reports freezing conditions.\nLowest cached value: %d C.",
                      min_celsius_rounded);
    }
    else
    {
        std::snprintf(out, out_size,
                      "Weather source reports high temperature.\nHighest cached value: %d C.",
                      max_celsius_rounded);
    }
}

/// @brief Formats the operator-facing wind alert detail.
void build_weather_wind_alert_detail(float max_wind_mph, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const int max_wind_rounded = static_cast<int>(std::lround(max_wind_mph));
    std::snprintf(out, out_size,
                  "Weather source reports wind up to %d mph.\nSecure loose items and check local "
                  "warnings.",
                  max_wind_rounded);
}

/// @brief Formats pascals as tenths of a hectopascal for operator messages.
void build_pressure_tenths_hpa_text(uint32_t pressure_pa, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const uint32_t tenths_hpa = (pressure_pa + 5U) / 10U;
    std::snprintf(out, out_size, "%lu.%lu", static_cast<unsigned long>(tenths_hpa / 10U),
                  static_cast<unsigned long>(tenths_hpa % 10U));
}

/// @brief Detects local pressure conditions that can indicate storm risk.
/// @details BME280 pressure is local station pressure, so a rapid fall is more
/// portable than an absolute threshold. The low-pressure threshold remains as a
/// secondary signal for low-altitude installations.
bool local_pressure_storm_warning(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* detail,
    size_t detail_size)
{
    if (!status.enabled || !status.bme_reading_valid)
    {
        return false;
    }

    uint32_t pressure_fall_pa = 0U;
    bool rapid_fall = false;
    if (status.bme_history_count >= kStormPressureMinimumSamples)
    {
        const uint16_t comparison_index =
            status.bme_history_count - kStormPressureMinimumSamples;
        const uint32_t comparison_pressure =
            static_cast<uint32_t>(status.bme_pressure_history_deci_hpa[comparison_index]) * 10U;
        const uint32_t latest_pressure =
            static_cast<uint32_t>(
                status.bme_pressure_history_deci_hpa[status.bme_history_count - 1U]) *
            10U;
        if (comparison_pressure > latest_pressure)
        {
            pressure_fall_pa = comparison_pressure - latest_pressure;
            rapid_fall = pressure_fall_pa >= kStormRapidPressureFallPa;
        }
    }

    const bool low_pressure = status.bme_pressure_pa <= kStormLowPressurePa;
    if (!low_pressure && !rapid_fall)
    {
        return false;
    }

    char current_pressure_text[12] = {};
    char pressure_fall_text[12] = {};
    build_pressure_tenths_hpa_text(status.bme_pressure_pa, current_pressure_text,
                                   sizeof(current_pressure_text));
    build_pressure_tenths_hpa_text(pressure_fall_pa, pressure_fall_text,
                                   sizeof(pressure_fall_text));

    if (detail != nullptr && detail_size > 0U)
    {
        if (low_pressure && rapid_fall)
        {
            std::snprintf(detail, detail_size,
                          "Local pressure is %s hPa and has fallen %s hPa across recent "
                          "averages.\nCheck local forecast and conditions.",
                          current_pressure_text, pressure_fall_text);
        }
        else if (rapid_fall)
        {
            std::snprintf(detail, detail_size,
                          "Local pressure has fallen %s hPa across recent five-minute "
                          "averages.\nCheck local forecast and conditions.",
                          pressure_fall_text);
        }
        else
        {
            std::snprintf(detail, detail_size,
                          "Local pressure is low at %s hPa.\nCheck local forecast and conditions.",
                          current_pressure_text);
        }
    }

    return true;
}

} // namespace

void sync(ConsoleState& console_state)
{
    const bool wifi_connected = console_state.wifi_status.state == WifiConnectionState::Connected;
    set_alert_condition(console_state, AlertCode::WifiDisconnected, !wifi_connected,
                        AlertSeverity::Warning, "NETWORK",
                        "Wi-Fi link is down.\nCheck SSID/password or signal.\nWithout network, "
                        "cloud features are unavailable.");
    const bool wifi_auth_failed =
        console_state.wifi_status.state == WifiConnectionState::AuthFailed;
    set_alert_condition(
        console_state, AlertCode::WifiAuthFailed, wifi_auth_failed, AlertSeverity::Alert,
        "WIFI AUTH", "Wi-Fi authentication failed.\nVerify SSID and password in NETWORK settings.");

    if (!console_state.time_status.synced && wifi_connected)
    {
        if (g_time_unsynced_samples < 255U)
        {
            ++g_time_unsynced_samples;
        }
    }
    else
    {
        g_time_unsynced_samples = 0U;
    }
    set_alert_condition(
        console_state, AlertCode::TimeNotSynced, g_time_unsynced_samples >= kAlertRetryThreshold,
        AlertSeverity::Warning, "TIME",
        "Clock sync has failed after 5 attempts.\nCheck NTP reachability and timezone setup.");

    const bool ha_enabled = config_manager::settings().home_assistant_enabled;
    const HomeAssistantConnectionState ha_state = console_state.home_assistant_status.state;
    if (!ha_enabled || ha_state == HomeAssistantConnectionState::Connected ||
        ha_state == HomeAssistantConnectionState::Unauthorized)
    {
        g_home_assistant_connect_failures = 0U;
    }
    else
    {
        if (g_home_assistant_connect_failures < 255U)
        {
            ++g_home_assistant_connect_failures;
        }
    }

    const bool ha_unauthorised = ha_state == HomeAssistantConnectionState::Unauthorized;
    const bool ha_offline = ha_enabled && ha_state != HomeAssistantConnectionState::Connected &&
                            ha_state != HomeAssistantConnectionState::Unauthorized &&
                            g_home_assistant_connect_failures >= kAlertRetryThreshold;
    set_alert_condition(console_state, AlertCode::HomeAssistantOffline, ha_offline,
                        AlertSeverity::Message, "HOME ASSISTANT",
                        "Home Assistant is not connected.\nCheck host/port/token and network "
                        "routing.\nStatus page shows the latest connector state.");
    set_alert_condition(console_state, AlertCode::HomeAssistantUnauthorized, ha_unauthorised,
                        AlertSeverity::Alert, "AUTH FAILED",
                        "Home Assistant rejected the API token.\nUpdate the token in Settings > "
                        "Home Assistant.\nThis alert clears after a successful authorisation.");
    const bool tracked_entity_configured =
        config_manager::settings().home_assistant_entity_id[0] != '\0';
    const bool tracked_entity_missing =
        ha_enabled && tracked_entity_configured &&
        ha_state == HomeAssistantConnectionState::Connected &&
        (console_state.home_assistant_status.tracked_entity_state[0] == '\0' ||
         std::strcmp(console_state.home_assistant_status.tracked_entity_state.data(),
                     "unknown") == 0 ||
         std::strcmp(console_state.home_assistant_status.tracked_entity_state.data(),
                     "unavailable") == 0);
    set_alert_condition(console_state, AlertCode::HomeAssistantEntityMissing, tracked_entity_missing,
                        AlertSeverity::Warning, "HA ENTITY",
                        "Tracked Home Assistant entity is missing or unavailable.\nCheck the "
                        "entity id in settings and HA integration state.");

    const bool weather_source_active =
        console_state.weather_source == WeatherSource::OpenMeteo ||
        console_state.weather_source == WeatherSource::HomeAssistant;
    const bool weather_payload_present =
        console_state.home_assistant_status.weather_condition[0] != '\0' ||
        console_state.home_assistant_status.weather_temperature[0] != '\0' ||
        console_state.home_assistant_status.weather_forecast_count > 0U ||
        console_state.home_assistant_status.weather_daily_forecast_count > 0U;
    const bool weather_prerequisites_ok =
        (console_state.weather_source == WeatherSource::HomeAssistant)
            ? (ha_state == HomeAssistantConnectionState::Connected)
            : console_state.wifi_status.internet_reachable;
    const bool weather_http_failed = console_state.home_assistant_status.last_http_status >= 400;
    const bool weather_failed_now = !weather_payload_present || weather_http_failed;
    if (weather_source_active && weather_prerequisites_ok && weather_failed_now)
    {
        if (g_weather_refresh_failures < 255U)
        {
            ++g_weather_refresh_failures;
        }
    }
    else
    {
        g_weather_refresh_failures = 0U;
    }

    const bool weather_unavailable = weather_source_active && weather_prerequisites_ok &&
                                     g_weather_refresh_failures >= kAlertRetryThreshold;
    set_alert_condition(
        console_state, AlertCode::WeatherUnavailable, weather_unavailable, AlertSeverity::Message,
        "WEATHER", "Weather refresh failed after 5 retries.\nCheck source settings and network path.");

    const bool weather_alert_data_current =
        weather_source_active && weather_prerequisites_ok && weather_payload_present &&
        !weather_unavailable;
    const WeatherAlertStatus& weather_alert_status =
        console_state.home_assistant_status.weather_alert_status;
    const AlertSeverity provider_warning_severity =
        weather_alert_status.provider_warning_severity == AlertSeverity::None
            ? AlertSeverity::Warning
            : weather_alert_status.provider_warning_severity;
    const char* provider_warning_summary =
        weather_alert_status.provider_warning_summary[0] != '\0'
            ? weather_alert_status.provider_warning_summary.data()
            : "WX WARNING";
    const char* provider_warning_detail =
        weather_alert_status.provider_warning_detail[0] != '\0'
            ? weather_alert_status.provider_warning_detail.data()
            : "Weather source reports a warning.\nCheck the latest local forecast.";
    // Official provider-warning APIs are not wired yet. Current weather providers
    // can still raise this hook from severe condition telemetry such as thunder.
    set_alert_condition(console_state, AlertCode::WeatherProviderWarning,
                        weather_alert_data_current && weather_alert_status.provider_warning_active,
                        provider_warning_severity, provider_warning_summary,
                        provider_warning_detail);

    const WeatherMetrics& weather_metrics = console_state.home_assistant_status.weather_metrics;
    float min_temperature_celsius = 0.0F;
    float max_temperature_celsius = 0.0F;
    const bool have_temperature_range = weather_temperature_alert_range(
        weather_metrics, min_temperature_celsius, max_temperature_celsius);
    const bool temperature_warning =
        weather_alert_data_current && have_temperature_range &&
        (min_temperature_celsius <= kFreezingTemperatureAlertCelsius ||
         max_temperature_celsius >= kHighTemperatureAlertCelsius);
    // Reused for each alert's detail text below rather than one 320-byte
    // buffer per alert: every use is built then immediately consumed by
    // set_alert_condition (which copies it into ActiveAlert), so lifetimes
    // never overlap. This function runs twice per keypress (from
    // update_softkeys_from_state and update_lamps_from_state), so trimming
    // its stack footprint matters on a memory-constrained MCU with no
    // configured stack guard.
    char alert_detail_scratch[sizeof(ActiveAlert::detail)] = {};
    if (temperature_warning)
    {
        build_weather_temperature_alert_detail(min_temperature_celsius, max_temperature_celsius,
                                               alert_detail_scratch, sizeof(alert_detail_scratch));
    }
    set_alert_condition(console_state, AlertCode::WeatherTemperatureWarning, temperature_warning,
                        AlertSeverity::Warning, "WX TEMP",
                        temperature_warning ? alert_detail_scratch : "");

    float max_wind_mph = 0.0F;
    const bool have_wind_maximum = weather_wind_alert_maximum(weather_metrics, max_wind_mph);
    const bool wind_warning =
        weather_alert_data_current && have_wind_maximum && max_wind_mph >= kHighWindAlertMph;
    if (wind_warning)
    {
        build_weather_wind_alert_detail(max_wind_mph, alert_detail_scratch,
                                        sizeof(alert_detail_scratch));
    }
    set_alert_condition(console_state, AlertCode::WeatherWindWarning, wind_warning,
                        AlertSeverity::Warning, "WX WIND", wind_warning ? alert_detail_scratch : "");

    const bool mqtt_enabled = config_manager::settings().mqtt_enabled;
    const MqttConnectionState mqtt_state = console_state.mqtt_status.state;
    if (!mqtt_enabled || mqtt_state == MqttConnectionState::Connected)
    {
        g_mqtt_connect_failures = 0U;
    }
    else
    {
        if (g_mqtt_connect_failures < 255U)
        {
            ++g_mqtt_connect_failures;
        }
    }
    set_alert_condition(
        console_state, AlertCode::MqttOffline,
        mqtt_enabled && mqtt_state != MqttConnectionState::Connected &&
            g_mqtt_connect_failures >= kAlertRetryThreshold,
        AlertSeverity::Message, "MQTT",
        "MQTT discovery is offline after 5 retries.\nCheck broker host/port and credentials.");

    const bool keypad_fault_now =
        (std::strcmp(console_state.keypad_debug_status.pressed_key_name.data(), "MULTI") == 0) ||
        console_state.keypad_debug_status.active_count > 3U;
    if (keypad_fault_now)
    {
        if (g_keypad_fault_samples < 255U)
        {
            ++g_keypad_fault_samples;
        }
    }
    else
    {
        g_keypad_fault_samples = 0U;
    }
    set_alert_condition(console_state, AlertCode::KeypadLineFault,
                        g_keypad_fault_samples >= kAlertRetryThreshold, AlertSeverity::Warning,
                        "KEYPAD",
                        "Matrix line fault suspected.\nMultiple simultaneous/stuck lines were "
                        "detected repeatedly.");

    const environment_sensor_manager::EnvironmentSensorStatus& environment_status =
        console_state.environment_sensor_status;
    const bool sgp40_detected = std::any_of(
        environment_status.devices.begin(), environment_status.devices.end(),
        [](const environment_sensor_manager::EnvironmentSensorPresence& presence) {
            return presence.device == environment_sensor_manager::EnvironmentSensorDevice::Sgp40 &&
                   presence.detected;
        });
    const bool environment_sensor_fault =
        environment_status.enabled &&
        (environment_status.health == environment_sensor_manager::EnvironmentSensorHealth::Fault ||
         environment_status.health ==
             environment_sensor_manager::EnvironmentSensorHealth::BoardMissing ||
         environment_status.health == environment_sensor_manager::EnvironmentSensorHealth::Partial ||
         (environment_status.bme_variant == environment_sensor_manager::EnvironmentBmeVariant::Bme280 &&
          !environment_status.bme_reading_valid &&
          environment_status.bme_read_error != PICO_ERROR_NONE) ||
         (sgp40_detected && !environment_status.air_quality_raw_valid &&
          environment_status.air_quality_read_error != PICO_ERROR_NONE));
    set_alert_condition(
        console_state, AlertCode::EnvironmentSensorFault, environment_sensor_fault,
        AlertSeverity::Warning, "ENV SENSOR",
        "Environment sensor board is not fully detected.\nCheck I2C pins, power, and address "
        "jumpers.\nStatus page shows the detected addresses and read errors.");

    const bool pressure_storm_warning = local_pressure_storm_warning(
        environment_status, alert_detail_scratch, sizeof(alert_detail_scratch));
    set_alert_condition(console_state, AlertCode::LocalPressureStormWarning, pressure_storm_warning,
                        AlertSeverity::Warning, "STORM WARN",
                        pressure_storm_warning ? alert_detail_scratch : "");

    // Placeholder: enable this once render/frame timing counters are exposed to console state.
    const bool display_pipeline_lag = false;
    set_alert_condition(console_state, AlertCode::DisplayPipelineLag, display_pipeline_lag,
                        AlertSeverity::Message, "DISPLAY LAG",
                        "Display update pipeline is lagging.\nAdd frame timing telemetry to "
                        "activate this alert.");

    const bool share_data_unavailable = console_state.share_data_configured &&
                                        !console_state.share_data_valid &&
                                        (console_state.share_data_last_error != 0 ||
                                         console_state.share_data_last_http_status >= 400);
    set_alert_condition(console_state, AlertCode::ShareDataUnavailable, share_data_unavailable,
                        AlertSeverity::Message, "SHARES",
                        "Share price data is unavailable.\nCheck the market data provider and "
                        "watchlist configuration.");

    // The Pinter workflow now records typed queue entries and planned durations.
    // Friday target forecasts and future fridge/dock reservation windows are
    // still needed before this can raise a reliable schedule-conflict alert.
    set_alert_condition(console_state, AlertCode::PinterScheduleConflict, false,
                        AlertSeverity::Message, "PINTER", "");

    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        console_state.alert_detail_index =
            console_state.alert_count > 0U ? static_cast<uint8_t>(console_state.alert_count - 1U)
                                           : 0U;
    }
    const uint8_t pages = page_count(console_state);
    if (console_state.alert_list_page_index >= pages)
    {
        console_state.alert_list_page_index = static_cast<uint8_t>(pages - 1U);
    }
}

uint8_t page_count(const ConsoleState& console_state)
{
    constexpr uint8_t kAlertsPerPage = 9U;
    if (console_state.alert_count == 0U)
    {
        return 1U;
    }

    return static_cast<uint8_t>((console_state.alert_count + (kAlertsPerPage - 1U)) /
                                kAlertsPerPage);
}

void build_display_indices(const ConsoleState& console_state,
                           std::array<uint8_t, kActiveAlertCapacity>& out_indices,
                           uint8_t* out_count)
{
    const uint8_t count =
        std::min(console_state.alert_count, static_cast<uint8_t>(out_indices.size()));
    alert_ordering::sort_display_indices(console_state.active_alerts, count, out_indices);
    *out_count = count;
}

const char* build_softkey_label(SoftKeyId key, const ActiveAlert& alert)
{
    size_t buffer_capacity = 0U;
    char* buffer = cci::dynamic_softkey_label_buffer(key, buffer_capacity);
    const char* time_text =
        (alert.occurred_time_text[0] != '\0') ? alert.occurred_time_text.data() : "--:--";
    std::snprintf(buffer, buffer_capacity, "%s\n[%s]", alert.summary.data(), time_text);
    return buffer;
}

alert_ordering::AnnunciationSummary annunciation_summary(const ConsoleState& console_state)
{
    return alert_ordering::summarize(console_state.active_alerts, console_state.alert_count,
                                     g_alert_acknowledged_sequence);
}

bool open_list_page(ConsoleState& console_state)
{
    sync(console_state);
    const alert_ordering::AnnunciationSummary annunciation = annunciation_summary(console_state);
    g_alert_acknowledged_sequence = annunciation.newest_sequence;
    if (console_state.active_page != MenuPage::AlertList &&
        console_state.active_page != MenuPage::AlertDetail)
    {
        console_state.alert_parent_page = console_state.active_page;
    }
    console_state.active_page = MenuPage::AlertList;
    return true;
}

bool open_detail_from_slot(ConsoleState& console_state, uint8_t page_slot)
{
    std::array<uint8_t, kActiveAlertCapacity> alert_indices = {};
    uint8_t sorted_count = 0U;
    build_display_indices(console_state, alert_indices, &sorted_count);
    constexpr uint8_t kAlertsPerPage = 9U;
    const uint8_t absolute =
        static_cast<uint8_t>((console_state.alert_list_page_index * kAlertsPerPage) + page_slot);
    if (absolute >= sorted_count)
    {
        return false;
    }
    console_state.alert_detail_index = alert_indices[absolute];
    console_state.alert_detail_scroll_line = 0U;
    console_state.active_page = MenuPage::AlertDetail;
    return true;
}

void erase_active_alert(ConsoleState& console_state, uint8_t index)
{
    if (index >= console_state.alert_count)
    {
        return;
    }

    for (uint8_t i = index; i + 1U < console_state.alert_count; ++i)
    {
        console_state.active_alerts[i] = console_state.active_alerts[i + 1U];
    }
    if (console_state.alert_count > 0U)
    {
        --console_state.alert_count;
    }
}

void suppress_alert_code(uint8_t alert_code_index)
{
    if (alert_code_index >= g_alert_suppressed.size())
    {
        return;
    }
    g_alert_suppressed[alert_code_index] = true;
}

void reset()
{
    g_alert_sequence_counter = 1U;
    g_alert_acknowledged_sequence = 0U;
    g_home_assistant_connect_failures = 0U;
    g_weather_refresh_failures = 0U;
    g_mqtt_connect_failures = 0U;
    g_time_unsynced_samples = 0U;
    g_keypad_fault_samples = 0U;
    g_alert_suppressed.fill(false);
    g_alert_was_active.fill(false);
}

} // namespace alert_controller
