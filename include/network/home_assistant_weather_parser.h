#pragma once

#include <cstddef>

#include "console_model.h"

/// @brief Pure Home Assistant entity/weather parsing (issue #47).
/// @details Extracted from home_assistant_manager.cpp -- both from its own
/// top-level helper functions and, for parse_current_weather_entity(), from
/// logic that used to be inlined directly inside the manager's HTTP
/// completion callback (`handle_http_status()`'s `FetchWeatherEntity`
/// branch). Operates on an explicit `HomeAssistantStatus&` rather than a
/// manager's own global status, so this is host-testable (see tests/host/).
/// No lwIP/Pico SDK dependency.
///
/// See open_meteo_parser.h for the equivalent direct-weather-provider path;
/// the two providers are intentionally kept as separate parsers (issue #47's
/// own scoping) since their payload shapes and unit-handling only partially
/// overlap.
namespace home_assistant_weather
{

/// @brief Updates the user-facing weather source hint from a weather entity
/// payload (prefers `attribution`, falls back to `friendly_name`).
void update_weather_source_hint_from_json(const char* json, HomeAssistantStatus& status);

/// @brief Updates sunrise and sunset display strings from the sun entity
/// payload.
void update_sun_times_from_json(const char* json, HomeAssistantStatus& status);

/// @brief Parses a Home Assistant weather-entity `state`/`temperature`/
/// `wind_speed` payload into `status`, replacing the current-weather fields
/// and their derived `WeatherMetrics`/`WeatherAlertStatus` state.
/// @details `temperature_unit` and `wind_source_unit` are the provider unit
/// markers this parse discovers (from the entity's `temperature_unit`/
/// `wind_speed_unit` attributes); callers keep these across calls so later
/// forecast requests in the same polling sequence can reuse them (see
/// weather_forecast_parser.h). Returns true when the entity's `state` field
/// (the weather condition) was present -- callers use this to know whether
/// to stamp a fresh success timestamp, deliberately left to the caller since
/// that timestamp comes from the hardware clock and this module stays
/// host-testable.
bool parse_current_weather_entity(const char* json, HomeAssistantStatus& status,
                                  char& temperature_unit, char* wind_source_unit,
                                  size_t wind_source_unit_size);

} // namespace home_assistant_weather
