#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "alert_codes.h"
#include "environment_sensor_manager.h"
#include "home_controls.h"
#include "reset_reason.h"

/// @brief Physical softkeys mounted beside the display.
enum class SoftKeyId : uint8_t
{
    Left1 = 0,
    Left2,
    Left3,
    Left4,
    Left5,
    Right1,
    Right2,
    Right3,
    Right4,
    Right5,
    Count,
};

/// @brief Physical hard keys on the Merlin front panel.
enum class HardKeyId : uint8_t
{
    Alert = 0,
    Test,
    Brt,
    Dim,
    Ltrs,
    BackStep,
    CursorLeft,
    CursorRight,
    Slash,
    Clr,
    AlphaA,
    AlphaB,
    AlphaC,
    AlphaD,
    AlphaE,
    AlphaF,
    AlphaG,
    AlphaH,
    AlphaI,
    AlphaJ,
    AlphaK,
    AlphaL,
    AlphaM,
    AlphaN,
    AlphaO,
    AlphaP,
    AlphaQ,
    AlphaR,
    AlphaS,
    AlphaT,
    AlphaU,
    AlphaV,
    AlphaW,
    AlphaX,
    AlphaY,
    AlphaZ,
    TFunc,
    Dot,
    Zero,
    Spc,
    Count,
};

/// @brief Panel annunciators and lighting channels.
enum class LampId : uint8_t
{
    AlertLamp = 0,
    TestLamp,
    KeyBacklight,
    PanelBacklight,
    Count,
};

/// @brief Visual lamp state for annunciators.
enum class LampMode : uint8_t
{
    Off = 0,
    On,
    FlashSlow,
    FlashFast,
};

/// @brief Home Assistant's setting for the ALRT or TEST lamp (#150). Auto
/// leaves the lamp to the CCU; any other value overrides it.
enum class LampOverride : uint8_t
{
    Auto = 0,
    Off,
    On,
    FlashSlow,
    FlashFast,
};

/// @brief Text entry mode controlled by the `LTRS` key.
/// @details The Merlin panel carries both letter and number legends. The LTRS
/// key cycles how those physical keys are interpreted by editable UI fields.
enum class LetterMode : uint8_t
{
    UpperCase = 0,
    LowerCase,
    Numbers,
};

/// @brief Brightness level placeholder for panel and key lighting.
enum class BrightnessLevel : uint8_t
{
    Off = 0,
    Low,
    Medium,
    High,
};

/// @brief High-level alert state that can be driven from Home Assistant later.
enum class AlertSeverity : uint8_t
{
    None = 0,
    Message,
    Warning,
    Alert,
};

/// @brief High-level system test state associated with the TEST key/lamp.
enum class SystemTestState : uint8_t
{
    Idle = 0,
    Running,
    Passed,
    Failed,
};

/// @brief High-level menu pages shown on the display.
enum class MenuPage : uint8_t
{
    Home = 0,
    Calendar,
    CalendarDetail,
    Weather,
    Status,
    StatusOverview,
    StatusConnectivity,
    StatusResources,
    StatusSensors,
    StatusIntegrations,
    LocalConditions,
    LocalConditionGraph,
    Settings,
    DeviceSettings,
    WifiSettings,
    HomeAssistantSettings,
    MqttSettings,
    ScreenSaverSettings,
    ScreenSaverOptions,
    WeatherSources,
    TimeZoneSettings,
    KeypadDebug,
    GreyscaleTest,
    AlertList,
    AlertDetail,
    Pinter,
    PinterSelectBrew,
    PinterStartTiming,
    Shares,
    ShareDetail,
    /// Home Assistant entities to switch or run (#152).
    Controls,
    /// The font of each text style, chosen on the panel (#123).
    FontSettings,
    /// The fonts one style can take.
    FontOptions,
    /// Alerts that have ended, newest first (#203).
    AlertHistory,
    /// One past alert, read-only.
    AlertHistoryDetail,
    Count,
};

/// @brief Number of menu pages; page_table.h holds one entry for each.
inline constexpr size_t kMenuPageCount = static_cast<size_t>(MenuPage::Count);

/// @brief High-level Wi-Fi connectivity state for the Pico W radio.
enum class WifiConnectionState : uint8_t
{
    Disabled = 0,
    Unconfigured,
    Initializing,
    Scanning,
    Connecting,
    WaitingForIp,
    Connected,
    AuthFailed,
    NoNetwork,
    ConnectFailed,
    Error,
};

/// @brief High-level Home Assistant connectivity state.
enum class HomeAssistantConnectionState : uint8_t
{
    Disabled = 0,
    Unconfigured,
    WaitingForWifi,
    Resolving,
    Connecting,
    Authorizing,
    Connected,
    Unauthorized,
    Error,
};

/// @brief High-level MQTT connectivity state for Home Assistant discovery.
enum class MqttConnectionState : uint8_t
{
    Disabled = 0,
    Unconfigured,
    WaitingForWifi,
    Resolving,
    Connecting,
    Connected,
    AuthFailed,
    Error,
};

/// @brief Snapshot of Wi-Fi state suitable for UI and controller use.
struct WifiStatus
{
    WifiConnectionState state;
    bool credentials_present;
    bool internet_reachable;
    bool internet_probe_pending;
    int last_error;
    int link_status;
    int internet_rtt_ms;
    std::array<char, 12> auth_mode;
    std::array<char, 18> mac_address;
    std::array<char, 33> ssid;
    std::array<char, 16> ip_address;
};

