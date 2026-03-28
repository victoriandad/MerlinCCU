#pragma once

#include <cstdint>

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

enum class ButtonEventType : uint8_t {
    None = 0,
    Pressed,
    Released,
};

struct ButtonEvent {
    ButtonId id;
    ButtonEventType type;
};

namespace input {

void init();
ButtonEvent poll_buttons();
void handle_button_event(const ButtonEvent& event);

}  // namespace input
