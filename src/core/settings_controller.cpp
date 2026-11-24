#include "settings_controller.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <optional>
#include <utility>

#include "config_manager.h"
#include "config_persistence.h"
#include "geo_coordinates.h"
#include "page_table.h"
#include "console_controller.h"
#include "console_controller_internal.h"
#include "softkey_builders.h"
#include "text_styles.h"

namespace settings_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

std::array<std::array<char, 16>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_values =
    {};
std::array<char, 16> g_screen_saver_timeout_selection_text = {};

// Shared with config_manager.cpp's save-time clamp so the two can't drift.
using config_persistence::kMaxScreenSaverTimeoutMinutes;

struct WeatherPeriodDefinition
{
    WeatherPeriod period;
    const char* selection_label;
};

struct SharePeriodDefinition
{
    SharePeriod period;
    const char* selection_label;
};

constexpr std::array<WeatherSourceDefinition, 2> kWeatherSources = {{
    {WeatherSource::HomeAssistant, "Home Assistant", "HOME ASSISTANT"},
    {WeatherSource::OpenMeteo, "Open-Meteo", "OPEN-METEO"},
}};

constexpr std::array<WeatherPeriodDefinition, 3> kWeatherPeriods = {{
    {WeatherPeriod::Hourly, "Hourly"},
    {WeatherPeriod::NextTwentyFourHours, "Next 24 Hours"},
    {WeatherPeriod::NextSevenDays, "Next 7 Days"},
}};

constexpr std::array<SharePeriodDefinition, 5> kSharePeriods = {{
    {SharePeriod::Today, "Today"},
    {SharePeriod::Week, "Week"},
    {SharePeriod::Month, "Month"},
    {SharePeriod::Year, "Year"},
    {SharePeriod::AllTime, "All-time"},
}};

