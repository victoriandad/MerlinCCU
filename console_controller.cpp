#include "console_controller.h"

#include <cstddef>
#include <cstring>
#include <cstdio>

namespace console_controller {

namespace {

ConsoleState g_console_state = make_default_console_state();
constexpr size_t kSoftkeyLabelCapacity = 48;
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_overrides = {};
std::array<bool, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_override_active = {};

constexpr size_t lamp_index(LampId lamp)
{
    return static_cast<size_t>(lamp);
}

constexpr size_t softkey_index(SoftKeyId key)
{
    return static_cast<size_t>(key);
}

void apply_softkey_label_overrides(SoftKeyMap& softkeys)
{
    for (size_t i = 0; i < g_softkey_label_override_active.size(); ++i) {
        if (!g_softkey_label_override_active[i]) {
            continue;
        }

        softkeys[i].label = g_softkey_label_overrides[i].data();
    }
}

SoftKeyId softkey_id_from_button(ButtonId button)
{
    switch (button) {
    case ButtonId::LeftTop:
        return SoftKeyId::Left1;
    case ButtonId::LeftUpper:
        return SoftKeyId::Left2;
    case ButtonId::LeftMiddle:
        return SoftKeyId::Left3;
    case ButtonId::LeftLower:
        return SoftKeyId::Left4;
    case ButtonId::LeftBottom:
        return SoftKeyId::Left5;
    case ButtonId::RightTop:
        return SoftKeyId::Right1;
    case ButtonId::RightUpper:
        return SoftKeyId::Right2;
    case ButtonId::RightMiddle:
        return SoftKeyId::Right3;
    case ButtonId::RightLower:
        return SoftKeyId::Right4;
    case ButtonId::RightBottom:
        return SoftKeyId::Right5;
    default:
        return SoftKeyId::Left1;
    }
}

void update_softkeys_from_state()
{
    SoftKeyMap softkeys = {{
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
    }};

    switch (g_console_state.active_page) {
    case MenuPage::Home:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"ALERT", SoftKeyRoute::CycleAlert, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {"LTRS", SoftKeyRoute::ToggleLetters, true};
        softkeys[softkey_index(SoftKeyId::Right3)] = {"TEST", SoftKeyRoute::CycleTest, true};
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            "PANEL +", SoftKeyRoute::PanelBrighter, g_console_state.panel_brightness != BrightnessLevel::High};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"SELECT SOURCE", SoftKeyRoute::GoWeatherSources, true};
        break;
    case MenuPage::Status:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"HOME", SoftKeyRoute::GoHome, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"CLR ALRT", SoftKeyRoute::ClearAlert,
                                                      g_console_state.alert_severity != AlertSeverity::None};
        break;
    case MenuPage::Settings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"HOME", SoftKeyRoute::GoHome, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"RESET", SoftKeyRoute::ResetConsoleState, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            "KEYS +", SoftKeyRoute::KeysBrighter, g_console_state.key_backlight_brightness != BrightnessLevel::High};
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            "KEYS -", SoftKeyRoute::KeysDimmer, g_console_state.key_backlight_brightness != BrightnessLevel::Off};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"ALERT", SoftKeyRoute::CycleAlert, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {"LTRS", SoftKeyRoute::ToggleLetters, true};
        softkeys[softkey_index(SoftKeyId::Right3)] = {"TEST", SoftKeyRoute::CycleTest, true};
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            "PANEL +", SoftKeyRoute::PanelBrighter, g_console_state.panel_brightness != BrightnessLevel::High};
        softkeys[softkey_index(SoftKeyId::Right5)] = {
            "PANEL -", SoftKeyRoute::PanelDimmer, g_console_state.panel_brightness != BrightnessLevel::Off};
        break;
    case MenuPage::WeatherSources:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"HOME", SoftKeyRoute::GoHome, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"SELECT", SoftKeyRoute::None, false};
        break;
    case MenuPage::Alignment:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"Short", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"Status page", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"Left softkey label", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {"1234 / 5678", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Left5)] = {"Reset panel state", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"Alert", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {"Panel +", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Right3)] = {"Two line wrap", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Right4)] = {"Tracked entity", SoftKeyRoute::None, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"Test / Dim mode", SoftKeyRoute::None, true};
        break;
    }

    apply_softkey_label_overrides(softkeys);
    g_console_state.softkeys = softkeys;
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

