#pragma once

#include <cstddef>

/// @brief Pure weather-provider text/unit normalisation shared by the Home
/// Assistant and Open-Meteo weather parsers (issue #47).
/// @details Extracted from home_assistant_manager.cpp. Covers provider
/// attribution text, unit conversion (temperature/wind speed/wind direction),
/// and condition-code-to-label mapping for both providers. No lwIP/Pico SDK
/// dependency, so this is host-testable (see tests/host/).
namespace weather_normalisation
{

/// @brief Trims leading/trailing whitespace and trailing periods in place.
void trim_text_in_place(char* text);

/// @brief Normalizes weather attribution text into a compact provider name.
bool normalize_weather_provider_name(const char* raw_text, char* out, size_t out_size);

/// @brief Normalizes a weather-provider wind-speed unit label (e.g. "km/h",
/// "m/s", "mph", "ft/s", "Bft", "kn") to the internal short form used for
/// convert_wind_speed_to_mph_value()'s `source_unit` parameter.
bool normalize_wind_speed_unit(const char* unit_text, char* out, size_t out_size);

/// @brief Formats a scalar text value into a compact display-oriented string
/// (rounds numeric values to the nearest integer; passes non-numeric text
/// through unchanged).
bool format_compact_scalar_value(const char* scalar_text, char* out, size_t out_size);

/// @brief Parses one provider numeric scalar into a float.
bool parse_float_value(const char* value_text, float* out);

/// @brief Converts a provider temperature reading into Celsius for alert
/// thresholds. `source_unit` is 'C'/'c' or 'F'/'f'; any other value fails.
bool convert_temperature_to_celsius(const char* temperature_text, char source_unit, float* out);

/// @brief Converts a provider wind-speed reading into mph for alert
/// thresholds. `source_unit` is one of the short forms produced by
/// normalize_wind_speed_unit() ("km/h", "m/s", "ft/s", "kn", "Bft"), or
/// nullptr/empty to treat the value as already in mph.
bool convert_wind_speed_to_mph_value(const char* speed_text, const char* source_unit, float* out);

/// @brief Converts a provider wind-speed reading into mph text.
bool convert_wind_speed_to_mph(const char* speed_text, const char* source_unit, char* out,
                               size_t out_size);

/// @brief Formats a wind-bearing value into 16-point compass text, or
/// upper-cased pass-through text when the bearing isn't numeric.
bool format_wind_direction_text(const char* bearing_text, char* out, size_t out_size);

/// @brief Combines wind speed and direction into one compact forecast
/// string, e.g. "12 NE". `wind_source_unit` is the short unit form used to
/// convert `speed_text` to mph (see convert_wind_speed_to_mph()).
bool format_compact_wind_text(const char* speed_text, const char* bearing_text,
                              const char* wind_source_unit, char* out, size_t out_size);

/// @brief Returns whether a provider condition is severe enough to surface
/// as a warning (lightning, thunder, hail, tornado, hurricane).
bool weather_condition_is_warning(const char* condition_text);

/// @brief Maps raw Home Assistant weather condition codes to friendly labels.
/// @details Returns the input unchanged if it isn't a recognised HA
/// condition code -- Open-Meteo uses open_meteo_condition_from_code()
/// instead, since the two providers' raw vocabularies don't overlap.
const char* friendly_weather_condition(const char* raw_condition);

/// @brief Maps an Open-Meteo numeric weather code to a friendly label.
const char* open_meteo_condition_from_code(int code);

/// @brief Extracts a normalized `C` or `F` temperature unit marker, or '\0'
/// if neither appears in `unit_text`.
char normalized_temperature_unit(const char* unit_text);

/// @brief Extracts `HH:MM` local time text from an ISO datetime string.
bool format_hour_text(const char* iso_datetime, char* out, size_t out_size);

/// @brief Extracts `YYYY-MM-DD` text from an ISO date or datetime string.
bool format_forecast_date_text(const char* iso_datetime, char* out, size_t out_size);

/// @brief Formats a compact daily high/low temperature string, e.g. "5-12C".
bool format_temperature_range_text(const char* high_text, const char* low_text, char unit,
                                   char* out, size_t out_size);

} // namespace weather_normalisation
