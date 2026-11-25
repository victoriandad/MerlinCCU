#include "alert_controller.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "alert_history.h"
#include "alert_ordering.h"
#include "config_manager.h"
#include "console_controller_internal.h"
#include "softkey_builders.h"
#include "weather_extremes.h"

namespace alert_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

// pico/error.h's PICO_ERROR_NONE is always 0; kept as a local constant
// (rather than including the Pico-SDK header) so this file stays
// host-testable -- see issue #78.
constexpr int kNoErrorCode = 0;

uint32_t g_alert_sequence_counter = 1U;
uint32_t g_alert_acknowledged_sequence = 0U;
uint8_t g_home_assistant_connect_failures = 0U;
uint8_t g_weather_refresh_failures = 0U;
uint8_t g_mqtt_connect_failures = 0U;
uint8_t g_time_unsynced_samples = 0U;
uint8_t g_keypad_fault_samples = 0U;
uint8_t g_display_slow_frame_samples = 0U;
uint32_t g_display_timing_last_sequence = 0U;
constexpr uint8_t kAlertRetryThreshold = 5U;
/// Scanout is counted as lagging when the measured frame-boundary rate falls
/// below this share of the rate the clocks should give. The bench rate tracks
/// the nominal one exactly (297 Hz on a Pico 2 W, issue #144), and a flash
/// erase that holds off interrupts costs only a few percent of one sample, so
/// a quarter lost is a real stall rather than noise.
constexpr uint32_t kDisplayFrameRateFloorPercent = 75U;
constexpr uint32_t kPercentScale = 100U;
/// The core-0 stack is flagged as low once its worst-case free space since
/// boot drops under this. The deepest chain measured on the bench leaves
/// several KB of the 8 KB stack (#164); 1 KB is room for one more large
/// frame, such as a printf with floats or an lwIP callback, on top of it.
constexpr uint32_t kStackLowFreeBytes = 1024U;
/// The heap is flagged as low once its untouched space has dropped under
/// this at some point since boot (#206). A fixed floor rather than a share,
/// since a new connection needs a fixed amount: an mbedTLS session's
/// buffers and an lwIP connection come to a few KB. Bench: about 9.6 KB of
/// arena with Home Assistant, MQTT and the web server connected (Plus 2 W).
constexpr uint32_t kHeapLowFreeBytes = 8U * 1024U;
constexpr uint32_t kStormLowPressurePa = 98000U;
constexpr uint32_t kStormRapidPressureFallPa = 200U;
constexpr uint8_t kStormPressureMinimumSamples = 6U;

static_assert(kActiveAlertCapacity == kAlertCodeCount,
              "the active alert list must hold one entry per alert code (NAV-6a)");
static_assert(kAlertCodeCount <= UINT8_MAX, "ConsoleState::alert_count is a uint8_t");

std::array<bool, kAlertCodeCount> g_alert_suppressed = {};
std::array<bool, kAlertCodeCount> g_alert_was_active = {};
/// Clear-to-active transitions per code since the last ACCEPT or reset. Kept
/// here rather than on the list entry, which is erased whenever the alert
/// clears, so a recurring alert's count keeps rising.
std::array<uint8_t, kAlertCodeCount> g_alert_occurrences = {};

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
            // It has ended on its own; history keeps it (#203).
            alert_history::record(console_state.active_alerts[static_cast<size_t>(existing)],
                                  alert_history::Ending::Cleared,
                                  alert_history::local_minute(console_state.time_status));
            erase_active_alert(console_state, static_cast<uint8_t>(existing));
        }
        return;
    }

    if (!g_alert_was_active[code_idx])
    {
        g_alert_occurrences[code_idx] = static_cast<uint8_t>(
            std::min<unsigned>(g_alert_occurrences[code_idx] + 1U, kMaxAlertOccurrenceCount));
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
        alert.occurrence_count = g_alert_occurrences[code_idx];
        // Existing active alerts keep their original arrival sequence/time so
        // periodic state re-evaluation does not retrigger annunciation.
        return;
    }

    // The list holds one entry per code (see the static_asserts above), so it
    // cannot be full here; this only guards the array bound.
    if (console_state.alert_count >= console_state.active_alerts.size())
    {
        return;
    }

    ActiveAlert& alert = console_state.active_alerts[console_state.alert_count++];
    alert.severity = severity;
    alert.code = static_cast<uint8_t>(code);
    alert.sequence = g_alert_sequence_counter++;
    alert.occurrence_count = g_alert_occurrences[code_idx];
    alert.ignored = false;
    alert.raised_minute = alert_history::local_minute(console_state.time_status);
    alert.ignored_minute = 0U;
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

