#pragma once

#include <cstddef>

/// @brief Shared latitude/longitude text parsing and hemisphere conversion
/// (issue #87). Single source of truth for every place that reads or writes
/// the "lat,lon" free-text form of `air_traffic_coordinates`/
/// `weather_coordinates`, so the web config UI, the on-device editor, and the
/// feature managers that consume the saved value can never disagree on
/// format or range.
namespace geo_coordinates
{

/// @brief Scans `text` for the first two signed decimal numbers (any
/// separator) and treats them as latitude then longitude.
/// @return false if fewer than two numbers are found, or either is out of
/// range (`latitude` in [-90, 90], `longitude` in [-180, 180]).
bool parse_coordinates_text(const char* text, double* out_lat, double* out_lon);

/// @brief Writes the canonical stored form of a coordinate pair, e.g.
/// "51.5074,-0.1278", at four decimal places.
/// @return false if `out` is null/zero-sized or the formatted text would not
/// fit in `out_size`.
bool format_coordinates_text(double lat, double lon, char* out, size_t out_size);

/// @brief Combines an unsigned magnitude with a hemisphere letter into a
/// signed coordinate value.
/// @param is_latitude Selects the valid range and hemisphere pair: true for
/// latitude (magnitude 0-90, hemisphere 'N'/'S'), false for longitude
/// (magnitude 0-180, hemisphere 'E'/'W'). The hemisphere letter is
/// case-insensitive.
/// @return false if the magnitude is out of range or the hemisphere letter
/// is not one of the two valid letters for `is_latitude`.
bool combine_hemisphere(double magnitude, char hemisphere, bool is_latitude, double* out_signed);

/// @brief Inverse of `combine_hemisphere`: splits a signed coordinate value
/// into an unsigned magnitude and a hemisphere letter ('N'/'S' or 'E'/'W').
/// @return false if `signed_value` is out of the range implied by
/// `is_latitude`.
bool split_hemisphere(double signed_value, bool is_latitude, double* out_magnitude,
                      char* out_hemisphere);

} // namespace geo_coordinates
