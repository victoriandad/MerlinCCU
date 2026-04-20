#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Logical button identifiers for the unfinished CCU keypad matrix.
/// @details These names describe the current screen-side button positions only.
/// Real GPIO assignments still live in `input.cpp` and can remain unassigned
/// until the keyboard hardware is wired.
enum class ButtonId : uint8_t {
    LeftTop = 0,
    LeftUpper,
    LeftMiddle,
    LeftLower,
    LeftBottom,
    RightTop,
    RightUpper,
    RightMiddle,
    RightLower,
    RightBottom,
    Count,
};

/// @brief Debounced button edge types surfaced to the rest of the firmware.
enum class ButtonEventType : uint8_t {
    None = 0,
    Pressed,
    Released,
};

/// @brief One debounced logical button event.
struct ButtonEvent {
    ButtonId id;
    ButtonEventType type;
};

inline constexpr size_t kKeypadObservedLineCount = 16;

/// @brief One observed front-panel matrix line wired to a Pico GPIO.
struct KeypadObservedLine {
    uint8_t panel_pin;
    int8_t pico_gpio;
    bool configured;
    bool active;
};

/// @brief Raw keypad monitor snapshot for bring-up work.
struct KeypadMonitorStatus {
    uint32_t active_mask;
    uint8_t configured_count;
    uint8_t active_count;
    uint8_t probe_drive_panel_pin;
    int8_t probe_drive_gpio;
    uint32_t probe_hit_mask;
    uint8_t probe_hit_count;
    std::array<KeypadObservedLine, kKeypadObservedLineCount> lines;
    std::array<uint16_t, kKeypadObservedLineCount> probe_hits_by_drive;
};

namespace input {

/// @brief Initializes any configured keypad GPIOs and debounce state.
void init();

/// @brief Returns a stable debug name for one logical button id.
const char* button_name(ButtonId id);

/// @brief Polls the keypad once and returns the next debounced edge, if any.
ButtonEvent poll_buttons();

/// @brief Returns the latest raw keypad line snapshot for diagnostics.
const KeypadMonitorStatus& keypad_monitor_status();

/// @brief Emits debug logging for a logical button event.
/// @details The input layer currently only reports edges over USB serial.
/// Higher-level behavior is handled later by `console_controller`.
void handle_button_event(const ButtonEvent& event);

}  // namespace input