/// @brief Snapshot of time state suitable for UI and controller use.
struct TimeStatus
{
    bool synced;
    std::array<char, 6> time_text;
    std::array<char, 11> date_text;
    uint32_t local_epoch_day;
    uint8_t weekday_index;
};

inline constexpr size_t kWeatherForecastEntryCount = 24;
inline constexpr size_t kWeatherDailyForecastEntryCount = 7;

/// @brief One compact hourly forecast entry for the dedicated weather page.
struct WeatherForecastEntry
{
    std::array<char, 6> time_text;
    std::array<char, 12> temperature_text;
    std::array<char, 8> wind_text;
    std::array<char, 20> condition_text;
    /// Exceptional weather in this hour (weather_extremes::Hazards, #202).
    uint8_t hazards;
};

/// @brief Compares two forecast rows field-by-field.
/// @details Equality is used to detect whether the rendered forecast content actually changed so
/// the UI can avoid redundant churn when Home Assistant data is stable.
inline bool operator==(const WeatherForecastEntry& lhs, const WeatherForecastEntry& rhs)
{
    return lhs.time_text == rhs.time_text && lhs.temperature_text == rhs.temperature_text &&
           lhs.wind_text == rhs.wind_text && lhs.condition_text == rhs.condition_text &&
           lhs.hazards == rhs.hazards;
}

/// @brief One compact daily forecast entry used by the Next 7 Days weather period.
struct WeatherDailyForecastEntry
{
    std::array<char, 11> date_text;
    std::array<char, 16> temperature_text;
    std::array<char, 8> wind_text;
    std::array<char, 20> condition_text;
    /// Exceptional weather on this day (weather_extremes::Hazards, #202).
    uint8_t hazards;
};

/// @brief Compares two daily forecast rows field-by-field.
inline bool operator==(const WeatherDailyForecastEntry& lhs, const WeatherDailyForecastEntry& rhs)
{
    return lhs.date_text == rhs.date_text && lhs.temperature_text == rhs.temperature_text &&
           lhs.wind_text == rhs.wind_text && lhs.condition_text == rhs.condition_text &&
           lhs.hazards == rhs.hazards;
}

/// @brief Typed weather values used for provider-neutral alert rules.
/// @details Display rows keep compact text because the EL320 UI needs fixed
/// buffers. Alerts need numeric values, so providers normalise current and
/// short-range forecast facts into this side-channel while preserving the
/// existing display payload.
struct WeatherMetrics
{
    bool current_temperature_celsius_valid;
    float current_temperature_celsius;
    bool current_wind_speed_mph_valid;
    float current_wind_speed_mph;
    bool forecast_min_temperature_celsius_valid;
    float forecast_min_temperature_celsius;
    bool forecast_max_temperature_celsius_valid;
    float forecast_max_temperature_celsius;
    bool forecast_max_wind_speed_mph_valid;
    float forecast_max_wind_speed_mph;
    bool forecast_max_wind_gust_mph_valid;
    float forecast_max_wind_gust_mph;
};

/// @brief Compares provider-neutral weather metrics field-by-field.
inline bool operator==(const WeatherMetrics& lhs, const WeatherMetrics& rhs)
{
    return lhs.current_temperature_celsius_valid == rhs.current_temperature_celsius_valid &&
           lhs.current_temperature_celsius == rhs.current_temperature_celsius &&
           lhs.current_wind_speed_mph_valid == rhs.current_wind_speed_mph_valid &&
           lhs.current_wind_speed_mph == rhs.current_wind_speed_mph &&
           lhs.forecast_min_temperature_celsius_valid ==
               rhs.forecast_min_temperature_celsius_valid &&
           lhs.forecast_min_temperature_celsius == rhs.forecast_min_temperature_celsius &&
           lhs.forecast_max_temperature_celsius_valid ==
               rhs.forecast_max_temperature_celsius_valid &&
           lhs.forecast_max_temperature_celsius == rhs.forecast_max_temperature_celsius &&
           lhs.forecast_max_wind_speed_mph_valid == rhs.forecast_max_wind_speed_mph_valid &&
           lhs.forecast_max_wind_speed_mph == rhs.forecast_max_wind_speed_mph &&
           lhs.forecast_max_wind_gust_mph_valid == rhs.forecast_max_wind_gust_mph_valid &&
           lhs.forecast_max_wind_gust_mph == rhs.forecast_max_wind_gust_mph;
}

inline bool operator!=(const WeatherMetrics& lhs, const WeatherMetrics& rhs)
{
    return !(lhs == rhs);
}

/// @brief One provider-originated weather warning normalised for the ALRT page.
/// @details Raised by severe conditions in a forecast, such as thunder, without
/// coupling the alert page to Home Assistant or Open-Meteo payloads. Official
/// Met Office and flood warnings are WeatherWarnings (#202).
struct WeatherAlertStatus
{
    bool provider_warning_active;
    AlertSeverity provider_warning_severity;
    std::array<char, 32> provider_warning_summary;
    std::array<char, 160> provider_warning_detail;
};

/// @brief Compares provider-originated warning state field-by-field.
inline bool operator==(const WeatherAlertStatus& lhs, const WeatherAlertStatus& rhs)
{
    return lhs.provider_warning_active == rhs.provider_warning_active &&
           lhs.provider_warning_severity == rhs.provider_warning_severity &&
           lhs.provider_warning_summary == rhs.provider_warning_summary &&
           lhs.provider_warning_detail == rhs.provider_warning_detail;
}

