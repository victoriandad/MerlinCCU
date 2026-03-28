#include "console_controller.h"

#include <cstddef>
#include <cstdio>

namespace console_controller {

namespace {

ConsoleState g_console_state = make_default_console_state();

void update_lamps_from_state()
{
    switch (g_console_state.alert_severity) {
    case AlertSeverity::None:
        g_console_state.lamps[static_cast<size_t>(LampId::AlertLamp)] = LampMode::Off;
        break;
    case AlertSeverity::Message:
        g_console_state.lamps[static_cast<size_t>(LampId::AlertLamp)] = LampMode::FlashSlow;
        break;
    case AlertSeverity::Warning:
    case AlertSeverity::Alert:
        g_console_state.lamps[static_cast<size_t>(LampId::AlertLamp)] = LampMode::FlashFast;
        break;
    }

    g_console_state.lamps[static_cast<size_t>(LampId::TestLamp)] =
        (g_console_state.test_state == SystemTestState::Running) ? LampMode::On : LampMode::Off;

    g_console_state.lamps[static_cast<size_t>(LampId::KeyBacklight)] =
        (g_console_state.key_backlight_brightness == BrightnessLevel::Off) ? LampMode::Off : LampMode::On;

    g_console_state.lamps[static_cast<size_t>(LampId::PanelBacklight)] =
        (g_console_state.panel_brightness == BrightnessLevel::Off) ? LampMode::Off : LampMode::On;
}

BrightnessLevel brighter(BrightnessLevel level)
{
    if (level == BrightnessLevel::High) return BrightnessLevel::High;
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) + 1);
}

BrightnessLevel dimmer(BrightnessLevel level)
{
    if (level == BrightnessLevel::Off) return BrightnessLevel::Off;
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) - 1);
}

}  // namespace

void init()
{
    g_console_state = make_default_console_state();
    update_lamps_from_state();
}

const ConsoleState& state()
{
    return g_console_state;
}

void handle_button_event(const ButtonEvent& event)
{
    if (event.type != ButtonEventType::Pressed) {
        return;
    }

    // This is placeholder behavior until the keypad hardware is mapped.
    // It exists so the eventual key wiring has somewhere sensible to land.
    switch (event.id) {
    case ButtonId::LeftTop:
        g_console_state.alert_severity =
            (g_console_state.alert_severity == AlertSeverity::None)
                ? AlertSeverity::Message
                : AlertSeverity::None;
        break;
    case ButtonId::RightTop:
        g_console_state.test_state =
            (g_console_state.test_state == SystemTestState::Idle)
                ? SystemTestState::Running
                : SystemTestState::Idle;
        break;
    default:
        break;
    }

    update_lamps_from_state();
    std::printf("Console controller received button event\n");
}

}  // namespace console_controller
