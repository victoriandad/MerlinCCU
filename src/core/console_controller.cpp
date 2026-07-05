#include "console_controller.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "debug_logging.h"
#include "pico/error.h"

#if __has_include("calendar_identities.h")
#include "calendar_identities.h"
#else
#include "calendar_identities.example.h"
#endif

namespace console_controller
{

namespace
{  /// @todo what is the purpose of this namespace, we are already inside namespace console_controller?

// The console controller owns the user-facing aggregate state. Subsystem
// managers push snapshots into it, while button events mutate the menu and
// annunciator model from one central place.
ConsoleState g_console_state = make_default_console_state();
bool g_redraw_requested = false;
bool g_user_activity_requested = false;
constexpr size_t kSoftkeyLabelCapacity = 48;
constexpr uint16_t kMaxScreenSaverTimeoutMinutes = 120U;
constexpr uint8_t kSettingsPageCount = 2U;
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_labels = {};
std::array<std::array<char, 16>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_values = {};
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_overrides = {};
std::array<bool, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_override_active = {};
std::array<char, 16> g_screen_saver_timeout_selection_text = {};
uint32_t g_alert_sequence_counter = 1U;
uint8_t g_home_assistant_connect_failures = 0U;
uint8_t g_weather_refresh_failures = 0U;
uint8_t g_mqtt_connect_failures = 0U;
uint8_t g_time_unsynced_samples = 0U;
uint8_t g_keypad_fault_samples = 0U;
uint32_t g_alert_acknowledged_sequence = 0U;
constexpr uint8_t kAlertRetryThreshold = 5U;
constexpr float kFreezingTemperatureAlertCelsius = 0.0F;
constexpr float kHighTemperatureAlertCelsius = 30.0F;
constexpr float kHighWindAlertMph = 40.0F;
constexpr uint32_t kStormLowPressurePa = 98000U;
constexpr uint32_t kStormRapidPressureFallPa = 200U;
constexpr uint8_t kStormPressureMinimumSamples = 6U;

enum class AlertCode : uint8_t
{
    WifiDisconnected = 0,
    WifiAuthFailed,
    TimeNotSynced,
    HomeAssistantOffline,
    HomeAssistantUnauthorized,
    HomeAssistantEntityMissing,
    WeatherUnavailable,
    WeatherProviderWarning,
    WeatherTemperatureWarning,
    WeatherWindWarning,
    MqttOffline,
    KeypadLineFault,
    EnvironmentSensorFault,
    LocalPressureStormWarning,
    DisplayPipelineLag,
    ShareDataUnavailable,
    PinterScheduleConflict,
    Count,
};

std::array<bool, static_cast<size_t>(AlertCode::Count)> g_alert_suppressed = {};
std::array<bool, static_cast<size_t>(AlertCode::Count)> g_alert_was_active = {};

void update_softkeys_from_state();
void sync_system_alerts();
void update_lamps_from_state();
const PinterStatus& selected_pinter_const();

/// @brief Copies a short label title and forces uppercase for consistent softkey headings.
/// @details Values remain untouched elsewhere, so only the heading/caption text
/// is normalised by this helper.
void build_uppercase_title(const char* input, char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0U)
    {
        return;
    }

    output[0] = '\0';
    if (input == nullptr || input[0] == '\0')
    {
        return;
    }

    size_t write_index = 0U;
    while (input[write_index] != '\0' && write_index + 1U < output_size)
    {
        output[write_index] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(input[write_index])));
        ++write_index;
    }

    output[write_index] = '\0';
}

struct WeatherSourceDefinition
{
    WeatherSource source;
    const char* selection_label;
    const char* option_label;
};

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

struct PinterSummaryCounts
{
    uint8_t waiting;
    uint8_t brewing;
    uint8_t conditioning;
    uint8_t ready;
};

