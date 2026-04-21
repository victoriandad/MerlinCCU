#include "input.h"

#include <cstddef>
#include <cstdio>

#include "pico/stdlib.h"

#if __has_include("keypad_matrix_config.h")
#include "keypad_matrix_config.h"
#else
inline constexpr int KEYPAD_PANEL_PIN_5_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_6_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_7_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_8_GPIO = -1;
inline constexpr int KEYPAD_PANEL_PIN_9_GPIO = -1;
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

namespace input
{

namespace
{

/// @brief Static GPIO configuration for one logical keypad button.
struct ButtonConfig
{
    ButtonId id;
    int pin;
    bool active_low;
    const char* name;
};

/// @brief Debounce tracking for one logical button.
struct ButtonState
{
    bool raw_level;
    bool stable_pressed;
    absolute_time_t last_change_time;
};

/// @brief Static GPIO configuration for one observed keypad matrix line.
struct ObservedLineConfig
{
    uint8_t panel_pin;
    int pico_gpio;
    bool active_low;
};

/// @brief Tracks the strongest keypad probe result seen during one scan pass.
struct BestProbeResult
{
    uint8_t drive_panel_pin = 0;
    int8_t drive_gpio = -1;
    uint32_t hit_mask = 0;
    uint8_t hit_count = 0;
};

/// @brief Carries the sensed hit mask and hit count for one driven keypad line.
struct ProbeHits
{
    uint16_t mask = 0;
    uint8_t count = 0;
};

constexpr int kButtonDebounceMs = 25;
constexpr int64_t kButtonDebounceUs = static_cast<int64_t>(kButtonDebounceMs) * 1000;
constexpr uint32_t kKeypadProbeSettleUs = 20U;
constexpr size_t kButtonCount = static_cast<size_t>(ButtonId::Count);

/// @brief Static metadata for the panel softkeys read as direct GPIO inputs.
/// @details This table ties each logical `ButtonId` to the eventual GPIO mapping so the
/// debounce and polling code stays generic while the hardware breakout evolves.
constexpr std::array<ButtonConfig, kButtonCount> kButtons = {{
    {ButtonId::LeftTop, -1, true, "LeftTop"},
    {ButtonId::LeftUpper, -1, true, "LeftUpper"},
    {ButtonId::LeftMiddle, -1, true, "LeftMiddle"},
    {ButtonId::LeftLower, -1, true, "LeftLower"},
    {ButtonId::LeftBottom, -1, true, "LeftBottom"},
    {ButtonId::RightTop, -1, true, "RightTop"},
    {ButtonId::RightUpper, -1, true, "RightUpper"},
    {ButtonId::RightMiddle, -1, true, "RightMiddle"},
    {ButtonId::RightLower, -1, true, "RightLower"},
    {ButtonId::RightBottom, -1, true, "RightBottom"},
}};

/// @brief Ribbon-pin to Pico-GPIO mapping for the keypad matrix probe logic.
/// @details The panel pin numbers intentionally match the bench spreadsheet so probe logs
/// and diagnostics screens can be compared directly with the hardware notes.
constexpr std::array<ObservedLineConfig, kKeypadObservedLineCount> kObservedLines = {{
    {5, kKeypadPanelPin5Gpio, true},
    {6, kKeypadPanelPin6Gpio, true},
    {7, kKeypadPanelPin7Gpio, true},
    {8, kKeypadPanelPin8Gpio, true},
    {9, kKeypadPanelPin9Gpio, true},
    {10, kKeypadPanelPin10Gpio, true},
    {11, kKeypadPanelPin11Gpio, true},
    {14, kKeypadPanelPin14Gpio, true},
    {15, kKeypadPanelPin15Gpio, true},
    {16, kKeypadPanelPin16Gpio, true},
    {17, kKeypadPanelPin17Gpio, true},
    {18, kKeypadPanelPin18Gpio, true},
    {19, kKeypadPanelPin19Gpio, true},
    {20, kKeypadPanelPin20Gpio, true},
    {21, kKeypadPanelPin21Gpio, true},
    {22, kKeypadPanelPin22Gpio, true},
}};

std::array<ButtonState, kButtonCount> g_button_states = {};
KeypadMonitorStatus g_keypad_monitor_status = {};
std::array<uint16_t, kKeypadObservedLineCount> g_probe_hits_by_drive = {};
std::array<uint16_t, kKeypadObservedLineCount> g_last_logged_probe_hits_by_drive = {};

/// @brief Returns the 16-bit hit mask bit for one observed line index.
constexpr uint16_t observed_line_hit_bit(size_t line_index)
{
    return static_cast<uint16_t>(1U << line_index);
}

/// @brief Returns the 32-bit active mask bit for one observed line index.
constexpr uint32_t observed_line_active_bit(size_t line_index)
{
    return static_cast<uint32_t>(1U << line_index);
}

/// @brief Restores one configured keypad line to its passive input state.
void configure_observed_line_as_input(const ObservedLineConfig& line)
{
    if (line.pico_gpio < 0)
    {
        return;
    }

    gpio_set_dir(static_cast<uint>(line.pico_gpio), false);
    if (line.active_low)
    {
        gpio_pull_up(static_cast<uint>(line.pico_gpio));
    }
    else
    {
        gpio_pull_down(static_cast<uint>(line.pico_gpio));
    }
}

/// @brief Drives one configured keypad line to its inactive electrical level.
void configure_observed_line_as_drive(const ObservedLineConfig& line)
{
    if (line.pico_gpio < 0)
    {
        return;
    }

    gpio_set_dir(static_cast<uint>(line.pico_gpio), true);
    gpio_put(static_cast<uint>(line.pico_gpio), !line.active_low);
}

/// @brief Logs any change in keypad probe connectivity since the previous scan.
void log_keypad_probe_results_if_changed()
{
    if (g_probe_hits_by_drive == g_last_logged_probe_hits_by_drive)
    {
        return;
    }

    g_last_logged_probe_hits_by_drive = g_probe_hits_by_drive;

    for (size_t i = 0; i < kKeypadObservedLineCount; ++i)
    {
        const ObservedLineConfig& drive_line = kObservedLines[i];
        const uint16_t kHitMask = g_probe_hits_by_drive[i];
        if (drive_line.pico_gpio < 0 || kHitMask == 0)
        {
            continue;
        }

        std::printf("Keypad probe: drive=%u hits=", static_cast<unsigned>(drive_line.panel_pin));

        bool printed_any = false;
        for (size_t j = 0; j < kKeypadObservedLineCount; ++j)
        {
            if ((kHitMask & observed_line_hit_bit(j)) == 0)
            {
                continue;
            }

            std::printf("%s%u", printed_any ? " " : "",
                        static_cast<unsigned>(kObservedLines[j].panel_pin));
            printed_any = true;
        }
        std::printf("\n");
    }

    if (g_keypad_monitor_status.active_mask == 0)
    {
        std::printf("Keypad probe: no closures detected\n");
    }
}

/// @brief Converts one raw GPIO level into a pressed/not-pressed meaning.
bool button_level_is_pressed(bool raw_level, const ButtonConfig& button)
{
    return button.active_low ? !raw_level : raw_level;
}

/// @brief Converts one raw observed-line GPIO level into an active/inactive state.
bool observed_line_is_active(bool raw_level, const ObservedLineConfig& line)
{
    return line.active_low ? !raw_level : raw_level;
}

/// @brief Clears the public keypad monitor snapshot before a fresh scan.
void reset_keypad_monitor_status()
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
}

/// @brief Seeds each observed-line snapshot entry and returns configured lines to input mode.
void initialize_keypad_monitor_lines()
{
    for (size_t i = 0; i < kKeypadObservedLineCount; ++i)
    {
        const ObservedLineConfig& line = kObservedLines[i];
        KeypadObservedLine& snapshot = g_keypad_monitor_status.lines[i];
        snapshot.panel_pin = line.panel_pin;
        snapshot.pico_gpio = static_cast<int8_t>(line.pico_gpio);
        snapshot.configured = (line.pico_gpio >= 0);
        snapshot.active = false;

        if (line.pico_gpio < 0)
        {
            continue;
        }

        ++g_keypad_monitor_status.configured_count;
        configure_observed_line_as_input(line);
    }
}

/// @brief Copies one drive-line probe result into the public diagnostics snapshot.
void record_probe_hits(size_t drive_index, const ObservedLineConfig& drive_line,
                       const ProbeHits& probe_hits, BestProbeResult& best_probe_result)
{
    if (probe_hits.mask == 0)
    {
        return;
    }

    g_probe_hits_by_drive[drive_index] = probe_hits.mask;
    g_keypad_monitor_status.probe_hits_by_drive[drive_index] = probe_hits.mask;
    g_keypad_monitor_status.active_mask |= observed_line_active_bit(drive_index);
    g_keypad_monitor_status.lines[drive_index].active = true;

    for (size_t sense_index = 0; sense_index < kKeypadObservedLineCount; ++sense_index)
    {
        if ((probe_hits.mask & observed_line_hit_bit(sense_index)) == 0)
        {
            continue;
        }

        g_keypad_monitor_status.active_mask |= observed_line_active_bit(sense_index);
    }

    if (probe_hits.count > best_probe_result.hit_count)
    {
        best_probe_result.drive_panel_pin = drive_line.panel_pin;
        best_probe_result.drive_gpio = static_cast<int8_t>(drive_line.pico_gpio);
        best_probe_result.hit_mask = probe_hits.mask;
        best_probe_result.hit_count = probe_hits.count;
    }
}

/// @brief Recomputes the count of electrically active observed lines.
void update_active_line_count()
{
    for (const KeypadObservedLine& line : g_keypad_monitor_status.lines)
    {
        if (line.active)
        {
            ++g_keypad_monitor_status.active_count;
        }
    }
}

/// @brief Scans the configured keypad matrix lines for closures and probe hits.
void refresh_keypad_monitor_status()
{
    // Start from a clean snapshot every pass so unplugged or transient lines do
    // not leave stale activity in the public diagnostics view.
    reset_keypad_monitor_status();
    initialize_keypad_monitor_lines();

    BestProbeResult best_probe_result;

    // Drive each observed line one at a time while all others stay passive.
    // That keeps the resulting hit mask easy to interpret on the bench.
    for (size_t drive_index = 0; drive_index < kKeypadObservedLineCount; ++drive_index)
    {
        const ObservedLineConfig& drive_line = kObservedLines[drive_index];
        if (drive_line.pico_gpio < 0)
        {
            continue;
        }

        configure_observed_line_as_drive(drive_line);
        sleep_us(kKeypadProbeSettleUs);

        ProbeHits probe_hits;

        // Every other configured line is sampled as a possible return path for
        // the currently driven line.
        for (size_t sense_index = 0; sense_index < kKeypadObservedLineCount; ++sense_index)
        {
            if (sense_index == drive_index)
            {
                continue;
            }

            const ObservedLineConfig& sense_line = kObservedLines[sense_index];
            if (sense_line.pico_gpio < 0)
            {
                continue;
            }

            const bool kRawLevel = gpio_get(static_cast<uint>(sense_line.pico_gpio));
            const bool kActive = observed_line_is_active(kRawLevel, sense_line);
            if (!kActive)
            {
                continue;
            }

            probe_hits.mask |= observed_line_hit_bit(sense_index);
            ++probe_hits.count;
            g_keypad_monitor_status.lines[sense_index].active = true;
        }

        record_probe_hits(drive_index, drive_line, probe_hits, best_probe_result);
        configure_observed_line_as_input(drive_line);
    }

    // Publish the strongest observed closure set as the one-line summary, while
    // still keeping the full per-drive matrix in the diagnostics snapshot.
    update_active_line_count();

    g_keypad_monitor_status.probe_drive_panel_pin = best_probe_result.drive_panel_pin;
    g_keypad_monitor_status.probe_drive_gpio = best_probe_result.drive_gpio;
    g_keypad_monitor_status.probe_hit_mask = best_probe_result.hit_mask;
    g_keypad_monitor_status.probe_hit_count = best_probe_result.hit_count;

    for (const ObservedLineConfig& line : kObservedLines)
    {
        configure_observed_line_as_input(line);
    }
}

/// @brief Polls and debounces one logical button definition.
ButtonEvent poll_button(ButtonState& state, const ButtonConfig& button)
{
    if (button.pin < 0)
    {
        return {button.id, ButtonEventType::None};
    }

    const bool kRawLevel = gpio_get(static_cast<uint>(button.pin));
    const absolute_time_t kNow = get_absolute_time();

    if (kRawLevel != state.raw_level)
    {
        state.raw_level = kRawLevel;
        state.last_change_time = kNow;
        return {button.id, ButtonEventType::None};
    }

    if (absolute_time_diff_us(state.last_change_time, kNow) < kButtonDebounceUs)
    {
        return {button.id, ButtonEventType::None};
    }

    const bool kPressed = button_level_is_pressed(kRawLevel, button);
    if (kPressed == state.stable_pressed)
    {
        return {button.id, ButtonEventType::None};
    }

    state.stable_pressed = kPressed;
    return {button.id, kPressed ? ButtonEventType::Pressed : ButtonEventType::Released};
}

} // namespace

