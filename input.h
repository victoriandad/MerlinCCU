#pragma once

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

namespace input {

/// @brief Initializes any configured keypad GPIOs and debounce state.
void init();

/// @brief Polls the keypad once and returns the next debounced edge, if any.
ButtonEvent poll_buttons();

/// @brief Emits debug logging for a logical button event.
/// @details The input layer currently only reports edges over USB serial.
/// Higher-level behavior is handled later by `console_controller`.
void handle_button_event(const ButtonEvent& event);

}  // namespace input