/// @brief The highest current or forecast mean wind used by weather alerts.
std::optional<float> weather_wind_alert_maximum(const WeatherMetrics& metrics)
{
    std::optional<float> max_wind_mph;
    if (metrics.current_wind_speed_mph_valid)
    {
        max_wind_mph = metrics.current_wind_speed_mph;
    }
    if (metrics.forecast_max_wind_speed_mph_valid &&
        (!max_wind_mph.has_value() || metrics.forecast_max_wind_speed_mph > *max_wind_mph))
    {
        max_wind_mph = metrics.forecast_max_wind_speed_mph;
    }
    return max_wind_mph;
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
    const bool freezing = weather_extremes::is_cold(min_celsius);
    const bool hot = weather_extremes::is_hot(max_celsius);

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

/// @brief Formats the operator-facing wind alert detail, with the gusts when
/// the source gives them.
void build_weather_wind_alert_detail(std::optional<float> max_wind_mph,
                                     std::optional<float> max_gust_mph, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const int kWind = static_cast<int>(std::lround(max_wind_mph.value_or(0.0F)));
    const int kGust = static_cast<int>(std::lround(max_gust_mph.value_or(0.0F)));
    constexpr const char* kAdvice = "Secure loose items and check local warnings.";
    if (max_wind_mph.has_value() && max_gust_mph.has_value())
    {
        std::snprintf(out, out_size,
                      "Weather source reports wind up to %d mph,\ngusts to %d mph.\n%s", kWind,
                      kGust, kAdvice);
    }
    else if (max_gust_mph.has_value())
    {
        std::snprintf(out, out_size, "Weather source reports gusts to %d mph.\n%s", kGust, kAdvice);
    }
    else
    {
        std::snprintf(out, out_size, "Weather source reports wind up to %d mph.\n%s", kWind,
                      kAdvice);
    }
}

/// @brief The alert severity of an official warning level (#202): Yellow is
/// a message, Amber a warning, and Red an alert, so the ALRT lamp treats a
/// Red warning like any other alert.
AlertSeverity warning_level_severity(WarningLevel level)
{
    switch (level)
    {
    case WarningLevel::Red:
        return AlertSeverity::Alert;
    case WarningLevel::Amber:
        return AlertSeverity::Warning;
    case WarningLevel::Yellow:
    case WarningLevel::None:
        break;
    }
    return AlertSeverity::Message;
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

/// @brief Debounces the display scanout rate once per new timing sample.
/// @details `sync()` runs on every console state change, so counting calls
/// would make the debounce depend on keypresses. The sample sequence moves once
/// per one-second window, so five slow samples mean five slow seconds.
bool display_pipeline_lagging(const DisplayTimingStatus& timing)
{
    if (timing.sample_sequence != g_display_timing_last_sequence)
    {
        g_display_timing_last_sequence = timing.sample_sequence;
        const bool slow_now =
            timing.valid && timing.nominal_frame_rate_hz > 0U &&
            static_cast<uint32_t>(timing.frame_rate_hz) * kPercentScale <
                static_cast<uint32_t>(timing.nominal_frame_rate_hz) * kDisplayFrameRateFloorPercent;
        if (!slow_now)
        {
            g_display_slow_frame_samples = 0U;
        }
        else if (g_display_slow_frame_samples < UINT8_MAX)
        {
            ++g_display_slow_frame_samples;
        }
    }
    return g_display_slow_frame_samples >= kAlertRetryThreshold;
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
                        "Home Assistant is not connected.\nCheck host/port/token, the https:// "
                        "certificate pin and network routing.\nStatus page shows the "
                        "latest connector state.");
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
    // HA's weather arrives over MQTT (#153), so its prerequisite is the MQTT
    // link (weather_state follows it), not HA's REST connection; a lost link
    // is MQTT's problem, not a weather one. Open-Meteo needs the internet
    // and is the only source with an HTTP status of its own.
    const bool ha_weather = console_state.weather_source == WeatherSource::HomeAssistant;
    const bool weather_prerequisites_ok =
        ha_weather ? (console_state.home_assistant_status.weather_state ==
                      HomeAssistantConnectionState::Connected)
                   : console_state.wifi_status.internet_reachable;
    const bool weather_http_failed =
        !ha_weather && console_state.home_assistant_status.weather_http_status >= 400;
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
    // Severe conditions in the forecast, such as thunder; official warnings
    // are OfficialWeatherWarning below.
    set_alert_condition(console_state, AlertCode::WeatherProviderWarning,
                        weather_alert_data_current && weather_alert_status.provider_warning_active,
                        provider_warning_severity, provider_warning_summary,
                        provider_warning_detail);

    const WeatherMetrics& weather_metrics = console_state.home_assistant_status.weather_metrics;
    float min_temperature_celsius = 0.0F;
    float max_temperature_celsius = 0.0F;
    const bool have_temperature_range = weather_temperature_alert_range(
        weather_metrics, min_temperature_celsius, max_temperature_celsius);
    const bool temperature_warning = weather_alert_data_current && have_temperature_range &&
                                     (weather_extremes::is_cold(min_temperature_celsius) ||
                                      weather_extremes::is_hot(max_temperature_celsius));
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

    const std::optional<float> max_wind_mph = weather_wind_alert_maximum(weather_metrics);
    const std::optional<float> max_gust_mph =
        weather_metrics.forecast_max_wind_gust_mph_valid
            ? std::optional<float>(weather_metrics.forecast_max_wind_gust_mph)
            : std::nullopt;
    const bool wind_warning =
        weather_alert_data_current && weather_extremes::is_high_wind(max_wind_mph, max_gust_mph);
    if (wind_warning)
    {
        build_weather_wind_alert_detail(max_wind_mph, max_gust_mph, alert_detail_scratch,
                                        sizeof(alert_detail_scratch));
    }
    set_alert_condition(console_state, AlertCode::WeatherWindWarning, wind_warning,
                        AlertSeverity::Warning, "WX WIND", wind_warning ? alert_detail_scratch : "");

    // Official warnings (#202) arrive from HA whatever the weather source.
    // Stale ones stay up, marked, rather than vanish: a warning HA could not
    // refresh is more likely still in force than over.
    const WeatherWarnings& warnings = console_state.weather_warnings;
    const bool official_warning = warnings.level != WarningLevel::None;
    if (official_warning)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch), "%s%s",
                      warnings.stale ? "Not updated for over 2 hours.\n" : "",
                      warnings.detail.data());
    }
    set_alert_condition(console_state, AlertCode::OfficialWeatherWarning, official_warning,
                        warning_level_severity(warnings.level), warnings.summary.data(),
                        official_warning ? alert_detail_scratch : "");

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
          environment_status.bme_read_error != kNoErrorCode) ||
         (sgp40_detected && !environment_status.air_quality_raw_valid &&
          environment_status.air_quality_read_error != kNoErrorCode));
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

    const DisplayTimingStatus& display_timing = console_state.display_timing_status;
    const bool display_pipeline_lag = display_pipeline_lagging(display_timing);
    if (display_pipeline_lag)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "Display scanout is running at %u Hz, expected %u Hz.\nScreen updates may "
                      "be delayed and the panel may flicker.\nRestart the CCU if it persists.",
                      static_cast<unsigned>(display_timing.frame_rate_hz),
                      static_cast<unsigned>(display_timing.nominal_frame_rate_hz));
    }
    set_alert_condition(console_state, AlertCode::DisplayPipelineLag, display_pipeline_lag,
                        AlertSeverity::Warning, "DISPLAY LAG",
                        display_pipeline_lag ? alert_detail_scratch : "");

    const StackStatus& stack = console_state.stack_status;
    const bool stack_low =
        stack.valid && (stack.overflow_reset || stack.free_bytes < kStackLowFreeBytes);
    if (stack.overflow_reset)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "The CCU restarted after its stack overflowed.\nThe last action before the "
                      "restart may need repeating.\nReport it with what was being done at the "
                      "time.");
    }
    else if (stack_low)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "Only %lu bytes of stack have stayed free since boot.\nA deeper call could "
                      "restart the CCU.\nReport it with what was being done at the time.",
                      static_cast<unsigned long>(stack.free_bytes));
    }
    set_alert_condition(console_state, AlertCode::StackLow, stack_low, AlertSeverity::Warning,
                        "STACK LOW", stack_low ? alert_detail_scratch : "");

    // The peak since boot only grows, so once raised this holds for the
    // boot, and an accepted one stays accepted: a brief peak overnight is
    // still seen in the morning.
    const HeapStatus& heap = console_state.heap_status;
    const ImageFootprintStatus& footprint = console_state.image_footprint_status;
    const uint32_t heap_bytes = footprint.total_ram_bytes > footprint.static_ram_bytes
                                    ? footprint.total_ram_bytes - footprint.static_ram_bytes
                                    : 0U;
    const uint32_t heap_lowest_free =
        heap_bytes > heap.peak_arena_bytes ? heap_bytes - heap.peak_arena_bytes : 0U;
    const bool heap_low =
        heap.valid && footprint.valid && heap_bytes > 0U && heap_lowest_free < kHeapLowFreeBytes;
    if (heap_low)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "At its peak since boot the heap had only %lu of %lu bytes left.\nA new "
                      "connection, such as to Home Assistant, may fail.\nRestart the CCU, then "
                      "report it with what was running.",
                      static_cast<unsigned long>(heap_lowest_free),
                      static_cast<unsigned long>(heap_bytes));
    }
    set_alert_condition(console_state, AlertCode::HeapLow, heap_low, AlertSeverity::Warning,
                        "HEAP LOW", heap_low ? alert_detail_scratch : "");

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

    // A message pushed by Home Assistant (#150). Accepting it on the panel
    // suppresses it until HA sends a new one; MQTT reports that back.
    const ConsoleState::HaMessage& ha_message = console_state.ha_message;
    set_alert_condition(console_state, AlertCode::HomeAssistantMessage, ha_message.active,
                        ha_message.severity == AlertSeverity::None ? AlertSeverity::Message
                                                                   : ha_message.severity,
                        ha_message.title[0] != '\0' ? ha_message.title.data() : "HA MESSAGE",
                        ha_message.text.data());

    // Holds for the whole boot: the unit is back on its previous firmware
    // until the next successful update. A power cut during the trial clears
    // the record (it lives in a watchdog scratch register), so that case
    // goes unreported.
    const FirmwareStatus& firmware = console_state.firmware_status;
    set_alert_condition(console_state, AlertCode::FirmwareUpdateRolledBack,
                        firmware.valid && firmware.rolled_back, AlertSeverity::Warning,
                        "UPDATE FAILED",
                        "The new firmware did not confirm itself in time.\nThe CCU went back to "
                        "its previous firmware.\nCheck the serial log, then upload again.");

    // A restart nobody asked for (#155): the watchdog caught a hang, the CPU
    // faulted, or the supply dipped. It stays until accepted, so one seen
    // overnight is not missed. A stack overflow is STACK LOW's, and power
    // cuts, the reset pin, updates and asked-for restarts are expected.
    const ResetReason reset = console_state.boot_status.reset_reason;
    const bool unexpected_restart =
        reset_reason::unexpected(reset) && reset != ResetReason::StackOverflow;
    if (unexpected_restart)
    {
        const char* cause = reset == ResetReason::Watchdog
                                ? "The watchdog restarted it after the firmware stopped "
                                  "responding."
                            : reset == ResetReason::CpuFault
                                ? "The processor hit a fault and restarted."
                                : "Its supply voltage dipped too low (a brown-out).";
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "The CCU restarted on its own.\n%s\n%s", cause,
                      reset == ResetReason::Brownout
                          ? "Check the power supply and its cable."
                          : "Report it with what was being done at the time.");
    }
    set_alert_condition(console_state, AlertCode::UnexpectedRestart, unexpected_restart,
                        AlertSeverity::Warning, "RESTARTED",
                        unexpected_restart ? alert_detail_scratch : "");

    // A Controls key press that did not work (#152) holds until a press
    // works, so a light that did not switch is not mistaken for one that
    // did once the page's short notice has gone.
    const HomeControls& controls = console_state.home_controls;
    const bool control_failed = controls.last_failure != ControlFailure::None;
    if (control_failed)
    {
        std::snprintf(alert_detail_scratch, sizeof(alert_detail_scratch),
                      "%s did not switch.\n%s\nTry again; this clears when a Controls key "
                      "works.",
                      controls.last_failure_name[0] != '\0' ? controls.last_failure_name.data()
                                                            : "A control",
                      controls.last_failure == ControlFailure::Refused
                          ? "Home Assistant refused it; its log says why."
                          : "Home Assistant did not answer within 5 seconds.");
    }
    set_alert_condition(console_state, AlertCode::ControlFailed, control_failed,
                        AlertSeverity::Warning, "CONTROL FAILED",
                        control_failed ? alert_detail_scratch : "");

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
    clamp_history_page(console_state);
}

