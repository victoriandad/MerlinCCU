#include "coordinate_editor.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "geo_coordinates.h"

namespace coordinate_editor
{

namespace
{

std::array<char, 10>& active_buffer(State& state)
{
    return state.axis == Axis::Lat ? state.lat_buffer : state.lon_buffer;
}

} // namespace

bool start(State& state, Field field, const char* stored_text)
{
    if (state.editing)
    {
        return false;
    }

    double lat = 0.0;
    double lon = 0.0;
    double lat_magnitude = 0.0;
    double lon_magnitude = 0.0;
    char lat_hemisphere = 'N';
    char lon_hemisphere = 'E';
    if (geo_coordinates::parse_coordinates_text(stored_text, &lat, &lon))
    {
        (void)geo_coordinates::split_hemisphere(lat, /*is_latitude=*/true, &lat_magnitude,
                                                &lat_hemisphere);
        (void)geo_coordinates::split_hemisphere(lon, /*is_latitude=*/false, &lon_magnitude,
                                                &lon_hemisphere);
    }

    state.editing = true;
    state.field = field;
    state.axis = Axis::Lat;
    std::snprintf(state.lat_buffer.data(), state.lat_buffer.size(), "%.4f", lat_magnitude);
    std::snprintf(state.lon_buffer.data(), state.lon_buffer.size(), "%.4f", lon_magnitude);
    state.lat_hemisphere = lat_hemisphere;
    state.lon_hemisphere = lon_hemisphere;
    return true;
}

bool stop(State& state)
{
    if (!state.editing)
    {
        return false;
    }

    state.editing = false;
    return true;
}

bool apply_digit(State& state, uint8_t digit)
{
    if (!state.editing)
    {
        return false;
    }

    std::array<char, 10>& buffer = active_buffer(state);
    const size_t length = std::strlen(buffer.data());
    if (length + 1 >= buffer.size())
    {
        return false;
    }

    buffer[length] = static_cast<char>('0' + digit);
    buffer[length + 1] = '\0';
    return true;
}

bool apply_dot(State& state)
{
    if (!state.editing)
    {
        return false;
    }

    std::array<char, 10>& buffer = active_buffer(state);
    if (std::strchr(buffer.data(), '.') != nullptr)
    {
        return false;
    }

    const size_t length = std::strlen(buffer.data());
    if (length + 1 >= buffer.size())
    {
        return false;
    }

    buffer[length] = '.';
    buffer[length + 1] = '\0';
    return true;
}

bool clear_buffer(State& state)
{
    if (!state.editing)
    {
        return false;
    }

    std::array<char, 10>& buffer = active_buffer(state);
    const bool changed = buffer[0] != '0' || buffer[1] != '\0';
    buffer.fill('\0');
    buffer[0] = '0';
    return changed;
}

bool toggle_hemisphere(State& state)
{
    if (!state.editing)
    {
        return false;
    }

    if (state.axis == Axis::Lat)
    {
        state.lat_hemisphere = state.lat_hemisphere == 'N' ? 'S' : 'N';
    }
    else
    {
        state.lon_hemisphere = state.lon_hemisphere == 'E' ? 'W' : 'E';
    }
    return true;
}

bool advance_axis(State& state)
{
    if (!state.editing || state.axis != Axis::Lat)
    {
        return false;
    }

    state.axis = Axis::Lon;
    return true;
}

bool try_confirm(const State& state, char* out_text, size_t out_text_size)
{
    if (!state.editing || state.axis != Axis::Lon)
    {
        return false;
    }

    double lat = 0.0;
    double lon = 0.0;
    if (!geo_coordinates::combine_hemisphere(std::strtod(state.lat_buffer.data(), nullptr),
                                             state.lat_hemisphere, /*is_latitude=*/true, &lat) ||
        !geo_coordinates::combine_hemisphere(std::strtod(state.lon_buffer.data(), nullptr),
                                             state.lon_hemisphere, /*is_latitude=*/false, &lon))
    {
        return false;
    }

    return geo_coordinates::format_coordinates_text(lat, lon, out_text, out_text_size);
}

} // namespace coordinate_editor