inline bool operator!=(const WeatherAlertStatus& lhs, const WeatherAlertStatus& rhs)
{
    return !(lhs == rhs);
}

/// @brief An official warning level, as the Met Office grades them (#202).
/// @details Environment Agency flood messages map onto the same three: flood
/// alert = Yellow, flood warning = Amber, severe flood warning = Red.
enum class WarningLevel : uint8_t
{
    None = 0,
    Yellow,
    Amber,
    Red,
};

/// @brief Official weather and flood warnings HA publishes over MQTT (#202).
/// @details Kept apart from HomeAssistantStatus, which some pages copy onto
/// the stack, and independent of the weather source: warnings come from HA
/// whichever source the forecast uses.
struct WeatherWarnings
{
    /// Highest level in force; None when HA reports no warnings, or before
    /// HA has sent any.
    WarningLevel level;
    /// The highest warning, e.g. `AMBER WIND`.
    std::array<char, 24> summary;
    /// Every warning in force, one per line, highest first.
    std::array<char, 240> detail;
    /// HA has not sent warnings for over two hours, so these may be out of
    /// date (home_assistant_manager::kWarningsStaleAfterMs).
    bool stale;
};

/// @brief Compares two warning snapshots field-by-field.
inline bool operator==(const WeatherWarnings& lhs, const WeatherWarnings& rhs)
{
    return lhs.level == rhs.level && lhs.summary == rhs.summary && lhs.detail == rhs.detail &&
           lhs.stale == rhs.stale;
}

inline bool operator!=(const WeatherWarnings& lhs, const WeatherWarnings& rhs)
{
    return !(lhs == rhs);
}

/// @brief Snapshot of Home Assistant state suitable for UI and controller use.
struct HomeAssistantStatus
{
    /// @name Home Assistant REST link (#165: independent of the weather source)
    /// @{
    HomeAssistantConnectionState state;
    bool configured;
    bool self_entity_published;
    int last_error;
    int last_http_status;
    std::array<char, 48> host;
    /// @}
    /// @name Weather fetch, from HA or Open-Meteo
    /// @{
    /// With HA as the source this follows `state`; with Open-Meteo it tracks
    /// that fetch alone, so neither failure marks the other down.
    HomeAssistantConnectionState weather_state;
    /// HTTP status of the latest weather request, 0 before the first.
    int weather_http_status;
    std::array<char, 48> weather_host;
    /// @}
    std::array<char, 48> tracked_entity_id;
    std::array<char, 24> tracked_entity_state;
    std::array<char, 48> weather_entity_id;
    std::array<char, 80> weather_source_hint;
    std::array<char, 24> weather_condition;
    std::array<char, 16> weather_temperature;
    std::array<char, 8> weather_wind_unit;
    std::array<char, 6> sunrise_text;
    std::array<char, 6> sunset_text;
    uint8_t weather_forecast_count;
    std::array<WeatherForecastEntry, kWeatherForecastEntryCount> weather_forecast;
    uint8_t weather_daily_forecast_count;
    std::array<WeatherDailyForecastEntry, kWeatherDailyForecastEntryCount> weather_daily_forecast;
    WeatherMetrics weather_metrics;
    WeatherAlertStatus weather_alert_status;
    std::array<char, 48> self_entity_id;
    /// @brief Boot-uptime (`to_ms_since_boot`) of the last genuinely successful
    /// weather refresh (condition text actually parsed, not a "?" fallback), 0
    /// if never. Used by the stale-data display policy (issue #16); see
    /// screens::build_data_freshness_text(). Deliberately excluded from
    /// operator== below so it doesn't force a redraw every tick -- see that
    /// function's own comment.
    uint32_t weather_last_success_ms;
};

/// @brief Compares two Home Assistant status snapshots field-by-field.
/// @details Used to detect whether `update()` produced a UI-visible change
/// without relying on `memcmp` over the whole struct, which is not immune to
/// padding bytes. `weather_last_success_ms` is deliberately NOT compared here:
/// it advances every successful refresh even when the displayed text is
/// unchanged, and pages that show its derived age text get their own periodic
/// redraw tick instead (see MerlinCCU.cpp) so the age can advance without
/// this comparator forcing a redraw on every single main-loop tick.
inline bool operator==(const HomeAssistantStatus& lhs, const HomeAssistantStatus& rhs)
{
    return lhs.state == rhs.state && lhs.configured == rhs.configured &&
           lhs.self_entity_published == rhs.self_entity_published &&
           lhs.last_error == rhs.last_error && lhs.last_http_status == rhs.last_http_status &&
           lhs.host == rhs.host && lhs.weather_state == rhs.weather_state &&
           lhs.weather_http_status == rhs.weather_http_status &&
           lhs.weather_host == rhs.weather_host && lhs.tracked_entity_id == rhs.tracked_entity_id &&
           lhs.tracked_entity_state == rhs.tracked_entity_state &&
           lhs.weather_entity_id == rhs.weather_entity_id &&
           lhs.weather_source_hint == rhs.weather_source_hint &&
           lhs.weather_condition == rhs.weather_condition &&
           lhs.weather_temperature == rhs.weather_temperature &&
           lhs.weather_wind_unit == rhs.weather_wind_unit &&
           lhs.sunrise_text == rhs.sunrise_text && lhs.sunset_text == rhs.sunset_text &&
           lhs.weather_forecast_count == rhs.weather_forecast_count &&
           lhs.weather_forecast == rhs.weather_forecast &&
           lhs.weather_daily_forecast_count == rhs.weather_daily_forecast_count &&
           lhs.weather_daily_forecast == rhs.weather_daily_forecast &&
           lhs.weather_metrics == rhs.weather_metrics &&
           lhs.weather_alert_status == rhs.weather_alert_status &&
           lhs.self_entity_id == rhs.self_entity_id;
}