uint8_t page_count(const ConsoleState& console_state)
{
    return alert_list_page_count(console_state.alert_count);
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
    if (alert.occurrence_count > 1U)
    {
        std::snprintf(buffer, buffer_capacity, "%s\n[%s x%u]", alert.summary.data(), time_text,
                      static_cast<unsigned>(alert.occurrence_count));
    }
    else
    {
        std::snprintf(buffer, buffer_capacity, "%s\n[%s]", alert.summary.data(), time_text);
    }
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

void accept_alert(ConsoleState& console_state, uint8_t index)
{
    if (index >= console_state.alert_count)
    {
        return;
    }
    const uint8_t code = console_state.active_alerts[index].code;
    alert_history::record(console_state.active_alerts[index], alert_history::Ending::Accepted,
                          alert_history::local_minute(console_state.time_status));
    suppress_alert_code(code);
    if (code < g_alert_occurrences.size())
    {
        g_alert_occurrences[code] = 0U;
    }
    erase_active_alert(console_state, index);
}

void ignore_alert(ConsoleState& console_state, uint8_t index)
{
    if (index >= console_state.alert_count)
    {
        return;
    }
    ActiveAlert& alert = console_state.active_alerts[index];
    alert.ignored = true;
    alert.ignored_minute = alert_history::local_minute(console_state.time_status);
}

bool acknowledged(AlertCode code)
{
    const size_t index = alert_code_index(code);
    return g_alert_was_active[index] && g_alert_suppressed[index];
}

void rearm(AlertCode code)
{
    const size_t index = alert_code_index(code);
    g_alert_suppressed[index] = false;
    g_alert_was_active[index] = false;
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
    g_display_slow_frame_samples = 0U;
    g_display_timing_last_sequence = 0U;
    g_alert_suppressed.fill(false);
    g_alert_was_active.fill(false);
    g_alert_occurrences.fill(0U);
}

void build_list_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    std::array<uint8_t, kActiveAlertCapacity> alert_indices = {};
    uint8_t sorted_count = 0U;
    build_display_indices(console_state, alert_indices, &sorted_count);
    const uint8_t page_start =
        static_cast<uint8_t>(console_state.alert_list_page_index * kAlertsPerPage);
    static_assert(softkey::kListSlots.size() == kAlertsPerPage, "one alert per list slot");
    for (uint8_t i = 0U; i < softkey::kListSlots.size(); ++i)
    {
        const uint8_t index = static_cast<uint8_t>(page_start + i);
        if (index >= sorted_count)
        {
            continue;
        }
        const SoftKeyId slot = softkey::kListSlots[i];
        const ActiveAlert& alert = console_state.active_alerts[alert_indices[index]];
        softkey::set(
            softkeys, slot,
            softkey::item(build_softkey_label(slot, alert), SoftKeyRoute::SelectAlertSlot, i));
    }
    // Past alerts are a page of their own rather than a view of this one, so
    // History's L5 is free for CLEAR ALL (#203).
    softkey::set(softkeys, SoftKeyId::Left5, softkey::navigate("HISTORY", MenuPage::AlertHistory));
}

