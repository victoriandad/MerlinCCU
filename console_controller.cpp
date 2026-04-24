#include "console_controller.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "debug_logging.h"

namespace console_controller
{

namespace
{

// The console controller owns the user-facing aggregate state. Subsystem
// managers push snapshots into it, while button events mutate the menu and
// annunciator model from one central place.
ConsoleState g_console_state = make_default_console_state();
bool g_redraw_requested = false;
constexpr size_t kSoftkeyLabelCapacity = 48;
constexpr uint16_t kMaxScreenSaverTimeoutMinutes = 120U;
constexpr uint8_t kSettingsPageCount = 2U;
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)>
    g_dynamic_softkey_labels = {};
std::array<std::array<char, 16>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_values =
    {};
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)>
    g_softkey_label_overrides = {};
std::array<bool, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_override_active = {};
std::array<char, 16> g_screen_saver_timeout_selection_text = {};

void update_softkeys_from_state();

struct WeatherSourceDefinition
{
    WeatherSource source;
    const char* selection_label;
    const char* option_label;
};

struct TimeZoneDefinition
{
    TimeZoneSelection zone;
    const char* selection_label;
    const char* option_label;
};

struct ScreenSaverDefinition
{
    ScreenSaverSelection selection;
    const char* selection_label;
    const char* option_label;
};

constexpr std::array<WeatherSourceDefinition, 3> kWeatherSources = {{
    {WeatherSource::HomeAssistant, "Home Assistant", "HOME ASSISTANT"},
    {WeatherSource::MetOffice, "Met Office", "MET OFFICE"},
    {WeatherSource::BbcWeather, "BBC Weather", "BBC WEATHER"},
}};

constexpr std::array<TimeZoneDefinition, 9> kTimeZones = {{
    {TimeZoneSelection::AtlanticStandard, "Atlantic Standard Time", "ATLANTIC"},
    {TimeZoneSelection::ArgentinaStandard, "Argentina Time", "ARGENTINA"},
    {TimeZoneSelection::SouthGeorgia, "South Georgia Time", "SOUTH GEORGIA"},
    {TimeZoneSelection::Azores, "Azores Time", "AZORES"},
    {TimeZoneSelection::EuropeLondon, "Europe/London", "LONDON"},
    {TimeZoneSelection::CentralEuropean, "Central European Time", "CENTRAL EUROPEAN"},
    {TimeZoneSelection::EasternEuropean, "Eastern European Time", "EASTERN EUROPEAN"},
    {TimeZoneSelection::ArabiaStandard, "Arabia Standard Time", "ARABIA"},
    {TimeZoneSelection::GulfStandard, "Gulf Standard Time", "GULF"},
}};

constexpr std::array<ScreenSaverDefinition, 8> kScreenSavers = {{
    {ScreenSaverSelection::Life, "Life", "LIFE"},
    {ScreenSaverSelection::Clock, "Clock", "CLOCK"},
    {ScreenSaverSelection::Starfield, "Starfield", "STARFIELD"},
    {ScreenSaverSelection::Matrix, "Matrix", "MATRIX"},
    {ScreenSaverSelection::Radar, "Radar", "RADAR"},
    {ScreenSaverSelection::Rain, "Rain", "RAIN"},
    {ScreenSaverSelection::Worms, "Worms", "WORMS"},
    {ScreenSaverSelection::Random, "Random", "RANDOM"},
}};

/// @brief Returns a short on/off label for selection-style softkeys.
const char* enabled_selection_text(bool enabled)
{
    return enabled ? "Enabled" : "Disabled";
}

/// @brief Returns the concise admin-save policy label used by the menu.
const char* admin_requirement_selection_text(bool require_admin_password)
{
    return require_admin_password ? "Required" : "Open";
}

/// @brief Returns whether a saved secret has a non-empty persisted value.
const char* secret_selection_text(bool present)
{
    return present ? "Stored" : "Not set";
}

/// @brief Returns the operator-facing identity label used on the settings menu.
const char* device_identity_selection_text()
{
    const RuntimeConfig& settings = config_manager::settings();
    if (settings.device_label[0] != '\0')
    {
        return settings.device_label.data();
    }

    return settings.device_name.data();
}

/// @brief Formats a numeric port for bracketed softkey labels.
const char* port_selection_text(SoftKeyId key, uint16_t port)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(key)];
    std::snprintf(buffer.data(), buffer.size(), "%u", static_cast<unsigned>(port));
    return buffer.data();
}

/// @brief Moves the top-level settings menu between paged section groups.
bool change_settings_page(int direction)
{
    if (g_console_state.active_page != MenuPage::Settings || direction == 0)
    {
        return false;
    }

    const int current_page = static_cast<int>(g_console_state.settings_page_index);
    const int target_page = current_page + direction;
    if (target_page < 0 || target_page >= static_cast<int>(kSettingsPageCount))
    {
        return false;
    }

    g_console_state.settings_page_index = static_cast<uint8_t>(target_page);
    return true;
}

/// @brief Persists one runtime-config mutation and refreshes the menu UI.
/// @details Front-panel settings must write back to the same flash-backed
/// configuration used by the web UI so the two surfaces never diverge.
template <typename Mutator>
bool persist_runtime_config_change(Mutator&& mutator)
{
    RuntimeConfig settings = config_manager::settings();
    if (!mutator(settings))
    {
        return false;
    }

    if (!config_manager::save(settings))
    {
        return false;
    }

    (void)console_controller::apply_runtime_config(config_manager::settings());
    console_controller::request_redraw();
    return true;
}

/// @brief Returns true when the panel pin is one of the confirmed matrix row pins.
constexpr bool is_keypad_row_pin(uint8_t panel_pin)
{
    return panel_pin >= 5U && panel_pin <= 11U;
}

/// @brief Returns true when the panel pin is one of the confirmed matrix column pins.
constexpr bool is_keypad_column_pin(uint8_t panel_pin)
{
    return panel_pin >= 15U && panel_pin <= 22U;
}