constexpr std::array<TimeZoneDefinition, kTimeZoneSelectionCount> kTimeZones = {{
    {TimeZoneSelection::AtlanticStandard, "Atlantic", "ATLANTIC"},
    {TimeZoneSelection::ArgentinaStandard, "Argentina", "ARGENTINA"},
    {TimeZoneSelection::SouthGeorgia, "South Georgia", "SOUTH GEORGIA"},
    {TimeZoneSelection::Azores, "Azores", "AZORES"},
    {TimeZoneSelection::EuropeLondon, "London", "LONDON"},
    {TimeZoneSelection::CentralEuropean, "Central Europe", "CENTRAL EUROPEAN"},
    {TimeZoneSelection::EasternEuropean, "Eastern Europe", "EASTERN EUROPEAN"},
    {TimeZoneSelection::ArabiaStandard, "Arabia", "ARABIA"},
    {TimeZoneSelection::GulfStandard, "Gulf", "GULF"},
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

/// @brief Returns the static metadata for one selectable weather period label.
const WeatherPeriodDefinition& weather_period_definition(WeatherPeriod period)
{
    for (const WeatherPeriodDefinition& definition : kWeatherPeriods)
    {
        if (definition.period == period)
        {
            return definition;
        }
    }

    return kWeatherPeriods[0];
}

/// @brief Returns the static metadata for one selectable share period.
const SharePeriodDefinition& share_period_definition(SharePeriod period)
{
    for (const SharePeriodDefinition& definition : kSharePeriods)
    {
        if (definition.period == period)
        {
            return definition;
        }
    }

    return kSharePeriods[0];
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

/// @brief Persists one runtime-config mutation and refreshes the menu UI.
/// @details Front-panel settings must write back to the same flash-backed
/// configuration used by the web UI so the two surfaces never diverge.
/// @brief Working copy for persist_runtime_config_change().
/// @details At namespace scope rather than on the 4 KB core-0 stack (#164),
/// and not a function-local static, which would give every template
/// instantiation its own copy. Settings keys are handled from the main loop
/// only, so the copy is never shared.
RuntimeConfig g_settings_edit = {};

template <typename Mutator> bool persist_runtime_config_change(Mutator&& mutator)
{
    RuntimeConfig& settings = g_settings_edit;
    settings = config_manager::settings();
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

} // namespace

const char* enabled_selection_text(bool enabled)
{
    return enabled ? "Enabled" : "Disabled";
}

const char* secret_selection_text(bool present)
{
    return present ? "Stored" : "Not set";
}

const char* device_identity_selection_text()
{
    const RuntimeConfig& settings = config_manager::settings();
    if (settings.device_label[0] != '\0')
    {
        return settings.device_label.data();
    }

    return settings.device_name.data();
}

const char* port_selection_text(SoftKeyId key, uint16_t port)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(key)];
    std::snprintf(buffer.data(), buffer.size(), "%u", static_cast<unsigned>(port));
    return buffer.data();
}

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

const TimeZoneDefinition& time_zone_definition(TimeZoneSelection zone)
{
    return kTimeZones[time_zone_index(zone)];
}

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

const TimeZoneDefinition* time_zone_for_slot(const ConsoleState& console_state, uint8_t slot)
{
    if (slot >= kTimeZoneOptionsPerPage)
    {
        return nullptr;
    }

    const size_t index =
        (static_cast<size_t>(console_state.time_zone_page_index) * kTimeZoneOptionsPerPage) + slot;
    return index < kTimeZones.size() ? &kTimeZones[index] : nullptr;
}

uint8_t time_zone_page_of_current(const ConsoleState& console_state)
{
    return static_cast<uint8_t>(time_zone_index(console_state.time_zone) /
                                kTimeZoneOptionsPerPage);
}

const char* wifi_selection_text(const ConsoleState& console_state)
{
    if (console_state.wifi_status.ssid[0] != '\0')
    {
        return console_state.wifi_status.ssid.data();
    }

    const RuntimeConfig& config = config_manager::settings();
    if (config.wifi_ssid[0] != '\0')
    {
        return config.wifi_ssid.data();
    }

    return console_state.wifi_status.credentials_present ? "Configured" : "Not Set";
}

const char* weather_source_selection_text(const ConsoleState& console_state)
{
    return weather_source_definition(console_state.weather_source).selection_label;
}

const char* weather_period_selection_text(const ConsoleState& console_state)
{
    return weather_period_definition(console_state.weather_period).selection_label;
}

const char* share_period_name(SharePeriod period)
{
    return share_period_definition(period).selection_label;
}

const char* share_period_selection_text(const ConsoleState& console_state)
{
    return share_period_name(console_state.share_period);
}

const char* screen_saver_selection_text(const ConsoleState& console_state)
{
    return screen_saver_definition(console_state.screen_saver_selection).selection_label;
}

const char* time_zone_selection_text(const ConsoleState& console_state)
{
    return time_zone_definition(console_state.time_zone).selection_label;
}

/// @details `0 mins` means the idle-triggered screen saver is disabled. Uses
/// the same `min`/`mins` wording as the entry scratchpad (TXT-6).
const char* screen_saver_timeout_selection_text(const ConsoleState& console_state)
{
    const char* unit = (console_state.screen_saver_timeout_minutes == 1U) ? "min" : "mins";
    std::snprintf(g_screen_saver_timeout_selection_text.data(),
                  g_screen_saver_timeout_selection_text.size(), "%u %s",
                  static_cast<unsigned>(console_state.screen_saver_timeout_minutes), unit);
    return g_screen_saver_timeout_selection_text.data();
}

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

namespace
{
/// @brief Returns the next weather range in the user-facing cycle order.
WeatherPeriod next_weather_period(WeatherPeriod period)
{
    switch (period)
    {
    case WeatherPeriod::Hourly:
        return WeatherPeriod::NextTwentyFourHours;
    case WeatherPeriod::NextTwentyFourHours:
        return WeatherPeriod::NextSevenDays;
    case WeatherPeriod::NextSevenDays:
        return WeatherPeriod::Hourly;
    }

    return WeatherPeriod::Hourly;
}

/// @brief Returns the next share-history period in the user-facing cycle order.
SharePeriod next_share_period(SharePeriod period)
{
    switch (period)
    {
    case SharePeriod::Today:
        return SharePeriod::Week;
    case SharePeriod::Week:
        return SharePeriod::Month;
    case SharePeriod::Month:
        return SharePeriod::Year;
    case SharePeriod::Year:
        return SharePeriod::AllTime;
    case SharePeriod::AllTime:
        return SharePeriod::Today;
    }

    return SharePeriod::Today;
}
} // namespace

bool cycle_weather_period(ConsoleState& console_state)
{
    const WeatherPeriod next = next_weather_period(console_state.weather_period);
    if (next == console_state.weather_period)
    {
        return false;
    }

    console_state.weather_period = next;
    return true;
}

bool cycle_share_period(ConsoleState& console_state)
{
    const SharePeriod next = next_share_period(console_state.share_period);
    if (next == console_state.share_period)
    {
        return false;
    }

    console_state.share_period = next;
    return true;
}

bool select_share_slot(ConsoleState& console_state, uint8_t slot)
{
    if (slot >= console_state.share_count || slot >= console_state.watched_shares.size())
    {
        return false;
    }

    console_state.selected_share_index = slot;
    console_state.active_page = MenuPage::ShareDetail;
    return true;
}

bool select_time_zone_slot(ConsoleState& console_state, uint8_t slot)
{
    const TimeZoneDefinition* target = time_zone_for_slot(console_state, slot);
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

bool toggle_remote_config_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.remote_config_enabled = !settings.remote_config_enabled;
            return true;
        });
}