inline bool operator!=(const HomeAssistantStatus& lhs, const HomeAssistantStatus& rhs)
{
    return !(lhs == rhs);
}

/// @brief Snapshot of MQTT discovery state suitable for UI and controller use.
struct MqttStatus
{
    MqttConnectionState state;
    bool configured;
    bool discovery_published;
    int last_error;
    std::array<char, 48> broker;
    std::array<char, 32> device_id;
};

/// @brief Compares two MQTT status snapshots field-by-field.
inline bool operator==(const MqttStatus& lhs, const MqttStatus& rhs)
{
    return lhs.state == rhs.state && lhs.configured == rhs.configured &&
           lhs.discovery_published == rhs.discovery_published &&
           lhs.last_error == rhs.last_error && lhs.broker == rhs.broker &&
           lhs.device_id == rhs.device_id;
}

inline bool operator!=(const MqttStatus& lhs, const MqttStatus& rhs)
{
    return !(lhs == rhs);
}

/// @brief Weather backends stored in flash and exposed to the CCU UI.
enum class WeatherSource : uint8_t
{
    HomeAssistant = 0,
    OpenMeteo,
    /// Legacy value kept so older flash settings can be migrated safely.
    MetNorway,
};

/// @brief Weather forecast range modes available on the Weather page.
enum class WeatherPeriod : uint8_t
{
    Hourly = 0,
    NextTwentyFourHours,
    NextSevenDays,
};

/// @brief Share price history periods shown on the share detail page.
enum class SharePeriod : uint8_t
{
    Today = 0,
    Week,
    Month,
    Year,
    AllTime,
};

/// @brief Family calendar owner filter shown on the Calendar page.
enum class CalendarOwner : uint8_t
{
    Combined = 0,
    Owner1,
    Owner2,
    Owner3,
    Owner4,
    Owner5,
    Owner6,
    Owner7,
    Owner8,
};

/// @brief Selectable screen-saver modes exposed by the current UI.
enum class ScreenSaverSelection : uint8_t
{
    Life = 0,
    Clock,
    Starfield,
    Matrix,
    Radar,
    Rain,
    Worms,
    Random,
};

/// @brief Selectable time-zone presets exposed by the current menu flow.
enum class TimeZoneSelection : uint8_t
{
    AtlanticStandard = 0,
    ArgentinaStandard,
    SouthGeorgia,
    Azores,
    EuropeLondon,
    CentralEuropean,
    EasternEuropean,
    ArabiaStandard,
    GulfStandard,
};

/// @brief Pages needed to list `item_count` items `per_page` to a page;
/// always at least one, so an empty list still has a page to show.
/// @details The one page-count rule (BODY-6). Every paged list derives its
/// page count from this, and the controller's paging, the header title
/// (`NAME n/m`) and the footer arrows all read that one count.
constexpr uint8_t list_page_count(size_t item_count, size_t per_page)
{
    return item_count == 0U || per_page == 0U
               ? 1U
               : static_cast<uint8_t>((item_count + per_page - 1U) / per_page);
}

/// @brief Number of top-level Settings menu section-group pages.
inline constexpr uint8_t kSettingsPageCount = 2U;

/// @brief Number of selectable time-zone presets (settings_controller's table).
inline constexpr uint8_t kTimeZoneSelectionCount = 9U;
static_assert(static_cast<uint8_t>(TimeZoneSelection::GulfStandard) + 1U == kTimeZoneSelectionCount,
              "kTimeZoneSelectionCount must match TimeZoneSelection");
/// @brief Option keys per Time Zone page: L1-L4 then R1-R4 (NAV-6).
inline constexpr uint8_t kTimeZoneOptionsPerPage = 8U;
/// @brief Pages needed to list every time zone, shared by the controller's
/// paging, the header title, and the footer arrows.
inline constexpr uint8_t kTimeZonePageCount =
    list_page_count(kTimeZoneSelectionCount, kTimeZoneOptionsPerPage);

/// @brief Fonts page entries (#123): one per text style, then ALL DEFAULTS.
/// settings_controller.cpp checks the style count against text_styles.
inline constexpr uint8_t kFontStyleCount = 17U;
inline constexpr uint8_t kFontListEntryCount = kFontStyleCount + 1U;
inline constexpr uint8_t kFontEntriesPerPage = 8U;
inline constexpr uint8_t kFontPageCount = list_page_count(kFontListEntryCount, kFontEntriesPerPage);

/// @brief Local environmental metric selected for the graph detail page.
enum class LocalConditionMetric : uint8_t
{
    Temperature = 0,
    Humidity,
    AirPressure,
    AirQuality,
};

/// @brief Physical Pinter lifecycle states tracked by the manual brew scheduler.
enum class PinterState : uint8_t
{
    Idle = 0,
    Brewing,
    ColdCrash,
    Conditioning,
    Ready,
    Consumed,
};