/// @brief Resolves one confirmed keypad matrix closure to its printed key legend.
/// @details The mapping follows the bench-confirmed pairs documented in
/// `README.md`, so the keypad debug page can show real legends like `LTRS`.
const char* keypad_key_legend(uint8_t panel_pin_a, uint8_t panel_pin_b)
{
    uint8_t row_pin = panel_pin_a;
    uint8_t column_pin = panel_pin_b;

    if (is_keypad_column_pin(row_pin) && is_keypad_row_pin(column_pin))
    {
        row_pin = panel_pin_b;
        column_pin = panel_pin_a;
    }

    if (!is_keypad_row_pin(row_pin) || !is_keypad_column_pin(column_pin))
    {
        return nullptr;
    }

    switch (row_pin)
    {
    case 5:
        switch (column_pin)
        {
        case 20:
            return "ALERT";
        case 17:
            return "TEST";
        case 16:
            return "BRT";
        case 15:
            return "DIM";
        }
        break;
    case 6:
        switch (column_pin)
        {
        case 21:
            return "LTRS";
        case 20:
            return "BACK STEP";
        case 19:
            return "LEFT";
        case 18:
            return "RIGHT";
        case 17:
            return "/";
        case 16:
            return "CLR";
        }
        break;
    case 7:
        switch (column_pin)
        {
        case 22:
            return "L1";
        case 21:
            return "A";
        case 20:
            return "B";
        case 19:
            return "C";
        case 18:
            return "D";
        case 17:
            return "E";
        case 16:
            return "F";
        case 15:
            return "R1";
        }
        break;
    case 8:
        switch (column_pin)
        {
        case 22:
            return "L2";
        case 21:
            return "G";
        case 20:
            return "H";
        case 19:
            return "I";
        case 18:
            return "J";
        case 17:
            return "K";
        case 16:
            return "L";
        case 15:
            return "R2";
        }
        break;
    case 9:
        switch (column_pin)
        {
        case 22:
            return "L3";
        case 21:
            return "M";
        case 20:
            return "N";
        case 19:
            return "O";
        case 18:
            return "P";
        case 17:
            return "Q";
        case 16:
            return "R";
        case 15:
            return "R3";
        }
        break;
    case 10:
        switch (column_pin)
        {
        case 22:
            return "L4";
        case 21:
            return "S";
        case 20:
            return "T";
        case 19:
            return "U";
        case 18:
            return "V";
        case 17:
            return "W";
        case 16:
            return "X";
        case 15:
            return "R4";
        }
        break;
    case 11:
        switch (column_pin)
        {
        case 22:
            return "L5";
        case 21:
            return "Y";
        case 20:
            return "Z";
        case 19:
            return "T FUNC";
        case 18:
            return ".";
        case 17:
            return "0";
        case 16:
            return "SPC";
        case 15:
            return "R5";
        }
        break;
    }

    return nullptr;
}

/// @brief Decodes the currently observed matrix closure set into one key legend.
/// @details Symmetric probe hits for the same physical key are collapsed, while
/// multiple simultaneous keys deliberately show `MULTI`.
const char* decoded_pressed_key(const KeypadMonitorStatus& keypad_status)
{
    const char* decoded_key = nullptr;

    for (size_t drive_index = 0; drive_index < keypad_status.lines.size(); ++drive_index)
    {
        const uint8_t drive_panel_pin = keypad_status.lines[drive_index].panel_pin;
        const uint16_t hit_mask = keypad_status.probe_hits_by_drive[drive_index];
        if (hit_mask == 0)
        {
            continue;
        }

        for (size_t sense_index = 0; sense_index < keypad_status.lines.size(); ++sense_index)
        {
            if ((hit_mask & (1U << sense_index)) == 0)
            {
                continue;
            }

            const uint8_t sense_panel_pin = keypad_status.lines[sense_index].panel_pin;
            const char* legend = keypad_key_legend(drive_panel_pin, sense_panel_pin);
            if (legend == nullptr)
            {
                continue;
            }

            if (decoded_key == nullptr)
            {
                decoded_key = legend;
                continue;
            }

            if (std::strcmp(decoded_key, legend) != 0)
            {
                return "MULTI";
            }
        }
    }

    return (decoded_key != nullptr) ? decoded_key : "-";
}

/// @brief Converts a lamp enum into a stable array index.
/// @details The controller stores lamp state in dense arrays so the UI update path can avoid
/// repeated switch statements when it touches annunciator data.
constexpr size_t lamp_index(LampId lamp)
{
    return static_cast<size_t>(lamp);
}

/// @brief Converts a softkey enum into a stable array index.
/// @details Softkey labels, routes, and override state all share enum-ordered arrays so one
/// index helper keeps the mapping explicit and consistent.
constexpr size_t softkey_index(SoftKeyId key)
{
    return static_cast<size_t>(key);
}

/// @brief Returns the static metadata for one selectable weather source.
const WeatherSourceDefinition& weather_source_definition(WeatherSource source)
{
    for (const WeatherSourceDefinition& definition : kWeatherSources)
    {
        if (definition.source == source)
        {
            return definition;
        }
    }

    return kWeatherSources[0];
}

/// @brief Returns the ordered array index for the currently selected time zone.
size_t time_zone_index(TimeZoneSelection zone)
{
    for (size_t i = 0; i < kTimeZones.size(); ++i)
    {
        if (kTimeZones[i].zone == zone)
        {
            return i;
        }
    }

    for (size_t i = 0; i < kTimeZones.size(); ++i)
    {
        if (kTimeZones[i].zone == TimeZoneSelection::EuropeLondon)
        {
            return i;
        }
    }

    return 0;
}

/// @brief Returns the static metadata for one selectable time-zone preset.
const TimeZoneDefinition& time_zone_definition(TimeZoneSelection zone)
{
    return kTimeZones[time_zone_index(zone)];
}

/// @brief Returns the static metadata for one selectable screen saver.
const ScreenSaverDefinition& screen_saver_definition(ScreenSaverSelection selection)
{
    for (const ScreenSaverDefinition& definition : kScreenSavers)
    {
        if (definition.selection == selection)
        {
            return definition;
        }
    }

    return kScreenSavers[0];
}

/// @brief Returns the selectable time zone at a relative offset from the current one.
const TimeZoneDefinition* relative_time_zone_definition(const ConsoleState& console_state, int offset)
{
    const int kCurrentIndex = static_cast<int>(time_zone_index(console_state.time_zone));
    const int kTargetIndex = kCurrentIndex + offset;
    if (kTargetIndex < 0 || kTargetIndex >= static_cast<int>(kTimeZones.size()))
    {
        return nullptr;
    }

    return &kTimeZones[static_cast<size_t>(kTargetIndex)];
}

/// @brief Formats the list of currently active keypad panel pins.
void build_active_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                 std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;

    // These helpers flatten the electrical keypad snapshot into compact text so
    // the debug page can show bench-oriented panel-pin numbers directly.
    for (const auto& line : keypad_status.lines)
    {
        if (!line.configured || !line.active)
        {
            continue;
        }

        const int kWritten =
            std::snprintf(out_text.data() + used, out_text.size() - used, "%s%u",
                          (used == 0) ? "" : " ", static_cast<unsigned>(line.panel_pin));
        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
}