bool apply_softkey_route(SoftKeyRoute route)
{
    switch (route) {
    case SoftKeyRoute::None:
        return false;
    case SoftKeyRoute::GoHome:
        g_console_state.active_page = MenuPage::Home;
        return true;
    case SoftKeyRoute::GoStatus:
        g_console_state.active_page = MenuPage::Status;
        return true;
    case SoftKeyRoute::GoSettings:
        g_console_state.active_page = MenuPage::Settings;
        return true;
    case SoftKeyRoute::GoWeatherSources:
        g_console_state.active_page = MenuPage::WeatherSources;
        return true;
    case SoftKeyRoute::CycleAlert:
        g_console_state.alert_severity = next_alert_severity(g_console_state.alert_severity);
        return true;
    case SoftKeyRoute::ToggleLetters:
        g_console_state.letter_mode =
            (g_console_state.letter_mode == LetterMode::Off) ? LetterMode::On : LetterMode::Off;
        return true;
    case SoftKeyRoute::CycleTest:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        return true;
    case SoftKeyRoute::ResetConsoleState:
        g_console_state = make_default_console_state();
        return true;
    case SoftKeyRoute::ClearAlert:
        if (g_console_state.alert_severity == AlertSeverity::None) {
            return false;
        }
        g_console_state.alert_severity = AlertSeverity::None;
        return true;
    case SoftKeyRoute::PanelBrighter:
        if (g_console_state.panel_brightness == BrightnessLevel::High) {
            return false;
        }
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::PanelDimmer:
        if (g_console_state.panel_brightness == BrightnessLevel::Off) {
            return false;
        }
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::KeysBrighter:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::High) {
            return false;
        }
        g_console_state.key_backlight_brightness = brighter(g_console_state.key_backlight_brightness);
        return true;
    case SoftKeyRoute::KeysDimmer:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::Off) {
            return false;
        }
        g_console_state.key_backlight_brightness = dimmer(g_console_state.key_backlight_brightness);
        return true;
    }

    return false;
}

}  // namespace

void init()
{
    g_console_state = make_default_console_state();
    g_softkey_label_override_active.fill(false);
    for (auto& label : g_softkey_label_overrides) {
        label.fill('\0');
    }
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
        g_console_state.wifi_status.internet_reachable != wifi_status.internet_reachable ||
        g_console_state.wifi_status.internet_probe_pending != wifi_status.internet_probe_pending ||
        g_console_state.wifi_status.last_error != wifi_status.last_error ||
        g_console_state.wifi_status.link_status != wifi_status.link_status ||
        g_console_state.wifi_status.internet_rtt_ms != wifi_status.internet_rtt_ms ||
        g_console_state.wifi_status.auth_mode != wifi_status.auth_mode ||
        g_console_state.wifi_status.mac_address != wifi_status.mac_address ||
        g_console_state.wifi_status.ssid != wifi_status.ssid ||
        g_console_state.wifi_status.ip_address != wifi_status.ip_address;

    if (!changed) {
        return false;
    }

    g_console_state.wifi_status = wifi_status;
    update_softkeys_from_state();
    return true;
}

bool set_time_status(const TimeStatus& time_status)
{
    const bool changed = g_console_state.time_status.synced != time_status.synced ||
                         g_console_state.time_status.time_text != time_status.time_text;

    if (!changed) {
        return false;
    }

    g_console_state.time_status = time_status;
    update_softkeys_from_state();
    return true;
}

bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status)
{
    const bool changed =
        g_console_state.home_assistant_status.state != home_assistant_status.state ||
        g_console_state.home_assistant_status.configured != home_assistant_status.configured ||
        g_console_state.home_assistant_status.self_entity_published != home_assistant_status.self_entity_published ||
        g_console_state.home_assistant_status.last_error != home_assistant_status.last_error ||
        g_console_state.home_assistant_status.last_http_status != home_assistant_status.last_http_status ||
        g_console_state.home_assistant_status.host != home_assistant_status.host ||
        g_console_state.home_assistant_status.tracked_entity_id != home_assistant_status.tracked_entity_id ||
        g_console_state.home_assistant_status.tracked_entity_state != home_assistant_status.tracked_entity_state ||
        g_console_state.home_assistant_status.weather_entity_id != home_assistant_status.weather_entity_id ||
        g_console_state.home_assistant_status.weather_source_hint != home_assistant_status.weather_source_hint ||
        g_console_state.home_assistant_status.weather_condition != home_assistant_status.weather_condition ||
        g_console_state.home_assistant_status.weather_temperature != home_assistant_status.weather_temperature ||
        g_console_state.home_assistant_status.weather_wind_unit != home_assistant_status.weather_wind_unit ||
        g_console_state.home_assistant_status.sunrise_text != home_assistant_status.sunrise_text ||
        g_console_state.home_assistant_status.sunset_text != home_assistant_status.sunset_text ||
        g_console_state.home_assistant_status.weather_forecast_count != home_assistant_status.weather_forecast_count ||
        g_console_state.home_assistant_status.weather_forecast != home_assistant_status.weather_forecast ||
        g_console_state.home_assistant_status.self_entity_id != home_assistant_status.self_entity_id;

    if (!changed) {
        return false;
    }

    g_console_state.home_assistant_status = home_assistant_status;
    update_softkeys_from_state();
    return true;
}

bool set_mqtt_status(const MqttStatus& mqtt_status)
{
    const bool changed =
        g_console_state.mqtt_status.state != mqtt_status.state ||
        g_console_state.mqtt_status.configured != mqtt_status.configured ||
        g_console_state.mqtt_status.discovery_published != mqtt_status.discovery_published ||
        g_console_state.mqtt_status.last_error != mqtt_status.last_error ||
        g_console_state.mqtt_status.broker != mqtt_status.broker ||
        g_console_state.mqtt_status.device_id != mqtt_status.device_id;

    if (!changed) {
        return false;
    }

    g_console_state.mqtt_status = mqtt_status;
    update_softkeys_from_state();
    return true;
}

bool set_softkey_label(SoftKeyId key, const char* label)
{
    const size_t index = softkey_index(key);
    const bool clear_override = (label == nullptr) || (label[0] == '\0');

    if (clear_override) {
        if (!g_softkey_label_override_active[index] && g_softkey_label_overrides[index][0] == '\0') {
            return false;
        }

        g_softkey_label_override_active[index] = false;
        g_softkey_label_overrides[index][0] = '\0';
        update_softkeys_from_state();
        return true;
    }

    char copied_label[kSoftkeyLabelCapacity] = {};
    std::snprintf(copied_label, sizeof(copied_label), "%s", label);

    const bool changed = !g_softkey_label_override_active[index] ||
                         std::strcmp(g_softkey_label_overrides[index].data(), copied_label) != 0;
    if (!changed) {
        return false;
    }

    std::snprintf(g_softkey_label_overrides[index].data(), g_softkey_label_overrides[index].size(), "%s", copied_label);
    g_softkey_label_override_active[index] = true;
    update_softkeys_from_state();
    return true;
}

bool handle_button_event(const ButtonEvent& event)
{
    if (event.type != ButtonEventType::Pressed) {
        return false;
    }

    const SoftKeyId key = softkey_id_from_button(event.id);
    const SoftKeyAction& action = g_console_state.softkeys[softkey_index(key)];
    if (!action.enabled) {
        return false;
    }

    const bool changed = apply_softkey_route(action.route);

    if (!changed) {
        return false;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    std::printf("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                static_cast<unsigned>(g_console_state.active_page),
                (g_console_state.letter_mode == LetterMode::On) ? "on" : "off",
                static_cast<unsigned>(g_console_state.alert_severity),
                static_cast<unsigned>(g_console_state.test_state),
                static_cast<unsigned>(g_console_state.panel_brightness),
                static_cast<unsigned>(g_console_state.key_backlight_brightness));
    return true;
}

}  // namespace console_controller
