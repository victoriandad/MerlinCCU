#pragma once

#include <cstddef>

#include "console_model.h"

/// @brief Pure forecast/metric aggregation shared by the Home Assistant and
/// Open-Meteo weather parsers (issue #47).
/// @details Extracted from home_assistant_manager.cpp. Owns the parts of
/// `HomeAssistantStatus` used for the UI display forecast rows and the
/// provider-neutral `WeatherMetrics`/`WeatherAlertStatus` alert side-channel
/// (see console_model.h). Operates on an explicit `HomeAssistantStatus&`
/// rather than a manager's own global status, so this is host-testable (see
/// tests/host/). No lwIP/Pico SDK dependency.
namespace weather_forecast
{

/// @brief Clears typed current-weather values used by threshold alerts.
void clear_current_weather_metrics(HomeAssistantStatus& status);

/// @brief Clears typed forecast values used by threshold alerts.
void clear_forecast_weather_metrics(HomeAssistantStatus& status);

/// @brief Clears provider-originated weather warning state.
void clear_weather_alert_status(HomeAssistantStatus& status);

/// @brief Adds one temperature reading to the cached forecast alert range.
void include_forecast_temperature_celsius(HomeAssistantStatus& status,
                                          float temperature_celsius);

/// @brief Adds one wind-speed reading to the cached forecast alert maximum.
void include_forecast_wind_speed_mph(HomeAssistantStatus& status, float wind_speed_mph);

/// @brief Records one severe provider condition as a weather warning for the
/// ALRT page, if `condition_text` is severe enough (see
/// weather_normalisation::weather_condition_is_warning()).
void record_weather_condition_warning(HomeAssistantStatus& status, const char* condition_text,
                                      const char* source_context);

/// @brief Clears the cached hourly weather forecast rows (and their derived
/// alert-range metrics).
void clear_weather_forecast(HomeAssistantStatus& status);

/// @brief Clears the cached daily weather forecast rows used by week mode.
void clear_weather_daily_forecast(HomeAssistantStatus& status);

/// @brief Parses a Home Assistant weather-entity `forecast` attribute array
/// (hourly mode) into `status.weather_forecast`.
/// @details `temperature_unit` and `wind_source_unit` are the provider units
/// currently in effect (see home_assistant_weather_parser.h), used to
/// populate `status.weather_metrics`' alert-range values alongside the
/// display text. Returns true if at least one forecast row was parsed.
bool parse_hourly_forecast_response(const char* json, HomeAssistantStatus& status,
                                    char temperature_unit, const char* wind_source_unit);

/// @brief Parses a Home Assistant weather-entity `forecast` attribute array
/// (daily mode) into `status.weather_daily_forecast`. Returns true if at
/// least one forecast row was parsed.
bool parse_daily_forecast_response(const char* json, HomeAssistantStatus& status,
                                   char temperature_unit, const char* wind_source_unit);

} // namespace weather_forecast