bool clear_web_pin()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.web_pin_set = false;
            settings.web_pin_salt.fill(0U);
            settings.web_pin_hash.fill(0U);
            return true;
        });
}

bool toggle_home_assistant_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.home_assistant_enabled = !settings.home_assistant_enabled;
            return true;
        });
}

bool toggle_mqtt_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.mqtt_enabled = !settings.mqtt_enabled;
            return true;
        });
}


/// @details Leaves the timeout scratchpad and restores normal page navigation.
bool stop_timeout_editing(ConsoleState& console_state)
{
    if (!console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    console_state.screen_saver_timeout_editing = false;
    console_state.screen_saver_timeout_edit_minutes = console_state.screen_saver_timeout_minutes;
    console_state.screen_saver_timeout_replace_on_next_digit = true;
    return true;
}

bool start_timeout_editing(ConsoleState& console_state)
{
    if (console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    console_state.screen_saver_timeout_editing = true;
    console_state.screen_saver_timeout_edit_minutes = console_state.screen_saver_timeout_minutes;
    console_state.screen_saver_timeout_replace_on_next_digit = true;
    return true;
}

namespace
{
/// @brief Appends or replaces the timeout scratchpad value with one digit.
bool apply_timeout_digit(ConsoleState& console_state, uint8_t digit)
{
    if (!console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const uint16_t kCurrentMinutes = console_state.screen_saver_timeout_replace_on_next_digit
                                         ? 0
                                         : console_state.screen_saver_timeout_edit_minutes;
    const uint16_t kCandidateMinutes = console_state.screen_saver_timeout_replace_on_next_digit
                                           ? digit
                                           : static_cast<uint16_t>((kCurrentMinutes * 10U) + digit);
    if (kCandidateMinutes > kMaxScreenSaverTimeoutMinutes)
    {
        return false;
    }

    const bool kChanged =
        console_state.screen_saver_timeout_edit_minutes != kCandidateMinutes ||
        console_state.screen_saver_timeout_replace_on_next_digit;
    console_state.screen_saver_timeout_edit_minutes = kCandidateMinutes;
    console_state.screen_saver_timeout_replace_on_next_digit = false;
    return kChanged;
}

/// @brief Clears the timeout scratchpad back to the disabled `0 mins` state.
bool clear_timeout_edit(ConsoleState& console_state)
{
    if (!console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const bool kChanged = console_state.screen_saver_timeout_edit_minutes != 0 ||
                          !console_state.screen_saver_timeout_replace_on_next_digit;
    console_state.screen_saver_timeout_edit_minutes = 0;
    console_state.screen_saver_timeout_replace_on_next_digit = true;
    return kChanged;
}
} // namespace

bool handle_timeout_edit_event(ConsoleState& console_state, const ButtonEvent& event)
{
    if (!console_state.screen_saver_timeout_editing || event.type != ButtonEventType::Pressed)
    {
        return false;
    }

    if (event.id == ButtonId::BackStep)
    {
        return stop_timeout_editing(console_state);
    }

    if (event.id == ButtonId::Clr)
    {
        return clear_timeout_edit(console_state);
    }

    uint8_t digit = 0;
    if (!cci::keypad_digit_value(event.id, &digit))
    {
        return false;
    }

    return apply_timeout_digit(console_state, digit);
}

bool confirm_timeout_edit(ConsoleState& console_state)
{
    if (!console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const uint16_t new_minutes = console_state.screen_saver_timeout_edit_minutes;
    if (new_minutes != console_state.screen_saver_timeout_minutes &&
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

    return stop_timeout_editing(console_state);
}

const char* location_selection_text()
{
    // Degrees and decimal minutes, as on the web page (#87); the panel fonts
    // are ASCII only, so a space stands in for the degree sign. One decimal
    // of a minute keeps the value on one 8x12 line (#115).
    constexpr int kPanelMinuteDecimals = 1;
    static std::array<char, 32> text = {};
    text[0] = '\0';
    if (const std::optional<geo_coordinates::GeoPoint> point =
            geo_coordinates::parse(config_manager::settings().weather_coordinates.data()))
    {
        (void)geo_coordinates::format_degrees_minutes(*point, " ", kPanelMinuteDecimals,
                                                      text.data(), text.size());
    }
    return text.data();
}

const char* entity_selection_text(const char* entity_id)
{
    if (entity_id == nullptr)
    {
        return "";
    }
    const char* dot = std::strchr(entity_id, '.');
    return (dot != nullptr && dot[1] != '\0') ? dot + 1 : entity_id;
}

void enter_settings(ConsoleState& console_state)
{
    console_state.settings_page_index = 0U;
}

void enter_time_zone_settings(ConsoleState& console_state)
{
    console_state.time_zone_page_index = time_zone_page_of_current(console_state);
}

void build_weather_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    softkey::set(softkeys, SoftKeyId::Left5,
                 softkey::view(SoftKeyId::Left5, "PERIOD",
                               weather_period_selection_text(console_state),
                               SoftKeyRoute::CycleWeatherPeriod));
}

void build_shares_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // Lists fill L1-L4 then R1-R4, leaving row 5 free (NAV-6).
    const size_t share_count = std::min<size_t>(console_state.share_count, kMaxWatchedShares);
    for (size_t i = 0U; i < share_count; ++i)
    {
        const SoftKeyId slot = softkey::kListSlots[i];
        const ShareWatchEntry& share = console_state.watched_shares[i];
        softkey::set(softkeys, slot,
                     softkey::item(cci::build_selection_softkey_label(
                                       slot, share.display_name.data(), share.price_text.data()),
                                   SoftKeyRoute::SelectShareSlot, static_cast<uint8_t>(i)));
    }
}

void build_share_detail_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    softkey::set(softkeys, SoftKeyId::Left5,
                 softkey::view(SoftKeyId::Left5, "PERIOD",
                               share_period_selection_text(console_state),
                               SoftKeyRoute::CycleSharePeriod));
}

void build_settings_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // Section keys open their sub-page and summarise it; WEATHER SOURCE and
    // TIME ZONE open option pages; REMOTE CONFIG switches in place.
    const RuntimeConfig& config = config_manager::settings();
    if (console_state.settings_page_index == 0U)
    {
        softkey::set(softkeys, SoftKeyId::Left1,
                     softkey::navigate(SoftKeyId::Left1, "DEVICE IDENTITY",
                                       device_identity_selection_text(), MenuPage::DeviceSettings));
        softkey::set(softkeys, SoftKeyId::Left2,
                     softkey::toggle(SoftKeyId::Left2, "REMOTE CONFIG",
                                     config.remote_config_enabled,
                                     SoftKeyRoute::ToggleRemoteConfig));
        softkey::set(softkeys, SoftKeyId::Left3,
                     softkey::navigate(SoftKeyId::Left3, "NETWORK",
                                       wifi_selection_text(console_state), MenuPage::WifiSettings));
        softkey::set(softkeys, SoftKeyId::Left4,
                     softkey::navigate(SoftKeyId::Left4, "HOME ASSISTANT",
                                       enabled_selection_text(config.home_assistant_enabled),
                                       MenuPage::HomeAssistantSettings));
        return;
    }

    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::navigate(SoftKeyId::Left1, "MQTT DISCOVERY",
                                   enabled_selection_text(config.mqtt_enabled),
                                   MenuPage::MqttSettings));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::select(SoftKeyId::Left2, "WEATHER SOURCE",
                                 weather_source_selection_text(console_state),
                                 MenuPage::WeatherSources));
    softkey::set(softkeys, SoftKeyId::Left3,
                 softkey::select(SoftKeyId::Left3, "TIME ZONE",
                                 time_zone_selection_text(console_state),
                                 MenuPage::TimeZoneSettings));
    softkey::set(softkeys, SoftKeyId::Left4,
                 softkey::navigate(SoftKeyId::Left4, "SCREEN SAVER",
                                   screen_saver_selection_text(console_state),
                                   MenuPage::ScreenSaverSettings));
    softkey::set(softkeys, SoftKeyId::Right1,
                 softkey::navigate(SoftKeyId::Right1, "FONTS", fonts_selection_text(),
                                   MenuPage::FontSettings));
}

