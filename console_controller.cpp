#include "console_controller.h"

#include <cstddef>
#include <cstdio>

namespace console_controller {

namespace {

ConsoleState g_console_state = make_default_console_state();

constexpr size_t lamp_index(LampId lamp)
{
    return static_cast<size_t>(lamp);
}

void update_softkeys_from_state()
{
    g_console_state.softkeys = {{
        {"ALERT", "cycle_alert", true},
        {"LTRS", "toggle_letters", true},
        {"TEST", "cycle_test", true},
        {"RESET", "reset_console_state", true},
        {"ALRT OFF", "clear_alert_only", g_console_state.alert_severity != AlertSeverity::None},
        {"PANEL +", "panel_brighter", g_console_state.panel_brightness != BrightnessLevel::High},
        {"PANEL -", "panel_dimmer", g_console_state.panel_brightness != BrightnessLevel::Off},
        {"KEYS +", "keys_brighter", g_console_state.key_backlight_brightness != BrightnessLevel::High},
        {"KEYS -", "keys_dimmer", g_console_state.key_backlight_brightness != BrightnessLevel::Off},
        {"TEST IDLE", "test_idle", g_console_state.test_state != SystemTestState::Idle},
    }};
}

AlertSeverity next_alert_severity(AlertSeverity severity)
{
    switch (severity) {
    case AlertSeverity::None:
        return AlertSeverity::Message;
    case AlertSeverity::Message:
        return AlertSeverity::Warning;
    case AlertSeverity::Warning:
        return AlertSeverity::Alert;
    case AlertSeverity::Alert:
        return AlertSeverity::None;
    }

    return AlertSeverity::None;
}

SystemTestState next_test_state(SystemTestState state)
{
    switch (state) {
    case SystemTestState::Idle:
        return SystemTestState::Running;
    case SystemTestState::Running:
        return SystemTestState::Passed;
    case SystemTestState::Passed:
        return SystemTestState::Failed;
    case SystemTestState::Failed:
        return SystemTestState::Idle;
    }

    return SystemTestState::Idle;
}

void update_lamps_from_state()
{
    switch (g_console_state.alert_severity) {
    case AlertSeverity::None:
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::Off;
        break;
    case AlertSeverity::Message:
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashSlow;
        break;
    case AlertSeverity::Warning:
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::On;
        break;
    case AlertSeverity::Alert:
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashFast;
        break;
    }

    switch (g_console_state.test_state) {
    case SystemTestState::Idle:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::Off;
        break;
    case SystemTestState::Running:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::On;
        break;
    case SystemTestState::Passed:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::FlashSlow;
        break;
    case SystemTestState::Failed:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::FlashFast;
        break;
    }

    g_console_state.lamps[lamp_index(LampId::KeyBacklight)] =
        (g_console_state.key_backlight_brightness == BrightnessLevel::Off) ? LampMode::Off : LampMode::On;

    g_console_state.lamps[lamp_index(LampId::PanelBacklight)] =
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
    update_softkeys_from_state();
    update_lamps_from_state();
}

const ConsoleState& state()
{
    return g_console_state;
}

bool set_wifi_status(const WifiStatus& wifi_status)
{
    const bool changed =
        g_console_state.wifi_status.state != wifi_status.state ||
        g_console_state.wifi_status.credentials_present != wifi_status.credentials_present ||
        g_console_state.wifi_status.last_error != wifi_status.last_error ||
        g_console_state.wifi_status.link_status != wifi_status.link_status ||
        g_console_state.wifi_status.ssid != wifi_status.ssid ||
        g_console_state.wifi_status.ip_address != wifi_status.ip_address;

    if (!changed) {
        return false;
    }

    g_console_state.wifi_status = wifi_status;
    update_softkeys_from_state();
    return true;
}

bool handle_button_event(const ButtonEvent& event)
{
    if (event.type != ButtonEventType::Pressed) {
        return false;
    }

    bool changed = true;

    // These button mappings are a development harness that lets the current
    // side-button skeleton exercise the front-panel model before the real
    // keypad matrix and lamp wiring are connected.
    switch (event.id) {
    case ButtonId::LeftTop:
        g_console_state.alert_severity = next_alert_severity(g_console_state.alert_severity);
        break;
    case ButtonId::LeftUpper:
        g_console_state.letter_mode =
            (g_console_state.letter_mode == LetterMode::Off) ? LetterMode::On : LetterMode::Off;
        break;
    case ButtonId::LeftMiddle:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        break;
    case ButtonId::LeftLower:
        g_console_state = make_default_console_state();
        break;
    case ButtonId::LeftBottom:
        g_console_state.alert_severity = AlertSeverity::None;
        break;
    case ButtonId::RightTop:
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        break;
    case ButtonId::RightUpper:
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        break;
    case ButtonId::RightMiddle:
        g_console_state.key_backlight_brightness = brighter(g_console_state.key_backlight_brightness);
        break;
    case ButtonId::RightLower:
        g_console_state.key_backlight_brightness = dimmer(g_console_state.key_backlight_brightness);
        break;
    case ButtonId::RightBottom:
        g_console_state.test_state = SystemTestState::Idle;
        break;
    default:
        changed = false;
        break;
    }

    if (!changed) {
        return false;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    std::printf("Console state updated: ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                (g_console_state.letter_mode == LetterMode::On) ? "on" : "off",
                static_cast<unsigned>(g_console_state.alert_severity),
                static_cast<unsigned>(g_console_state.test_state),
                static_cast<unsigned>(g_console_state.panel_brightness),
                static_cast<unsigned>(g_console_state.key_backlight_brightness));
    return true;
}

}  // namespace console_controller