inline constexpr size_t kPinterCount = 4;
inline constexpr size_t kPinterBrewCatalogueCount = 35;
inline constexpr uint8_t kDefaultPinterBrewIndex = 0U;
inline constexpr uint8_t kPinterBrewDockCapacity = 3U;
inline constexpr uint8_t kPinterFridgeCapacity = 2U;
inline constexpr size_t kPinterBrewListVisibleCount = 8;
/// @brief Pages needed to show the whole brew catalogue on Select Brew.
/// @details Shared by the header title (`SELECT BREW n/m`) and the footer
/// arrows so both agree with the controller's cursor paging.
inline constexpr uint8_t kPinterBrewListPageCount =
    list_page_count(kPinterBrewCatalogueCount, kPinterBrewListVisibleCount);

/// @brief Semantic action currently assigned to a contextual softkey.
/// @details Routes that act on one of several things (a page, a list entry,
/// an option) take which one from SoftKeyAction::slot, so a new page or list
/// entry needs no new route.
enum class SoftKeyRoute : uint8_t
{
    None = 0,
    /// Opens the page `slot` (a MenuPage), running its table entry's
    /// on_enter hook (page_table.h).
    GoPage,
    /// Opens the Local Conditions graph for metric `slot` (LocalConditionMetric).
    ShowLocalGraph,
    EditScreenSaverTimeout,
    ConfirmScreenSaverTimeout,
    CancelScreenSaverTimeout,
    ToggleRemoteConfig,
    ToggleHomeAssistantEnabled,
    ToggleMqttEnabled,
    /// Chooses screen saver `slot` (ScreenSaverSelection).
    SelectScreenSaver,
    /// Chooses weather source `slot` (WeatherSource).
    SelectWeatherSource,
    CycleWeatherPeriod,
    /// Chooses the time zone in key `slot` of the current list page.
    SelectTimeZoneSlot,
    /// Opens the alert in key `slot` of the current list page.
    SelectAlertSlot,
    AlertAccept,
    AlertIgnore,
    /// Opens the past alert in key `slot` of the current History page (#203).
    SelectAlertHistorySlot,
    /// History's CLEAR ALL, with a confirm step.
    RequestClearAlertHistory,
    ConfirmClearAlertHistory,
    CancelClearAlertHistory,
    /// Selects Pinter vessel `slot`.
    SelectPinterSlot,
    ApplyPinterPrimaryAction,
    RequestPinterReset,
    ConfirmPinterReset,
    CancelPinterReset,
    /// Device Identity: clear a forgotten web PIN (#158), with a confirm step.
    RequestClearWebPin,
    ConfirmClearWebPin,
    CancelClearWebPin,
    DecreaseSelectedPinterDay,
    IncreaseSelectedPinterDay,
    /// Picks the recipe in key `slot` of the current catalogue page.
    SelectPinterListItem,
    SelectPinterMinimumTiming,
    SelectPinterRecommendedTiming,
    DecreasePinterBrewDays,
    IncreasePinterBrewDays,
    DecreasePinterConditioningDays,
    IncreasePinterConditioningDays,
    DecreasePinterColdCrashDays,
    IncreasePinterColdCrashDays,
    ConfirmPinterStart,
    /// Opens watched share `slot`.
    SelectShareSlot,
    CycleSharePeriod,
    CycleCalendarOwner,
    ResetCalendarFilters,
    /// Opens the calendar event in visible key `slot`.
    SelectCalendarSlot,
    /// Switches or runs the Home Assistant entity in key `slot` of the
    /// current Controls page (#152).
    PressHomeControl,
    /// Opens the font options for the style in key `slot` of the current
    /// Fonts page (#123).
    SelectFontStyle,
    /// Puts every text style back to its default font.
    ResetFonts,
    /// Gives the selected style font `slot` (fonts::FontFace).
    SelectFontFace,
};

/// @brief Maximum simultaneously active alerts (matches ConsoleState::active_alerts).
/// @details One entry per alert type, so this follows the alert code list and
/// never needs raising by hand (NAV-6a).
inline constexpr size_t kActiveAlertCapacity = kAlertCodeCount;

/// @brief Alerts shown on one Alert or History list page: L1-L4 and R1-R4
/// (NAV-6). L5 opens History, or clears it (#203).
inline constexpr uint8_t kAlertsPerPage = 8U;

/// @brief Alert list pages needed for `alert_count` alerts; always at least one.
constexpr uint8_t alert_list_page_count(uint8_t alert_count)
{
    return list_page_count(alert_count, kAlertsPerPage);
}

/// @brief Highest occurrence count kept for one alert; shown as `x99`.
inline constexpr uint8_t kMaxAlertOccurrenceCount = 99U;

/// @brief One active alert surfaced by the CCU alert workflow.
struct ActiveAlert
{
    AlertSeverity severity;
    uint8_t code;
    uint32_t sequence;
    std::array<char, 6> occurred_time_text;
    /// Times this alert has gone from clear to active since it was last
    /// accepted or the CCU restarted, capped at kMaxAlertOccurrenceCount.
    uint8_t occurrence_count;
    /// The operator pressed IGNORE on it; history notes that (#203).
    bool ignored;
    /// Local minutes when it was raised and last ignored, 0 when the clock
    /// had not synced (alert_history::local_minute).
    uint32_t raised_minute;
    uint32_t ignored_minute;
    std::array<char, 32> summary;
    std::array<char, 320> detail;
};

/// @brief Maximum number of shares a user can watch simultaneously.
inline constexpr size_t kMaxWatchedShares = 6U;

