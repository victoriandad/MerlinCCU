#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

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
enum class LetterMode : uint8_t
{
    Off = 0,
    On,
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
    Weather,
    Status,
    Settings,
    WifiSettings,
    ScreenSaverSettings,
    WeatherSources,
    TimeZoneSettings,
    Alignment,
    KeypadDebug,
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
};

inline constexpr size_t kWeatherForecastEntryCount = 10;

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

/// @brief Selectable weather backends exposed to the CCU UI.
enum class WeatherSource : uint8_t
{
    HomeAssistant = 0,
    MetOffice,
    BbcWeather,
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

/// @brief Semantic action currently assigned to a contextual softkey.
enum class SoftKeyRoute : uint8_t
{
    None = 0,
    GoHome,
    GoWeather,
    GoStatus,
    GoSettings,
    GoWifiSettings,
    GoScreenSaverSettings,
    EditScreenSaverTimeout,
    ConfirmScreenSaverTimeout,
    GoWeatherSources,
    GoTimeZoneSettings,
    GoKeypadDebug,
    SelectScreenSaverLife,
    SelectScreenSaverClock,
    SelectScreenSaverStarfield,
    SelectScreenSaverMatrix,
    SelectScreenSaverRadar,
    SelectScreenSaverRain,
    SelectScreenSaverWorms,
    SelectScreenSaverRandom,
    SelectWeatherHomeAssistant,
    SelectWeatherMetOffice,
    SelectWeatherBbcWeather,
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
    PanelBrighter,
    PanelDimmer,
    KeysBrighter,
    KeysDimmer,
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

/// @brief Captures the logical front-panel state independent of hardware wiring.
struct ConsoleState
{
    MenuPage active_page;
    WeatherSource weather_source;
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
    KeypadDebugStatus keypad_debug_status;
    std::array<LampMode, static_cast<size_t>(LampId::Count)> lamps;
    SoftKeyMap softkeys;
};

/// @brief Returns the physical legends printed on a hard key.
const KeyLegend& key_legend(HardKeyId key);

/// @brief Builds a default console state for startup.
ConsoleState make_default_console_state();
