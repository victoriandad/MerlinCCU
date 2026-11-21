#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

/// @brief Reads and writes the CCU's location as latitude and longitude
/// (issue #87).
/// @details One place decides what a typed location means, so the web form,
/// the weather fetch and the panel all agree. Pure logic, host-tested.
namespace geo_coordinates
{

/// @brief A position in signed decimal degrees: north and east positive.
struct GeoPoint
{
    double latitude;
    double longitude;
};

/// @brief Reads a position typed in any common form.
/// @details Accepted, with any spacing:
/// - signed decimal, latitude first: `50.23, -2.39` or `50.23 -2.39`
/// - hemisphere letters before or after each value, in either order:
///   `N50.23 W2.39`, `50.23N 2.39W`, `W2.39 N50.23`
/// - degrees and minutes, or degrees, minutes and seconds, with or without
///   the degree, minute and second marks: `N50°13.80' W002°23.40'`,
///   `N50 13 48 W2 23 24`, `50:13.8N 2:23.4W`
///
/// Letters fix which value is which; without them latitude comes first.
/// Minutes and seconds must be under 60, only the last part of a value may
/// have a fraction, and latitude must be within 90 and longitude within 180.
/// @return The position, or nothing when the text is not one.
std::optional<GeoPoint> parse(std::string_view text);

/// @brief Writes `point` in the stored form, `50.230000,-2.390000`.
/// @return False when `out` is too small.
bool format_decimal(const GeoPoint& point, char* out, size_t out_size);

/// @brief Writes `point` as degrees and decimal minutes,
/// `N50<deg>13.80' W002<deg>23.40'`, where `<deg>` is `degree_mark`.
/// @details The web page passes the degree sign; the panel's fonts are ASCII
/// only, so it passes a space.
/// @return False when `out` is too small.
bool format_degrees_minutes(const GeoPoint& point, const char* degree_mark, char* out,
                            size_t out_size);

} // namespace geo_coordinates