/// @brief Formats the panel-pin list for the current probe hit mask.
void build_probe_hit_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                    std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;
    for (size_t i = 0; i < keypad_status.lines.size(); ++i)
    {
        if ((keypad_status.probe_hit_mask & (1U << i)) == 0)
        {
            continue;
        }

        const int kWritten = std::snprintf(out_text.data() + used, out_text.size() - used, "%s%u",
                                           (used == 0) ? "" : " ",
                                           static_cast<unsigned>(keypad_status.lines[i].panel_pin));
        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
}

/// @brief Applies any temporary softkey label overrides onto a page map.
void apply_softkey_label_overrides(SoftKeyMap& softkeys)
{
    // Overrides are applied last so page defaults remain the single source of
    // truth unless a diagnostics or test flow deliberately replaces a label.
    for (size_t i = 0; i < g_softkey_label_override_active.size(); ++i)
    {
        if (!g_softkey_label_override_active[i])
        {
            continue;
        }

        softkeys[i].label = g_softkey_label_overrides[i].data();
    }
}

/// @brief Returns the configured or connected Wi-Fi name for the settings menu.
const char* wifi_selection_text(const ConsoleState& console_state)
{
    const RuntimeConfig& config = config_manager::settings();
    if (config.wifi_ssid[0] != '\0')
    {
        return config.wifi_ssid.data();
    }

    if (console_state.wifi_status.ssid[0] != '\0')
    {
        return console_state.wifi_status.ssid.data();
    }

    return console_state.wifi_status.credentials_present ? "Configured" : "Not Set";
}

/// @brief Returns the currently selected weather-source label for menu softkeys.
const char* weather_source_selection_text(const ConsoleState& console_state)
{
    return weather_source_definition(console_state.weather_source).selection_label;
}

/// @brief Returns the currently selected screen-saver label for menu softkeys.
const char* screen_saver_selection_text(const ConsoleState& console_state)
{
    return screen_saver_definition(console_state.screen_saver_selection).selection_label;
}

/// @brief Returns the currently selected time-zone label for menu softkeys.
const char* time_zone_selection_text(const ConsoleState& console_state)
{
    return time_zone_definition(console_state.time_zone).selection_label;
}

/// @brief Returns the currently configured screen-saver timeout label.
/// @details `0 minutes` means the idle-triggered screen saver is disabled, and
/// the label uses singular/plural wording that reads naturally on the menu.
const char* screen_saver_timeout_selection_text(const ConsoleState& console_state)
{
    const char* unit =
        (console_state.screen_saver_timeout_minutes == 1U) ? "minute" : "minutes";
    std::snprintf(g_screen_saver_timeout_selection_text.data(),
                  g_screen_saver_timeout_selection_text.size(), "%u %s",
                  static_cast<unsigned>(console_state.screen_saver_timeout_minutes), unit);
    return g_screen_saver_timeout_selection_text.data();
}

/// @brief Formats one two-line softkey label with a square-bracket selection.
/// @details The controller owns these buffers so menu pages can rebuild labels
/// whenever integration state changes without leaving dangling pointers behind.
const char* build_selection_softkey_label(SoftKeyId key, const char* title, const char* selection)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* value = (selection != nullptr && selection[0] != '\0') ? selection : "-";
    std::snprintf(buffer.data(), buffer.size(), "%s\n[%s]", title, value);
    return buffer.data();
}

/// @brief Returns the parent page for one menu route in the current hierarchy.
MenuPage parent_page(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
        return MenuPage::Home;
    case MenuPage::Weather:
    case MenuPage::Status:
    case MenuPage::Settings:
    case MenuPage::Alignment:
        return MenuPage::Home;
    case MenuPage::DeviceSettings:
    case MenuPage::SecuritySettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::ScreenSaverSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
    case MenuPage::KeypadDebug:
        return MenuPage::Settings;
    }

    return MenuPage::Home;
}

/// @brief Moves the active page one level up the menu hierarchy.
bool navigate_up_one_level()
{
    const MenuPage kParentPage = parent_page(g_console_state.active_page);
    if (kParentPage == g_console_state.active_page)
    {
        return false;
    }

    g_console_state.active_page = kParentPage;
    return true;
}

