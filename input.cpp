#include "input.h"

#include <cstddef>
#include <cstdio>

#include "pico/stdlib.h"

namespace input {

namespace {

/// @brief Static GPIO configuration for one logical keypad button.
struct ButtonConfig {
    ButtonId id;
    int pin;
    bool active_low;
    const char* name;
};

/// @brief Debounce tracking for one logical button.
struct ButtonState {
    bool raw_level;
    bool stable_pressed;
    absolute_time_t last_change_time;
};

constexpr int BUTTON_DEBOUNCE_MS = 25;
constexpr size_t BUTTON_COUNT = static_cast<size_t>(ButtonId::Count);

constexpr ButtonConfig BUTTONS[BUTTON_COUNT] = {
    {ButtonId::LeftTop,     -1, true, "LeftTop"},
    {ButtonId::LeftUpper,   -1, true, "LeftUpper"},
    {ButtonId::LeftMiddle,  -1, true, "LeftMiddle"},
    {ButtonId::LeftLower,   -1, true, "LeftLower"},
    {ButtonId::LeftBottom,  -1, true, "LeftBottom"},
    {ButtonId::RightTop,    -1, true, "RightTop"},
    {ButtonId::RightUpper,  -1, true, "RightUpper"},
    {ButtonId::RightMiddle, -1, true, "RightMiddle"},
    {ButtonId::RightLower,  -1, true, "RightLower"},
    {ButtonId::RightBottom, -1, true, "RightBottom"},
};

ButtonState button_states[BUTTON_COUNT];

/// @brief Converts one raw GPIO level into a pressed/not-pressed meaning.
bool button_level_is_pressed(bool raw_level, const ButtonConfig& button)
{
    return button.active_low ? !raw_level : raw_level;
}

/// @brief Polls and debounces one logical button definition.
ButtonEvent poll_button(ButtonState& state, const ButtonConfig& button)
{
    if (button.pin < 0) {
        return {button.id, ButtonEventType::None};
    }

    const bool raw_level = gpio_get(static_cast<uint>(button.pin));
    const absolute_time_t now = get_absolute_time();

    if (raw_level != state.raw_level) {
        state.raw_level = raw_level;
        state.last_change_time = now;
        return {button.id, ButtonEventType::None};
    }

    if (absolute_time_diff_us(state.last_change_time, now) < (BUTTON_DEBOUNCE_MS * 1000)) {
        return {button.id, ButtonEventType::None};
    }

    const bool pressed = button_level_is_pressed(raw_level, button);
    if (pressed == state.stable_pressed) {
        return {button.id, ButtonEventType::None};
    }

    state.stable_pressed = pressed;
    return {button.id, pressed ? ButtonEventType::Pressed : ButtonEventType::Released};
}

}  // namespace

/// @brief Initializes any configured button GPIOs and debounce state.
void init()
{
    const absolute_time_t now = get_absolute_time();

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        button_states[i] = {
            .raw_level = false,
            .stable_pressed = false,
            .last_change_time = now,
        };

        const ButtonConfig& button = BUTTONS[i];
        if (button.pin < 0) {
            continue;
        }

        gpio_init(static_cast<uint>(button.pin));
        gpio_set_dir(static_cast<uint>(button.pin), GPIO_IN);

        if (button.active_low) {
            gpio_pull_up(static_cast<uint>(button.pin));
        } else {
            gpio_pull_down(static_cast<uint>(button.pin));
        }

        const bool raw_level = gpio_get(static_cast<uint>(button.pin));
        button_states[i].raw_level = raw_level;
        button_states[i].stable_pressed = button_level_is_pressed(raw_level, button);
    }
}

/// @brief Returns the first debounced button edge seen in the current scan.
ButtonEvent poll_buttons()
{
    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        ButtonEvent event = poll_button(button_states[i], BUTTONS[i]);
        if (event.type != ButtonEventType::None) {
            return event;
        }
    }

    return {ButtonId::LeftTop, ButtonEventType::None};
}

/// @brief Logs button edges for keypad bring-up and controller debugging.
void handle_button_event(const ButtonEvent& event)
{
    if (event.type == ButtonEventType::None) {
        return;
    }

    for (const ButtonConfig& button : BUTTONS) {
        if (button.id == event.id) {
            const char* edge = (event.type == ButtonEventType::Pressed) ? "pressed" : "released";
            std::printf("Button %s %s\n", button.name, edge);
            return;
        }
    }
}

}  // namespace input