/// @brief One watched share shown by the CCU shares workflow.
struct ShareWatchEntry
{
    std::array<char, 24> display_name;
    std::array<char, 10> symbol;
    std::array<char, 8> exchange;
    std::array<char, 8> currency;
    std::array<char, 12> price_text;
    std::array<char, 12> change_text;
    /// Closing prices across the period, oldest first, in the feed's unit
    /// (pence for GBX), fractions kept (#178).
    std::array<float, 24> history_points;
    /// The price the period's change is measured from, when the feed sends
    /// it: the previous close for Today. Otherwise the first point is used.
    float base_price;
    bool base_valid;
};

/// @brief Compares two watched-share rows field-by-field for redraw detection.
inline bool operator==(const ShareWatchEntry& lhs, const ShareWatchEntry& rhs)
{
    return lhs.display_name == rhs.display_name && lhs.symbol == rhs.symbol &&
           lhs.exchange == rhs.exchange && lhs.currency == rhs.currency &&
           lhs.price_text == rhs.price_text && lhs.change_text == rhs.change_text &&
           lhs.history_points == rhs.history_points && lhs.base_price == rhs.base_price &&
           lhs.base_valid == rhs.base_valid;
}

/// @brief Latest market-data snapshot that can be copied into the UI model.
/// @details The share fetcher owns provider/network state and the controller
/// mirrors only this compact, display-ready representation.
struct ShareMarketStatus
{
    bool configured;
    bool data_valid;
    int last_error;
    int last_http_status;
    SharePeriod period;
    uint8_t share_count;
    std::array<ShareWatchEntry, kMaxWatchedShares> watched_shares;
    /// @brief Boot-uptime of the last successful live fetch, 0 if never (this
    /// stays 0 for the whole time kEnableLiveShareFetch is false -- demo data
    /// is a deliberate placeholder, not a freshness concept). Excluded from
    /// share_price_manager.cpp's status_changed() for the same reason
    /// HomeAssistantStatus::weather_last_success_ms is excluded there.
    uint32_t last_success_ms;
};

inline constexpr uint8_t kInvalidWeekdayIndex = 255U;
inline constexpr int kCalendarMinDayOffset = -14;
inline constexpr int kCalendarMaxDayOffset = 14;
inline constexpr size_t kCalendarEventCapacity = 48;

/// @brief One compact calendar row ready for display on a softkey.
struct CalendarEvent
{
    CalendarOwner owner;
    int8_t day_offset;
    std::array<char, 6> start_time;
    std::array<char, 6> end_time;
    std::array<char, 32> title;
    std::array<char, 40> location;
    std::array<char, 32> reminder;
    std::array<char, 48> attendees;
    std::array<char, 64> description;
};

/// @brief The role of a softkey (docs/ui_requirements.md section 3, SK-1).
/// @details The role fixes how the label is written and what a press does.
/// Keys are only ever built by the matching builder in softkey_builders.h.
enum class SoftKeyRole : uint8_t
{
    Blank,    ///< No label; a press does nothing.
    Navigate, ///< Opens the named page; may summarise it in brackets.
    Select,   ///< Opens a page of option keys for its value.
    Action,   ///< Does the named thing immediately.
    Toggle,   ///< Switches a two-state setting in place.
    View,     ///< At L5: steps what the whole page shows.
    Step,     ///< Moves a number down or up by one step.
    Status,   ///< Shows a value reported from elsewhere; inert.
    Option,   ///< One choice on the page a select key opens.
    Item,     ///< One entry of a list.
};

/// @brief Semantic action currently assigned to a contextual softkey.
struct SoftKeyAction
{
    const char* label;
    SoftKeyRoute route;
    /// False when a press must do nothing (status and blank keys, and a
    /// field while it is being edited).
    bool enabled;
    /// Inverse video: the selected option or item, or the field being
    /// edited, and nothing else (LBL-5).
    bool inverted = false;
    SoftKeyRole role = SoftKeyRole::Blank;
    /// Which page, entry or option the route acts on; see SoftKeyRoute.
    uint8_t slot = 0U;
};

/// @brief The ten softkey assignments around the screen.
using SoftKeyMap = std::array<SoftKeyAction, static_cast<size_t>(SoftKeyId::Count)>;

/// @brief True when pressing `action` opens `page`.
constexpr bool opens_page(const SoftKeyAction& action, MenuPage page)
{
    return action.route == SoftKeyRoute::GoPage && action.slot == static_cast<uint8_t>(page);
}

/// @brief One key meaning in the current letter mode.
struct KeyLegend
{
    const char* primary;
    const char* alternate;
};

/// @brief Snapshot of keypad bring-up state shown on the diagnostics page.
struct KeypadDebugStatus
{
    std::array<char, 24> pressed_key_name;
    uint32_t active_mask;
    uint8_t configured_count;
    uint8_t active_count;
    std::array<char, 48> active_panel_pins;
    uint8_t probe_drive_panel_pin;
    uint32_t probe_hit_mask;
    uint8_t probe_hit_count;
    std::array<char, 48> probe_hit_panel_pins;
};

/// @brief Low-overhead foreground loop telemetry shown on the Resources status page.
/// @details This is main-loop load, not whole-chip CPU usage. It measures time
/// spent doing foreground firmware work versus intentional foreground sleep, so
/// DMA/PIO scanout and background Wi-Fi callbacks are not fully represented.
struct MainLoopLoadStatus
{
    bool valid;
    uint8_t load_percent;
    uint16_t sample_ms;
};

