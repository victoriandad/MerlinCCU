#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "environment_sensor_manager.h"

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
    SecuritySettings,
    WifiSettings,
    HomeAssistantSettings,
    MqttSettings,
    ScreenSaverSettings,
    WeatherSources,
    TimeZoneSettings,
    Alignment,
    KeypadDebug,
    AlertList,
    AlertDetail,
    Pinter,
    PinterPacks,
    PinterToBeBrewed,
    PinterSelectBrew,
    PinterSelectedBrews,
    PinterStartBrew,
    PinterStartTiming,
    Shares,
    ShareDetail,
};

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
};

/// @brief Compares two forecast rows field-by-field.
/// @details Equality is used to detect whether the rendered forecast content actually changed so
/// the UI can avoid redundant churn when Home Assistant data is stable.
inline bool operator==(const WeatherForecastEntry& lhs, const WeatherForecastEntry& rhs)
{
    return lhs.time_text == rhs.time_text && lhs.temperature_text == rhs.temperature_text &&
           lhs.wind_text == rhs.wind_text && lhs.condition_text == rhs.condition_text;
}

/// @brief One compact daily forecast entry used by the Next 7 Days weather period.
struct WeatherDailyForecastEntry
{
    std::array<char, 11> date_text;
    std::array<char, 16> temperature_text;
    std::array<char, 8> wind_text;
    std::array<char, 20> condition_text;
};

/// @brief Compares two daily forecast rows field-by-field.
inline bool operator==(const WeatherDailyForecastEntry& lhs, const WeatherDailyForecastEntry& rhs)
{
    return lhs.date_text == rhs.date_text && lhs.temperature_text == rhs.temperature_text &&
           lhs.wind_text == rhs.wind_text && lhs.condition_text == rhs.condition_text;
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
           lhs.forecast_max_wind_speed_mph == rhs.forecast_max_wind_speed_mph;
}

inline bool operator!=(const WeatherMetrics& lhs, const WeatherMetrics& rhs)
{
    return !(lhs == rhs);
}

/// @brief One provider-originated weather warning normalised for the ALRT page.
/// @details Official warning APIs are not wired yet. The field still gives
/// current providers a stable place to surface severe condition telemetry
/// without coupling the alert page to Home Assistant or Open-Meteo payloads.
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

/// @brief Snapshot of Home Assistant state suitable for UI and controller use.
struct HomeAssistantStatus
{
    HomeAssistantConnectionState state;
    bool configured;
    bool self_entity_published;
    int last_error;
    int last_http_status;
    std::array<char, 48> host;
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
};

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
inline constexpr size_t kPinterSelectedBrewCapacity = 24;
inline constexpr uint8_t kInvalidPinterBrewSelection = 255U;

/// @brief Semantic action currently assigned to a contextual softkey.
enum class SoftKeyRoute : uint8_t
{
    None = 0,
    GoHome,
    GoCalendar,
    GoWeather,
    GoStatus,
    GoLocalConditions,
    ShowLocalTemperatureGraph,
    ShowLocalHumidityGraph,
    ShowLocalPressureGraph,
    ShowLocalAirQualityGraph,
    GoSettings,
    GoDeviceSettings,
    GoSecuritySettings,
    GoWifiSettings,
    GoHomeAssistantSettings,
    GoMqttSettings,
    GoScreenSaverSettings,
    EditScreenSaverTimeout,
    ConfirmScreenSaverTimeout,
    GoWeatherSources,
    GoTimeZoneSettings,
    GoKeypadDebug,
    ToggleRemoteConfig,
    ToggleRequireAdminPassword,
    ToggleHomeAssistantEnabled,
    ToggleMqttEnabled,
    SelectScreenSaverLife,
    SelectScreenSaverClock,
    SelectScreenSaverStarfield,
    SelectScreenSaverMatrix,
    SelectScreenSaverRadar,
    SelectScreenSaverRain,
    SelectScreenSaverWorms,
    SelectScreenSaverRandom,
    SelectWeatherHomeAssistant,
    SelectWeatherOpenMeteo,
    CycleWeatherPeriod,
    GoSelectedShareDetail,
    GoStatusOverview,
    GoStatusConnectivity,
    GoStatusResources,
    GoStatusSensors,
    GoStatusIntegrations,
    SelectTimeZoneWest1,
    SelectTimeZoneWest2,
    SelectTimeZoneWest3,
    SelectTimeZoneWest4,
    SelectTimeZoneEast1,
    SelectTimeZoneEast2,
    SelectTimeZoneEast3,
    SelectTimeZoneEast4,
    CycleAlert,
    ToggleLetters,
    CycleTest,
    ResetConsoleState,
    ClearAlert,
    SelectAlertSlot1,
    SelectAlertSlot2,
    SelectAlertSlot3,
    SelectAlertSlot4,
    SelectAlertSlot5,
    SelectAlertSlot6,
    SelectAlertSlot7,
    SelectAlertSlot8,
    SelectAlertSlot9,
    AlertAccept,
    AlertIgnore,
    GoPinter,
    GoPinterPacks,
    SelectPinterSlot1,
    SelectPinterSlot2,
    SelectPinterSlot3,
    SelectPinterSlot4,
    CyclePinterBrew,
    AddPinterBrewPack,
    RemovePinterBrewPack,
    ApplyPinterPrimaryAction,
    ResetSelectedPinter,
    GoPinterToBeBrewed,
    GoPinterSelectBrew,
    GoPinterSelectedBrews,
    GoPinterStartBrew,
    SelectPinterListItem1,
    SelectPinterListItem2,
    SelectPinterListItem3,
    SelectPinterListItem4,
    SelectPinterListItem5,
    SelectPinterListItem6,
    SelectPinterListItem7,
    SelectPinterListItem8,
    PinterListPreviousPage,
    PinterListNextPage,
    SelectPinterMinimumTiming,
    SelectPinterRecommendedTiming,
    DecreasePinterBrewDays,
    IncreasePinterBrewDays,
    DecreasePinterConditioningDays,
    IncreasePinterConditioningDays,
    DecreasePinterColdCrashDays,
    IncreasePinterColdCrashDays,
    ConfirmPinterStart,
    GoShares,
    SelectShareSlot1,
    CycleSharePeriod,
    CycleCalendarOwner,
    ResetCalendarFilters,
    SelectCalendarSlot1,
    SelectCalendarSlot2,
    SelectCalendarSlot3,
    SelectCalendarSlot4,
    SelectCalendarSlot5,
    SelectCalendarSlot6,
    SelectCalendarSlot7,
    SelectCalendarSlot8,
    SelectCalendarSlot9,
    PanelBrighter,
    PanelDimmer,
    KeysBrighter,
    KeysDimmer,
};