void build_device_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    const RuntimeConfig& config = config_manager::settings();
    if (console_state.web_pin_clear_confirming)
    {
        // Decision state: only decline (L5) and confirm (R5) (NAV-3).
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::action("CANCEL", SoftKeyRoute::CancelClearWebPin));
        softkey::set(softkeys, SoftKeyId::Right5,
                     softkey::action("CLEAR PIN", SoftKeyRoute::ConfirmClearWebPin));
        return;
    }

    // Set in the web config; shown here for reference.
    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::status(SoftKeyId::Left1, "NAME", config.device_name.data()));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::status(SoftKeyId::Left2, "LABEL", config.device_label.data()));
    softkey::set(softkeys, SoftKeyId::Left3,
                 softkey::status(SoftKeyId::Left3, "LOCATION", config.location.data()));
    softkey::set(softkeys, SoftKeyId::Left4,
                 softkey::status(SoftKeyId::Left4, "ROOM", config.room.data()));
    // The way back in when the web PIN is forgotten (#158): only someone at
    // the unit can press it, and the web keypad needs the PIN itself.
    if (config.web_pin_set)
    {
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::action(SoftKeyId::Left5, "WEB PIN", "Set",
                                     SoftKeyRoute::RequestClearWebPin));
    }
    else
    {
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::status(SoftKeyId::Left5, "WEB PIN", "Off"));
    }
}