/// @brief Leaves the timeout scratchpad and restores normal page navigation.
bool stop_screen_saver_timeout_editing()
{
    if (!g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    g_console_state.screen_saver_timeout_editing = false;
    g_console_state.screen_saver_timeout_edit_minutes = g_console_state.screen_saver_timeout_minutes;
    g_console_state.screen_saver_timeout_replace_on_next_digit = true;
    return true;
}

/// @brief Enters the timeout scratchpad using the currently saved timeout.
bool start_screen_saver_timeout_editing()
{
    if (g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    g_console_state.screen_saver_timeout_editing = true;
    g_console_state.screen_saver_timeout_edit_minutes = g_console_state.screen_saver_timeout_minutes;
    g_console_state.screen_saver_timeout_replace_on_next_digit = true;
    return true;
}

/// @brief Stores a new screen-saver timeout, clamped to the supported range.
bool set_screen_saver_timeout_minutes(uint16_t minutes)
{
    if (minutes > kMaxScreenSaverTimeoutMinutes)
    {
        return false;
    }

    if (g_console_state.screen_saver_timeout_minutes == minutes)
    {
        return false;
    }

    g_console_state.screen_saver_timeout_minutes = minutes;
    return true;
}

/// @brief Returns true when the button is one of the numeric timeout-editor keys.
bool button_digit_value(ButtonId id, uint8_t* out_digit)
{
    if (out_digit == nullptr)
    {
        return false;
    }

    switch (id)
    {
    case ButtonId::Digit1:
        *out_digit = 1;
        return true;
    case ButtonId::Digit2:
        *out_digit = 2;
        return true;
    case ButtonId::Digit3:
        *out_digit = 3;
        return true;
    case ButtonId::Digit4:
        *out_digit = 4;
        return true;
    case ButtonId::Digit5:
        *out_digit = 5;
        return true;
    case ButtonId::Digit6:
        *out_digit = 6;
        return true;
    case ButtonId::Digit7:
        *out_digit = 7;
        return true;
    case ButtonId::Digit8:
        *out_digit = 8;
        return true;
    case ButtonId::Digit9:
        *out_digit = 9;
        return true;
    case ButtonId::Digit0:
        *out_digit = 0;
        return true;
    case ButtonId::LeftTop:
    case ButtonId::LeftUpper:
    case ButtonId::LeftMiddle:
    case ButtonId::LeftLower:
    case ButtonId::LeftBottom:
    case ButtonId::RightTop:
    case ButtonId::RightUpper:
    case ButtonId::RightMiddle:
    case ButtonId::RightLower:
    case ButtonId::RightBottom:
    case ButtonId::BackStep:
    case ButtonId::CursorLeft:
    case ButtonId::CursorRight:
    case ButtonId::Clr:
        break;
    }

    return false;
}

/// @brief Appends or replaces the timeout scratchpad value with one digit.
bool apply_screen_saver_timeout_digit(uint8_t digit)
{
    if (!g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const uint16_t kCurrentMinutes =
        g_console_state.screen_saver_timeout_replace_on_next_digit
            ? 0
            : g_console_state.screen_saver_timeout_edit_minutes;
    const uint16_t kCandidateMinutes =
        g_console_state.screen_saver_timeout_replace_on_next_digit
            ? digit
            : static_cast<uint16_t>((kCurrentMinutes * 10U) + digit);
    if (kCandidateMinutes > kMaxScreenSaverTimeoutMinutes)
    {
        return false;
    }

    const bool kChanged =
        g_console_state.screen_saver_timeout_edit_minutes != kCandidateMinutes ||
        g_console_state.screen_saver_timeout_replace_on_next_digit;
    g_console_state.screen_saver_timeout_edit_minutes = kCandidateMinutes;
    g_console_state.screen_saver_timeout_replace_on_next_digit = false;
    return kChanged;
}

/// @brief Clears the timeout scratchpad back to the disabled `0 mins` state.
bool clear_screen_saver_timeout_edit()
{
    if (!g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const bool kChanged = g_console_state.screen_saver_timeout_edit_minutes != 0 ||
                          !g_console_state.screen_saver_timeout_replace_on_next_digit;
    g_console_state.screen_saver_timeout_edit_minutes = 0;
    g_console_state.screen_saver_timeout_replace_on_next_digit = true;
    return kChanged;
}

/// @brief Handles digit-only timeout entry while the scratchpad is visible.
bool handle_screen_saver_timeout_edit_event(const ButtonEvent& event)
{
    if (!g_console_state.screen_saver_timeout_editing || event.type != ButtonEventType::Pressed)
    {
        return false;
    }

    if (event.id == ButtonId::BackStep)
    {
        return stop_screen_saver_timeout_editing();
    }

    if (event.id == ButtonId::Clr)
    {
        return clear_screen_saver_timeout_edit();
    }

    uint8_t digit = 0;
    if (!button_digit_value(event.id, &digit))
    {
        return false;
    }

    return apply_screen_saver_timeout_digit(digit);
}

/// @brief Updates the selected weather source when a new provider is chosen.
bool select_weather_source(WeatherSource source)
{
    return persist_runtime_config_change(
        [source](RuntimeConfig& settings)
        {
            if (settings.weather_source == source)
            {
                return false;
            }

            settings.weather_source = source;
            return true;
        });
}

/// @brief Updates the selected time zone by moving relative to the current choice.
bool select_relative_time_zone(int offset)
{
    const TimeZoneDefinition* target = relative_time_zone_definition(g_console_state, offset);
    if (target == nullptr)
    {
        return false;
    }

    return persist_runtime_config_change(
        [target](RuntimeConfig& settings)
        {
            if (settings.time_zone == target->zone)
            {
                return false;
            }

            settings.time_zone = target->zone;
            return true;
        });
}

/// @brief Updates the selected screen saver when the user chooses a new stub.
bool select_screen_saver(ScreenSaverSelection selection)
{
    return persist_runtime_config_change(
        [selection](RuntimeConfig& settings)
        {
            if (settings.screen_saver == selection)
            {
                return false;
            }

            settings.screen_saver = selection;
            return true;
        });
}

/// @brief Toggles whether the local web configuration server may run.
bool toggle_remote_config_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.remote_config_enabled = !settings.remote_config_enabled;
            return true;
        });
}

/// @brief Toggles whether web saves require the admin password.
bool toggle_require_admin_password()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.require_admin_password = !settings.require_admin_password;
            return true;
        });
}

/// @brief Toggles the Home Assistant REST integration enable flag.
bool toggle_home_assistant_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.home_assistant_enabled = !settings.home_assistant_enabled;
            return true;
        });
}

/// @brief Toggles the MQTT discovery integration enable flag.
bool toggle_mqtt_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.mqtt_enabled = !settings.mqtt_enabled;
            return true;
        });
}