struct CalendarOwnerDefinition
{
    CalendarOwner owner;
    const char* selection_label;
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

// The runtime catalogue stores only scheduling data for actual brew packs.
// Shop-only fields and glass/bundle products are deliberately omitted here.
constexpr std::array<PinterBrewTiming, kPinterBrewCatalogueCount> kPinterBrewCatalogue = {{
    {"Adnams Ghost Ship Remixed", 8U, 5U, 6U, 3U},
    {"After Midnight", 10U, 7U, 7U, 3U},
    {"Ancestor's", 8U, 5U, 6U, 3U},
    {"Appalachian Mountain Brewery", 9U, 5U, 7U, 3U},
    {"Black Magic Hour", 8U, 7U, 7U, 5U},
    {"BrewDog Elvis Juice Remixed", 9U, 5U, 7U, 3U},
    {"BrewDog Hazy Jane Remixed", 9U, 5U, 7U, 3U},
    {"BrewDog Punk IPA Remixed", 9U, 5U, 7U, 3U},
    {"Brewgooder Hazy IPA Remixed", 7U, 5U, 6U, 3U},
    {"Dark Matter", 5U, 7U, 4U, 3U},
    {"Deep Shade", 13U, 9U, 11U, 7U},
    {"En Casa", 10U, 10U, 7U, 5U},
    {"En Casa Lime", 10U, 10U, 7U, 5U},
    {"Fourpure Citrus IPA Remixed", 8U, 6U, 6U, 4U},
    {"Golden Grove", 5U, 7U, 4U, 3U},
    {"Great Lakes Burning River Remixed", 9U, 5U, 7U, 3U},
    {"Guinness 'Dublin Porter, Brewers Edition'", 6U, 4U, 4U, 3U},
    {"Hopewell", 10U, 10U, 8U, 7U},
    {"Inner Circle", 7U, 7U, 5U, 3U},
    {"Iron Maiden's Trooper Remixed", 7U, 5U, 5U, 3U},
    {"Lagunitas Sumpin' Easy Remixed", 9U, 7U, 8U, 5U},
    {"Lemon & Lime Hard Seltzer", 7U, 7U, 5U, 5U},
    {"Pear With Me", 9U, 5U, 7U, 3U},
    {"Public House", 5U, 7U, 4U, 3U},
    {"Razz", 7U, 5U, 5U, 3U},
    {"Shadow & Cream", 5U, 7U, 4U, 3U},
    {"Snap", 10U, 10U, 8U, 4U},
    {"Space Hopper", 7U, 7U, 5U, 3U},
    {"Space Hopper West Coast Edition", 7U, 7U, 5U, 3U},
    {"Stars & Stripes", 5U, 7U, 4U, 3U},
    {"Summer Haze", 9U, 5U, 7U, 3U},
    {"Sunlit", 10U, 15U, 8U, 4U},
    {"Waltham Forest", 8U, 3U, 6U, 2U},
    {"Whole Nine Yards", 8U, 3U, 6U, 2U},
    {"Yeastie Boys Bigmouth Remixed", 7U, 5U, 6U, 3U},
}};

static_assert(kPinterBrewCatalogue.size() <= 255U,
              "Pinter brew catalogue indices must fit in ConsoleState");

constexpr std::array<SoftKeyId, kPinterBrewListVisibleCount> kPinterBrewListSoftkeys = {
    SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,  SoftKeyId::Left4,
    SoftKeyId::Right1, SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};

constexpr std::array<SoftKeyRoute, kPinterBrewListVisibleCount> kPinterBrewListRoutes = {
    SoftKeyRoute::SelectPinterListItem1, SoftKeyRoute::SelectPinterListItem2,
    SoftKeyRoute::SelectPinterListItem3, SoftKeyRoute::SelectPinterListItem4,
    SoftKeyRoute::SelectPinterListItem5, SoftKeyRoute::SelectPinterListItem6,
    SoftKeyRoute::SelectPinterListItem7, SoftKeyRoute::SelectPinterListItem8};

const char* calendar_identity_label(CalendarOwner owner)
{
    if (owner == CalendarOwner::Combined)
    {
        return "Combined";
    }

    const size_t index = static_cast<size_t>(owner) - 1U;
    if (index < kLocalCalendarIdentities.size() && kLocalCalendarIdentities[index][0] != '\0')
    {
        return kLocalCalendarIdentities[index];
    }

    switch (owner)
    {
    case CalendarOwner::Owner1:
        return "Owner 1";
    case CalendarOwner::Owner2:
        return "Owner 2";
    case CalendarOwner::Owner3:
        return "Owner 3";
    case CalendarOwner::Owner4:
        return "Owner 4";
    case CalendarOwner::Owner5:
        return "Owner 5";
    case CalendarOwner::Owner6:
        return "Owner 6";
    case CalendarOwner::Owner7:
        return "Owner 7";
    case CalendarOwner::Owner8:
        return "Owner 8";
    case CalendarOwner::Combined:
        break;
    }

    return "Owner";
}

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

constexpr size_t alert_code_index(AlertCode code)
{
    return static_cast<size_t>(code);
}

/// @brief Compares environment sensor snapshots without relying on structure padding.
bool environment_sensor_status_matches(
    const environment_sensor_manager::EnvironmentSensorStatus& lhs,
    const environment_sensor_manager::EnvironmentSensorStatus& rhs)
{
    if (lhs.enabled !=                                  rhs.enabled || 
        lhs.board !=                                    rhs.board || 
        lhs.health !=                                   rhs.health ||
        lhs.detected_device_count !=                    rhs.detected_device_count ||
        lhs.last_scan_ms !=                             rhs.last_scan_ms ||
        lhs.successful_scan_count !=                    rhs.successful_scan_count ||
        lhs.failed_scan_count !=                        rhs.failed_scan_count || 
        lhs.last_error !=                               rhs.last_error ||
        lhs.i2c_bus !=                                  rhs.i2c_bus || 
        lhs.sda_gpio !=                                 rhs.sda_gpio ||
        lhs.scl_gpio !=                                 rhs.scl_gpio || 
        lhs.baudrate_hz !=                              rhs.baudrate_hz ||
        lhs.bme_variant !=                              rhs.bme_variant || 
        lhs.bme_chip_id !=                              rhs.bme_chip_id ||
        lhs.bme_last_error !=                           rhs.bme_last_error ||
        lhs.bme_reading_valid !=                        rhs.bme_reading_valid ||
        lhs.bme_temperature_centi_celsius !=            rhs.bme_temperature_centi_celsius ||
        lhs.bme_pressure_pa !=                          rhs.bme_pressure_pa ||
        lhs.bme_humidity_milli_percent !=               rhs.bme_humidity_milli_percent ||
        lhs.bme_last_read_ms !=                         rhs.bme_last_read_ms ||
        lhs.bme_read_error !=                           rhs.bme_read_error ||
        lhs.bme_history_count !=                        rhs.bme_history_count ||
        lhs.bme_temperature_history_centi_celsius !=    rhs.bme_temperature_history_centi_celsius ||
        lhs.bme_pressure_history_deci_hpa !=            rhs.bme_pressure_history_deci_hpa ||
        lhs.bme_humidity_history_centi_percent !=       rhs.bme_humidity_history_centi_percent ||
        lhs.air_quality_raw_valid !=                    rhs.air_quality_raw_valid ||
        lhs.air_quality_raw_signal !=                   rhs.air_quality_raw_signal ||
        lhs.air_quality_baseline_raw_signal !=          rhs.air_quality_baseline_raw_signal ||
        lhs.air_quality_score_valid !=                  rhs.air_quality_score_valid ||
        lhs.air_quality_score !=                        rhs.air_quality_score ||
        lhs.air_quality_last_read_ms !=                 rhs.air_quality_last_read_ms ||
        lhs.air_quality_read_error !=                   rhs.air_quality_read_error ||
        lhs.air_quality_history_count !=                rhs.air_quality_history_count ||
        lhs.air_quality_history_score !=                rhs.air_quality_history_score)
    {
        return false;
    }

    for (size_t index = 0; index < lhs.devices.size(); ++index)
    {
        if (lhs.devices[index].device !=        rhs.devices[index].device ||
            lhs.devices[index].i2c_address !=   rhs.devices[index].i2c_address ||
            lhs.devices[index].detected !=      rhs.devices[index].detected)
        {
            return false;
        }
    }

    return true;
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

/// @brief Returns a valid catalogue index even if older state contains stale data.
size_t pinter_brew_catalogue_index(uint8_t brew_index)
{
    if (brew_index >= kPinterBrewCatalogue.size())
    {
        return kDefaultPinterBrewIndex;
    }

    return brew_index;
}

/// @brief Returns the static scheduling metadata for one Pinter brew pack.
const PinterBrewTiming& pinter_brew_definition(uint8_t brew_index)
{
    return kPinterBrewCatalogue[pinter_brew_catalogue_index(brew_index)];
}

/// @brief Returns the recommended start-to-ready duration for one brew pack.
uint8_t pinter_recommended_total_days(const PinterBrewTiming& brew)
{
    return static_cast<uint8_t>(brew.recommended_brewing_days + brew.recommended_conditioning_days);
}

/// @brief Returns the shortest supported start-to-ready duration for one brew pack.
uint8_t pinter_minimum_total_days(const PinterBrewTiming& brew)
{
    return static_cast<uint8_t>(brew.minimum_brewing_days + brew.minimum_conditioning_days);
}

/// @brief Returns the number of pages needed to show a Pinter list.
uint8_t pinter_list_page_count(size_t item_count)
{
    if (item_count == 0U)
    {
        return 1U;
    }

    return static_cast<uint8_t>(
        (item_count + (kPinterBrewListVisibleCount - 1U)) / kPinterBrewListVisibleCount);
}

/// @brief Returns the bounded number of queued brew packs selected by the user.
uint8_t pinter_selected_brew_count()
{
    return std::min(g_console_state.pinter_selected_brew_count,
                    static_cast<uint8_t>(g_console_state.pinter_selected_brews.size()));
}

/// @brief Keeps the legacy pack-count field aligned with the typed brew queue.
void sync_pinter_pack_count()
{
    g_console_state.pinter_brew_pack_count = pinter_selected_brew_count();
}

/// @brief Clamps a Pinter list page index after queue changes.
void clamp_pinter_list_page(uint8_t& page_index, size_t item_count)
{
    const uint8_t page_count = pinter_list_page_count(item_count);
    if (page_index >= page_count)
    {
        page_index = static_cast<uint8_t>(page_count - 1U);
    }
}

/// @brief Returns the selected brew-pack index for one queue slot.
uint8_t pinter_queued_brew_index(uint8_t queue_index)
{
    if (queue_index >= pinter_selected_brew_count())
    {
        return kDefaultPinterBrewIndex;
    }

    return static_cast<uint8_t>(
        pinter_brew_catalogue_index(g_console_state.pinter_selected_brews[queue_index]));
}

/// @brief Returns whether the selected Pinter still has a planned cold crash transition.
bool selected_pinter_has_pending_cold_crash()
{
    const PinterStatus& pinter = selected_pinter_const();
    return pinter.state == PinterState::Brewing && pinter.planned_cold_crash_days > 0U &&
           !pinter.cold_crash_used;
}

/// @brief Returns a concise state label for Pinter softkeys.
const char* pinter_state_selection_text(PinterState state)
{
    switch (state)
    {
    case PinterState::Idle:
        return "Idle";
    case PinterState::Brewing:
        return "Brew";
    case PinterState::ColdCrash:
        return "Crash";
    case PinterState::Conditioning:
        return "Cond";
    case PinterState::Ready:
        return "Ready";
    case PinterState::Consumed:
        return "Done";
    }

    return "-";
}

/// @brief Counts Pinters currently occupying a brew dock.
uint8_t pinter_brew_dock_count()
{
    uint8_t count = 0U;
    for (const PinterStatus& pinter : g_console_state.pinters)
    {
        if (pinter.state == PinterState::Brewing)
        {
            ++count;
        }
    }
    return count;
}

/// @brief Returns true when a state occupies one of the two fridge slots.
bool pinter_uses_fridge(PinterState state)
{
    return state == PinterState::Conditioning || state == PinterState::Ready;
}

/// @brief Counts Pinters currently occupying fridge space.
uint8_t pinter_fridge_count()
{
    uint8_t count = 0U;
    for (const PinterStatus& pinter : g_console_state.pinters)
    {
        if (pinter_uses_fridge(pinter.state))
        {
            ++count;
        }
    }
    return count;
}

/// @brief Counts the user-facing Pinter workflow buckets used on Home.
/// @details Waiting means brew packs that have not yet been started. Any
/// pre-conditioning hold state remains grouped with brewing for this summary.
PinterSummaryCounts pinter_summary_counts()
{
    PinterSummaryCounts counts = {pinter_selected_brew_count(), 0U, 0U, 0U};

    for (const PinterStatus& pinter : g_console_state.pinters)
    {
        switch (pinter.state)
        {
        case PinterState::Brewing:
        case PinterState::ColdCrash:
            ++counts.brewing;
            break;
        case PinterState::Conditioning:
            ++counts.conditioning;
            break;
        case PinterState::Ready:
            ++counts.ready;
            break;
        case PinterState::Idle:
        case PinterState::Consumed:
            break;
        }
    }

    return counts;
}

/// @brief Returns the currently selected Pinter record.
PinterStatus& selected_pinter()
{
    if (g_console_state.selected_pinter_index >= g_console_state.pinters.size())
    {
        g_console_state.selected_pinter_index = 0U;
    }
    return g_console_state.pinters[g_console_state.selected_pinter_index];
}

/// @brief Returns the currently selected Pinter record.
const PinterStatus& selected_pinter_const()
{
    const uint8_t index = g_console_state.selected_pinter_index < g_console_state.pinters.size()
                              ? g_console_state.selected_pinter_index
                              : 0U;
    return g_console_state.pinters[index];
}

/// @brief Returns today's local epoch day for Pinter event stamping.
uint32_t current_pinter_event_day()
{
    if (!g_console_state.time_status.synced || g_console_state.time_status.local_epoch_day == 0U)
    {
        return 0U;
    }

    return g_console_state.time_status.local_epoch_day;
}

/// @brief Returns the static metadata for one selectable calendar owner filter.
const CalendarOwnerDefinition& calendar_owner_definition(CalendarOwner owner)
{
    static CalendarOwnerDefinition definitions[] = {
        {CalendarOwner::Combined, "Combined"}, {CalendarOwner::Owner1, "Owner 1"},
        {CalendarOwner::Owner2, "Owner 2"},     {CalendarOwner::Owner3, "Owner 3"},
        {CalendarOwner::Owner4, "Owner 4"},     {CalendarOwner::Owner5, "Owner 5"},
        {CalendarOwner::Owner6, "Owner 6"},     {CalendarOwner::Owner7, "Owner 7"},
        {CalendarOwner::Owner8, "Owner 8"},
    };

    const size_t index = static_cast<size_t>(owner);
    if (index < (sizeof(definitions) / sizeof(definitions[0])))
    {
        definitions[index].selection_label = calendar_identity_label(owner);
        return definitions[index];
    }

    return definitions[0];
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

        const int kWritten = std::snprintf(
            out_text.data() + used, 
            out_text.size() - used, 
            "%s%u", 
            (used == 0) ? "" : " ", 
            static_cast<unsigned>(line.panel_pin));

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

/// @brief Returns the currently selected weather-source label for menu softkeys.
const char* weather_source_selection_text(const ConsoleState& console_state)
{
    return weather_source_definition(console_state.weather_source).selection_label;
}

/// @brief Returns the currently selected weather period label for menu softkeys.
const char* weather_period_selection_text(const ConsoleState& console_state)
{
    return weather_period_definition(console_state.weather_period).selection_label;
}

/// @brief Returns the currently selected share history period label.
const char* share_period_selection_text(const ConsoleState& console_state)
{
    return share_period_definition(console_state.share_period).selection_label;
}

/// @brief Returns the currently selected shared-calendar owner label.
const char* calendar_owner_selection_text(const ConsoleState& console_state)
{
    return calendar_owner_definition(console_state.calendar_owner).selection_label;
}

/// @brief Formats local temperature for softkey value brackets.
const char* local_temperature_selection_text()
{
    auto& buffer = g_dynamic_softkey_values[softkey_index(SoftKeyId::Left1)];
    const auto& status = g_console_state.environment_sensor_status;
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

/// @brief Formats local humidity for softkey value brackets.
const char* local_humidity_selection_text()
{
    auto& buffer = g_dynamic_softkey_values[softkey_index(SoftKeyId::Left2)];
    const auto& status = g_console_state.environment_sensor_status;
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

/// @brief Formats local pressure for softkey value brackets.
const char* local_pressure_selection_text()
{
    auto& buffer = g_dynamic_softkey_values[softkey_index(SoftKeyId::Left3)];
    const auto& status = g_console_state.environment_sensor_status;
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

/// @brief Formats the local VOC-change band for softkey value brackets.
const char* local_air_quality_selection_text()
{
    auto& buffer = g_dynamic_softkey_values[softkey_index(SoftKeyId::Left4)];
    const auto& status = g_console_state.environment_sensor_status;
    if (!status.enabled || !status.air_quality_score_valid)
    {
        std::snprintf(buffer.data(), buffer.size(), "%s",
                      status.air_quality_read_error == PICO_ERROR_NONE ? "-" : "ERR");
        return buffer.data();
    }

    std::snprintf(buffer.data(), buffer.size(), "%s",
                  environment_sensor_manager::air_quality_band_text(status.air_quality_score));
    return buffer.data();
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
    const char* unit = (console_state.screen_saver_timeout_minutes == 1U) ? "minute" : "minutes";
    std::snprintf(g_screen_saver_timeout_selection_text.data(),
                  g_screen_saver_timeout_selection_text.size(), "%u %s",
                  static_cast<unsigned>(console_state.screen_saver_timeout_minutes), unit);
    return g_screen_saver_timeout_selection_text.data();
}

/// @brief Returns the number of alert list pages required for the current queue.
uint8_t alert_page_count()
{
    constexpr uint8_t kAlertsPerPage = 9U;
    if (g_console_state.alert_count == 0U)
    {
        return 1U;
    }

    return static_cast<uint8_t>((g_console_state.alert_count + (kAlertsPerPage - 1U)) /
                                kAlertsPerPage);
}

/// @brief Sorts active alerts from newest to oldest for list-page mapping.
void build_alert_display_indices(std::array<uint8_t, 24>& out_indices, uint8_t* out_count)
{
    uint8_t count = 0U;
    for (uint8_t i = 0U; i < g_console_state.alert_count && i < out_indices.size(); ++i)
    {
        out_indices[count++] = i;
    }

    for (uint8_t i = 0U; i < count; ++i)
    {
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < count; ++j)
        {
            if (g_console_state.active_alerts[out_indices[j]].sequence >
                g_console_state.active_alerts[out_indices[i]].sequence)
            {
                const uint8_t tmp = out_indices[i];
                out_indices[i] = out_indices[j];
                out_indices[j] = tmp;
            }
        }
    }

    *out_count = count;
}

/// @brief Finds an alert by code in the active queue.
int find_alert_by_code(AlertCode code)
{
    for (uint8_t i = 0U; i < g_console_state.alert_count; ++i)
    {
        if (g_console_state.active_alerts[i].code == static_cast<uint8_t>(code))
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

/// @brief Removes one alert from the active queue and compacts trailing entries.
void erase_alert_at(uint8_t index)
{
    if (index >= g_console_state.alert_count)
    {
        return;
    }

    for (uint8_t i = index; i + 1U < g_console_state.alert_count; ++i)
    {
        g_console_state.active_alerts[i] = g_console_state.active_alerts[i + 1U];
    }
    if (g_console_state.alert_count > 0U)
    {
        --g_console_state.alert_count;
    }
}

/// @brief Adds or updates one alert condition in the active queue.
void set_alert_condition(AlertCode code, bool active, AlertSeverity severity, const char* summary,
                         const char* detail)
{
    const size_t code_idx = alert_code_index(code);
    const bool was_active = g_alert_was_active[code_idx];
    if (!active)
    {
        g_alert_was_active[code_idx] = false;
        g_alert_suppressed[code_idx] = false;
        const int existing = find_alert_by_code(code);
        if (existing >= 0)
        {
            erase_alert_at(static_cast<uint8_t>(existing));
        }
        return;
    }

    g_alert_was_active[code_idx] = true;
    if (g_alert_suppressed[code_idx])
    {
        return;
    }

    const int existing = find_alert_by_code(code);
    if (existing >= 0)
    {
        ActiveAlert& alert = g_console_state.active_alerts[static_cast<size_t>(existing)];
        alert.severity = severity;
        std::snprintf(alert.summary.data(), alert.summary.size(), "%s", summary);
        std::snprintf(alert.detail.data(), alert.detail.size(), "%s", detail);
        // Existing active alerts keep their original arrival sequence/time so
        // periodic state re-evaluation does not retrigger annunciation.
        return;
    }

    if (g_console_state.alert_count >= g_console_state.active_alerts.size())
    {
        erase_alert_at(0U);
    }

    ActiveAlert& alert = g_console_state.active_alerts[g_console_state.alert_count++];
    alert.severity = severity;
    alert.code = static_cast<uint8_t>(code);
    alert.sequence = g_alert_sequence_counter++;
    std::snprintf(alert.occurred_time_text.data(), alert.occurred_time_text.size(), "%s",
                  g_console_state.time_status.synced ? g_console_state.time_status.time_text.data()
                                                     : "--:--");
    std::snprintf(alert.summary.data(), alert.summary.size(), "%s", summary);
    std::snprintf(alert.detail.data(), alert.detail.size(), "%s", detail);
}

/// @brief Adds one optional temperature value to a min/max range.
void include_temperature_alert_value(bool valid, float value_celsius, bool& have_range,
                                     float& min_celsius, float& max_celsius)
{
    if (!valid)
    {
        return;
    }

    if (!have_range)
    {
        min_celsius = value_celsius;
        max_celsius = value_celsius;
        have_range = true;
        return;
    }

    min_celsius = std::min(min_celsius, value_celsius);
    max_celsius = std::max(max_celsius, value_celsius);
}

/// @brief Builds the combined current/forecast temperature range used by weather alerts.
bool weather_temperature_alert_range(const WeatherMetrics& metrics, float& min_celsius,
                                     float& max_celsius)
{
    bool have_range = false;
    include_temperature_alert_value(metrics.current_temperature_celsius_valid,
                                    metrics.current_temperature_celsius, have_range, min_celsius,
                                    max_celsius);
    include_temperature_alert_value(metrics.forecast_min_temperature_celsius_valid,
                                    metrics.forecast_min_temperature_celsius, have_range,
                                    min_celsius, max_celsius);
    include_temperature_alert_value(metrics.forecast_max_temperature_celsius_valid,
                                    metrics.forecast_max_temperature_celsius, have_range,
                                    min_celsius, max_celsius);
    return have_range;
}

/// @brief Builds the maximum current/forecast wind value used by weather alerts.
bool weather_wind_alert_maximum(const WeatherMetrics& metrics, float& max_wind_mph)
{
    bool have_wind = false;
    if (metrics.current_wind_speed_mph_valid)
    {
        max_wind_mph = metrics.current_wind_speed_mph;
        have_wind = true;
    }
    if (metrics.forecast_max_wind_speed_mph_valid &&
        (!have_wind || metrics.forecast_max_wind_speed_mph > max_wind_mph))
    {
        max_wind_mph = metrics.forecast_max_wind_speed_mph;
        have_wind = true;
    }

    return have_wind;
}

/// @brief Formats the operator-facing temperature alert detail.
void build_weather_temperature_alert_detail(float min_celsius, float max_celsius, char* out,
                                            size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const int min_celsius_rounded = static_cast<int>(std::lround(min_celsius));
    const int max_celsius_rounded = static_cast<int>(std::lround(max_celsius));
    const bool freezing = min_celsius <= kFreezingTemperatureAlertCelsius;
    const bool hot = max_celsius >= kHighTemperatureAlertCelsius;

    if (freezing && hot)
    {
        std::snprintf(out, out_size,
                      "Weather temperature thresholds are active.\nLowest: %d C.\nHighest: %d C.",
                      min_celsius_rounded, max_celsius_rounded);
    }
    else if (freezing)
    {
        std::snprintf(out, out_size,
                      "Weather source reports freezing conditions.\nLowest cached value: %d C.",
                      min_celsius_rounded);
    }
    else
    {
        std::snprintf(out, out_size,
                      "Weather source reports high temperature.\nHighest cached value: %d C.",
                      max_celsius_rounded);
    }
}

/// @brief Formats the operator-facing wind alert detail.
void build_weather_wind_alert_detail(float max_wind_mph, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const int max_wind_rounded = static_cast<int>(std::lround(max_wind_mph));
    std::snprintf(out, out_size,
                  "Weather source reports wind up to %d mph.\nSecure loose items and check local "
                  "warnings.",
                  max_wind_rounded);
}

/// @brief Formats pascals as tenths of a hectopascal for operator messages.
void build_pressure_tenths_hpa_text(uint32_t pressure_pa, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    const uint32_t tenths_hpa = (pressure_pa + 5U) / 10U;
    std::snprintf(out, out_size, "%lu.%lu", static_cast<unsigned long>(tenths_hpa / 10U),
                  static_cast<unsigned long>(tenths_hpa % 10U));
}

/// @brief Detects local pressure conditions that can indicate storm risk.
/// @details BME280 pressure is local station pressure, so a rapid fall is more
/// portable than an absolute threshold. The low-pressure threshold remains as a
/// secondary signal for low-altitude installations.
bool local_pressure_storm_warning(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* detail,
    size_t detail_size)
{
    if (!status.enabled || !status.bme_reading_valid)
    {
        return false;
    }

    uint32_t pressure_fall_pa = 0U;
    bool rapid_fall = false;
    if (status.bme_history_count >= kStormPressureMinimumSamples)
    {
        const uint16_t comparison_index =
            status.bme_history_count - kStormPressureMinimumSamples;
        const uint32_t comparison_pressure =
            static_cast<uint32_t>(status.bme_pressure_history_deci_hpa[comparison_index]) * 10U;
        const uint32_t latest_pressure =
            static_cast<uint32_t>(
                status.bme_pressure_history_deci_hpa[status.bme_history_count - 1U]) *
            10U;
        if (comparison_pressure > latest_pressure)
        {
            pressure_fall_pa = comparison_pressure - latest_pressure;
            rapid_fall = pressure_fall_pa >= kStormRapidPressureFallPa;
        }
    }

    const bool low_pressure = status.bme_pressure_pa <= kStormLowPressurePa;
    if (!low_pressure && !rapid_fall)
    {
        return false;
    }

    char current_pressure_text[12] = {};
    char pressure_fall_text[12] = {};
    build_pressure_tenths_hpa_text(status.bme_pressure_pa, current_pressure_text,
                                   sizeof(current_pressure_text));
    build_pressure_tenths_hpa_text(pressure_fall_pa, pressure_fall_text,
                                   sizeof(pressure_fall_text));

    if (detail != nullptr && detail_size > 0U)
    {
        if (low_pressure && rapid_fall)
        {
            std::snprintf(detail, detail_size,
                          "Local pressure is %s hPa and has fallen %s hPa across recent "
                          "averages.\nCheck local forecast and conditions.",
                          current_pressure_text, pressure_fall_text);
        }
        else if (rapid_fall)
        {
            std::snprintf(detail, detail_size,
                          "Local pressure has fallen %s hPa across recent five-minute "
                          "averages.\nCheck local forecast and conditions.",
                          pressure_fall_text);
        }
        else
        {
            std::snprintf(detail, detail_size,
                          "Local pressure is low at %s hPa.\nCheck local forecast and conditions.",
                          current_pressure_text);
        }
    }

    return true;
}

/// @brief Rebuilds currently active alerts from live subsystem conditions.
void sync_system_alerts()
{
    const bool wifi_connected = g_console_state.wifi_status.state == WifiConnectionState::Connected;
    set_alert_condition(AlertCode::WifiDisconnected, !wifi_connected, AlertSeverity::Warning,
                        "NETWORK",
                        "Wi-Fi link is down.\nCheck SSID/password or signal.\nWithout network, "
                        "cloud features are unavailable.");
    const bool wifi_auth_failed =
        g_console_state.wifi_status.state == WifiConnectionState::AuthFailed;
    set_alert_condition(
        AlertCode::WifiAuthFailed, wifi_auth_failed, AlertSeverity::Alert, "WIFI AUTH",
        "Wi-Fi authentication failed.\nVerify SSID and password in NETWORK settings.");

    if (!g_console_state.time_status.synced && wifi_connected)
    {
        if (g_time_unsynced_samples < 255U)
        {
            ++g_time_unsynced_samples;
        }
    }
    else
    {
        g_time_unsynced_samples = 0U;
    }
    set_alert_condition(
        AlertCode::TimeNotSynced, g_time_unsynced_samples >= kAlertRetryThreshold,
        AlertSeverity::Warning, "TIME",
        "Clock sync has failed after 5 attempts.\nCheck NTP reachability and timezone setup.");

    const bool ha_enabled = config_manager::settings().home_assistant_enabled;
    const HomeAssistantConnectionState ha_state = g_console_state.home_assistant_status.state;
    if (!ha_enabled || ha_state == HomeAssistantConnectionState::Connected ||
        ha_state == HomeAssistantConnectionState::Unauthorized)
    {
        g_home_assistant_connect_failures = 0U;
    }
    else
    {
        if (g_home_assistant_connect_failures < 255U)
        {
            ++g_home_assistant_connect_failures;
        }
    }

    const bool ha_unauthorised = ha_state == HomeAssistantConnectionState::Unauthorized;
    const bool ha_offline = ha_enabled && ha_state != HomeAssistantConnectionState::Connected &&
                            ha_state != HomeAssistantConnectionState::Unauthorized &&
                            g_home_assistant_connect_failures >= kAlertRetryThreshold;
    set_alert_condition(AlertCode::HomeAssistantOffline, ha_offline, AlertSeverity::Message,
                        "HOME ASSISTANT",
                        "Home Assistant is not connected.\nCheck host/port/token and network "
                        "routing.\nStatus page shows the latest connector state.");
    set_alert_condition(AlertCode::HomeAssistantUnauthorized, ha_unauthorised, AlertSeverity::Alert,
                        "AUTH FAILED",
                        "Home Assistant rejected the API token.\nUpdate the token in Settings > "
                        "Home Assistant.\nThis alert clears after a successful authorisation.");
    const bool tracked_entity_configured =
        config_manager::settings().home_assistant_entity_id[0] != '\0';
    const bool tracked_entity_missing =
        ha_enabled && tracked_entity_configured &&
        ha_state == HomeAssistantConnectionState::Connected &&
        (g_console_state.home_assistant_status.tracked_entity_state[0] == '\0' ||
         std::strcmp(g_console_state.home_assistant_status.tracked_entity_state.data(),
                     "unknown") == 0 ||
         std::strcmp(g_console_state.home_assistant_status.tracked_entity_state.data(),
                     "unavailable") == 0);
    set_alert_condition(AlertCode::HomeAssistantEntityMissing, tracked_entity_missing,
                        AlertSeverity::Warning, "HA ENTITY",
                        "Tracked Home Assistant entity is missing or unavailable.\nCheck the "
                        "entity id in settings and HA integration state.");

    const bool weather_source_active =
        g_console_state.weather_source == WeatherSource::OpenMeteo ||
        g_console_state.weather_source == WeatherSource::HomeAssistant;
    const bool weather_payload_present =
        g_console_state.home_assistant_status.weather_condition[0] != '\0' ||
        g_console_state.home_assistant_status.weather_temperature[0] != '\0' ||
        g_console_state.home_assistant_status.weather_forecast_count > 0U ||
        g_console_state.home_assistant_status.weather_daily_forecast_count > 0U;
    const bool weather_prerequisites_ok =
        (g_console_state.weather_source == WeatherSource::HomeAssistant)
            ? (ha_state == HomeAssistantConnectionState::Connected)
            : g_console_state.wifi_status.internet_reachable;
    const bool weather_http_failed = g_console_state.home_assistant_status.last_http_status >= 400;
    const bool weather_failed_now = !weather_payload_present || weather_http_failed;
    if (weather_source_active && weather_prerequisites_ok && weather_failed_now)
    {
        if (g_weather_refresh_failures < 255U)
        {
            ++g_weather_refresh_failures;
        }
    }
    else
    {
        g_weather_refresh_failures = 0U;
    }

    const bool weather_unavailable = weather_source_active && weather_prerequisites_ok &&
                                     g_weather_refresh_failures >= kAlertRetryThreshold;
    set_alert_condition(
        AlertCode::WeatherUnavailable, weather_unavailable, AlertSeverity::Message, "WEATHER",
        "Weather refresh failed after 5 retries.\nCheck source settings and network path.");

    const bool weather_alert_data_current =
        weather_source_active && weather_prerequisites_ok && weather_payload_present &&
        !weather_unavailable;
    const WeatherAlertStatus& weather_alert_status =
        g_console_state.home_assistant_status.weather_alert_status;
    const AlertSeverity provider_warning_severity =
        weather_alert_status.provider_warning_severity == AlertSeverity::None
            ? AlertSeverity::Warning
            : weather_alert_status.provider_warning_severity;
    const char* provider_warning_summary =
        weather_alert_status.provider_warning_summary[0] != '\0'
            ? weather_alert_status.provider_warning_summary.data()
            : "WX WARNING";
    const char* provider_warning_detail =
        weather_alert_status.provider_warning_detail[0] != '\0'
            ? weather_alert_status.provider_warning_detail.data()
            : "Weather source reports a warning.\nCheck the latest local forecast.";
    // Official provider-warning APIs are not wired yet. Current weather providers
    // can still raise this hook from severe condition telemetry such as thunder.
    set_alert_condition(AlertCode::WeatherProviderWarning,
                        weather_alert_data_current &&
                            weather_alert_status.provider_warning_active,
                        provider_warning_severity, provider_warning_summary,
                        provider_warning_detail);

    const WeatherMetrics& weather_metrics = g_console_state.home_assistant_status.weather_metrics;
    float min_temperature_celsius = 0.0F;
    float max_temperature_celsius = 0.0F;
    const bool have_temperature_range = weather_temperature_alert_range(
        weather_metrics, min_temperature_celsius, max_temperature_celsius);
    const bool temperature_warning =
        weather_alert_data_current && have_temperature_range &&
        (min_temperature_celsius <= kFreezingTemperatureAlertCelsius ||
         max_temperature_celsius >= kHighTemperatureAlertCelsius);
    // Reused for each alert's detail text below rather than one 320-byte
    // buffer per alert: every use is built then immediately consumed by
    // set_alert_condition (which copies it into ActiveAlert), so lifetimes
    // never overlap. This function runs twice per keypress (from
    // update_softkeys_from_state and update_lamps_from_state), so trimming
    // its stack footprint matters on a memory-constrained MCU with no
    // configured stack guard.
    char alert_detail_scratch[sizeof(ActiveAlert::detail)] = {};
    if (temperature_warning)
    {
        build_weather_temperature_alert_detail(min_temperature_celsius, max_temperature_celsius,
                                               alert_detail_scratch,
                                               sizeof(alert_detail_scratch));
    }
    set_alert_condition(AlertCode::WeatherTemperatureWarning, temperature_warning,
                        AlertSeverity::Warning, "WX TEMP",
                        temperature_warning ? alert_detail_scratch : "");

    float max_wind_mph = 0.0F;
    const bool have_wind_maximum = weather_wind_alert_maximum(weather_metrics, max_wind_mph);
    const bool wind_warning =
        weather_alert_data_current && have_wind_maximum && max_wind_mph >= kHighWindAlertMph;
    if (wind_warning)
    {
        build_weather_wind_alert_detail(max_wind_mph, alert_detail_scratch,
                                        sizeof(alert_detail_scratch));
    }
    set_alert_condition(AlertCode::WeatherWindWarning, wind_warning, AlertSeverity::Warning,
                        "WX WIND", wind_warning ? alert_detail_scratch : "");

    const bool mqtt_enabled = config_manager::settings().mqtt_enabled;
    const MqttConnectionState mqtt_state = g_console_state.mqtt_status.state;
    if (!mqtt_enabled || mqtt_state == MqttConnectionState::Connected)
    {
        g_mqtt_connect_failures = 0U;
    }
    else
    {
        if (g_mqtt_connect_failures < 255U)
        {
            ++g_mqtt_connect_failures;
        }
    }
    set_alert_condition(
        AlertCode::MqttOffline,
        mqtt_enabled && mqtt_state != MqttConnectionState::Connected &&
            g_mqtt_connect_failures >= kAlertRetryThreshold,
        AlertSeverity::Message, "MQTT",
        "MQTT discovery is offline after 5 retries.\nCheck broker host/port and credentials.");

    const bool keypad_fault_now =
        (std::strcmp(g_console_state.keypad_debug_status.pressed_key_name.data(), "MULTI") == 0) ||
        g_console_state.keypad_debug_status.active_count > 3U;
    if (keypad_fault_now)
    {
        if (g_keypad_fault_samples < 255U)
        {
            ++g_keypad_fault_samples;
        }
    }
    else
    {
        g_keypad_fault_samples = 0U;
    }
    set_alert_condition(AlertCode::KeypadLineFault, g_keypad_fault_samples >= kAlertRetryThreshold,
                        AlertSeverity::Warning, "KEYPAD",
                        "Matrix line fault suspected.\nMultiple simultaneous/stuck lines were "
                        "detected repeatedly.");

    const environment_sensor_manager::EnvironmentSensorStatus& environment_status =
        g_console_state.environment_sensor_status;
    const bool sgp40_detected = std::any_of(
        environment_status.devices.begin(), environment_status.devices.end(),
        [](const environment_sensor_manager::EnvironmentSensorPresence& presence) {
            return presence.device == environment_sensor_manager::EnvironmentSensorDevice::Sgp40 &&
                   presence.detected;
        });
    const bool environment_sensor_fault =
        environment_status.enabled &&
        (environment_status.health == environment_sensor_manager::EnvironmentSensorHealth::Fault ||
         environment_status.health ==
             environment_sensor_manager::EnvironmentSensorHealth::BoardMissing ||
         environment_status.health == environment_sensor_manager::EnvironmentSensorHealth::Partial ||
         (environment_status.bme_variant == environment_sensor_manager::EnvironmentBmeVariant::Bme280 &&
          !environment_status.bme_reading_valid &&
          environment_status.bme_read_error != PICO_ERROR_NONE) ||
         (sgp40_detected && !environment_status.air_quality_raw_valid &&
          environment_status.air_quality_read_error != PICO_ERROR_NONE));
    set_alert_condition(
        AlertCode::EnvironmentSensorFault, environment_sensor_fault, AlertSeverity::Warning,
        "ENV SENSOR",
        "Environment sensor board is not fully detected.\nCheck I2C pins, power, and address "
        "jumpers.\nStatus page shows the detected addresses and read errors.");

    const bool pressure_storm_warning = local_pressure_storm_warning(
        environment_status, alert_detail_scratch, sizeof(alert_detail_scratch));
    set_alert_condition(AlertCode::LocalPressureStormWarning, pressure_storm_warning,
                        AlertSeverity::Warning, "STORM WARN",
                        pressure_storm_warning ? alert_detail_scratch : "");

    // Placeholder: enable this once render/frame timing counters are exposed to console state.
    const bool display_pipeline_lag = false;
    set_alert_condition(
        AlertCode::DisplayPipelineLag, display_pipeline_lag, AlertSeverity::Message, "DISPLAY LAG",
        "Display update pipeline is lagging.\nAdd frame timing telemetry to activate this alert.");

    const bool share_data_unavailable = g_console_state.share_data_configured &&
                                        !g_console_state.share_data_valid &&
                                        (g_console_state.share_data_last_error != 0 ||
                                         g_console_state.share_data_last_http_status >= 400);
    set_alert_condition(AlertCode::ShareDataUnavailable, share_data_unavailable,
                        AlertSeverity::Message, "SHARES",
                        "Share price data is unavailable.\nCheck the market data provider and "
                        "watchlist configuration.");

    // The Pinter workflow now records typed queue entries and planned durations.
    // Friday target forecasts and future fridge/dock reservation windows are
    // still needed before this can raise a reliable schedule-conflict alert.
    set_alert_condition(AlertCode::PinterScheduleConflict, false, AlertSeverity::Message, "PINTER",
                        "");

    if (g_console_state.alert_detail_index >= g_console_state.alert_count)
    {
        g_console_state.alert_detail_index =
            g_console_state.alert_count > 0U
                ? static_cast<uint8_t>(g_console_state.alert_count - 1U)
                : 0U;
    }
    const uint8_t pages = alert_page_count();
    if (g_console_state.alert_list_page_index >= pages)
    {
        g_console_state.alert_list_page_index = static_cast<uint8_t>(pages - 1U);
    }
}

/// @brief Formats one two-line softkey label with a square-bracket selection.
/// @details The controller owns these buffers so menu pages can rebuild labels
/// whenever integration state changes without leaving dangling pointers behind.
const char* build_selection_softkey_label(SoftKeyId key, const char* title, const char* selection)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* value = (selection != nullptr && selection[0] != '\0') ? selection : "-";
    constexpr size_t kSoftkeyTitleBufferSize = 24U;
    char title_upper[kSoftkeyTitleBufferSize] = {};
    build_uppercase_title(title, title_upper, sizeof(title_upper));
    std::snprintf(buffer.data(), buffer.size(), "%s\n[%s]", title_upper, value);
    return buffer.data();
}

/// @brief Formats one two-line alert softkey label using summary and occurred time.
const char* build_alert_softkey_label(SoftKeyId key, const ActiveAlert& alert)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* time_text =
        (alert.occurred_time_text[0] != '\0') ? alert.occurred_time_text.data() : "--:--";
    std::snprintf(buffer.data(), buffer.size(), "%s\n[%s]", alert.summary.data(), time_text);
    return buffer.data();
}

/// @brief Returns whether one event belongs in the active Calendar page filter.
bool calendar_event_matches_filter(const CalendarEvent& event)
{
    if (event.title[0] == '\0' || event.day_offset != g_console_state.calendar_day_offset)
    {
        return false;
    }

    return g_console_state.calendar_owner == CalendarOwner::Combined ||
           event.owner == g_console_state.calendar_owner;
}

/// @brief Formats one calendar event for the surrounding softkey labels.
/// @details The data portion deliberately carries both time and owner so the
/// Combined view remains useful without needing a wider centre table.
const char* build_calendar_event_softkey_label(SoftKeyId key, const CalendarEvent& event)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* owner_text = calendar_owner_definition(event.owner).selection_label;
    char value[24] = {};
    std::snprintf(value, sizeof(value), "%s %s",
                  event.start_time[0] != '\0' ? event.start_time.data() : "--:--", owner_text);
    return build_selection_softkey_label(key, event.title.data(), value);
}

/// @brief Formats one Pinter selector softkey using the current lifecycle state.
const char* build_pinter_slot_softkey_label(SoftKeyId key, const PinterStatus& pinter)
{
    const char* state_text = pinter_state_selection_text(pinter.state);
    if (pinter.state == PinterState::Idle || pinter.state == PinterState::Consumed)
    {
        return build_selection_softkey_label(key, pinter.label.data(), state_text);
    }

    const PinterBrewTiming& brew = pinter_brew_definition(pinter.brew_index);
    char value[40] = {};
    std::snprintf(value, sizeof(value), "%s %s", state_text, brew.name);
    return build_selection_softkey_label(key, pinter.label.data(), value);
}

/// @brief Formats the Home-page Pinter summary as waiting/brewing/conditioning/ready.
const char* build_pinter_home_softkey_label(SoftKeyId key)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const PinterSummaryCounts counts = pinter_summary_counts();
    std::snprintf(buffer.data(), buffer.size(), "PINTER\n[%uW, %uB, %uC, %uR]",
                  static_cast<unsigned>(counts.waiting), static_cast<unsigned>(counts.brewing),
                  static_cast<unsigned>(counts.conditioning),
                  static_cast<unsigned>(counts.ready));
    return buffer.data();
}

/// @brief Formats a Pinter pack-management softkey with the current pack count.
const char* build_pinter_pack_count_softkey_label(SoftKeyId key, const char* title)
{
    char count_text[8] = {};
    std::snprintf(count_text, sizeof(count_text), "%u",
                  static_cast<unsigned>(pinter_selected_brew_count()));
    return build_selection_softkey_label(key, title, count_text);
}

/// @brief Formats one catalogue brew item with recommended and minimum totals.
const char* build_pinter_catalogue_item_label(SoftKeyId key, uint8_t brew_index)
{
    const PinterBrewTiming& brew = pinter_brew_definition(brew_index);
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    std::snprintf(buffer.data(), buffer.size(), "%s", brew.name);
    return buffer.data();
}

/// @brief Formats one user-selected brew queue item.
const char* build_pinter_queued_item_label(SoftKeyId key, uint8_t queue_index)
{
    const uint8_t brew_index = pinter_queued_brew_index(queue_index);
    const PinterBrewTiming& brew = pinter_brew_definition(brew_index);
    char timing_text[16] = {};
    std::snprintf(timing_text, sizeof(timing_text), "#%u R%u M%u",
                  static_cast<unsigned>(queue_index + 1U),
                  static_cast<unsigned>(pinter_recommended_total_days(brew)),
                  static_cast<unsigned>(pinter_minimum_total_days(brew)));
    return build_selection_softkey_label(key, brew.name, timing_text);
}

/// @brief Formats one timing-adjustment label in the pending Pinter start flow.
const char* build_pinter_days_label(SoftKeyId key, const char* title, uint8_t days)
{
    char day_text[8] = {};
    std::snprintf(day_text, sizeof(day_text), "%ud", static_cast<unsigned>(days));
    return build_selection_softkey_label(key, title, day_text);
}

/// @brief Returns true when the selected Pinter can be started now.
bool selected_pinter_can_start()
{
    const PinterStatus& pinter = selected_pinter_const();
    return pinter.state == PinterState::Idle && pinter_selected_brew_count() > 0U &&
           pinter_brew_dock_count() < kPinterBrewDockCapacity;
}

/// @brief Returns true when the selected Pinter can enter the fridge now.
bool selected_pinter_can_enter_fridge()
{
    const PinterStatus& pinter = selected_pinter_const();
    if (pinter.state == PinterState::Brewing && selected_pinter_has_pending_cold_crash())
    {
        return true;
    }
    if (pinter.state != PinterState::Brewing && pinter.state != PinterState::ColdCrash)
    {
        return false;
    }

    return pinter_fridge_count() < kPinterFridgeCapacity;
}

/// @brief Returns whether the context-sensitive Pinter action can be applied.
bool pinter_primary_action_enabled()
{
    const PinterStatus& pinter = selected_pinter_const();
    switch (pinter.state)
    {
    case PinterState::Idle:
        return selected_pinter_can_start();
    case PinterState::Brewing:
        if (selected_pinter_has_pending_cold_crash())
        {
            return true;
        }
        return selected_pinter_can_enter_fridge();
    case PinterState::ColdCrash:
        return selected_pinter_can_enter_fridge();
    case PinterState::Conditioning:
    case PinterState::Ready:
    case PinterState::Consumed:
        return true;
    }

    return false;
}

/// @brief Formats the context-sensitive Pinter event action and any block reason.
const char* build_pinter_primary_action_label(SoftKeyId key)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const PinterStatus& pinter = selected_pinter_const();

    switch (pinter.state)
    {
    case PinterState::Idle:
        if (pinter_selected_brew_count() == 0U)
        {
            std::snprintf(buffer.data(), buffer.size(), "START\n[NO BREWS]");
            return buffer.data();
        }
        if (pinter_brew_dock_count() >= kPinterBrewDockCapacity)
        {
            std::snprintf(buffer.data(), buffer.size(), "START\n[NO DOCK]");
            return buffer.data();
        }
        std::snprintf(buffer.data(), buffer.size(), "START");
        return buffer.data();
    case PinterState::Brewing:
        if (selected_pinter_has_pending_cold_crash())
        {
            std::snprintf(buffer.data(), buffer.size(), "COLD\nCRASH");
            return buffer.data();
        }
        if (pinter_fridge_count() >= kPinterFridgeCapacity)
        {
            std::snprintf(buffer.data(), buffer.size(), "FRIDGE\n[FULL]");
            return buffer.data();
        }
        std::snprintf(buffer.data(), buffer.size(), "FRIDGE");
        return buffer.data();
    case PinterState::ColdCrash:
        if (pinter_fridge_count() >= kPinterFridgeCapacity)
        {
            std::snprintf(buffer.data(), buffer.size(), "FRIDGE\n[FULL]");
            return buffer.data();
        }
        std::snprintf(buffer.data(), buffer.size(), "FRIDGE");
        return buffer.data();
    case PinterState::Conditioning:
        std::snprintf(buffer.data(), buffer.size(), "READY");
        return buffer.data();
    case PinterState::Ready:
        std::snprintf(buffer.data(), buffer.size(), "DRINK");
        return buffer.data();
    case PinterState::Consumed:
        std::snprintf(buffer.data(), buffer.size(), "CLEAN");
        return buffer.data();
    }

    std::snprintf(buffer.data(), buffer.size(), "-");
    return buffer.data();
}

/// @brief Selects one physical Pinter vessel for subsequent event actions.
bool select_pinter_slot(uint8_t index)
{
    if (index >= g_console_state.pinters.size())
    {
        return false;
    }

    if (g_console_state.selected_pinter_index == index)
    {
        return false;
    }

    g_console_state.selected_pinter_index = index;
    return true;
}

/// @brief Cycles the brew pack used when the next Pinter is started.
bool cycle_pinter_brew()
{
    const size_t next_index =
        pinter_brew_catalogue_index(g_console_state.pinter_selected_brew_index) + 1U;
    g_console_state.pinter_selected_brew_index =
        static_cast<uint8_t>(next_index % kPinterBrewCatalogue.size());
    return true;
}

/// @brief Adds one selected brew type to the typed Pinter queue.
bool add_pinter_selected_brew(uint8_t brew_index)
{
    const uint8_t count = pinter_selected_brew_count();
    if (count >= g_console_state.pinter_selected_brews.size())
    {
        return false;
    }

    g_console_state.pinter_selected_brews[count] =
        static_cast<uint8_t>(pinter_brew_catalogue_index(brew_index));
    g_console_state.pinter_selected_brew_count = static_cast<uint8_t>(count + 1U);
    sync_pinter_pack_count();
    return true;
}

/// @brief Removes one queued brew after it has been assigned to a Pinter.
bool remove_pinter_selected_brew(uint8_t queue_index)
{
    const uint8_t count = pinter_selected_brew_count();
    if (queue_index >= count)
    {
        return false;
    }

    for (uint8_t i = queue_index; i + 1U < count; ++i)
    {
        g_console_state.pinter_selected_brews[i] = g_console_state.pinter_selected_brews[i + 1U];
    }
    g_console_state.pinter_selected_brews[count - 1U] = kInvalidPinterBrewSelection;
    g_console_state.pinter_selected_brew_count = static_cast<uint8_t>(count - 1U);
    sync_pinter_pack_count();
    clamp_pinter_list_page(g_console_state.pinter_selected_brews_page_index,
                           pinter_selected_brew_count());
    clamp_pinter_list_page(g_console_state.pinter_start_brews_page_index,
                           pinter_selected_brew_count());
    return true;
}

/// @brief Records receipt of one additional brew pack.
bool add_pinter_brew_pack()
{
    return add_pinter_selected_brew(g_console_state.pinter_selected_brew_index);
}

/// @brief Removes one brew pack from inventory to correct manual counts.
bool remove_pinter_brew_pack()
{
    const uint8_t count = pinter_selected_brew_count();
    return count > 0U ? remove_pinter_selected_brew(static_cast<uint8_t>(count - 1U)) : false;
}

/// @brief Changes the current page for whichever Pinter list is visible.
bool change_pinter_list_page(int direction)
{
    uint8_t* page_index = nullptr;
    size_t item_count = 0U;

    switch (g_console_state.active_page)
    {
    case MenuPage::PinterSelectBrew:
        page_index = &g_console_state.pinter_catalogue_page_index;
        item_count = kPinterBrewCatalogue.size();
        break;
    case MenuPage::PinterSelectedBrews:
        page_index = &g_console_state.pinter_selected_brews_page_index;
        item_count = pinter_selected_brew_count();
        break;
    case MenuPage::PinterStartBrew:
        page_index = &g_console_state.pinter_start_brews_page_index;
        item_count = pinter_selected_brew_count();
        break;
    default:
        return false;
    }

    const uint8_t page_count = pinter_list_page_count(item_count);
    const int next_page = static_cast<int>(*page_index) + direction;
    if (next_page < 0 || next_page >= static_cast<int>(page_count))
    {
        return false;
    }

    *page_index = static_cast<uint8_t>(next_page);
    return true;
}

/// @brief Loads default recommended timing values for a queued brew selection.
bool prepare_pinter_start_from_queue(uint8_t queue_index)
{
    if (queue_index >= pinter_selected_brew_count())
    {
        return false;
    }

    const uint8_t brew_index = pinter_queued_brew_index(queue_index);
    const PinterBrewTiming& brew = pinter_brew_definition(brew_index);
    g_console_state.pinter_pending_inventory_index = queue_index;
    g_console_state.pinter_pending_brew_index = brew_index;
    g_console_state.pinter_pending_brewing_days = brew.recommended_brewing_days;
    g_console_state.pinter_pending_cold_crash_days = 0U;
    g_console_state.pinter_pending_conditioning_days = brew.recommended_conditioning_days;
    g_console_state.active_page = MenuPage::PinterStartTiming;
    return true;
}

/// @brief Handles one visible softkey item on the current Pinter list page.
bool select_pinter_list_item(uint8_t visible_index)
{
    if (visible_index >= kPinterBrewListVisibleCount)
    {
        return false;
    }

    switch (g_console_state.active_page)
    {
    case MenuPage::PinterSelectBrew:
    {
        const size_t brew_index =
            (static_cast<size_t>(g_console_state.pinter_catalogue_page_index) *
             kPinterBrewListVisibleCount) +
            visible_index;
        if (brew_index >= kPinterBrewCatalogue.size())
        {
            return false;
        }
        if (!add_pinter_selected_brew(static_cast<uint8_t>(brew_index)))
        {
            return false;
        }
        g_console_state.active_page = MenuPage::PinterSelectedBrews;
        return true;
    }
    case MenuPage::PinterStartBrew:
    {
        const uint8_t queue_index = static_cast<uint8_t>(
            (g_console_state.pinter_start_brews_page_index * kPinterBrewListVisibleCount) +
            visible_index);
        return prepare_pinter_start_from_queue(queue_index);
    }
    default:
        return false;
    }
}

/// @brief Sets pending start timings from one of the catalogue-provided presets.
bool set_pinter_pending_timing(bool minimum)
{
    const PinterBrewTiming& brew = pinter_brew_definition(g_console_state.pinter_pending_brew_index);
    g_console_state.pinter_pending_brewing_days = minimum ? brew.minimum_brewing_days : brew.recommended_brewing_days;
    g_console_state.pinter_pending_conditioning_days = minimum ? brew.minimum_conditioning_days : brew.recommended_conditioning_days;
    return true; /// @todo what is the purpose of this return, it will always be true?
}

/// @brief Adjusts one pending Pinter duration while keeping it in a sensible range.
bool adjust_pinter_pending_days(uint8_t& value, int direction, uint8_t minimum, uint8_t maximum)
{
    const int next_value = static_cast<int>(value) + direction;
    if (next_value < static_cast<int>(minimum) || next_value > static_cast<int>(maximum))
    {
        return false;
    }

    value = static_cast<uint8_t>(next_value);
    return true;
}

/// @brief Commits the pending start flow to the selected idle Pinter.
bool confirm_pinter_start()
{
    if (!selected_pinter_can_start())
    {
        return false;
    }

    const uint8_t queue_index = g_console_state.pinter_pending_inventory_index;
    if (queue_index >= pinter_selected_brew_count())
    {
        return false;
    }

    PinterStatus& pinter = selected_pinter();
    pinter.state = PinterState::Brewing;
    pinter.brew_index = pinter_queued_brew_index(queue_index);
    pinter.brew_start_day = current_pinter_event_day();
    pinter.cold_crash_start_day = 0U;
    pinter.conditioning_start_day = 0U;
    pinter.ready_day = 0U;
    pinter.planned_brewing_days = g_console_state.pinter_pending_brewing_days;
    pinter.planned_cold_crash_days = g_console_state.pinter_pending_cold_crash_days;
    pinter.planned_conditioning_days = g_console_state.pinter_pending_conditioning_days;
    pinter.cold_crash_used = false;

    const bool removed = remove_pinter_selected_brew(queue_index);
    g_console_state.pinter_pending_inventory_index = kInvalidPinterBrewSelection;
    g_console_state.active_page = MenuPage::Pinter;
    return removed;
}

/// @brief Applies the normal next real-world event for the selected Pinter.
bool apply_pinter_primary_action()
{
    PinterStatus& pinter = selected_pinter();
    if (!pinter_primary_action_enabled())
    {
        return false;
    }

    switch (pinter.state)
    {
    case PinterState::Idle:
        if (!prepare_pinter_start_from_queue(0U))
        {
            return false;
        }
        if (!confirm_pinter_start())
        {
            return false;
        }
        g_console_state.active_page = MenuPage::Pinter;
        return true;
    case PinterState::Brewing:
        if (pinter.planned_cold_crash_days > 0U && !pinter.cold_crash_used)
        {
            pinter.state = PinterState::ColdCrash;
            pinter.cold_crash_start_day = current_pinter_event_day();
            pinter.cold_crash_used = true;
            return true;
        }
        pinter.state = PinterState::Conditioning;
        pinter.conditioning_start_day = current_pinter_event_day();
        return true;
    case PinterState::ColdCrash:
        pinter.state = PinterState::Conditioning;
        pinter.conditioning_start_day = current_pinter_event_day();
        return true;
    case PinterState::Conditioning:
        pinter.state = PinterState::Ready;
        pinter.ready_day = current_pinter_event_day();
        return true;
    case PinterState::Ready:
        pinter.state = PinterState::Consumed;
        return true;
    case PinterState::Consumed:
        pinter.state = PinterState::Idle;
        pinter.brew_start_day = 0U;
        pinter.cold_crash_start_day = 0U;
        pinter.conditioning_start_day = 0U;
        pinter.ready_day = 0U;
        pinter.planned_brewing_days = 0U;
        pinter.planned_cold_crash_days = 0U;
        pinter.planned_conditioning_days = 0U;
        pinter.cold_crash_used = false;
        return true;
    }

    return false;
}

/// @brief Clears the selected Pinter back to idle after a mistaken manual event.
bool reset_selected_pinter()
{
    PinterStatus& pinter = selected_pinter();
    if (pinter.state == PinterState::Idle && !pinter.cold_crash_used)
    {
        return false;
    }

    pinter.state = PinterState::Idle;
    pinter.brew_index = static_cast<uint8_t>(
        pinter_brew_catalogue_index(g_console_state.pinter_selected_brew_index));
    pinter.brew_start_day = 0U;
    pinter.cold_crash_start_day = 0U;
    pinter.conditioning_start_day = 0U;
    pinter.ready_day = 0U;
    pinter.planned_brewing_days = 0U;
    pinter.planned_cold_crash_days = 0U;
    pinter.planned_conditioning_days = 0U;
    pinter.cold_crash_used = false;
    return true;
}

/// @brief Returns the parent page for one menu route in the current hierarchy.
MenuPage parent_page(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
    case MenuPage::Weather:
    case MenuPage::Calendar:
    case MenuPage::Status:
    case MenuPage::LocalConditions:
    case MenuPage::Settings:
    case MenuPage::Alignment:
    case MenuPage::Pinter:
    case MenuPage::Shares:
        return MenuPage::Home;
    case MenuPage::PinterPacks:
    case MenuPage::PinterToBeBrewed:
    case MenuPage::PinterStartBrew:
        return MenuPage::Pinter;
    case MenuPage::PinterSelectBrew:
    case MenuPage::PinterSelectedBrews:
        return MenuPage::PinterToBeBrewed;
    case MenuPage::PinterStartTiming:
        return MenuPage::PinterStartBrew;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        return MenuPage::Status;
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
    case MenuPage::AlertList:
        return g_console_state.alert_parent_page;
    case MenuPage::AlertDetail:
        return MenuPage::AlertList;
    case MenuPage::CalendarDetail:
        return MenuPage::Calendar;
    case MenuPage::ShareDetail:
        return MenuPage::Shares;
    case MenuPage::LocalConditionGraph:
        return MenuPage::LocalConditions;
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
    g_console_state.screen_saver_timeout_edit_minutes =
        g_console_state.screen_saver_timeout_minutes;
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
    g_console_state.screen_saver_timeout_edit_minutes =
        g_console_state.screen_saver_timeout_minutes;
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

/// @brief Returns the compact label for the current LTRS interpretation mode.
const char* letter_mode_text(LetterMode mode)
{
    switch (mode)
    {
    case LetterMode::UpperCase:
        return "ABC";
    case LetterMode::LowerCase:
        return "abc";
    case LetterMode::Numbers:
        return "123";
    }

    return "ABC";
}

/// @brief Advances the LTRS input mode through upper, lower, and numeric entry.
LetterMode next_letter_mode(LetterMode mode)
{
    switch (mode)
    {
    case LetterMode::UpperCase:
        return LetterMode::Numbers;
    case LetterMode::Numbers:
        return LetterMode::LowerCase;
    case LetterMode::LowerCase:
        return LetterMode::UpperCase;
    }

    return LetterMode::UpperCase;
}

/// @brief Cycles the shared LTRS input mode and reports the state change.
bool cycle_letter_mode()
{
    g_console_state.letter_mode = next_letter_mode(g_console_state.letter_mode);
    return true;
}

/// @brief Returns true when the current LTRS mode maps a button to a digit.
bool button_digit_value(ButtonId id, uint8_t* out_digit)
{
    if (out_digit == nullptr)
    {
        return false;
    }

    if (g_console_state.letter_mode != LetterMode::Numbers)
    {
        return false;
    }

    switch (id)
    {
    case ButtonId::AlphaJ:
        *out_digit = 1;
        return true;
    case ButtonId::AlphaK:
        *out_digit = 2;
        return true;
    case ButtonId::AlphaL:
        *out_digit = 3;
        return true;
    case ButtonId::AlphaP:
        *out_digit = 4;
        return true;
    case ButtonId::AlphaQ:
        *out_digit = 5;
        return true;
    case ButtonId::AlphaR:
        *out_digit = 6;
        return true;
    case ButtonId::AlphaV:
        *out_digit = 7;
        return true;
    case ButtonId::AlphaW:
        *out_digit = 8;
        return true;
    case ButtonId::AlphaX:
        *out_digit = 9;
        return true;
    case ButtonId::Zero:
        *out_digit = 0;
        return true;
    default:
        break;
    }

    return false;
}

/// @brief Maps a physical alpha key to its alphabetical character.
bool alpha_character_from_button(ButtonId id, char* out_character)
{
    if (out_character == nullptr)
    {
        return false;
    }

    // The `AlphaA..AlphaZ` enum values are intentionally contiguous to mirror
    // the physical keypad block. That keeps the conversion table-free while
    // still preserving the physical key identifiers elsewhere in the model.
    const uint8_t value = static_cast<uint8_t>(id);
    const uint8_t kFirstAlpha = static_cast<uint8_t>(ButtonId::AlphaA);
    const uint8_t kLastAlpha = static_cast<uint8_t>(ButtonId::AlphaZ);
    if (value < kFirstAlpha || value > kLastAlpha)
    {
        return false;
    }

    *out_character = static_cast<char>('A' + (value - kFirstAlpha));
    return true;
}

/// @brief Resolves one printable key according to the active LTRS mode.
bool text_character_from_button(ButtonId id, char* out_character)
{
    if (out_character == nullptr)
    {
        return false;
    }

    uint8_t digit = 0U;
    if (button_digit_value(id, &digit))
    {
        *out_character = static_cast<char>('0' + digit);
        return true;
    }

    switch (id)
    {
    case ButtonId::Slash:
        *out_character = '/';
        return true;
    case ButtonId::Dot:
        *out_character = '.';
        return true;
    case ButtonId::Spc:
        *out_character = ' ';
        return true;
    case ButtonId::Zero:
        if (g_console_state.letter_mode == LetterMode::Numbers)
        {
            *out_character = '0';
            return true;
        }
        return false;
    default:
        break;
    }

    if (g_console_state.letter_mode == LetterMode::Numbers)
    {
        return false;
    }

    char alpha = '\0';
    if (!alpha_character_from_button(id, &alpha))
    {
        return false;
    }

    if (g_console_state.letter_mode == LetterMode::LowerCase)
    {
        alpha = static_cast<char>(alpha - 'A' + 'a');
    }
    *out_character = alpha;
    return true;
}

/// @brief Appends or replaces the timeout scratchpad value with one digit.
bool apply_screen_saver_timeout_digit(uint8_t digit)
{
    if (!g_console_state.screen_saver_timeout_editing)
    {
        return false;
    }

    const uint16_t kCurrentMinutes = g_console_state.screen_saver_timeout_replace_on_next_digit
                                         ? 0
                                         : g_console_state.screen_saver_timeout_edit_minutes;
    const uint16_t kCandidateMinutes = g_console_state.screen_saver_timeout_replace_on_next_digit
                                           ? digit
                                           : static_cast<uint16_t>((kCurrentMinutes * 10U) + digit);
    if (kCandidateMinutes > kMaxScreenSaverTimeoutMinutes)
    {
        return false;
    }

    const bool kChanged = g_console_state.screen_saver_timeout_edit_minutes != kCandidateMinutes ||
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

/// @brief Advances the active weather page range without touching persisted config.
bool cycle_weather_period()
{
    const WeatherPeriod next = next_weather_period(g_console_state.weather_period);
    if (next == g_console_state.weather_period)
    {
        return false;
    }

    g_console_state.weather_period = next;
    return true;
}

/// @brief Returns the next shared-calendar owner filter in the Calendar page cycle.
CalendarOwner next_calendar_owner(CalendarOwner owner)
{
    switch (owner)
    {
    case CalendarOwner::Combined:
        return CalendarOwner::Owner1;
    case CalendarOwner::Owner1:
        return CalendarOwner::Owner2;
    case CalendarOwner::Owner2:
        return CalendarOwner::Owner3;
    case CalendarOwner::Owner3:
        return CalendarOwner::Owner4;
    case CalendarOwner::Owner4:
        return CalendarOwner::Owner5;
    case CalendarOwner::Owner5:
        return CalendarOwner::Owner6;
    case CalendarOwner::Owner6:
        return CalendarOwner::Owner7;
    case CalendarOwner::Owner7:
        return CalendarOwner::Owner8;
    case CalendarOwner::Owner8:
        return CalendarOwner::Combined;
    }

    return CalendarOwner::Combined;
}

/// @brief Advances the Calendar owner filter without touching persisted config.
bool cycle_calendar_owner()
{
    const CalendarOwner next = next_calendar_owner(g_console_state.calendar_owner);
    if (next == g_console_state.calendar_owner)
    {
        return false;
    }

    g_console_state.calendar_owner = next;
    return true;
}

/// @brief Returns whether the Calendar filters are showing the default view.
bool calendar_filters_are_default()
{
    return g_console_state.calendar_owner == CalendarOwner::Combined &&
           g_console_state.calendar_day_offset == 0;
}

/// @brief Restores the Calendar page to the default combined-today view.
bool reset_calendar_filters()
{
    if (calendar_filters_are_default())
    {
        return false;
    }

    g_console_state.calendar_owner = CalendarOwner::Combined;
    g_console_state.calendar_day_offset = 0;
    return true;
}

/// @brief Moves the Calendar page day selection within a bounded preview window.
/// @details The current UI slice stores days as offsets from today so the same
/// model can be filled by Home Assistant calendar data later.
bool change_calendar_day(int direction)
{
    if (g_console_state.active_page != MenuPage::Calendar || direction == 0)
    {
        return false;
    }

    const int target = static_cast<int>(g_console_state.calendar_day_offset) + direction;
    if (target < kCalendarMinDayOffset || target > kCalendarMaxDayOffset)
    {
        return false;
    }

    g_console_state.calendar_day_offset = static_cast<int8_t>(target);
    return true;
}

/// @brief Returns the backing event index for one visible Calendar softkey slot.
/// @details Slots are rebuilt from the filtered event list on demand so Home
/// Assistant data can replace the sample rows without duplicate indices.
uint8_t calendar_event_index_for_visible_slot(uint8_t visible_slot)
{
    uint8_t visible_index = 0U;
    const uint8_t event_count =
        std::min(g_console_state.calendar_event_count,
                 static_cast<uint8_t>(g_console_state.calendar_events.size()));
    for (uint8_t i = 0U; i < event_count; ++i)
    {
        if (!calendar_event_matches_filter(g_console_state.calendar_events[i]))
        {
            continue;
        }
        if (visible_index == visible_slot)
        {
            return i;
        }
        ++visible_index;
    }

    return static_cast<uint8_t>(g_console_state.calendar_events.size());
}

/// @brief Opens the detail page for one visible calendar event slot.
bool open_calendar_detail_from_slot(uint8_t visible_slot)
{
    const uint8_t event_index = calendar_event_index_for_visible_slot(visible_slot);
    if (event_index >= g_console_state.calendar_events.size())
    {
        return false;
    }

    g_console_state.selected_calendar_event_index = event_index;
    g_console_state.active_page = MenuPage::CalendarDetail;
    return true;
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

/// @brief Advances the active share detail period without touching persisted config.
bool cycle_share_period()
{
    const SharePeriod next = next_share_period(g_console_state.share_period);
    if (next == g_console_state.share_period)
    {
        return false;
    }

    g_console_state.share_period = next;
    return true;
}

/// @brief Opens the requested share detail page from the current watchlist.
bool select_share_slot(uint8_t slot)
{
    if (slot >= g_console_state.share_count || slot >= g_console_state.watched_shares.size())
    {
        return false;
    }

    g_console_state.selected_share_index = slot;
    g_console_state.active_page = MenuPage::ShareDetail;
    return true;
}

/// @brief Opens the currently selected share detail page from the watchlist.
bool open_selected_share_detail()
{
    return select_share_slot(g_console_state.selected_share_index);
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
    if (static_cast<uint8_t>(button) > static_cast<uint8_t>(ButtonId::RightBottom))
    {
        return SoftKeyId::Left1;
    }

    return static_cast<SoftKeyId>(static_cast<uint8_t>(button));
}

/// @brief Returns whether the input event belongs to one of the ten softkeys.
bool button_maps_to_softkey(ButtonId button)
{
    return static_cast<uint8_t>(button) <= static_cast<uint8_t>(ButtonId::RightBottom);
}

/// @brief Rebuilds the current softkey map from the active console state.
void update_softkeys_from_state()
{
    sync_system_alerts();

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
        softkeys[softkey_index(SoftKeyId::Left1)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"SHARES", SoftKeyRoute::GoShares, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"CALENDAR", SoftKeyRoute::GoCalendar, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_pinter_home_softkey_label(SoftKeyId::Left4), SoftKeyRoute::GoPinter, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "WEATHER",
                                          weather_source_selection_text(g_console_state)),
            SoftKeyRoute::GoWeather,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            "LOCAL\nCONDITIONS", SoftKeyRoute::GoLocalConditions, true};
        break;
    case MenuPage::Calendar:
    {
        constexpr std::array<SoftKeyId, 8> slots = {
            SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,  SoftKeyId::Left4,
            SoftKeyId::Right1, SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};
        constexpr std::array<SoftKeyRoute, 8> routes = {
            SoftKeyRoute::SelectCalendarSlot1, SoftKeyRoute::SelectCalendarSlot2,
            SoftKeyRoute::SelectCalendarSlot3, SoftKeyRoute::SelectCalendarSlot4,
            SoftKeyRoute::SelectCalendarSlot5, SoftKeyRoute::SelectCalendarSlot6,
            SoftKeyRoute::SelectCalendarSlot7, SoftKeyRoute::SelectCalendarSlot8};
        uint8_t visible_index = 0U;
        const uint8_t event_count =
            std::min(g_console_state.calendar_event_count,
                     static_cast<uint8_t>(g_console_state.calendar_events.size()));
        for (uint8_t i = 0U; i < event_count && visible_index < slots.size(); ++i)
        {
            const CalendarEvent& event = g_console_state.calendar_events[i];
            if (!calendar_event_matches_filter(event))
            {
                continue;
            }

            const SoftKeyId slot = slots[visible_index];
            softkeys[softkey_index(slot)] = {
                build_calendar_event_softkey_label(slot, event),
                routes[visible_index],
                true,
            };
            ++visible_index;
        }
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERSON",
                                          calendar_owner_selection_text(g_console_state)),
            SoftKeyRoute::CycleCalendarOwner,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right5)] =
            calendar_filters_are_default()
                ? SoftKeyAction{"HOME", SoftKeyRoute::GoHome, true}
                : SoftKeyAction{"RESET", SoftKeyRoute::ResetCalendarFilters, true};
        break;
    }
    case MenuPage::CalendarDetail:
        break;
    case MenuPage::Weather:
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERIOD",
                                          weather_period_selection_text(g_console_state)),
            SoftKeyRoute::CycleWeatherPeriod,
            true,
        };
        break;
    case MenuPage::Pinter:
    {
        constexpr std::array<SoftKeyId, kPinterCount> slots = {
            SoftKeyId::Left1,
            SoftKeyId::Left2,
            SoftKeyId::Left3,
            SoftKeyId::Left4,
        };
        constexpr std::array<SoftKeyRoute, kPinterCount> routes = {
            SoftKeyRoute::SelectPinterSlot1,
            SoftKeyRoute::SelectPinterSlot2,
            SoftKeyRoute::SelectPinterSlot3,
            SoftKeyRoute::SelectPinterSlot4,
        };
        for (size_t i = 0U; i < g_console_state.pinters.size(); ++i)
        {
            softkeys[softkey_index(slots[i])] = {
                build_pinter_slot_softkey_label(slots[i], g_console_state.pinters[i]),
                routes[i],
                true,
                i == g_console_state.selected_pinter_index,
            };
        }
        const bool selected_pinter_idle = selected_pinter_const().state == PinterState::Idle;
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_pinter_primary_action_label(SoftKeyId::Right1),
            selected_pinter_idle ? SoftKeyRoute::GoPinterStartBrew
                                 : SoftKeyRoute::ApplyPinterPrimaryAction,
            selected_pinter_idle ? pinter_brew_dock_count() < kPinterBrewDockCapacity
                                 : pinter_primary_action_enabled(),
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_pinter_pack_count_softkey_label(SoftKeyId::Right2, "TO BREW"),
            SoftKeyRoute::GoPinterToBeBrewed,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            "RESET",
            SoftKeyRoute::ResetSelectedPinter,
            selected_pinter_const().state != PinterState::Idle,
        };
        break;
    }
    case MenuPage::PinterPacks:
    case MenuPage::PinterToBeBrewed:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            "SELECT\nBREW",
            SoftKeyRoute::GoPinterSelectBrew,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_pinter_pack_count_softkey_label(SoftKeyId::Left2, "SELECTED"),
            SoftKeyRoute::GoPinterSelectedBrews,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {"PINTER", SoftKeyRoute::GoPinter, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    case MenuPage::PinterSelectBrew:
    {
        const size_t base_index = static_cast<size_t>(g_console_state.pinter_catalogue_page_index) *
                                  kPinterBrewListVisibleCount;
        for (uint8_t i = 0U; i < kPinterBrewListVisibleCount; ++i)
        {
            const size_t brew_index = base_index + i;
            if (brew_index >= kPinterBrewCatalogue.size())
            {
                break;
            }
            const SoftKeyId key = kPinterBrewListSoftkeys[i];
            softkeys[softkey_index(key)] = {
                build_pinter_catalogue_item_label(key, static_cast<uint8_t>(brew_index)),
                kPinterBrewListRoutes[i],
                pinter_selected_brew_count() < g_console_state.pinter_selected_brews.size(),
            };
        }
        break;
    }
    case MenuPage::PinterSelectedBrews:
    {
        const uint8_t selected_count = pinter_selected_brew_count();
        const uint8_t base_index = static_cast<uint8_t>(
            g_console_state.pinter_selected_brews_page_index * kPinterBrewListVisibleCount);
        if (selected_count == 0U)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {"NO BREWS\n[ADD FIRST]",
                                                         SoftKeyRoute::None, false};
        }
        for (uint8_t i = 0U; i < kPinterBrewListVisibleCount; ++i)
        {
            const uint8_t queue_index = static_cast<uint8_t>(base_index + i);
            if (queue_index >= selected_count)
            {
                break;
            }
            const SoftKeyId key = kPinterBrewListSoftkeys[i];
            softkeys[softkey_index(key)] = {
                build_pinter_queued_item_label(key, queue_index),
                SoftKeyRoute::None,
                false,
            };
        }
        break;
    }
    case MenuPage::PinterStartBrew:
    {
        const uint8_t selected_count = pinter_selected_brew_count();
        if (selected_pinter_const().state != PinterState::Idle)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {"NOT IDLE", SoftKeyRoute::None, false};
            softkeys[softkey_index(SoftKeyId::Right4)] = {"PINTER", SoftKeyRoute::GoPinter, true};
            break;
        }
        if (pinter_brew_dock_count() >= kPinterBrewDockCapacity)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {"NO DOCK\n[WAIT]",
                                                         SoftKeyRoute::None, false};
            softkeys[softkey_index(SoftKeyId::Right4)] = {"PINTER", SoftKeyRoute::GoPinter, true};
            break;
        }
        if (selected_count == 0U)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {"NO BREWS\n[ADD FIRST]",
                                                         SoftKeyRoute::None, false};
            softkeys[softkey_index(SoftKeyId::Right1)] = {
                "TO BE\nBREWED", SoftKeyRoute::GoPinterToBeBrewed, true};
            softkeys[softkey_index(SoftKeyId::Right4)] = {"PINTER", SoftKeyRoute::GoPinter, true};
            break;
        }

        const uint8_t base_index = static_cast<uint8_t>(
            g_console_state.pinter_start_brews_page_index * kPinterBrewListVisibleCount);
        for (uint8_t i = 0U; i < kPinterBrewListVisibleCount; ++i)
        {
            const uint8_t queue_index = static_cast<uint8_t>(base_index + i);
            if (queue_index >= selected_count)
            {
                break;
            }
            const SoftKeyId key = kPinterBrewListSoftkeys[i];
            softkeys[softkey_index(key)] = {
                build_pinter_queued_item_label(key, queue_index),
                kPinterBrewListRoutes[i],
                true,
            };
        }
        break;
    }
    case MenuPage::PinterStartTiming:
    {
        const PinterBrewTiming& brew =
            pinter_brew_definition(g_console_state.pinter_pending_brew_index);
        softkeys[softkey_index(SoftKeyId::Left1)] = {"MINIMUM",
                                                     SoftKeyRoute::SelectPinterMinimumTiming,
                                                     true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"RECOMM",
                                                     SoftKeyRoute::SelectPinterRecommendedTiming,
                                                     true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_pinter_days_label(SoftKeyId::Left3, "BREW -",
                                    g_console_state.pinter_pending_brewing_days),
            SoftKeyRoute::DecreasePinterBrewDays,
            g_console_state.pinter_pending_brewing_days > 1U,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            build_pinter_days_label(SoftKeyId::Right3, "BREW +",
                                    g_console_state.pinter_pending_brewing_days),
            SoftKeyRoute::IncreasePinterBrewDays,
            g_console_state.pinter_pending_brewing_days < 30U,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_pinter_days_label(SoftKeyId::Left4, "COND -",
                                    g_console_state.pinter_pending_conditioning_days),
            SoftKeyRoute::DecreasePinterConditioningDays,
            g_console_state.pinter_pending_conditioning_days > 1U,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            build_pinter_days_label(SoftKeyId::Right4, "COND +",
                                    g_console_state.pinter_pending_conditioning_days),
            SoftKeyRoute::IncreasePinterConditioningDays,
            g_console_state.pinter_pending_conditioning_days < 45U,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_pinter_days_label(SoftKeyId::Left5, "CRASH -",
                                    g_console_state.pinter_pending_cold_crash_days),
            SoftKeyRoute::DecreasePinterColdCrashDays,
            g_console_state.pinter_pending_cold_crash_days > 0U,
        };
        softkeys[softkey_index(SoftKeyId::Right5)] = {
            build_pinter_days_label(SoftKeyId::Right5, "CRASH +",
                                    g_console_state.pinter_pending_cold_crash_days),
            SoftKeyRoute::IncreasePinterColdCrashDays,
            g_console_state.pinter_pending_cold_crash_days < 3U,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(SoftKeyId::Right1, "START", selected_pinter_const().label.data()),
            SoftKeyRoute::ConfirmPinterStart,
            selected_pinter_can_start(),
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "BREW", brew.name),
            SoftKeyRoute::GoPinterStartBrew,
            true,
        };
        break;
    }
    case MenuPage::Shares:
        if (g_console_state.share_count > 0U)
        {
            const ShareWatchEntry& share = g_console_state.watched_shares[0];
            softkeys[softkey_index(SoftKeyId::Left1)] = {
                build_selection_softkey_label(SoftKeyId::Left1, share.display_name.data(),
                                              share.price_text.data()),
                SoftKeyRoute::SelectShareSlot1,
                true,
            };
        }
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            "HISTORY", SoftKeyRoute::GoSelectedShareDetail, g_console_state.share_count > 0U};
        softkeys[softkey_index(SoftKeyId::Right2)] = {"REMOVE", SoftKeyRoute::None, false};
        break;
    case MenuPage::ShareDetail:
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERIOD",
                                          share_period_selection_text(g_console_state)),
            SoftKeyRoute::CycleSharePeriod,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {"SHARES", SoftKeyRoute::GoShares, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    case MenuPage::Status:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"OVERVIEW", SoftKeyRoute::GoStatusOverview, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"CONNECT", SoftKeyRoute::GoStatusConnectivity, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"RESOURCES", SoftKeyRoute::GoStatusResources, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {"SENSORS", SoftKeyRoute::GoStatusSensors, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            "INTEGR.", SoftKeyRoute::GoStatusIntegrations, true};
        softkeys[softkey_index(SoftKeyId::Right4)] = {"KEYPAD", SoftKeyRoute::GoKeypadDebug, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        if (g_console_state.active_page == MenuPage::StatusSensors)
        {
            softkeys[softkey_index(SoftKeyId::Right4)] = {"KEYPAD", SoftKeyRoute::GoKeypadDebug, true};
        }
        softkeys[softkey_index(SoftKeyId::Right5)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        break;
    case MenuPage::LocalConditions:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "TEMP",
                                          local_temperature_selection_text()),
            SoftKeyRoute::ShowLocalTemperatureGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "HUMIDITY",
                                          local_humidity_selection_text()),
            SoftKeyRoute::ShowLocalHumidityGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(SoftKeyId::Left3, "AIR PRESSURE",
                                          local_pressure_selection_text()),
            SoftKeyRoute::ShowLocalPressureGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "VOC CHANGE",
                                          local_air_quality_selection_text()),
            SoftKeyRoute::ShowLocalAirQualityGraph,
            true,
        };
        break;
    case MenuPage::LocalConditionGraph:
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            "BACK", SoftKeyRoute::GoLocalConditions, true};
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
            build_selection_softkey_label(SoftKeyId::Left2, "SAVE PASSWORD",
                                          admin_requirement_selection_text(
                                              config_manager::settings().require_admin_password)),
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
            weather_source_definition(WeatherSource::OpenMeteo).option_label,
            SoftKeyRoute::SelectWeatherOpenMeteo,
            true,
            g_console_state.weather_source == WeatherSource::OpenMeteo,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1,
                g_console_state.weather_source == WeatherSource::HomeAssistant ? "WEATHER"
                                                                               : "COORDS",
                g_console_state.weather_source == WeatherSource::HomeAssistant
                    ? config_manager::settings().weather_entity_id.data()
                    : config_manager::settings().weather_coordinates.data()),
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
    case MenuPage::AlertList:
    {
        std::array<uint8_t, 24> alert_indices = {};
        uint8_t sorted_count = 0U;
        build_alert_display_indices(alert_indices, &sorted_count);
        constexpr uint8_t kAlertsPerPage = 9U;
        const uint8_t page_start =
            static_cast<uint8_t>(g_console_state.alert_list_page_index * kAlertsPerPage);
        const std::array<SoftKeyId, 9> slots = {
            SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,
            SoftKeyId::Left4,  SoftKeyId::Left5,  SoftKeyId::Right1,
            SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};
        const std::array<SoftKeyRoute, 9> routes = {
            SoftKeyRoute::SelectAlertSlot1, SoftKeyRoute::SelectAlertSlot2,
            SoftKeyRoute::SelectAlertSlot3, SoftKeyRoute::SelectAlertSlot4,
            SoftKeyRoute::SelectAlertSlot5, SoftKeyRoute::SelectAlertSlot6,
            SoftKeyRoute::SelectAlertSlot7, SoftKeyRoute::SelectAlertSlot8,
            SoftKeyRoute::SelectAlertSlot9};

        for (size_t i = 0U; i < slots.size(); ++i)
        {
            const uint8_t index = static_cast<uint8_t>(page_start + i);
            if (index >= sorted_count)
            {
                continue;
            }
            const ActiveAlert& alert = g_console_state.active_alerts[alert_indices[index]];
            softkeys[softkey_index(slots[i])] = {build_alert_softkey_label(slots[i], alert),
                                                 routes[i], true};
        }
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    }
    case MenuPage::AlertDetail:
        softkeys[softkey_index(SoftKeyId::Left5)] = {"IGNORE", SoftKeyRoute::AlertIgnore, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"ACCEPT", SoftKeyRoute::AlertAccept, true};
        break;
    }

    if (g_console_state.active_page != MenuPage::Home &&
        g_console_state.active_page != MenuPage::Calendar &&
        g_console_state.active_page != MenuPage::AlertList &&
        g_console_state.active_page != MenuPage::AlertDetail &&
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
    sync_system_alerts();

    // Alert and test lamps mirror the current logical state so the front panel
    // behaves like annunciators rather than generic status LEDs.
    AlertSeverity highest_severity = AlertSeverity::None;
    uint32_t newest_sequence = 0U;
    for (uint8_t i = 0U; i < g_console_state.alert_count; ++i)
    {
        const AlertSeverity severity = g_console_state.active_alerts[i].severity;
        if (static_cast<uint8_t>(severity) > static_cast<uint8_t>(highest_severity))
        {
            highest_severity = severity;
        }
        newest_sequence = std::max(newest_sequence, g_console_state.active_alerts[i].sequence);
    }
    g_console_state.alert_severity = highest_severity;

    const bool alert_annunciation_suppressed =
        g_console_state.alert_count > 0U && newest_sequence <= g_alert_acknowledged_sequence;
    if (alert_annunciation_suppressed)
    {
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::Off;
    }
    else
    {
        switch (highest_severity)
        {
        case AlertSeverity::None:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::Off;
            break;
        case AlertSeverity::Message:
        case AlertSeverity::Warning:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashSlow;
            break;
        case AlertSeverity::Alert:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashFast;
            break;
        }
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

/// @brief Opens the alert list from the current page when alerts exist.
bool open_alert_list_page()
{
    sync_system_alerts();
    uint32_t newest_sequence = 0U;
    for (uint8_t i = 0U; i < g_console_state.alert_count; ++i)
    {
        newest_sequence = std::max(newest_sequence, g_console_state.active_alerts[i].sequence);
    }
    g_alert_acknowledged_sequence = newest_sequence;
    if (g_console_state.active_page != MenuPage::AlertList &&
        g_console_state.active_page != MenuPage::AlertDetail)
    {
        g_console_state.alert_parent_page = g_console_state.active_page;
    }
    g_console_state.active_page = MenuPage::AlertList;
    return true;
}

/// @brief Opens one alert-detail page from the currently visible list page slot.
bool open_alert_detail_from_slot(uint8_t page_slot)
{
    std::array<uint8_t, 24> alert_indices = {};
    uint8_t sorted_count = 0U;
    build_alert_display_indices(alert_indices, &sorted_count);
    constexpr uint8_t kAlertsPerPage = 9U;
    const uint8_t absolute =
        static_cast<uint8_t>((g_console_state.alert_list_page_index * kAlertsPerPage) + page_slot);
    if (absolute >= sorted_count)
    {
        return false;
    }
    g_console_state.alert_detail_index = alert_indices[absolute];
    g_console_state.alert_detail_scroll_line = 0U;
    g_console_state.active_page = MenuPage::AlertDetail;
    return true;
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
    case SoftKeyRoute::GoCalendar:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Calendar;
        return true;
    case SoftKeyRoute::GoWeather:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Weather;
        return true;
    case SoftKeyRoute::GoPinter:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Pinter;
        return true;
    case SoftKeyRoute::GoPinterPacks:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::PinterPacks;
        return true;
    case SoftKeyRoute::GoPinterToBeBrewed:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::PinterToBeBrewed;
        return true;
    case SoftKeyRoute::GoPinterSelectBrew:
        stop_screen_saver_timeout_editing();
        clamp_pinter_list_page(g_console_state.pinter_catalogue_page_index,
                               kPinterBrewCatalogue.size());
        g_console_state.active_page = MenuPage::PinterSelectBrew;
        return true;
    case SoftKeyRoute::GoPinterSelectedBrews:
        stop_screen_saver_timeout_editing();
        clamp_pinter_list_page(g_console_state.pinter_selected_brews_page_index,
                               pinter_selected_brew_count());
        g_console_state.active_page = MenuPage::PinterSelectedBrews;
        return true;
    case SoftKeyRoute::GoPinterStartBrew:
        stop_screen_saver_timeout_editing();
        clamp_pinter_list_page(g_console_state.pinter_start_brews_page_index,
                               pinter_selected_brew_count());
        g_console_state.active_page = MenuPage::PinterStartBrew;
        return true;
    case SoftKeyRoute::SelectPinterSlot1:
        return select_pinter_slot(0U);
    case SoftKeyRoute::SelectPinterSlot2:
        return select_pinter_slot(1U);
    case SoftKeyRoute::SelectPinterSlot3:
        return select_pinter_slot(2U);
    case SoftKeyRoute::SelectPinterSlot4:
        return select_pinter_slot(3U);
    case SoftKeyRoute::CyclePinterBrew:
        return cycle_pinter_brew();
    case SoftKeyRoute::AddPinterBrewPack:
        return add_pinter_brew_pack();
    case SoftKeyRoute::RemovePinterBrewPack:
        return remove_pinter_brew_pack();
    case SoftKeyRoute::ApplyPinterPrimaryAction:
        return apply_pinter_primary_action();
    case SoftKeyRoute::ResetSelectedPinter:
        return reset_selected_pinter();
    case SoftKeyRoute::SelectPinterListItem1:
        return select_pinter_list_item(0U);
    case SoftKeyRoute::SelectPinterListItem2:
        return select_pinter_list_item(1U);
    case SoftKeyRoute::SelectPinterListItem3:
        return select_pinter_list_item(2U);
    case SoftKeyRoute::SelectPinterListItem4:
        return select_pinter_list_item(3U);
    case SoftKeyRoute::SelectPinterListItem5:
        return select_pinter_list_item(4U);
    case SoftKeyRoute::SelectPinterListItem6:
        return select_pinter_list_item(5U);
    case SoftKeyRoute::SelectPinterListItem7:
        return select_pinter_list_item(6U);
    case SoftKeyRoute::SelectPinterListItem8:
        return select_pinter_list_item(7U);
    case SoftKeyRoute::PinterListPreviousPage:
        return change_pinter_list_page(-1);
    case SoftKeyRoute::PinterListNextPage:
        return change_pinter_list_page(1);
    case SoftKeyRoute::SelectPinterMinimumTiming:
        return set_pinter_pending_timing(true);
    case SoftKeyRoute::SelectPinterRecommendedTiming:
        return set_pinter_pending_timing(false);
    case SoftKeyRoute::DecreasePinterBrewDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_brewing_days, -1, 1U,
                                          30U);
    case SoftKeyRoute::IncreasePinterBrewDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_brewing_days, 1, 1U,
                                          30U);
    case SoftKeyRoute::DecreasePinterConditioningDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_conditioning_days, -1,
                                          1U, 45U);
    case SoftKeyRoute::IncreasePinterConditioningDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_conditioning_days, 1, 1U,
                                          45U);
    case SoftKeyRoute::DecreasePinterColdCrashDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_cold_crash_days, -1, 0U,
                                          3U);
    case SoftKeyRoute::IncreasePinterColdCrashDays:
        return adjust_pinter_pending_days(g_console_state.pinter_pending_cold_crash_days, 1, 0U,
                                          3U);
    case SoftKeyRoute::ConfirmPinterStart:
        return confirm_pinter_start();
    case SoftKeyRoute::GoShares:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Shares;
        return true;
    case SoftKeyRoute::GoStatus:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::Status;
        return true;
    case SoftKeyRoute::GoStatusOverview:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::StatusOverview;
        return true;
    case SoftKeyRoute::GoStatusConnectivity:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::StatusConnectivity;
        return true;
    case SoftKeyRoute::GoStatusResources:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::StatusResources;
        return true;
    case SoftKeyRoute::GoStatusSensors:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::StatusSensors;
        return true;
    case SoftKeyRoute::GoStatusIntegrations:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::StatusIntegrations;
        return true;
    case SoftKeyRoute::GoLocalConditions:
        stop_screen_saver_timeout_editing();
        g_console_state.active_page = MenuPage::LocalConditions;
        return true;
    case SoftKeyRoute::ShowLocalTemperatureGraph:
        stop_screen_saver_timeout_editing();
        g_console_state.local_condition_metric = LocalConditionMetric::Temperature;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalHumidityGraph:
        stop_screen_saver_timeout_editing();
        g_console_state.local_condition_metric = LocalConditionMetric::Humidity;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalPressureGraph:
        stop_screen_saver_timeout_editing();
        g_console_state.local_condition_metric = LocalConditionMetric::AirPressure;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalAirQualityGraph:
        stop_screen_saver_timeout_editing();
        g_console_state.local_condition_metric = LocalConditionMetric::AirQuality;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
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
    case SoftKeyRoute::SelectWeatherOpenMeteo:
        return select_weather_source(WeatherSource::OpenMeteo);
    case SoftKeyRoute::CycleWeatherPeriod:
        return cycle_weather_period();
    case SoftKeyRoute::SelectShareSlot1:
        return select_share_slot(0U);
    case SoftKeyRoute::CycleSharePeriod:
        return cycle_share_period();
    case SoftKeyRoute::GoSelectedShareDetail:
        return open_selected_share_detail();
    case SoftKeyRoute::CycleCalendarOwner:
        return cycle_calendar_owner();
    case SoftKeyRoute::ResetCalendarFilters:
        return reset_calendar_filters();
    case SoftKeyRoute::SelectCalendarSlot1:
        return open_calendar_detail_from_slot(0U);
    case SoftKeyRoute::SelectCalendarSlot2:
        return open_calendar_detail_from_slot(1U);
    case SoftKeyRoute::SelectCalendarSlot3:
        return open_calendar_detail_from_slot(2U);
    case SoftKeyRoute::SelectCalendarSlot4:
        return open_calendar_detail_from_slot(3U);
    case SoftKeyRoute::SelectCalendarSlot5:
        return open_calendar_detail_from_slot(4U);
    case SoftKeyRoute::SelectCalendarSlot6:
        return open_calendar_detail_from_slot(5U);
    case SoftKeyRoute::SelectCalendarSlot7:
        return open_calendar_detail_from_slot(6U);
    case SoftKeyRoute::SelectCalendarSlot8:
        return open_calendar_detail_from_slot(7U);
    case SoftKeyRoute::SelectCalendarSlot9:
        return open_calendar_detail_from_slot(8U);
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
        return open_alert_list_page();
    case SoftKeyRoute::ToggleLetters:
        return cycle_letter_mode();
    case SoftKeyRoute::CycleTest:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        return true;
    case SoftKeyRoute::ResetConsoleState:
        g_console_state = make_default_console_state();
        g_alert_sequence_counter = 1U;
        g_home_assistant_connect_failures = 0U;
        g_weather_refresh_failures = 0U;
        g_mqtt_connect_failures = 0U;
        g_time_unsynced_samples = 0U;
        g_keypad_fault_samples = 0U;
        g_alert_acknowledged_sequence = 0U;
        g_alert_suppressed.fill(false);
        g_alert_was_active.fill(false);
        return true;
    case SoftKeyRoute::ClearAlert:
        if (g_console_state.alert_severity == AlertSeverity::None)
        {
            return false;
        }
        g_console_state.alert_severity = AlertSeverity::None;
        return true;
    case SoftKeyRoute::SelectAlertSlot1:
        return open_alert_detail_from_slot(0U);
    case SoftKeyRoute::SelectAlertSlot2:
        return open_alert_detail_from_slot(1U);
    case SoftKeyRoute::SelectAlertSlot3:
        return open_alert_detail_from_slot(2U);
    case SoftKeyRoute::SelectAlertSlot4:
        return open_alert_detail_from_slot(3U);
    case SoftKeyRoute::SelectAlertSlot5:
        return open_alert_detail_from_slot(4U);
    case SoftKeyRoute::SelectAlertSlot6:
        return open_alert_detail_from_slot(5U);
    case SoftKeyRoute::SelectAlertSlot7:
        return open_alert_detail_from_slot(6U);
    case SoftKeyRoute::SelectAlertSlot8:
        return open_alert_detail_from_slot(7U);
    case SoftKeyRoute::SelectAlertSlot9:
        return open_alert_detail_from_slot(8U);
    case SoftKeyRoute::AlertAccept:
        if (g_console_state.active_page != MenuPage::AlertDetail ||
            g_console_state.alert_detail_index >= g_console_state.alert_count)
        {
            return false;
        }
        g_alert_suppressed[static_cast<size_t>(
            g_console_state.active_alerts[g_console_state.alert_detail_index].code)] = true;
        erase_alert_at(g_console_state.alert_detail_index);
        g_console_state.active_page = MenuPage::AlertList;
        return true;
    case SoftKeyRoute::AlertIgnore:
        if (g_console_state.active_page != MenuPage::AlertDetail)
        {
            return false;
        }
        g_console_state.active_page = MenuPage::AlertList;
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
    g_alert_sequence_counter = 1U;
    g_home_assistant_connect_failures = 0U;
    g_weather_refresh_failures = 0U;
    g_mqtt_connect_failures = 0U;
    g_time_unsynced_samples = 0U;
    g_keypad_fault_samples = 0U;
    g_alert_acknowledged_sequence = 0U;
    g_alert_suppressed.fill(false);
    g_alert_was_active.fill(false);
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