void build_wifi_softkeys(ConsoleState& /*console_state*/, SoftKeyMap& softkeys)
{
    const RuntimeConfig& config = config_manager::settings();
    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::status(SoftKeyId::Left1, "SSID", config.wifi_ssid.data()));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::status(SoftKeyId::Left2, "PASSWORD",
                                 secret_selection_text(config.wifi_password[0] != '\0')));
}

void build_home_assistant_softkeys(ConsoleState& /*console_state*/, SoftKeyMap& softkeys)
{
    const RuntimeConfig& config = config_manager::settings();
    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::toggle(SoftKeyId::Left1, "REST API", config.home_assistant_enabled,
                                 SoftKeyRoute::ToggleHomeAssistantEnabled));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::status(SoftKeyId::Left2, "HOST", config.home_assistant_host.data()));
    softkey::set(softkeys, SoftKeyId::Left3,
                 softkey::status(SoftKeyId::Left3, "PORT",
                                 port_selection_text(SoftKeyId::Left3, config.home_assistant_port)));
    softkey::set(softkeys, SoftKeyId::Left4,
                 softkey::status(SoftKeyId::Left4, "TOKEN",
                                 secret_selection_text(config.home_assistant_token[0] != '\0')));
    softkey::set(softkeys, SoftKeyId::Left5,
                 softkey::status(SoftKeyId::Left5, "TRACKED",
                                 entity_selection_text(config.home_assistant_entity_id.data())));
    softkey::set(softkeys, SoftKeyId::Right1,
                 softkey::status(SoftKeyId::Right1, "SELF",
                                 entity_selection_text(config.home_assistant_self_entity_id.data())));
}

void build_mqtt_softkeys(ConsoleState& /*console_state*/, SoftKeyMap& softkeys)
{
    const RuntimeConfig& config = config_manager::settings();
    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::toggle(SoftKeyId::Left1, "MQTT", config.mqtt_enabled,
                                 SoftKeyRoute::ToggleMqttEnabled));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::status(SoftKeyId::Left2, "BROKER", config.mqtt_host.data()));
    softkey::set(softkeys, SoftKeyId::Left3,
                 softkey::status(SoftKeyId::Left3, "PORT",
                                 port_selection_text(SoftKeyId::Left3, config.mqtt_port)));
    softkey::set(softkeys, SoftKeyId::Left4,
                 softkey::status(SoftKeyId::Left4, "USERNAME", config.mqtt_username.data()));
    softkey::set(softkeys, SoftKeyId::Left5,
                 softkey::status(SoftKeyId::Left5, "PASSWORD",
                                 secret_selection_text(config.mqtt_password[0] != '\0')));
    softkey::set(softkeys, SoftKeyId::Right1,
                 softkey::status(SoftKeyId::Right1, "PREFIX",
                                 config.mqtt_discovery_prefix.data()));
    softkey::set(softkeys, SoftKeyId::Right2,
                 softkey::status(SoftKeyId::Right2, "TOPIC", config.mqtt_base_topic.data()));
}

