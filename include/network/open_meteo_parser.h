#pragma once

#include <cstddef>

#include "console_model.h"

/// @brief Pure Open-Meteo weather-provider parsing (issue #47).
/// @details Extracted from home_assistant_manager.cpp's
/// `parse_open_meteo_weather()`. Parses a combined current+hourly+daily
/// Open-Meteo API response into `HomeAssistantStatus` (the direct-weather
/// path reuses the same UI-facing status struct as the Home Assistant path
/// -- see console_model.h). Operates on an explicit `HomeAssistantStatus&`
/// rather than a manager's own global status, so this is host-testable (see
/// tests/host/). No lwIP/Pico SDK dependency.
///
/// See home_assistant_weather_parser.h for the equivalent Home Assistant
/// entity path; the two providers are intentionally kept as separate parsers
/// (issue #47's own scoping) since their payload shapes and unit-handling
/// only partially overlap.
namespace open_meteo
{

/// @brief Parses an Open-Meteo `current`/`hourly`/`daily` weather response
/// into `status`, replacing the current, hourly-forecast, daily-forecast,
/// and sunrise/sunset fields plus their derived `WeatherMetrics`/
/// `WeatherAlertStatus` state. Always normalises to Celsius/mph (Open-Meteo
/// is requested in those units), so `temperature_unit` is set to 'C' and
/// `wind_source_unit` to "mph" on success. Returns true if at least one
/// hourly forecast row was parsed.
bool parse_weather(const char* json, HomeAssistantStatus& status, char& temperature_unit,
                   char* wind_source_unit, size_t wind_source_unit_size);

} // namespace open_meteo