/// @brief Initializes any configured button GPIOs and debounce state.
void init()
{
    const absolute_time_t kNow = get_absolute_time();

    // Initialize any discrete button inputs first so debounce state starts from
    // the real electrical level instead of assuming "not pressed".
    for (size_t i = 0; i < kButtonCount; ++i)
    {
        g_button_states[i] = {false, false, kNow};

        const ButtonConfig& button = kButtons[i];
        if (button.pin < 0)
        {
            continue;
        }

        gpio_init(static_cast<uint>(button.pin));
        gpio_set_dir(static_cast<uint>(button.pin), false);

        if (button.active_low)
        {
            gpio_pull_up(static_cast<uint>(button.pin));
        }
        else
        {
            gpio_pull_down(static_cast<uint>(button.pin));
        }

        const bool kRawLevel = gpio_get(static_cast<uint>(button.pin));
        g_button_states[i].raw_level = kRawLevel;
        g_button_states[i].stable_pressed = button_level_is_pressed(kRawLevel, button);
    }

    // Observed keypad lines are separate from direct buttons; they are all left
    // passive here because probing will actively drive them later as needed.
    for (const ObservedLineConfig& line : kObservedLines)
    {
        if (line.pico_gpio < 0)
        {
            continue;
        }

        gpio_init(static_cast<uint>(line.pico_gpio));
        gpio_set_dir(static_cast<uint>(line.pico_gpio), false);
        if (line.active_low)
        {
            gpio_pull_up(static_cast<uint>(line.pico_gpio));
        }
        else
        {
            gpio_pull_down(static_cast<uint>(line.pico_gpio));
        }
    }

    // Prime the diagnostics snapshot immediately so the status page has useful
    // keypad information even before the first poll loop iteration.
    refresh_keypad_monitor_status();
    log_keypad_probe_results_if_changed();
}

/// @brief Returns the fixed debug label for one logical button.
const char* button_name(ButtonId id)
{
    for (const ButtonConfig& button : kButtons)
    {
        if (button.id == id)
        {
            return button.name;
        }
    }

    return "Unknown";
}

/// @brief Returns the first debounced button edge seen in the current scan.
ButtonEvent poll_buttons()
{
    // Refresh the matrix diagnostics every scan pass so the keypad debug page
    // tracks the live electrical picture, not just debounced button edges.
    refresh_keypad_monitor_status();
    log_keypad_probe_results_if_changed();

    // Return the first debounced edge found so the rest of the UI can keep a
    // simple "one event per loop" input model.
    for (size_t i = 0; i < kButtonCount; ++i)
    {
        ButtonEvent event = poll_button(g_button_states[i], kButtons[i]);
        if (event.type != ButtonEventType::None)
        {
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
    if (event.type == ButtonEventType::None)
    {
        return;
    }

    for (const ButtonConfig& button : kButtons)
    {
        if (button.id == event.id)
        {
            const char* edge = (event.type == ButtonEventType::Pressed) ? "pressed" : "released";
            std::printf("Button %s %s\n", button.name, edge);
            return;
        }
    }
}

} // namespace input
