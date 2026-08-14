#include "settings_controller.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "config_manager.h"
#include "config_persistence.h"
#include "console_controller.h"
#include "console_controller_internal.h"

namespace settings_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

// pico/error.h's PICO_ERROR_NONE is always 0; kept as a local constant
// (rather than including the Pico-SDK header) so this file stays
// host-testable -- see issue #78.
constexpr int kNoErrorCode = 0;

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
template <typename Mutator> bool persist_runtime_config_change(Mutator&& mutator)
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

const char* radius_nm_selection_text(SoftKeyId key, uint16_t radius_nm)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(key)];
    std::snprintf(buffer.data(), buffer.size(), "%unm", static_cast<unsigned>(radius_nm));
    return buffer.data();
}

bool change_page(ConsoleState& console_state, int direction)
{
    if (console_state.active_page != MenuPage::Settings || direction == 0)
    {
        return false;
    }

    const int current_page = static_cast<int>(console_state.settings_page_index);
    const int target_page = current_page + direction;
    if (target_page < 0 || target_page >= static_cast<int>(kSettingsPageCount))
    {
        return false;
    }

    console_state.settings_page_index = static_cast<uint8_t>(target_page);
    return true;
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

const TimeZoneDefinition* relative_time_zone_definition(const ConsoleState& console_state,
                                                        int offset)
{
    const int kCurrentIndex = static_cast<int>(time_zone_index(console_state.time_zone));
    const int kTargetIndex = kCurrentIndex + offset;
    if (kTargetIndex < 0 || kTargetIndex >= static_cast<int>(kTimeZones.size()))
    {
        return nullptr;
    }

    return &kTimeZones[static_cast<size_t>(kTargetIndex)];
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

const char* share_period_selection_text(const ConsoleState& console_state)
{
    return share_period_definition(console_state.share_period).selection_label;
}

const char* air_traffic_view_mode_selection_text(const ConsoleState& console_state)
{
    return console_state.air_traffic_view_mode == AirTrafficViewMode::Tabular ? "Tabular" : "Plot";
}

const char* local_temperature_selection_text(const ConsoleState& console_state)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(SoftKeyId::Left1)];
    const auto& status = console_state.environment_sensor_status;
    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(buffer.data(), buffer.size(), "-");
        return buffer.data();
    }

    const int32_t raw_value = status.bme_temperature_centi_celsius;
    const bool negative = raw_value < 0;
    const uint32_t absolute_value =
        static_cast<uint32_t>(negative ? -static_cast<int64_t>(raw_value) : raw_value);
    std::snprintf(buffer.data(), buffer.size(), "%s%lu.%02luC", negative ? "-" : "",
                  static_cast<unsigned long>(absolute_value / 100U),
                  static_cast<unsigned long>(absolute_value % 100U));
    return buffer.data();
}

const char* local_humidity_selection_text(const ConsoleState& console_state)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(SoftKeyId::Left2)];
    const auto& status = console_state.environment_sensor_status;
    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(buffer.data(), buffer.size(), "-");
        return buffer.data();
    }

    const uint32_t tenths_percent = (status.bme_humidity_milli_percent + 50U) / 100U;
    std::snprintf(buffer.data(), buffer.size(), "%lu.%lu%%",
                  static_cast<unsigned long>(tenths_percent / 10U),
                  static_cast<unsigned long>(tenths_percent % 10U));
    return buffer.data();
}

const char* local_pressure_selection_text(const ConsoleState& console_state)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(SoftKeyId::Left3)];
    const auto& status = console_state.environment_sensor_status;
    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(buffer.data(), buffer.size(), "-");
        return buffer.data();
    }

    const uint32_t tenths_hpa = (status.bme_pressure_pa + 5U) / 10U;
    std::snprintf(buffer.data(), buffer.size(), "%lu.%luhPa",
                  static_cast<unsigned long>(tenths_hpa / 10U),
                  static_cast<unsigned long>(tenths_hpa % 10U));
    return buffer.data();
}

const char* local_air_quality_selection_text(const ConsoleState& console_state)
{
    auto& buffer = g_dynamic_softkey_values[static_cast<size_t>(SoftKeyId::Left4)];
    const auto& status = console_state.environment_sensor_status;
    if (!status.enabled || !status.air_quality_score_valid)
    {
        std::snprintf(buffer.data(), buffer.size(), "%s",
                      status.air_quality_read_error == kNoErrorCode ? "-" : "ERR");
        return buffer.data();
    }

    std::snprintf(buffer.data(), buffer.size(), "%s",
                  environment_sensor_manager::air_quality_band_text(status.air_quality_score));
    return buffer.data();
}

const char* screen_saver_selection_text(const ConsoleState& console_state)
{
    return screen_saver_definition(console_state.screen_saver_selection).selection_label;
}

const char* time_zone_selection_text(const ConsoleState& console_state)
{
    return time_zone_definition(console_state.time_zone).selection_label;
}

/// @details `0 minutes` means the idle-triggered screen saver is disabled, and
/// the label uses singular/plural wording that reads naturally on the menu.
const char* screen_saver_timeout_selection_text(const ConsoleState& console_state)
{
    const char* unit = (console_state.screen_saver_timeout_minutes == 1U) ? "minute" : "minutes";
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

bool toggle_air_traffic_view_mode(ConsoleState& console_state)
{
    console_state.air_traffic_view_mode =
        (console_state.air_traffic_view_mode == AirTrafficViewMode::Tabular)
            ? AirTrafficViewMode::Plot
            : AirTrafficViewMode::Tabular;
    console_state.air_traffic_page_index = 0U;
    return true;
}

uint8_t air_traffic_page_count(const ConsoleState& console_state)
{
    const uint8_t count = console_state.air_traffic_status.aircraft_count;
    if (count == 0U)
    {
        return 1U;
    }

    return static_cast<uint8_t>((count + (kAirTrafficRowsPerPage - 1U)) / kAirTrafficRowsPerPage);
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

bool open_selected_share_detail(ConsoleState& console_state)
{
    return select_share_slot(console_state, console_state.selected_share_index);
}

bool select_relative_time_zone(ConsoleState& console_state, int offset)
{
    const TimeZoneDefinition* target = relative_time_zone_definition(console_state, offset);
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

bool toggle_air_traffic_enabled()
{
    return persist_runtime_config_change(
        [](RuntimeConfig& settings)
        {
            settings.air_traffic_enabled = !settings.air_traffic_enabled;
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

} // namespace settings_controller
