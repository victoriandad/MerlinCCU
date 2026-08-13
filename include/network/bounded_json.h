#pragma once

#include <cstddef>

/// @brief Pure, hardware-independent bounded-range JSON scanning primitives
/// shared by the share-price and air-traffic response parsers (issue #72).
/// @details Extracted from what used to be two independently-converged
/// copies in `share_price_manager.cpp` and `air_traffic_manager.cpp`. Unlike
/// `weather_json_scan.h` (which scans from a key to the end of a
/// null-terminated buffer), every function here takes an explicit
/// `[start, end)` range so a caller can search within one already-isolated
/// JSON object without it reading past that object's closing brace. No
/// lwIP/Pico SDK dependency, so this is host-testable (see tests/host/).
namespace bounded_json
{

/// @brief Skips JSON whitespace and returns the next meaningful character.
/// @details Relies on `cursor` pointing into a null-terminated buffer (every
/// caller operates on a manager's own null-terminated response buffer, or a
/// null-terminated test fixture) rather than taking an explicit end bound.
const char* skip_space(const char* cursor);

/// @brief Finds the first occurrence of `key` within `[start, end)`.
const char* find_bounded(const char* start, const char* end, const char* key);

/// @brief Extracts one bounded JSON string scalar by key.
bool extract_bounded_string(const char* start, const char* end, const char* key, char* out,
                            size_t out_size);

/// @brief Extracts one bounded JSON numeric scalar by key.
/// @details Rejects a quoted value explicitly (returns false) rather than
/// relying on the numeric parse to fail on the opening `"` -- the two
/// original copies of this function had diverged on exactly this point;
/// this is the more defensive of the two and was confirmed to produce the
/// same practical result either way.
bool extract_bounded_number(const char* start, const char* end, const char* key,
                            double* out_value);

/// @brief Returns true when `key`'s value is a quoted string rather than a
/// number (e.g. adsb.lol's `"alt_baro":"ground"` convention for grounded
/// aircraft).
bool bounded_value_is_string(const char* start, const char* end, const char* key);

/// @brief Finds the closing `}` matching the `{` at `obj_start`, string-aware
/// so a brace inside a quoted value cannot desync the depth count.
const char* find_object_end(const char* obj_start, const char* buffer_end);

} // namespace bounded_json
