#include "geo_coordinates.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace geo_coordinates
{

namespace
{

constexpr double kMaxLatitudeDegrees = 90.0;
constexpr double kMaxLongitudeDegrees = 180.0;

/// @brief Scans forward for the next signed decimal number in `*cursor`.
bool parse_next_double(const char** cursor, double* out_value)
{
    if (cursor == nullptr || *cursor == nullptr || out_value == nullptr)
    {
        return false;
    }

    const char* scan = *cursor;
    while (*scan != '\0')
    {
        const bool could_start_number =
            (*scan >= '0' && *scan <= '9') || *scan == '-' || *scan == '+' || *scan == '.';
        if (could_start_number)
        {
            char* end = nullptr;
            const double value = std::strtod(scan, &end);
            if (end != scan)
            {
                *cursor = end;
                *out_value = value;
                return true;
            }
        }
        ++scan;
    }

    return false;
}

} // namespace

bool parse_coordinates_text(const char* text, double* out_lat, double* out_lon)
{
    if (text == nullptr || text[0] == '\0' || out_lat == nullptr || out_lon == nullptr)
    {
        return false;
    }

    const char* cursor = text;
    double latitude = 0.0;
    double longitude = 0.0;
    if (!parse_next_double(&cursor, &latitude) || !parse_next_double(&cursor, &longitude))
    {
        return false;
    }

    if (std::isnan(latitude) || std::isnan(longitude) || latitude < -kMaxLatitudeDegrees ||
        latitude > kMaxLatitudeDegrees || longitude < -kMaxLongitudeDegrees ||
        longitude > kMaxLongitudeDegrees)
    {
        return false;
    }

    *out_lat = latitude;
    *out_lon = longitude;
    return true;
}

bool format_coordinates_text(double lat, double lon, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return false;
    }

    const int written = std::snprintf(out, out_size, "%.4f,%.4f", lat, lon);
    return written > 0 && static_cast<size_t>(written) < out_size;
}

bool combine_hemisphere(double magnitude, char hemisphere, bool is_latitude, double* out_signed)
{
    if (out_signed == nullptr || std::isnan(magnitude) || magnitude < 0.0)
    {
        return false;
    }

    const double max_magnitude = is_latitude ? kMaxLatitudeDegrees : kMaxLongitudeDegrees;
    if (magnitude > max_magnitude)
    {
        return false;
    }

    const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(hemisphere)));
    const char positive_letter = is_latitude ? 'N' : 'E';
    const char negative_letter = is_latitude ? 'S' : 'W';

    if (upper == positive_letter)
    {
        *out_signed = magnitude;
        return true;
    }
    if (upper == negative_letter)
    {
        *out_signed = -magnitude;
        return true;
    }

    return false;
}

bool split_hemisphere(double signed_value, bool is_latitude, double* out_magnitude,
                      char* out_hemisphere)
{
    if (out_magnitude == nullptr || out_hemisphere == nullptr || std::isnan(signed_value))
    {
        return false;
    }

    const double max_magnitude = is_latitude ? kMaxLatitudeDegrees : kMaxLongitudeDegrees;
    const double magnitude = std::fabs(signed_value);
    if (magnitude > max_magnitude)
    {
        return false;
    }

    const char positive_letter = is_latitude ? 'N' : 'E';
    const char negative_letter = is_latitude ? 'S' : 'W';

    *out_magnitude = magnitude;
    *out_hemisphere = signed_value < 0.0 ? negative_letter : positive_letter;
    return true;
}

} // namespace geo_coordinates
