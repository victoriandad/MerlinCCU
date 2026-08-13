#pragma once

#include <cstddef>

/// @brief Pure, hardware-independent JSON scanning primitives shared by the
/// Home Assistant and Open-Meteo weather parsers (issue #47).
/// @details Extracted from home_assistant_manager.cpp. These are not a
/// general JSON parser -- they scan a flat or lightly-nested payload for one
/// named field or array at a time, matching what the weather providers'
/// responses actually look like. No lwIP/Pico SDK dependency, so this is
/// host-testable (see tests/host/).
namespace weather_json
{

/// @brief Extracts one JSON string field by key from a simple response body.
/// @details `key` must include the field name and opening quote, e.g.
/// `"\"state\":\""`. Returns false (and clears `out`) if the key is absent or
/// the string's closing quote never arrives in `json` -- a missing closing
/// quote means the buffer holds a truncated value, and treating that as
/// "field missing" is safer than displaying a value cut off mid-network-read.
bool extract_json_string_value(const char* json, const char* key, char* out, size_t out_size);

/// @brief Extracts one non-string JSON scalar field by key.
bool extract_json_scalar_value(const char* json, const char* key, char* out, size_t out_size);

/// @brief Finds the matching closing brace for one JSON object.
/// @details Tracks quoted-string state so braces inside string values (e.g.
/// descriptive text) aren't mistaken for structural braces.
const char* find_matching_json_object_end(const char* object_start);

/// @brief Finds the start of a JSON array's contents (just past `[`) by key.
const char* find_json_array_start(const char* json, const char* key);

/// @brief Returns the string element at `index` within a JSON array.
bool json_array_string_at(const char* array, size_t index, char* out, size_t out_size);

/// @brief Returns the numeric (or other bare-token) element at `index`
/// within a JSON array.
bool json_array_number_at(const char* array, size_t index, char* out, size_t out_size);

/// @brief Copies one value from an Open-Meteo `daily` section array, trying
/// `primary_key` first and falling back to `legacy_key` (some Open-Meteo API
/// revisions renamed daily field keys).
bool open_meteo_daily_scalar(const char* daily_section, const char* primary_key,
                             const char* legacy_key, size_t index, char* out, size_t out_size,
                             bool string_value);

} // namespace weather_json
