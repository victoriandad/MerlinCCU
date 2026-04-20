#include "input.h"

#include <cstddef>
#include <cstdio>

#include "pico/stdlib.h"

#if __has_include("keypad_matrix_config.h")
#include "keypad_matrix_config.h"
#else
inline constexpr int KEYPAD_PANEL_PIN_5_GPIO  = -1;
inline constexpr int KEYPAD_PANEL_PIN_6_GPIO  = -1;
inline constexpr int KEYPAD_PANEL_PIN_7_GPIO  = -1;
inline constexpr int KEYPAD_PANEL_PIN_8_GPIO  = -1;
inline constexpr int KEYPAD_PANEL_PIN_9_GPIO  = -1;
inline constexpr int KEYPAD_PANEL_PIN_10_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_11_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_14_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_15_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_16_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_17_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_18_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_19_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_20_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_21_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_22_GPIO = -1;
#endif

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

struct ObservedLineConfig {
    uint8_t panel_pin;
    int pico_gpio;
    bool active_low;
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

// Fill these in as the ribbon breakout is wired to the Pico. The panel_pin
// numbers come from the spreadsheet notes and let the diagnostics page talk in
// the same language as the hardware bench work.
constexpr ObservedLineConfig OBSERVED_LINES[kKeypadObservedLineCount] = {
    { 5, KEYPAD_PANEL_PIN_5_GPIO,  true},
    { 6, KEYPAD_PANEL_PIN_6_GPIO,  true},
    { 7, KEYPAD_PANEL_PIN_7_GPIO,  true},
    { 8, KEYPAD_PANEL_PIN_8_GPIO,  true},
    { 9, KEYPAD_PANEL_PIN_9_GPIO,  true},
    {10, KEYPAD_PANEL_PIN_10_GPIO, true},
    {11, KEYPAD_PANEL_PIN_11_GPIO, true},
    {14, KEYPAD_PANEL_PIN_14_GPIO, true},
    {15, KEYPAD_PANEL_PIN_15_GPIO, true},
    {16, KEYPAD_PANEL_PIN_16_GPIO, true},
    {17, KEYPAD_PANEL_PIN_17_GPIO, true},
    {18, KEYPAD_PANEL_PIN_18_GPIO, true},
    {19, KEYPAD_PANEL_PIN_19_GPIO, true},
    {20, KEYPAD_PANEL_PIN_20_GPIO, true},
    {21, KEYPAD_PANEL_PIN_21_GPIO, true},
    {22, KEYPAD_PANEL_PIN_22_GPIO, true},
};

ButtonState button_states[BUTTON_COUNT];
KeypadMonitorStatus g_keypad_monitor_status = {};
std::array<uint16_t, kKeypadObservedLineCount> g_probe_hits_by_drive = {};
std::array<uint16_t, kKeypadObservedLineCount> g_last_logged_probe_hits_by_drive = {};

void configure_observed_line_as_input(const ObservedLineConfig& line)
{
    if (line.pico_gpio < 0) {
        return;
    }

    gpio_set_dir(static_cast<uint>(line.pico_gpio), GPIO_IN);
    if (line.active_low) {
        gpio_pull_up(static_cast<uint>(line.pico_gpio));
    } else {
        gpio_pull_down(static_cast<uint>(line.pico_gpio));
    }
}

void configure_observed_line_as_drive(const ObservedLineConfig& line)
{
    if (line.pico_gpio < 0) {
        return;
    }

    gpio_set_dir(static_cast<uint>(line.pico_gpio), GPIO_OUT);
    gpio_put(static_cast<uint>(line.pico_gpio), line.active_low ? 0 : 1);
}

void log_keypad_probe_results_if_changed()
{
    if (g_probe_hits_by_drive == g_last_logged_probe_hits_by_drive) {
        return;
    }

    g_last_logged_probe_hits_by_drive = g_probe_hits_by_drive;

    for (size_t i = 0; i < kKeypadObservedLineCount; ++i) {
        const ObservedLineConfig& drive_line = OBSERVED_LINES[i];
        const uint16_t hit_mask = g_probe_hits_by_drive[i];
        if (drive_line.pico_gpio < 0 || hit_mask == 0) {
            continue;
        }

        std::printf("Keypad probe: drive=%u hits=",
                    static_cast<unsigned>(drive_line.panel_pin));

        bool printed_any = false;
        for (size_t j = 0; j < kKeypadObservedLineCount; ++j) {
            if ((hit_mask & (1u << j)) == 0) {
                continue;
            }

            std::printf("%s%u",
                        printed_any ? " " : "",
                        static_cast<unsigned>(OBSERVED_LINES[j].panel_pin));
            printed_any = true;
        }
        std::printf("\n");
    }

    if (g_keypad_monitor_status.active_mask == 0) {
        std::printf("Keypad probe: no closures detected\n");
    }
}

/// @brief Converts one raw GPIO level into a pressed/not-pressed meaning.
bool button_level_is_pressed(bool raw_level, const ButtonConfig& button)
{
    return button.active_low ? !raw_level : raw_level;
}

bool observed_line_is_active(bool raw_level, const ObservedLineConfig& line)
{
    return line.active_low ? !raw_level : raw_level;
}

void refresh_keypad_monitor_status()
{
    g_probe_hits_by_drive.fill(0);
    g_keypad_monitor_status.active_mask = 0;
    g_keypad_monitor_status.configured_count = 0;
    g_keypad_monitor_status.active_count = 0;
    g_keypad_monitor_status.probe_drive_panel_pin = 0;
    g_keypad_monitor_status.probe_drive_gpio = -1;
    g_keypad_monitor_status.probe_hit_mask = 0;
    g_keypad_monitor_status.probe_hit_count = 0;
    g_keypad_monitor_status.probe_hits_by_drive.fill(0);

    for (size_t i = 0; i < kKeypadObservedLineCount; ++i) {
        const ObservedLineConfig& line = OBSERVED_LINES[i];
        KeypadObservedLine& snapshot = g_keypad_monitor_status.lines[i];
        snapshot.panel_pin = line.panel_pin;
        snapshot.pico_gpio = static_cast<int8_t>(line.pico_gpio);
        snapshot.configured = (line.pico_gpio >= 0);
        snapshot.active = false;

        if (line.pico_gpio < 0) {
            continue;
        }

        ++g_keypad_monitor_status.configured_count;
        configure_observed_line_as_input(line);
    }

    uint8_t best_drive_panel_pin = 0;
    int8_t best_drive_gpio = -1;
    uint32_t best_hit_mask = 0;
    uint8_t best_hit_count = 0;

    for (size_t drive_index = 0; drive_index < kKeypadObservedLineCount; ++drive_index) {
        const ObservedLineConfig& drive_line = OBSERVED_LINES[drive_index];
        if (drive_line.pico_gpio < 0) {
            continue;
        }

        configure_observed_line_as_drive(drive_line);
        sleep_us(20);

        uint16_t hit_mask = 0;
        uint8_t hit_count = 0;

        for (size_t sense_index = 0; sense_index < kKeypadObservedLineCount; ++sense_index) {
            if (sense_index == drive_index) {
                continue;
            }

            const ObservedLineConfig& sense_line = OBSERVED_LINES[sense_index];
            if (sense_line.pico_gpio < 0) {
                continue;
            }

            const bool raw_level = gpio_get(static_cast<uint>(sense_line.pico_gpio));
            const bool active = observed_line_is_active(raw_level, sense_line);
            if (!active) {
                continue;
            }

            hit_mask |= static_cast<uint16_t>(1u << sense_index);
            ++hit_count;
            g_keypad_monitor_status.lines[sense_index].active = true;
        }

        if (hit_mask != 0) {
            g_probe_hits_by_drive[drive_index] = hit_mask;
            g_keypad_monitor_status.probe_hits_by_drive[drive_index] = hit_mask;
            g_keypad_monitor_status.active_mask |= (1u << drive_index);
            g_keypad_monitor_status.lines[drive_index].active = true;
            for (size_t sense_index = 0; sense_index < kKeypadObservedLineCount; ++sense_index) {
                if ((hit_mask & (1u << sense_index)) != 0) {
                    g_keypad_monitor_status.active_mask |= (1u << sense_index);
                }
            }

            if (hit_count > best_hit_count) {
                best_drive_panel_pin = drive_line.panel_pin;
                best_drive_gpio = static_cast<int8_t>(drive_line.pico_gpio);
                best_hit_mask = hit_mask;
                best_hit_count = hit_count;
            }
        }

        configure_observed_line_as_input(drive_line);
    }

    for (size_t i = 0; i < kKeypadObservedLineCount; ++i) {
        if (g_keypad_monitor_status.lines[i].active) {
            ++g_keypad_monitor_status.active_count;
        }
    }

    g_keypad_monitor_status.probe_drive_panel_pin = best_drive_panel_pin;
    g_keypad_monitor_status.probe_drive_gpio = best_drive_gpio;
    g_keypad_monitor_status.probe_hit_mask = best_hit_mask;
    g_keypad_monitor_status.probe_hit_count = best_hit_count;

    for (const ObservedLineConfig& line : OBSERVED_LINES) {
        configure_observed_line_as_input(line);
    }
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

    for (const ObservedLineConfig& line : OBSERVED_LINES) {
        if (line.pico_gpio < 0) {
            continue;
        }

        gpio_init(static_cast<uint>(line.pico_gpio));
        gpio_set_dir(static_cast<uint>(line.pico_gpio), GPIO_IN);
        if (line.active_low) {
            gpio_pull_up(static_cast<uint>(line.pico_gpio));
        } else {
            gpio_pull_down(static_cast<uint>(line.pico_gpio));
        }
    }

    refresh_keypad_monitor_status();
    log_keypad_probe_results_if_changed();
}

/// @brief Returns the fixed debug label for one logical button.
const char* button_name(ButtonId id)
{
    for (const ButtonConfig& button : BUTTONS) {
        if (button.id == id) {
            return button.name;
        }
    }

    return "Unknown";
}

/// @brief Returns the first debounced button edge seen in the current scan.
ButtonEvent poll_buttons()
{
    refresh_keypad_monitor_status();
    log_keypad_probe_results_if_changed();

    for (size_t i = 0; i < BUTTON_COUNT; ++i) {
        ButtonEvent event = poll_button(button_states[i], BUTTONS[i]);
        if (event.type != ButtonEventType::None) {
            return event;
        }
    }

    return {ButtonId::LeftTop, ButtonEventType::None};
}

const KeypadMonitorStatus& keypad_monitor_status()
{
    return g_keypad_monitor_status;
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