void build_screen_saver_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // Two select keys (SK-7). TIMEOUT opens a numeric entry state on this
    // page: the field being edited stays inverse, CANCEL at L5 declines and
    // ENTER at R5 confirms (NAV-3); every other key is blank (SK-5).
    if (console_state.screen_saver_timeout_editing)
    {
        softkey::set(softkeys, SoftKeyId::Left1,
                     softkey::select_editing(SoftKeyId::Left1, "TIMEOUT",
                                             screen_saver_timeout_selection_text(console_state)));
        softkey::set(softkeys, SoftKeyId::Left5,
                     softkey::action("CANCEL", SoftKeyRoute::CancelScreenSaverTimeout));
        softkey::set(softkeys, SoftKeyId::Right5,
                     softkey::action("ENTER", SoftKeyRoute::ConfirmScreenSaverTimeout));
        return;
    }
    softkey::set(softkeys, SoftKeyId::Left1,
                 softkey::select(SoftKeyId::Left1, "TIMEOUT",
                                 screen_saver_timeout_selection_text(console_state),
                                 SoftKeyRoute::EditScreenSaverTimeout));
    softkey::set(softkeys, SoftKeyId::Left2,
                 softkey::select(SoftKeyId::Left2, "SAVER",
                                 screen_saver_selection_text(console_state),
                                 MenuPage::ScreenSaverOptions));
}

void build_screen_saver_options_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // A fixed list of option keys in L1-L4, R1-R4 with the current saver
    // inverse (SK-7, NAV-6); choosing one takes effect at once.
    constexpr std::array<ScreenSaverSelection, softkey::kListSlots.size()> kOptions = {
        ScreenSaverSelection::Life,   ScreenSaverSelection::Clock, ScreenSaverSelection::Starfield,
        ScreenSaverSelection::Random, ScreenSaverSelection::Matrix, ScreenSaverSelection::Radar,
        ScreenSaverSelection::Rain,   ScreenSaverSelection::Worms};
    for (size_t i = 0U; i < kOptions.size(); ++i)
    {
        softkey::set(softkeys, softkey::kListSlots[i],
                     softkey::option(screen_saver_definition(kOptions[i]).option_label,
                                     SoftKeyRoute::SelectScreenSaver,
                                     static_cast<uint8_t>(kOptions[i]),
                                     console_state.screen_saver_selection == kOptions[i]));
    }
}

void build_weather_sources_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // Option keys for the source, then the selected source's settings from
    // the web config for reference.
    const RuntimeConfig& config = config_manager::settings();
    constexpr std::array<std::pair<SoftKeyId, WeatherSource>, 2> kSources = {
        {{SoftKeyId::Left1, WeatherSource::HomeAssistant},
         {SoftKeyId::Left2, WeatherSource::OpenMeteo}}};
    for (const auto& [key, source] : kSources)
    {
        softkey::set(softkeys, key,
                     softkey::option(weather_source_definition(source).option_label,
                                     SoftKeyRoute::SelectWeatherSource,
                                     static_cast<uint8_t>(source),
                                     console_state.weather_source == source));
    }
    // HA weather arrives over MQTT (#153), so HA needs MQTT set up.
    const bool home_assistant = console_state.weather_source == WeatherSource::HomeAssistant;
    const bool mqtt_set_up = config.mqtt_enabled && config.mqtt_host[0] != '\0';
    softkey::set(softkeys, SoftKeyId::Right1,
                 softkey::status(SoftKeyId::Right1, home_assistant ? "MQTT" : "LOCATION",
                                 home_assistant ? enabled_selection_text(mqtt_set_up)
                                                : location_selection_text()));
}

void build_time_zone_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // A fixed, ordered list of option keys: a zone keeps its key whichever
    // zone is selected, and the selected one is shown inverse (SK-7).
    for (uint8_t slot = 0U; slot < kTimeZoneOptionsPerPage; ++slot)
    {
        const TimeZoneDefinition* zone = time_zone_for_slot(console_state, slot);
        if (zone == nullptr)
        {
            break;
        }
        softkey::set(softkeys, softkey::kListSlots[slot],
                     softkey::option(zone->option_label, SoftKeyRoute::SelectTimeZoneSlot, slot,
                                     zone->zone == console_state.time_zone));
    }
}