/// @brief Persists the scratchpad timeout value when the user presses Enter.
bool confirm_screen_saver_timeout_edit()
{
    if (!g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const uint16_t new_minutes = g_console_state.screen_saver_timeout_edit_minutes;
    if (new_minutes != g_console_state.screen_saver_timeout_minutes &&
        !persist_runtime_config_change(
            [new_minutes](RuntimeConfig& settings)
            {
                if (settings.screen_saver_timeout_minutes == new_minutes)
                {
                    return false;
                }

                settings.screen_saver_timeout_minutes = new_minutes;
                return true;
            }))
    {
        return false;
    }

    return stop_screen_saver_timeout_editing();
}

/// @brief Maps a physical bezel button to its logical softkey slot.
SoftKeyId softkey_id_from_button(ButtonId button)
{
    switch (button)
    {
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
    case ButtonId::CursorLeft:
    case ButtonId::CursorRight:
    default:
        return SoftKeyId::Left1;
    }
}

/// @brief Returns whether the input event belongs to one of the ten softkeys.
bool button_maps_to_softkey(ButtonId button)
{
    switch (button)
    {
    case ButtonId::LeftTop:
    case ButtonId::LeftUpper:
    case ButtonId::LeftMiddle:
    case ButtonId::LeftLower:
    case ButtonId::LeftBottom:
    case ButtonId::RightTop:
    case ButtonId::RightUpper:
    case ButtonId::RightMiddle:
    case ButtonId::RightLower:
    case ButtonId::RightBottom:
        return true;
    case ButtonId::BackStep:
    case ButtonId::CursorLeft:
    case ButtonId::CursorRight:
        return false;
    }

    return false;
}

/// @brief Rebuilds the current softkey map from the active console state.
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

    // Each page declares only the actions that make sense in that context. The
    // top-level menus are intentionally sparse, while deeper pages reserve `R5`
    // as a consistent one-press jump home.
    switch (g_console_state.active_page)
    {
    case MenuPage::Home:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"HOME ASSISTANT", SoftKeyRoute::GoStatus,
                                                     true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "WEATHER",
                                          weather_source_selection_text(g_console_state)),
            SoftKeyRoute::GoWeather,
            true,
        };
        break;
    case MenuPage::Weather:
        break;
    case MenuPage::Status:
        break;
    case MenuPage::Settings:
        if (g_console_state.settings_page_index == 0U)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {
                build_selection_softkey_label(SoftKeyId::Left1, "DEVICE IDENTITY",
                                              device_identity_selection_text()),
                SoftKeyRoute::GoDeviceSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left2)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left2, "SECURITY",
                    admin_requirement_selection_text(
                        config_manager::settings().require_admin_password)),
                SoftKeyRoute::GoSecuritySettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left3)] = {
                build_selection_softkey_label(SoftKeyId::Left3, "NETWORK",
                                              wifi_selection_text(g_console_state)),
                SoftKeyRoute::GoWifiSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left4)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left4, "HOME ASSISTANT",
                    enabled_selection_text(config_manager::settings().home_assistant_enabled)),
                SoftKeyRoute::GoHomeAssistantSettings,
                true,
            };
        }
        else
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left1, "MQTT DISCOVERY",
                    enabled_selection_text(config_manager::settings().mqtt_enabled)),
                SoftKeyRoute::GoMqttSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left2)] = {
                build_selection_softkey_label(SoftKeyId::Left2, "WEATHER SOURCE",
                                              weather_source_selection_text(g_console_state)),
                SoftKeyRoute::GoWeatherSources,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left3)] = {
                build_selection_softkey_label(SoftKeyId::Left3, "DISPLAY & TIME",
                                              time_zone_selection_text(g_console_state)),
                SoftKeyRoute::GoTimeZoneSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left4)] = {
                build_selection_softkey_label(SoftKeyId::Left4, "SCREEN SAVER",
                                              screen_saver_selection_text(g_console_state)),
                SoftKeyRoute::GoScreenSaverSettings,
                true,
            };
        }
        break;
    case MenuPage::DeviceSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "NAME",
                                          config_manager::settings().device_name.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "LABEL",
                                          config_manager::settings().device_label.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(SoftKeyId::Left3, "LOCATION",
                                          config_manager::settings().location.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "ROOM",
                                          config_manager::settings().room.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::SecuritySettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "REMOTE CONFIG",
                enabled_selection_text(config_manager::settings().remote_config_enabled)),
            SoftKeyRoute::ToggleRemoteConfig,
            true,
            config_manager::settings().remote_config_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(
                SoftKeyId::Left2, "SAVE PASSWORD",
                admin_requirement_selection_text(config_manager::settings().require_admin_password)),
            SoftKeyRoute::ToggleRequireAdminPassword,
            true,
            config_manager::settings().require_admin_password,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "ADMIN PW",
                secret_selection_text(config_manager::settings().admin_password[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::WifiSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "SSID",
                                          config_manager::settings().wifi_ssid.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(
                SoftKeyId::Left2, "PASSWORD",
                secret_selection_text(config_manager::settings().wifi_password[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::HomeAssistantSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "REST API",
                enabled_selection_text(config_manager::settings().home_assistant_enabled)),
            SoftKeyRoute::ToggleHomeAssistantEnabled,
            true,
            config_manager::settings().home_assistant_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "HOST",
                                          config_manager::settings().home_assistant_host.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "PORT",
                port_selection_text(SoftKeyId::Left3,
                                    config_manager::settings().home_assistant_port)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(
                SoftKeyId::Left4, "TOKEN",
                secret_selection_text(config_manager::settings().home_assistant_token[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(
                SoftKeyId::Left5, "TRACKED",
                config_manager::settings().home_assistant_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1, "SELF",
                config_manager::settings().home_assistant_self_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::MqttSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "MQTT",
                enabled_selection_text(config_manager::settings().mqtt_enabled)),
            SoftKeyRoute::ToggleMqttEnabled,
            true,
            config_manager::settings().mqtt_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "BROKER",
                                          config_manager::settings().mqtt_host.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "PORT",
                port_selection_text(SoftKeyId::Left3, config_manager::settings().mqtt_port)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "USERNAME",
                                          config_manager::settings().mqtt_username.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(
                SoftKeyId::Left5, "PASSWORD",
                secret_selection_text(config_manager::settings().mqtt_password[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(SoftKeyId::Right1, "PREFIX",
                                          config_manager::settings().mqtt_discovery_prefix.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "TOPIC",
                                          config_manager::settings().mqtt_base_topic.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::ScreenSaverSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "TIMEOUT PERIOD",
                                          screen_saver_timeout_selection_text(g_console_state)),
            SoftKeyRoute::EditScreenSaverTimeout,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_timeout_editing,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            screen_saver_definition(ScreenSaverSelection::Life).option_label,
            SoftKeyRoute::SelectScreenSaverLife,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Life,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            screen_saver_definition(ScreenSaverSelection::Clock).option_label,
            SoftKeyRoute::SelectScreenSaverClock,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Clock,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            screen_saver_definition(ScreenSaverSelection::Starfield).option_label,
            SoftKeyRoute::SelectScreenSaverStarfield,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Starfield,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            screen_saver_definition(ScreenSaverSelection::Random).option_label,
            SoftKeyRoute::SelectScreenSaverRandom,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Random,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            screen_saver_definition(ScreenSaverSelection::Matrix).option_label,
            SoftKeyRoute::SelectScreenSaverMatrix,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Matrix,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            screen_saver_definition(ScreenSaverSelection::Radar).option_label,
            SoftKeyRoute::SelectScreenSaverRadar,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Radar,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            screen_saver_definition(ScreenSaverSelection::Rain).option_label,
            SoftKeyRoute::SelectScreenSaverRain,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Rain,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            screen_saver_definition(ScreenSaverSelection::Worms).option_label,
            SoftKeyRoute::SelectScreenSaverWorms,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Worms,
        };
        if (g_console_state.screen_saver_timeout_editing)
        {
            softkeys[softkey_index(SoftKeyId::Right5)] = {
                "ENTER",
                SoftKeyRoute::ConfirmScreenSaverTimeout,
                true,
            };
        }
        break;
    case MenuPage::WeatherSources:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            weather_source_definition(WeatherSource::HomeAssistant).option_label,
            SoftKeyRoute::SelectWeatherHomeAssistant,
            true,
            g_console_state.weather_source == WeatherSource::HomeAssistant,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            weather_source_definition(WeatherSource::MetOffice).option_label,
            SoftKeyRoute::SelectWeatherMetOffice,
            true,
            g_console_state.weather_source == WeatherSource::MetOffice,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            weather_source_definition(WeatherSource::BbcWeather).option_label,
            SoftKeyRoute::SelectWeatherBbcWeather,
            true,
            g_console_state.weather_source == WeatherSource::BbcWeather,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(SoftKeyId::Right1, "WEATHER",
                                          config_manager::settings().weather_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "SUN",
                                          config_manager::settings().sun_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::TimeZoneSettings:
    {
        const TimeZoneDefinition* west_one = relative_time_zone_definition(g_console_state, -1);
        const TimeZoneDefinition* west_two = relative_time_zone_definition(g_console_state, -2);
        const TimeZoneDefinition* west_three = relative_time_zone_definition(g_console_state, -3);
        const TimeZoneDefinition* west_four = relative_time_zone_definition(g_console_state, -4);
        const TimeZoneDefinition* east_one = relative_time_zone_definition(g_console_state, 1);
        const TimeZoneDefinition* east_two = relative_time_zone_definition(g_console_state, 2);
        const TimeZoneDefinition* east_three = relative_time_zone_definition(g_console_state, 3);
        const TimeZoneDefinition* east_four = relative_time_zone_definition(g_console_state, 4);

        if (west_one != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {west_one->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest1, true};
        }
        if (west_two != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left2)] = {west_two->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest2, true};
        }
        if (west_three != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left3)] = {west_three->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest3, true};
        }
        if (west_four != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left4)] = {west_four->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest4, true};
        }
        if (east_one != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right1)] = {east_one->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast1, true};
        }
        if (east_two != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right2)] = {east_two->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast2, true};
        }
        if (east_three != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right3)] = {
                east_three->option_label,
                SoftKeyRoute::SelectTimeZoneEast3,
                true,
            };
        }
        if (east_four != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right4)] = {east_four->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast4, true};
        }
        break;
    }
    case MenuPage::Alignment:
    case MenuPage::KeypadDebug:
        break;
    }

    if (g_console_state.active_page != MenuPage::Home &&
        !(g_console_state.active_page == MenuPage::ScreenSaverSettings &&
          g_console_state.screen_saver_timeout_editing))
    {
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
    }

    // Any temporary label overrides are layered on after the page defaults so
    // experiments do not need to duplicate the entire softkey map.
    apply_softkey_label_overrides(softkeys);
    g_console_state.softkeys = softkeys;
}