const PinterBrewTiming& pinter_brew_timing(uint8_t brew_index)
{
    return pinter_brew_definition(brew_index);
}

/// @brief Returns and clears the pending redraw flag.
bool consume_redraw_request()
{
    const bool requested = g_redraw_requested;
    g_redraw_requested = false;
    return requested;
}

/// @brief Marks that non-hardware input should count as user activity.
void request_user_activity()
{
    g_user_activity_requested = true;
}

/// @brief Returns and clears the pending non-hardware user activity flag.
bool consume_user_activity_request()
{
    const bool requested = g_user_activity_requested;
    g_user_activity_requested = false;
    return requested;
}

/// @brief Applies persisted runtime preferences to the visible console state.
bool apply_runtime_config(const RuntimeConfig& settings)
{
    bool changed = false;

    const WeatherSource weather_source = settings.weather_source == WeatherSource::MetNorway
                                             ? WeatherSource::OpenMeteo
                                             : settings.weather_source;
    if (g_console_state.weather_source != weather_source)
    {
        g_console_state.weather_source = weather_source;
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
                          g_console_state.time_status.time_text != time_status.time_text ||
                          g_console_state.time_status.date_text != time_status.date_text ||
                          g_console_state.time_status.local_epoch_day !=
                              time_status.local_epoch_day ||
                          g_console_state.time_status.weekday_index != time_status.weekday_index;

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
        g_console_state.home_assistant_status.weather_daily_forecast_count !=
            home_assistant_status.weather_daily_forecast_count ||
        g_console_state.home_assistant_status.weather_daily_forecast !=
            home_assistant_status.weather_daily_forecast ||
        g_console_state.home_assistant_status.weather_metrics !=
            home_assistant_status.weather_metrics ||
        g_console_state.home_assistant_status.weather_alert_status !=
            home_assistant_status.weather_alert_status ||
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

/// @brief Updates the cached share market-data snapshot in the console model.
bool set_share_market_status(const ShareMarketStatus& share_market_status)
{
    const bool kChanged =
        g_console_state.share_data_configured != share_market_status.configured ||
        g_console_state.share_data_valid != share_market_status.data_valid ||
        g_console_state.share_data_last_error != share_market_status.last_error ||
        g_console_state.share_data_last_http_status != share_market_status.last_http_status ||
        g_console_state.share_count != share_market_status.share_count ||
        g_console_state.watched_shares != share_market_status.watched_shares;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.share_data_configured = share_market_status.configured;
    g_console_state.share_data_valid = share_market_status.data_valid;
    g_console_state.share_data_last_error = share_market_status.last_error;
    g_console_state.share_data_last_http_status = share_market_status.last_http_status;
    g_console_state.share_count = share_market_status.share_count;
    g_console_state.watched_shares = share_market_status.watched_shares;
    if (g_console_state.selected_share_index >= g_console_state.share_count)
    {
        g_console_state.selected_share_index = 0U;
    }
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached environment sensor discovery snapshot in the console model.
bool set_environment_sensor_status(
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status)
{
    if (environment_sensor_status_matches(g_console_state.environment_sensor_status,
                                          environment_sensor_status))
    {
        return false;
    }

    g_console_state.environment_sensor_status = environment_sensor_status;
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

/// @brief Updates foreground main-loop load telemetry shown by Resources status.
bool set_main_loop_load_status(const MainLoopLoadStatus& status)
{
    if (g_console_state.main_loop_load_status.valid == status.valid &&
        g_console_state.main_loop_load_status.load_percent == status.load_percent &&
        g_console_state.main_loop_load_status.sample_ms == status.sample_ms)
    {
        return false;
    }

    g_console_state.main_loop_load_status = status;
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

/// @brief Returns true for hard keys that represent text-entry surfaces.
bool is_text_entry_button(ButtonId id)
{
    const uint8_t value = static_cast<uint8_t>(id);
    if (value >= static_cast<uint8_t>(ButtonId::AlphaA) &&
        value <= static_cast<uint8_t>(ButtonId::AlphaZ))
    {
        return true;
    }

    switch (id)
    {
    case ButtonId::Slash:
    case ButtonId::TFunc:
    case ButtonId::Dot:
    case ButtonId::Zero:
    case ButtonId::Spc:
        return true;
    default:
        break;
    }

    return false;
}

/// @brief Applies globally active hard-key behaviours before softkey routing.
bool handle_direct_hard_key_event(ButtonId id)
{
    switch (id)
    {
    case ButtonId::Alert:
        return open_alert_list_page();
    case ButtonId::Test:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        return true;
    case ButtonId::Brt:
        if (g_console_state.panel_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        return true;
    case ButtonId::Dim:
        if (g_console_state.panel_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        return true;
    case ButtonId::Ltrs:
        if (!cycle_letter_mode())
        {
            return false;
        }
        std::printf("LTRS mode -> %s\n", letter_mode_text(g_console_state.letter_mode));
        return true;
    default:
        break;
    }

    if (!is_text_entry_button(id))
    {
        return false;
    }

    char character = '\0';
    if (text_character_from_button(id, &character))
    {
        PERIODIC_LOG("Text key accepted: mode=%s char=%c\n",
                     letter_mode_text(g_console_state.letter_mode),
                     (character == ' ') ? '_' : character);
    }
    else
    {
        PERIODIC_LOG("Text key accepted: mode=%s key=%s\n",
                     letter_mode_text(g_console_state.letter_mode), input::button_name(id));
    }
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

    if (event.id == ButtonId::Alert || event.id == ButtonId::Test || event.id == ButtonId::Brt ||
        event.id == ButtonId::Dim || event.id == ButtonId::Ltrs)
    {
        const bool kHardKeyChanged = handle_direct_hard_key_event(event.id);
        if (!kHardKeyChanged)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
        return true;
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
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
        return true;
    }

    if (event.id == ButtonId::CursorLeft || event.id == ButtonId::CursorRight)
    {
        const int direction = (event.id == ButtonId::CursorLeft) ? -1 : 1;
        bool changed = false;
        if (g_console_state.active_page == MenuPage::Settings)
        {
            changed = change_settings_page(direction);
        }
        else if (g_console_state.active_page == MenuPage::Calendar)
        {
            changed = change_calendar_day(direction);
        }
        else if (g_console_state.active_page == MenuPage::AlertList)
        {
            const int next_page =
                static_cast<int>(g_console_state.alert_list_page_index) + direction;
            if (next_page >= 0 && next_page < static_cast<int>(alert_page_count()))
            {
                g_console_state.alert_list_page_index = static_cast<uint8_t>(next_page);
                changed = true;
            }
        }
        else if (g_console_state.active_page == MenuPage::AlertDetail)
        {
            const ActiveAlert& alert =
                g_console_state.active_alerts[g_console_state.alert_detail_index];
            uint8_t max_lines = 0U;
            for (char c : alert.detail)
            {
                if (c == '\0')
                {
                    break;
                }
                if (c == '\n')
                {
                    ++max_lines;
                }
            }
            if (direction < 0 && g_console_state.alert_detail_scroll_line > 0U)
            {
                --g_console_state.alert_detail_scroll_line;
                changed = true;
            }
            else if (direction > 0 && g_console_state.alert_detail_scroll_line < max_lines)
            {
                ++g_console_state.alert_detail_scroll_line;
                changed = true;
            }
        }
        else if (g_console_state.active_page == MenuPage::PinterSelectBrew ||
                 g_console_state.active_page == MenuPage::PinterSelectedBrews ||
                 g_console_state.active_page == MenuPage::PinterStartBrew)
        {
            changed = change_pinter_list_page(direction);
        }
        else if (g_console_state.active_page == MenuPage::Shares && direction > 0)
        {
            changed = open_selected_share_detail();
        }

        if (!changed)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u settings=%u/%u calendar_day=%d\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     static_cast<unsigned>(g_console_state.settings_page_index + 1U),
                     static_cast<unsigned>(kSettingsPageCount),
                     static_cast<int>(g_console_state.calendar_day_offset));
        return true;
    }

    if (handle_direct_hard_key_event(event.id))
    {
        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
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
                 letter_mode_text(g_console_state.letter_mode),
                 static_cast<unsigned>(g_console_state.alert_severity),
                 static_cast<unsigned>(g_console_state.test_state),
                 static_cast<unsigned>(g_console_state.panel_brightness),
                 static_cast<unsigned>(g_console_state.key_backlight_brightness));
    return true;
}

/// @brief Cycles the test annunciator state for web preview bring-up.
bool cycle_test_lamp_preview()
{
    g_console_state.test_state = next_test_state(g_console_state.test_state);
    update_lamps_from_state();
    return true;
}

/// @brief Opens the alert list page on demand, preserving the caller page for IGNORE.
bool open_alert_page()
{
    const bool changed = open_alert_list_page();
    if (changed)
    {
        update_softkeys_from_state();
        update_lamps_from_state();
    }
    return changed;
}

} // namespace console_controller