// ---------------------------------------------------------------------------
// Fonts (#123)
// ---------------------------------------------------------------------------

static_assert(kFontStyleCount == text_styles::kTextStyleCount,
              "Fonts page entries must match the text styles");
static_assert(kFontChoiceCount == text_styles::kTextStyleCount,
              "RuntimeConfig::font_choices must hold one choice per text style");
static_assert(kMaxFontChoice == text_styles::kFontFaceCount,
              "Stored font choices run from 1 to the number of faces");

namespace
{

/// @brief Option labels, in FontFace order (LBL-2: captions are uppercase).
constexpr std::array<const char*, text_styles::kFontFaceCount> kFontOptionLabels = {
    "5X7", "TITLE 8X12", "8X12", "8X14"};

/// @brief The face a stored choice selects for `style`.
fonts::FontFace face_for_choice(text_styles::TextStyle style, uint8_t choice)
{
    return (choice == 0U || choice > kMaxFontChoice) ? text_styles::default_font(style)
                                                     : static_cast<fonts::FontFace>(choice - 1U);
}

} // namespace

bool apply_font_choices(const RuntimeConfig& settings)
{
    bool changed = false;
    for (size_t i = 0U; i < kFontChoiceCount; ++i)
    {
        const auto style = static_cast<text_styles::TextStyle>(i);
        const fonts::FontFace face = face_for_choice(style, settings.font_choices[i]);
        if (text_styles::font(style) != face)
        {
            text_styles::set_font(style, face);
            changed = true;
        }
    }
    return changed;
}

const char* fonts_selection_text()
{
    for (size_t i = 0U; i < kFontChoiceCount; ++i)
    {
        const auto style = static_cast<text_styles::TextStyle>(i);
        if (text_styles::font(style) != text_styles::default_font(style))
        {
            return "Custom";
        }
    }
    return "Default";
}

void build_font_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // One select key per text style, eight to a page (NAV-6), each showing
    // its current font and opening the four faces as options (SK-7). The
    // last entry puts every style back to its default.
    const size_t first = static_cast<size_t>(console_state.font_page_index) * kFontEntriesPerPage;
    for (uint8_t slot = 0U; slot < kFontEntriesPerPage; ++slot)
    {
        const size_t entry = first + slot;
        const SoftKeyId key = softkey::kListSlots[slot];
        if (entry < kFontStyleCount)
        {
            const auto style = static_cast<text_styles::TextStyle>(entry);
            softkey::set(softkeys, key,
                         softkey::select(key, text_styles::style_caption(style),
                                         text_styles::font_name(text_styles::font(style)),
                                         SoftKeyRoute::SelectFontStyle, slot));
        }
        else if (entry == kFontStyleCount)
        {
            softkey::set(softkeys, key, softkey::action("ALL DEFAULTS", SoftKeyRoute::ResetFonts));
        }
    }
}

void build_font_options_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    const auto style = static_cast<text_styles::TextStyle>(
        std::min<uint8_t>(console_state.font_style_index, kFontStyleCount - 1U));
    const fonts::FontFace current = text_styles::font(style);
    for (uint8_t face = 0U; face < text_styles::kFontFaceCount; ++face)
    {
        softkey::set(softkeys, softkey::kListSlots[face],
                     softkey::option(kFontOptionLabels[face], SoftKeyRoute::SelectFontFace, face,
                                     static_cast<fonts::FontFace>(face) == current));
    }
}

bool select_font_style_slot(ConsoleState& console_state, uint8_t slot)
{
    const size_t entry =
        static_cast<size_t>(console_state.font_page_index) * kFontEntriesPerPage + slot;
    if (slot >= kFontEntriesPerPage || entry >= kFontStyleCount)
    {
        return false;
    }
    console_state.font_style_index = static_cast<uint8_t>(entry);
    return page_table::open(console_state, MenuPage::FontOptions);
}

bool select_font_face(const ConsoleState& console_state, uint8_t face)
{
    if (face >= text_styles::kFontFaceCount || console_state.font_style_index >= kFontStyleCount)
    {
        return false;
    }
    const uint8_t style = console_state.font_style_index;
    return persist_runtime_config_change(
        [style, face](RuntimeConfig& settings)
        {
            settings.font_choices[style] = static_cast<uint8_t>(face + 1U);
            return true;
        });
}

bool reset_font_choices()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.font_choices.fill(0U);
            return true;
        });
}

} // namespace settings_controller