/// @brief One active alert surfaced by the CCU alert workflow.
struct ActiveAlert
{
    AlertSeverity severity;
    uint8_t code;
    uint32_t sequence;
    std::array<char, 6> occurred_time_text;
    std::array<char, 32> summary;
    std::array<char, 320> detail;
};

/// @brief One watched share shown by the CCU shares workflow.
struct ShareWatchEntry
{
    std::array<char, 24> display_name;
    std::array<char, 10> symbol;
    std::array<char, 8> exchange;
    std::array<char, 8> currency;
    std::array<char, 12> price_text;
    std::array<char, 12> change_text;
    std::array<uint16_t, 24> history_points;
};

/// @brief Compares two watched-share rows field-by-field for redraw detection.
inline bool operator==(const ShareWatchEntry& lhs, const ShareWatchEntry& rhs)
{
    return lhs.display_name == rhs.display_name && lhs.symbol == rhs.symbol &&
           lhs.exchange == rhs.exchange && lhs.currency == rhs.currency &&
           lhs.price_text == rhs.price_text && lhs.change_text == rhs.change_text &&
           lhs.history_points == rhs.history_points;
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
    std::array<ShareWatchEntry, 6> watched_shares;
};

inline constexpr uint8_t kInvalidWeekdayIndex = 255U;
inline constexpr int kCalendarMinDayOffset = -14;
inline constexpr int kCalendarMaxDayOffset = 14;
inline constexpr size_t kCalendarEventCapacity = 48;
inline constexpr size_t kCalendarVisibleEventCount = 9;

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

/// @brief Semantic action currently assigned to a contextual softkey.
struct SoftKeyAction
{
    const char* label;
    SoftKeyRoute route;
    bool enabled;
    bool inverted = false;
};

/// @brief The ten softkey assignments around the screen.
using SoftKeyMap = std::array<SoftKeyAction, static_cast<size_t>(SoftKeyId::Count)>;

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

/// @brief One manually tracked Pinter vessel in the home brewing workflow.
struct PinterStatus
{
    std::array<char, 8> label;
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
    uint8_t share_count;
    uint8_t selected_share_index;
    uint8_t selected_pinter_index;
    uint8_t pinter_brew_pack_count;
    uint8_t pinter_selected_brew_index;
    uint8_t pinter_selected_brew_count;
    std::array<uint8_t, kPinterSelectedBrewCapacity> pinter_selected_brews;
    uint8_t pinter_catalogue_page_index;
    uint8_t pinter_selected_brews_page_index;
    uint8_t pinter_start_brews_page_index;
    uint8_t pinter_pending_inventory_index;
    uint8_t pinter_pending_brew_index;
    uint8_t pinter_pending_brewing_days;
    uint8_t pinter_pending_cold_crash_days;
    uint8_t pinter_pending_conditioning_days;
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
    MqttStatus mqtt_status;
    TimeStatus time_status;
    MainLoopLoadStatus main_loop_load_status;
    environment_sensor_manager::EnvironmentSensorStatus environment_sensor_status;
    KeypadDebugStatus keypad_debug_status;
    uint8_t alert_count;
    uint8_t alert_list_page_index;
    uint8_t alert_detail_index;
    uint8_t alert_detail_scroll_line;
    MenuPage alert_parent_page;
    std::array<ActiveAlert, 24> active_alerts;
    std::array<CalendarEvent, kCalendarEventCapacity> calendar_events;
    std::array<PinterStatus, kPinterCount> pinters;
    std::array<ShareWatchEntry, 6> watched_shares;
    std::array<LampMode, static_cast<size_t>(LampId::Count)> lamps;
    SoftKeyMap softkeys;
};

/// @brief Returns the physical legends printed on a hard key.
const KeyLegend& key_legend(HardKeyId key);

/// @brief Builds a default console state for startup.
ConsoleState make_default_console_state();
