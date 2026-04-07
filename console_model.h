#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

/// @brief Physical softkeys mounted beside the display.
enum class SoftKeyId : uint8_t {
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
enum class HardKeyId : uint8_t {
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
enum class LampId : uint8_t {
    AlertLamp = 0,
    TestLamp,
    KeyBacklight,
    PanelBacklight,
    Count,
};

/// @brief Visual lamp state for annunciators.
enum class LampMode : uint8_t {
    Off = 0,
    On,
    FlashSlow,
    FlashFast,
};

/// @brief Text entry mode controlled by the `LTRS` key.
enum class LetterMode : uint8_t {
    Off = 0,
    On,
};

/// @brief Brightness level placeholder for panel and key lighting.
enum class BrightnessLevel : uint8_t {
    Off = 0,
    Low,
    Medium,
    High,
};

/// @brief High-level alert state that can be driven from Home Assistant later.
enum class AlertSeverity : uint8_t {
    None = 0,
    Message,
    Warning,
    Alert,
};

/// @brief High-level system test state associated with the TEST key/lamp.
enum class SystemTestState : uint8_t {
    Idle = 0,
    Running,
    Passed,
    Failed,
};

/// @brief High-level menu pages shown on the display.
enum class MenuPage : uint8_t {
    Home = 0,
    Status,
    Settings,
};

/// @brief High-level Wi-Fi connectivity state for the Pico W radio.
enum class WifiConnectionState : uint8_t {
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
enum class HomeAssistantConnectionState : uint8_t {
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
enum class MqttConnectionState : uint8_t {
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
struct WifiStatus {
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
struct TimeStatus {
    bool synced;
    std::array<char, 6> time_text;
};

/// @brief Snapshot of Home Assistant state suitable for UI and controller use.
struct HomeAssistantStatus {
    HomeAssistantConnectionState state;
    bool configured;
    bool self_entity_published;
    int last_error;
    int last_http_status;
    std::array<char, 48> host;
    std::array<char, 48> tracked_entity_id;
    std::array<char, 24> tracked_entity_state;
    std::array<char, 48> self_entity_id;
};

/// @brief Snapshot of MQTT discovery state suitable for UI and controller use.
struct MqttStatus {
    MqttConnectionState state;
    bool configured;
    bool discovery_published;
    int last_error;
    std::array<char, 48> broker;
    std::array<char, 32> device_id;
};

/// @brief Semantic action currently assigned to a contextual softkey.
enum class SoftKeyRoute : uint8_t {
    None = 0,
    GoHome,
    GoStatus,
    GoSettings,
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
struct SoftKeyAction {
    const char* label;
    SoftKeyRoute route;
    bool enabled;
};

/// @brief The ten softkey assignments around the screen.
using SoftKeyMap = std::array<SoftKeyAction, static_cast<size_t>(SoftKeyId::Count)>;

/// @brief One key meaning in the current letter mode.
struct KeyLegend {
    const char* primary;
    const char* alternate;
};

/// @brief Captures the logical front-panel state independent of hardware wiring.
struct ConsoleState {
    MenuPage active_page;
    LetterMode letter_mode;
    AlertSeverity alert_severity;
    SystemTestState test_state;
    BrightnessLevel panel_brightness;
    BrightnessLevel key_backlight_brightness;
    WifiStatus wifi_status;
    HomeAssistantStatus home_assistant_status;
    MqttStatus mqtt_status;
    TimeStatus time_status;
    std::array<LampMode, static_cast<size_t>(LampId::Count)> lamps;
    SoftKeyMap softkeys;
};

/// @brief Returns the physical legends printed on a hard key.
const KeyLegend& key_legend(HardKeyId key);

/// @brief Builds a default console state for startup.
ConsoleState make_default_console_state();
