#include "console_controller.h"

#include <cstddef>
#include <cstring>
#include <cstdio>

#include "debug_logging.h"

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

void build_active_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                 std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;
    for (const auto& line : keypad_status.lines) {
        if (!line.configured || !line.active) {
            continue;
        }

        const int written = std::snprintf(out_text.data() + used,
                                          out_text.size() - used,
                                          "%s%u",
                                          (used == 0) ? "" : " ",
                                          static_cast<unsigned>(line.panel_pin));
        if (written <= 0) {
            break;
        }

        const size_t write_size = static_cast<size_t>(written);
        if (write_size >= (out_text.size() - used)) {
            used = out_text.size() - 1;
            break;
        }
        used += write_size;
    }
}

void build_probe_hit_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                    std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;
    for (size_t i = 0; i < keypad_status.lines.size(); ++i) {
        if ((keypad_status.probe_hit_mask & (1u << i)) == 0) {
            continue;
        }

        const int written = std::snprintf(out_text.data() + used,
                                          out_text.size() - used,
                                          "%s%u",
                                          (used == 0) ? "" : " ",
                                          static_cast<unsigned>(keypad_status.lines[i].panel_pin));
        if (written <= 0) {
            break;
        }

        const size_t write_size = static_cast<size_t>(written);
        if (write_size >= (out_text.size() - used)) {
            used = out_text.size() - 1;
            break;
        }
        used += write_size;
    }
}