/// @brief Live newlib heap usage shown on the Resources status page.
/// @details Sourced from mallinfo(): arena_bytes is how far the heap has grown
/// from the system (via sbrk), used_bytes is how much of that growth is
/// currently allocated. Static buffers (ConsoleState, framebuffers) are
/// tracked separately since they never touch the heap. peak_arena_bytes is
/// the largest arena seen since boot; with the heap's size (RAM left after
/// the static image, `ImageFootprintStatus`) it drives the HeapLow alert.
struct HeapStatus
{
    bool valid;
    uint32_t used_bytes;
    uint32_t arena_bytes;
    uint32_t peak_arena_bytes;
};

/// @brief Worst-case core-0 stack headroom shown on the Resources status page.
/// @details Sourced from the boot-time canary fill and high-water scan in
/// stack_guard -- a measured figure, not a guess. Only ever decreases across
/// a boot, so it is a worst case, not a live value. Drives the StackLow alert.
struct StackStatus
{
    bool valid;
    uint32_t free_bytes;
    /// The previous reset was the stack limit fault (an overflow caught).
    bool overflow_reset;
};

/// @brief Why the unit last started and how long it has run since (#155).
/// @details The reason is read once at boot (reset_reason.h); uptime is
/// whole minutes so the Overview page redraws once a minute at most.
struct BootStatus
{
    ResetReason reset_reason;
    uint32_t uptime_minutes;
};

/// @brief Which firmware slot is running and how the last update went (#154).
/// @details Set by ota_manager from the boot ROM's boot info at start-up and
/// again when a trial image confirms itself. Drives the
/// FirmwareUpdateRolledBack alert.
struct FirmwareStatus
{
    bool valid;
    /// The flash holds an A/B partition table; without one, OTA is off.
    bool partitioned;
    /// Running slot: 0 is A, 1 is B; -1 when not booted from a partition.
    int8_t slot;
    /// Running on trial after an update, not yet confirmed.
    bool trial;
    /// The previous boot tried an uploaded image and fell back to this one.
    bool rolled_back;
    /// Git commit the running image was built from, e.g. "2bf47da" ("+"
    /// marks uncommitted changes).
    std::array<char, 16> version;
    /// When the running image was built, local time, e.g. "26 Nov 15:35".
    std::array<char, 16> built;
};

/// @brief Panel scanout timing telemetry, sampled from `display::frame_count()`
/// and `display::last_rebuild_us()`.
/// @details Exists to measure actual hardware timing before committing to any
/// temporal-dithering or per-frame raster-regeneration design — see
/// docs/greyscale-investigation.md and docs/multicore-raster-regen-design.md.
/// The `DisplayPipelineLag` alert compares `frame_rate_hz` with
/// `nominal_frame_rate_hz` once per sample (issue #144).
struct DisplayTimingStatus
{
    bool valid;
    uint16_t frame_rate_hz;
    uint32_t last_rebuild_us;
    /// @brief `display::present_skipped_count()` at the same sample point.
    /// Sampled here (issue #71) rather than read directly by
    /// status_screens.cpp's Resources page, so that file stays free of
    /// `display.h`'s `hardware/pio.h` dependency and is host-testable.
    uint32_t present_skipped_count;
    /// @brief `display::nominal_frame_rate_hz()` at the same sample point.
    uint16_t nominal_frame_rate_hz;
    /// @brief Rises by one per sampling window, so the alert debounce counts
    /// samples rather than `alert_controller::sync()` calls, which happen on
    /// every state change.
    uint32_t sample_sequence;
};

/// @brief Linked-image flash/RAM footprint, shown on the Resources status
/// page.
/// @details Sampled exactly once, in `make_default_console_state()` --
/// unlike `HeapStatus`/`StackStatus`, these figures come from linker symbols
/// fixed at build time, so they never change across a boot and don't need
/// the periodic re-sampling those two get in `MerlinCCU.cpp`. Keeping the
/// linker-symbol reads out of `screens.cpp`/`status_screens.cpp` entirely
/// (issue #71) is what makes those files host-testable: the render layer
/// just reads these plain fields instead of calling into `hardware/flash.h`.
struct ImageFootprintStatus
{
    bool valid;
    uint32_t program_flash_bytes;
    uint32_t flash_budget_bytes;
    uint32_t reserved_flash_bytes;
    uint32_t static_ram_bytes;
    uint32_t total_ram_bytes;
};

/// @brief One manually tracked Pinter vessel in the home brewing workflow.
struct PinterStatus
{
    std::array<char, 24> label;
    PinterState state;
    uint8_t brew_index;
    uint32_t brew_start_day;
    uint32_t cold_crash_start_day;
    uint32_t conditioning_start_day;
    uint32_t ready_day;
    uint8_t planned_brewing_days;
    uint8_t planned_cold_crash_days;
    uint8_t planned_conditioning_days;
    bool cold_crash_used;
};