void build_detail_softkeys(ConsoleState& /*console_state*/, SoftKeyMap& softkeys)
{
    softkey::set(softkeys, SoftKeyId::Left5, softkey::action("IGNORE", SoftKeyRoute::AlertIgnore));
    softkey::set(softkeys, SoftKeyId::Right5, softkey::action("ACCEPT", SoftKeyRoute::AlertAccept));
}

void enter_history(ConsoleState& console_state)
{
    console_state.alert_history_page_index = 0U;
    console_state.alert_history_clear_confirming = false;
}

void build_history_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    if (console_state.alert_history_clear_confirming)
    {
        // Decision state: only decline (L5) and confirm (R5) (NAV-3).
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::action("CANCEL", SoftKeyRoute::CancelClearAlertHistory));
        softkey::set(softkeys, SoftKeyId::Right5,
                     softkey::action("CLEAR ALL", SoftKeyRoute::ConfirmClearAlertHistory));
        return;
    }

    const uint8_t count = alert_history::count();
    const uint32_t today = alert_history::local_minute(console_state.time_status);
    const uint8_t page_start =
        static_cast<uint8_t>(console_state.alert_history_page_index * kAlertsPerPage);
    for (uint8_t i = 0U; i < softkey::kListSlots.size(); ++i)
    {
        const uint8_t index = static_cast<uint8_t>(page_start + i);
        if (index >= count)
        {
            break;
        }
        const SoftKeyId slot = softkey::kListSlots[i];
        const alert_history::Entry& entry = alert_history::entry(index);
        char value[24] = {};
        alert_history::format_key_value(entry, today, value, sizeof(value));
        size_t capacity = 0U;
        char* label = cci::dynamic_softkey_label_buffer(slot, capacity);
        std::snprintf(label, capacity, "%s\n%s", entry.summary.data(), value);
        softkey::set(softkeys, slot, softkey::item(label, SoftKeyRoute::SelectAlertHistorySlot, i));
    }
    if (count > 0U)
    {
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::action("CLEAR ALL", SoftKeyRoute::RequestClearAlertHistory));
    }
}

bool open_history_detail_from_slot(ConsoleState& console_state, uint8_t page_slot)
{
    const uint8_t index =
        static_cast<uint8_t>((console_state.alert_history_page_index * kAlertsPerPage) + page_slot);
    if (index >= alert_history::count())
    {
        return false;
    }
    console_state.alert_history_detail_sequence = alert_history::entry(index).sequence;
    console_state.alert_history_scroll_line = 0U;
    console_state.active_page = MenuPage::AlertHistoryDetail;
    return true;
}

void clamp_history_page(ConsoleState& console_state)
{
    const uint8_t pages = alert_history::page_count();
    if (console_state.alert_history_page_index >= pages)
    {
        console_state.alert_history_page_index = static_cast<uint8_t>(pages - 1U);
    }
}

} // namespace alert_controller