void build_drive_hit_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                    uint8_t drive_panel_pin,
                                    std::array<char, 24>& out_text)
{
    out_text.fill('\0');

    size_t drive_index = keypad_status.lines.size();
    for (size_t i = 0; i < keypad_status.lines.size(); ++i) {
        if (keypad_status.lines[i].panel_pin == drive_panel_pin) {
            drive_index = i;
            break;
        }
    }

    if (drive_index >= keypad_status.lines.size()) {
        return;
    }

    const uint16_t hit_mask = keypad_status.probe_hits_by_drive[drive_index];
    if (hit_mask == 0) {
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < keypad_status.lines.size(); ++i) {
        if ((hit_mask & (1u << i)) == 0) {
            continue;
        }

        const int written = std::snprintf(out_text.data() + used,
                                          out_text.size() - used,
                                          "%s%u",
                                          (used == 0) ? "" : " ",
                                          static_cast<unsigned>(keypad_status.lines[i].panel_pin));
        if (written <= 0) {
            break;
        }

        const size_t write_size = static_cast<size_t>(written);
        if (write_size >= (out_text.size() - used)) {
            used = out_text.size() - 1;
            break;
        }
        used += write_size;
    }
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
        softkeys[softkey_index(SoftKeyId::Right5)] = {"KEYPAD", SoftKeyRoute::GoKeypadDebug, true};
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
    case MenuPage::KeypadDebug:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"HOME", SoftKeyRoute::GoHome, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
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
    case SoftKeyRoute::GoKeypadDebug:
        g_console_state.active_page = MenuPage::KeypadDebug;
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

bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status)
{
    std::array<char, 48> active_panel_pins = {};
    std::array<char, 48> probe_hit_panel_pins = {};
    std::array<char, 24> drive_5_hits = {};
    std::array<char, 24> drive_14_hits = {};
    std::array<char, 24> drive_19_hits = {};
    build_active_panel_pin_text(keypad_status, active_panel_pins);
    build_probe_hit_panel_pin_text(keypad_status, probe_hit_panel_pins);
    build_drive_hit_panel_pin_text(keypad_status, 5, drive_5_hits);
    build_drive_hit_panel_pin_text(keypad_status, 14, drive_14_hits);
    build_drive_hit_panel_pin_text(keypad_status, 19, drive_19_hits);

    const bool changed =
        g_console_state.keypad_debug_status.active_mask != keypad_status.active_mask ||
        g_console_state.keypad_debug_status.configured_count != keypad_status.configured_count ||
        g_console_state.keypad_debug_status.active_count != keypad_status.active_count ||
        g_console_state.keypad_debug_status.active_panel_pins != active_panel_pins ||
        g_console_state.keypad_debug_status.probe_drive_panel_pin != keypad_status.probe_drive_panel_pin ||
        g_console_state.keypad_debug_status.probe_hit_mask != keypad_status.probe_hit_mask ||
        g_console_state.keypad_debug_status.probe_hit_count != keypad_status.probe_hit_count ||
        g_console_state.keypad_debug_status.probe_hit_panel_pins != probe_hit_panel_pins ||
        g_console_state.keypad_debug_status.drive_5_hits != drive_5_hits ||
        g_console_state.keypad_debug_status.drive_14_hits != drive_14_hits ||
        g_console_state.keypad_debug_status.drive_19_hits != drive_19_hits;

    if (!changed) {
        return false;
    }

    g_console_state.keypad_debug_status.active_mask = keypad_status.active_mask;
    g_console_state.keypad_debug_status.configured_count = keypad_status.configured_count;
    g_console_state.keypad_debug_status.active_count = keypad_status.active_count;
    g_console_state.keypad_debug_status.active_panel_pins = active_panel_pins;
    g_console_state.keypad_debug_status.probe_drive_panel_pin = keypad_status.probe_drive_panel_pin;
    g_console_state.keypad_debug_status.probe_hit_mask = keypad_status.probe_hit_mask;
    g_console_state.keypad_debug_status.probe_hit_count = keypad_status.probe_hit_count;
    g_console_state.keypad_debug_status.probe_hit_panel_pins = probe_hit_panel_pins;
    g_console_state.keypad_debug_status.drive_5_hits = drive_5_hits;
    g_console_state.keypad_debug_status.drive_14_hits = drive_14_hits;
    g_console_state.keypad_debug_status.drive_19_hits = drive_19_hits;
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
    if (event.type == ButtonEventType::None) {
        return false;
    }

    bool changed = false;

    const char* event_name = input::button_name(event.id);
    const char* event_type = (event.type == ButtonEventType::Pressed) ? "Pressed" : "Released";
    char button_name[sizeof(g_console_state.keypad_debug_status.last_button_name)] = {};
    std::snprintf(button_name, sizeof(button_name), "%s", event_name);
    char event_type_text[sizeof(g_console_state.keypad_debug_status.last_event_type)] = {};
    std::snprintf(event_type_text, sizeof(event_type_text), "%s", event_type);

    if (std::strcmp(g_console_state.keypad_debug_status.last_button_name.data(), button_name) != 0 ||
        std::strcmp(g_console_state.keypad_debug_status.last_event_type.data(), event_type_text) != 0) {
        std::snprintf(g_console_state.keypad_debug_status.last_button_name.data(),
                      g_console_state.keypad_debug_status.last_button_name.size(),
                      "%s",
                      button_name);
        std::snprintf(g_console_state.keypad_debug_status.last_event_type.data(),
                      g_console_state.keypad_debug_status.last_event_type.size(),
                      "%s",
                      event_type_text);
        changed = true;
    }
    ++g_console_state.keypad_debug_status.event_count;
    changed = true;

    if (event.type != ButtonEventType::Pressed) {
        return changed;
    }

    const SoftKeyId key = softkey_id_from_button(event.id);
    const SoftKeyAction& action = g_console_state.softkeys[softkey_index(key)];
    if (!action.enabled) {
        return changed;
    }

    const bool route_changed = apply_softkey_route(action.route);

    if (!route_changed) {
        return changed;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                 static_cast<unsigned>(g_console_state.active_page),
                 (g_console_state.letter_mode == LetterMode::On) ? "on" : "off",
                 static_cast<unsigned>(g_console_state.alert_severity),
                 static_cast<unsigned>(g_console_state.test_state),
                 static_cast<unsigned>(g_console_state.panel_brightness),
                 static_cast<unsigned>(g_console_state.key_backlight_brightness));
    return true;
}

}  // namespace console_controller