/// @brief Advances the alert annunciator through its test cycle.
AlertSeverity next_alert_severity(AlertSeverity severity)
{
    switch (severity)
    {
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

/// @brief Advances the test annunciator through its demo states.
SystemTestState next_test_state(SystemTestState state)
{
    switch (state)
    {
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

/// @brief Recomputes lamp outputs from the current logical console state.
void update_lamps_from_state()
{
    // Alert and test lamps mirror the current logical state so the front panel
    // behaves like annunciators rather than generic status LEDs.
    switch (g_console_state.alert_severity)
    {
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

    switch (g_console_state.test_state)
    {
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

    // Backlights are modeled as simple on/off lamps for now because the UI only
    // needs to show whether brightness is disabled, not the PWM details.
    g_console_state.lamps[lamp_index(LampId::KeyBacklight)] =
        (g_console_state.key_backlight_brightness == BrightnessLevel::Off) ? LampMode::Off
                                                                           : LampMode::On;

    g_console_state.lamps[lamp_index(LampId::PanelBacklight)] =
        (g_console_state.panel_brightness == BrightnessLevel::Off) ? LampMode::Off : LampMode::On;
}

/// @brief Returns the next brighter backlight level without exceeding the max.
BrightnessLevel brighter(BrightnessLevel level)
{
    if (level == BrightnessLevel::High)
    {
        return BrightnessLevel::High;
    }
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) + 1);
}

/// @brief Returns the next dimmer backlight level without going below off.
BrightnessLevel dimmer(BrightnessLevel level)
{
    if (level == BrightnessLevel::Off)
    {
        return BrightnessLevel::Off;
    }
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) - 1);
}

/// @brief Applies one softkey action to the console state.
bool apply_softkey_route(SoftKeyRoute route)
{
    // Route handling mutates only the console model. Rendering and hardware
    // reactions happen later from the updated shared state.
    switch (route)
    {
    case SoftKeyRoute::None:
        return false;
    case SoftKeyRoute::GoHome:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Home;
        return true;
    case SoftKeyRoute::GoWeather:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Weather;
        return true;
    case SoftKeyRoute::GoStatus:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Status;
        return true;
    case SoftKeyRoute::GoSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Settings;
        g_console_state.settings_page_index = 0;
        return true;
    case SoftKeyRoute::GoDeviceSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::DeviceSettings;
        return true;
    case SoftKeyRoute::GoSecuritySettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::SecuritySettings;
        return true;
    case SoftKeyRoute::GoWifiSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::WifiSettings;
        return true;
    case SoftKeyRoute::GoHomeAssistantSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::HomeAssistantSettings;
        return true;
    case SoftKeyRoute::GoMqttSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::MqttSettings;
        return true;
    case SoftKeyRoute::GoScreenSaverSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::ScreenSaverSettings;
        return true;
    case SoftKeyRoute::EditScreenSaverTimeout:
        return start_screen_saver_timeout_editing();
    case SoftKeyRoute::ConfirmScreenSaverTimeout:
        return confirm_screen_saver_timeout_edit();
    case SoftKeyRoute::GoWeatherSources:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::WeatherSources;
        return true;
    case SoftKeyRoute::GoTimeZoneSettings:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::TimeZoneSettings;
        return true;
    case SoftKeyRoute::GoKeypadDebug:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::KeypadDebug;
        return true;
    case SoftKeyRoute::ToggleRemoteConfig:
        return toggle_remote_config_enabled();
    case SoftKeyRoute::ToggleRequireAdminPassword:
        return toggle_require_admin_password();
    case SoftKeyRoute::ToggleHomeAssistantEnabled:
        return toggle_home_assistant_enabled();
    case SoftKeyRoute::ToggleMqttEnabled:
        return toggle_mqtt_enabled();
    case SoftKeyRoute::SelectScreenSaverLife:
        return select_screen_saver(ScreenSaverSelection::Life);
    case SoftKeyRoute::SelectScreenSaverClock:
        return select_screen_saver(ScreenSaverSelection::Clock);
    case SoftKeyRoute::SelectScreenSaverStarfield:
        return select_screen_saver(ScreenSaverSelection::Starfield);
    case SoftKeyRoute::SelectScreenSaverMatrix:
        return select_screen_saver(ScreenSaverSelection::Matrix);
    case SoftKeyRoute::SelectScreenSaverRadar:
        return select_screen_saver(ScreenSaverSelection::Radar);
    case SoftKeyRoute::SelectScreenSaverRain:
        return select_screen_saver(ScreenSaverSelection::Rain);
    case SoftKeyRoute::SelectScreenSaverWorms:
        return select_screen_saver(ScreenSaverSelection::Worms);
    case SoftKeyRoute::SelectScreenSaverRandom:
        return select_screen_saver(ScreenSaverSelection::Random);
    case SoftKeyRoute::SelectWeatherHomeAssistant:
        return select_weather_source(WeatherSource::HomeAssistant);
    case SoftKeyRoute::SelectWeatherMetOffice:
        return select_weather_source(WeatherSource::MetOffice);
    case SoftKeyRoute::SelectWeatherBbcWeather:
        return select_weather_source(WeatherSource::BbcWeather);
    case SoftKeyRoute::SelectTimeZoneWest1:
        return select_relative_time_zone(-1);
    case SoftKeyRoute::SelectTimeZoneWest2:
        return select_relative_time_zone(-2);
    case SoftKeyRoute::SelectTimeZoneWest3:
        return select_relative_time_zone(-3);
    case SoftKeyRoute::SelectTimeZoneWest4:
        return select_relative_time_zone(-4);
    case SoftKeyRoute::SelectTimeZoneEast1:
        return select_relative_time_zone(1);
    case SoftKeyRoute::SelectTimeZoneEast2:
        return select_relative_time_zone(2);
    case SoftKeyRoute::SelectTimeZoneEast3:
        return select_relative_time_zone(3);
    case SoftKeyRoute::SelectTimeZoneEast4:
        return select_relative_time_zone(4);
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
        if (g_console_state.alert_severity == AlertSeverity::None)
        {
            return false;
        }
        g_console_state.alert_severity = AlertSeverity::None;
        return true;
    case SoftKeyRoute::PanelBrighter:
        if (g_console_state.panel_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::PanelDimmer:
        if (g_console_state.panel_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::KeysBrighter:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.key_backlight_brightness =
            brighter(g_console_state.key_backlight_brightness);
        return true;
    case SoftKeyRoute::KeysDimmer:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.key_backlight_brightness = dimmer(g_console_state.key_backlight_brightness);
        return true;
    }

    return false;
}

} // namespace

/// @brief Initializes the console controller state and derived outputs.
void init()
{
    g_console_state = make_default_console_state();
    g_redraw_requested = false;
    g_softkey_label_override_active.fill(false);

    for (auto& label : g_dynamic_softkey_labels)
    {
        label.fill('\0');
    }

    // Override storage is cleared explicitly so later strcmp checks can safely
    // treat an all-zero buffer as "no custom label".
    for (auto& label : g_softkey_label_overrides)
    {
        label.fill('\0');
    }
    update_softkeys_from_state();
    update_lamps_from_state();
}

const ConsoleState& state()
{
    return g_console_state;
}

/// @brief Marks the menu as needing a redraw on the next main-loop pass.
void request_redraw()
{
    update_softkeys_from_state();
    g_redraw_requested = true;
}

/// @brief Returns and clears the pending redraw flag.
bool consume_redraw_request()
{
    const bool requested = g_redraw_requested;
    g_redraw_requested = false;
    return requested;
}

/// @brief Applies persisted runtime preferences to the visible console state.
bool apply_runtime_config(const RuntimeConfig& settings)
{
    bool changed = false;

    if (g_console_state.weather_source != settings.weather_source)
    {
        g_console_state.weather_source = settings.weather_source;
        changed = true;
    }
    if (g_console_state.time_zone != settings.time_zone)
    {
        g_console_state.time_zone = settings.time_zone;
        changed = true;
    }
    if (g_console_state.screen_saver_selection != settings.screen_saver)
    {
        g_console_state.screen_saver_selection = settings.screen_saver;
        changed = true;
    }
    if (g_console_state.screen_saver_timeout_minutes != settings.screen_saver_timeout_minutes)
    {
        g_console_state.screen_saver_timeout_minutes = settings.screen_saver_timeout_minutes;
        g_console_state.screen_saver_timeout_edit_minutes = settings.screen_saver_timeout_minutes;
        changed = true;
    }

    if (!changed)
    {
        return false;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    return true;
}

/// @brief Updates the cached Wi-Fi snapshot in the console model.
bool set_wifi_status(const WifiStatus& wifi_status)
{
    // These setters short-circuit unchanged snapshots so the UI does not redraw
    // every loop when the subsystem state is stable.
    const bool kChanged =
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

    if (!kChanged)
    {
        return false;
    }

    g_console_state.wifi_status = wifi_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached time snapshot in the console model.
bool set_time_status(const TimeStatus& time_status)
{
    const bool kChanged = g_console_state.time_status.synced != time_status.synced ||
                          g_console_state.time_status.time_text != time_status.time_text;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.time_status = time_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached Home Assistant snapshot in the console model.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status)
{
    const bool kChanged =
        g_console_state.home_assistant_status.state != home_assistant_status.state ||
        g_console_state.home_assistant_status.configured != home_assistant_status.configured ||
        g_console_state.home_assistant_status.self_entity_published !=
            home_assistant_status.self_entity_published ||
        g_console_state.home_assistant_status.last_error != home_assistant_status.last_error ||
        g_console_state.home_assistant_status.last_http_status !=
            home_assistant_status.last_http_status ||
        g_console_state.home_assistant_status.host != home_assistant_status.host ||
        g_console_state.home_assistant_status.tracked_entity_id !=
            home_assistant_status.tracked_entity_id ||
        g_console_state.home_assistant_status.tracked_entity_state !=
            home_assistant_status.tracked_entity_state ||
        g_console_state.home_assistant_status.weather_entity_id !=
            home_assistant_status.weather_entity_id ||
        g_console_state.home_assistant_status.weather_source_hint !=
            home_assistant_status.weather_source_hint ||
        g_console_state.home_assistant_status.weather_condition !=
            home_assistant_status.weather_condition ||
        g_console_state.home_assistant_status.weather_temperature !=
            home_assistant_status.weather_temperature ||
        g_console_state.home_assistant_status.weather_wind_unit !=
            home_assistant_status.weather_wind_unit ||
        g_console_state.home_assistant_status.sunrise_text != home_assistant_status.sunrise_text ||
        g_console_state.home_assistant_status.sunset_text != home_assistant_status.sunset_text ||
        g_console_state.home_assistant_status.weather_forecast_count !=
            home_assistant_status.weather_forecast_count ||
        g_console_state.home_assistant_status.weather_forecast !=
            home_assistant_status.weather_forecast ||
        g_console_state.home_assistant_status.self_entity_id !=
            home_assistant_status.self_entity_id;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.home_assistant_status = home_assistant_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached MQTT snapshot in the console model.
bool set_mqtt_status(const MqttStatus& mqtt_status)
{
    const bool kChanged =
        g_console_state.mqtt_status.state != mqtt_status.state ||
        g_console_state.mqtt_status.configured != mqtt_status.configured ||
        g_console_state.mqtt_status.discovery_published != mqtt_status.discovery_published ||
        g_console_state.mqtt_status.last_error != mqtt_status.last_error ||
        g_console_state.mqtt_status.broker != mqtt_status.broker ||
        g_console_state.mqtt_status.device_id != mqtt_status.device_id;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.mqtt_status = mqtt_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the keypad diagnostics snapshot shown by the UI.
bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status)
{
    std::array<char, 48> active_panel_pins = {};
    std::array<char, 48> probe_hit_panel_pins = {};
    std::array<char, 24> pressed_key_name = {};

    // Build the display strings up front so the change detection compares the
    // exact text the diagnostics page will eventually render.
    build_active_panel_pin_text(keypad_status, active_panel_pins);
    build_probe_hit_panel_pin_text(keypad_status, probe_hit_panel_pins);
    std::snprintf(pressed_key_name.data(), pressed_key_name.size(), "%s",
                  decoded_pressed_key(keypad_status));

    const bool kChanged =
        g_console_state.keypad_debug_status.active_mask != keypad_status.active_mask ||
        g_console_state.keypad_debug_status.configured_count != keypad_status.configured_count ||
        g_console_state.keypad_debug_status.active_count != keypad_status.active_count ||
        g_console_state.keypad_debug_status.pressed_key_name != pressed_key_name ||
        g_console_state.keypad_debug_status.active_panel_pins != active_panel_pins ||
        g_console_state.keypad_debug_status.probe_drive_panel_pin !=
            keypad_status.probe_drive_panel_pin ||
        g_console_state.keypad_debug_status.probe_hit_mask != keypad_status.probe_hit_mask ||
        g_console_state.keypad_debug_status.probe_hit_count != keypad_status.probe_hit_count ||
        g_console_state.keypad_debug_status.probe_hit_panel_pins != probe_hit_panel_pins;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.keypad_debug_status.active_mask = keypad_status.active_mask;
    g_console_state.keypad_debug_status.configured_count = keypad_status.configured_count;
    g_console_state.keypad_debug_status.active_count = keypad_status.active_count;
    g_console_state.keypad_debug_status.pressed_key_name = pressed_key_name;
    g_console_state.keypad_debug_status.active_panel_pins = active_panel_pins;
    g_console_state.keypad_debug_status.probe_drive_panel_pin = keypad_status.probe_drive_panel_pin;
    g_console_state.keypad_debug_status.probe_hit_mask = keypad_status.probe_hit_mask;
    g_console_state.keypad_debug_status.probe_hit_count = keypad_status.probe_hit_count;
    g_console_state.keypad_debug_status.probe_hit_panel_pins = probe_hit_panel_pins;
    update_softkeys_from_state();
    return true;
}

/// @brief Applies or clears a temporary label override for one softkey.
bool set_softkey_label(SoftKeyId key, const char* label)
{
    const size_t kIndex = softkey_index(key);
    const bool kClearOverride = (label == nullptr) || (label[0] == '\0');

    // Clearing the override falls back to the page-defined label instead of
    // keeping an empty string that would mask the underlying softkey action.
    if (kClearOverride)
    {
        if (!g_softkey_label_override_active[kIndex] &&
            g_softkey_label_overrides[kIndex][0] == '\0')
        {
            return false;
        }

        g_softkey_label_override_active[kIndex] = false;
        g_softkey_label_overrides[kIndex][0] = '\0';
        update_softkeys_from_state();
        return true;
    }

    char copied_label[kSoftkeyLabelCapacity] = {};
    std::snprintf(copied_label, sizeof(copied_label), "%s", label);

    const bool kChanged = !g_softkey_label_override_active[kIndex] ||
                          std::strcmp(g_softkey_label_overrides[kIndex].data(), copied_label) != 0;
    if (!kChanged)
    {
        return false;
    }

    std::snprintf(g_softkey_label_overrides[kIndex].data(),
                  g_softkey_label_overrides[kIndex].size(), "%s", copied_label);
    g_softkey_label_override_active[kIndex] = true;
    update_softkeys_from_state();
    return true;
}

/// @brief Records one button event and applies any enabled softkey action.
bool handle_button_event(const ButtonEvent& event)
{
    if (event.type == ButtonEventType::None)
    {
        return false;
    }

    // Only press events drive menu navigation. The keypad debug page now shows
    // the live matrix decode directly, so release edges are ignored here.
    if (event.type != ButtonEventType::Pressed)
    {
        return false;
    }

    if (g_console_state.screen_saver_timeout_editing)
    {
        const bool kEditChanged = handle_screen_saver_timeout_edit_event(event);
        if (kEditChanged)
        {
            update_softkeys_from_state();
            update_lamps_from_state();
            PERIODIC_LOG("Console state updated: page=%u timeout=%u edit=%s\n",
                         static_cast<unsigned>(g_console_state.active_page),
                         static_cast<unsigned>(g_console_state.screen_saver_timeout_minutes),
                         g_console_state.screen_saver_timeout_editing ? "on" : "off");
            return true;
        }

        if (!button_maps_to_softkey(event.id))
        {
            return false;
        }
    }

    if (event.id == ButtonId::BackStep)
    {
        const bool kRouteChanged = navigate_up_one_level();
        if (!kRouteChanged)
        {
            return false;
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

    if (event.id == ButtonId::CursorLeft || event.id == ButtonId::CursorRight)
    {
        const int direction = (event.id == ButtonId::CursorLeft) ? -1 : 1;
        const bool kPageChanged = change_settings_page(direction);
        if (!kPageChanged)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: settings page=%u/%u\n",
                     static_cast<unsigned>(g_console_state.settings_page_index + 1U),
                     static_cast<unsigned>(kSettingsPageCount));
        return true;
    }

    if (!button_maps_to_softkey(event.id))
    {
        return false;
    }

    const SoftKeyId kEy = softkey_id_from_button(event.id);
    const SoftKeyAction& action = g_console_state.softkeys[softkey_index(kEy)];
    if (!action.enabled)
    {
        return false;
    }

    // The route mutates the logical console state first, then softkeys and lamp
    // outputs are recomputed from that new state as a separate step.
    const bool kRouteChanged = apply_softkey_route(action.route);

    if (!kRouteChanged)
    {
        return false;
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

} // namespace console_controller