/// @brief Captures the logical front-panel state independent of hardware wiring.
struct ConsoleState
{
    MenuPage active_page;
    uint8_t settings_page_index;
    uint8_t time_zone_page_index;
    /// Fonts page (#123): the list page shown, and the style being changed.
    uint8_t font_page_index;
    uint8_t font_style_index;
    WeatherSource weather_source;
    WeatherPeriod weather_period;
    LocalConditionMetric local_condition_metric;
    CalendarOwner calendar_owner;
    int8_t calendar_day_offset;
    uint8_t selected_calendar_event_index;
    uint8_t calendar_event_count;
    SharePeriod share_period;
    bool share_data_configured;
    bool share_data_valid;
    int share_data_last_error;
    int share_data_last_http_status;
    /// @brief Mirrors ShareMarketStatus::last_success_ms; see that field's
    /// comment for why it's excluded from set_share_market_status()'s
    /// change-detection in console_controller.cpp.
    uint32_t share_data_last_success_ms;
    uint8_t share_count;
    uint8_t selected_share_index;
    uint8_t selected_pinter_index;
    uint8_t pinter_catalogue_page_index;
    uint8_t pinter_pending_brew_index;
    uint8_t pinter_pending_brewing_days;
    uint8_t pinter_pending_cold_crash_days;
    uint8_t pinter_pending_conditioning_days;
    /// @brief Human-readable reason the primary Pinter action is blocked, or
    /// empty when it isn't. Exists so a capacity block (fridge/dock full) is an
    /// unmissable on-screen message rather than only a two-line softkey hint.
    std::array<char, 40> pinter_block_reason;
    /// @brief True while the Pinter page is asking the operator to confirm
    /// a reset of the selected vessel (SK-6: destructive actions get their
    /// own state). Cleared on confirm, cancel, BACK STEP, or leaving the page.
    bool pinter_reset_confirming;
    /// @brief True while Device Identity asks the operator to confirm
    /// clearing the web PIN (#158, SK-6). Cleared on confirm, cancel or BACK
    /// STEP.
    bool web_pin_clear_confirming;

    /// @brief Home Assistant's lamp settings over MQTT (#150), indexed
    /// ALRT then TEST.
    std::array<LampOverride, 2> lamp_overrides;

    /// @brief A message Home Assistant put in the alert list (#150).
    struct HaMessage
    {
        bool active;
        AlertSeverity severity;
        std::array<char, 21> title;
        std::array<char, 301> text;
    } ha_message;
    ScreenSaverSelection screen_saver_selection;
    TimeZoneSelection time_zone;
    uint16_t screen_saver_timeout_minutes;
    bool screen_saver_timeout_editing;
    uint16_t screen_saver_timeout_edit_minutes;
    bool screen_saver_timeout_replace_on_next_digit;
    LetterMode letter_mode;
    AlertSeverity alert_severity;
    SystemTestState test_state;
    BrightnessLevel panel_brightness;
    BrightnessLevel key_backlight_brightness;
    WifiStatus wifi_status;
    HomeAssistantStatus home_assistant_status;
    /// Official weather and flood warnings from HA (#202).
    WeatherWarnings weather_warnings;
    MqttStatus mqtt_status;
    TimeStatus time_status;
    MainLoopLoadStatus main_loop_load_status;
    HeapStatus heap_status;
    StackStatus stack_status;
    BootStatus boot_status;
    FirmwareStatus firmware_status;
    ImageFootprintStatus image_footprint_status;
    DisplayTimingStatus display_timing_status;
    environment_sensor_manager::EnvironmentSensorStatus environment_sensor_status;
    KeypadDebugStatus keypad_debug_status;
    uint8_t alert_count;
    uint8_t alert_list_page_index;
    uint8_t alert_detail_index;
    uint8_t alert_detail_scroll_line;
    MenuPage alert_parent_page;
    /// History page shown, the past alert open on its detail page (by
    /// alert_history::Entry::sequence, so a new entry arriving does not
    /// change which one), and that page's scroll line (#203).
    uint8_t alert_history_page_index;
    uint32_t alert_history_detail_sequence;
    uint8_t alert_history_scroll_line;
    /// History's CLEAR ALL is asking for confirmation.
    bool alert_history_clear_confirming;
    std::array<ActiveAlert, kActiveAlertCapacity> active_alerts;
    std::array<CalendarEvent, kCalendarEventCapacity> calendar_events;
    std::array<PinterStatus, kPinterCount> pinters;
    std::array<ShareWatchEntry, kMaxWatchedShares> watched_shares;
    /// @brief The Controls page's list from Home Assistant (#152).
    HomeControls home_controls;
    std::array<LampMode, static_cast<size_t>(LampId::Count)> lamps;
    SoftKeyMap softkeys;
};

/// @brief True when the page is asking for a decision or accepting entry,
/// so R5 confirms and L5 declines instead of R5 being HOME (NAV-3).
inline bool page_is_asking_for_decision(const ConsoleState& console_state)
{
    switch (console_state.active_page)
    {
    case MenuPage::AlertDetail:
    case MenuPage::PinterStartTiming:
        return true;
    case MenuPage::Pinter:
        return console_state.pinter_reset_confirming;
    case MenuPage::AlertHistory:
        return console_state.alert_history_clear_confirming;
    case MenuPage::ScreenSaverSettings:
        return console_state.screen_saver_timeout_editing;
    default:
        return false;
    }
}

/// @brief Returns the physical legends printed on a hard key.
const KeyLegend& key_legend(HardKeyId key);

/// @brief Builds a default console state for startup.
/// @details Writes directly into `out` rather than returning by value --
/// `ConsoleState` is tens of KB, and returning it through an assignment
/// (rather than a direct-initialization the compiler can elide) requires a
/// full stack-resident temporary, which overflows this MCU's tiny default
/// stack. Never change this back to return-by-value without confirming the
/// call site is a guaranteed-elided direct-initialization.
void make_default_console_state(ConsoleState& out);
