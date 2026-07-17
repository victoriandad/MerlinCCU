#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Pure, host-testable state machine for the on-device latitude/
/// longitude editor (issue #87). Deliberately has no dependency on
/// `ConsoleState`, `config_manager`, or the keypad button model -- those live
/// in `console_controller.cpp`, which decodes physical key presses into
/// calls here and owns persisting a confirmed edit. Keeping this module free
/// of those dependencies is what makes it host-testable, following the same
/// split already used for `calendar_navigation`/`pinter_scheduling`.
namespace coordinate_editor
{

/// @brief Which RuntimeConfig coordinate field is being edited.
enum class Field : uint8_t
{
    AirTraffic = 0,
    Weather,
};

/// @brief Which axis of the lat/lon pair is currently being edited.
enum class Axis : uint8_t
{
    Lat = 0,
    Lon,
};

/// @brief All mutable state for one in-progress coordinate edit. The
/// magnitude buffers hold free-typed "DDD.DDDD" text (not an accumulated
/// integer), sized for up to 3 integer digits (longitude magnitude can reach
/// 180) plus 4 fraction digits plus the terminator.
struct State
{
    bool editing = false;
    Field field = Field::AirTraffic;
    Axis axis = Axis::Lat;
    std::array<char, 10> lat_buffer = {'0'};
    std::array<char, 10> lon_buffer = {'0'};
    char lat_hemisphere = 'N';
    char lon_hemisphere = 'E';
};

/// @brief Enters the editor for `field`, pre-loading both axis buffers and
/// hemispheres by parsing `stored_text` (the field's current "lat,lon"
/// text). Falls back to 0/N/E for either axis that fails to parse.
/// @return false if an edit is already in progress.
bool start(State& state, Field field, const char* stored_text);

/// @brief Leaves the editor without saving.
/// @return false if no edit was in progress.
bool stop(State& state);

/// @brief Appends one typed digit to the active axis's buffer.
/// @return false if not editing, or the buffer is already at capacity.
bool apply_digit(State& state, uint8_t digit);

/// @brief Appends a decimal point to the active axis's buffer.
/// @return false if not editing, a point is already present, or the buffer
/// is already at capacity.
bool apply_dot(State& state);

/// @brief Resets the active axis's buffer to the disabled `0` state.
/// @return false if not editing, or the buffer was already `0`.
bool clear_buffer(State& state);

/// @brief Flips the active axis's hemisphere (N/S for latitude, E/W for
/// longitude).
/// @return false if not editing.
bool toggle_hemisphere(State& state);

/// @brief Advances editing from the latitude axis to the longitude axis.
/// @return false if not editing, or not currently on the latitude axis.
bool advance_axis(State& state);

/// @brief Validates both axes and, on success, writes the canonical
/// "lat,lon" text to `out_text`. Does not mutate `state` or persist
/// anything -- the caller decides whether/how to save and must call `stop()`
/// itself.
/// @return false if not editing, not on the longitude axis yet, or either
/// axis's typed value is out of range.
bool try_confirm(const State& state, char* out_text, size_t out_text_size);

} // namespace coordinate_editor
