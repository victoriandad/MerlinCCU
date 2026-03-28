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

/// @brief High-level Wi-Fi connectivity state for the Pico W radio.
enum class WifiConnectionState : uint8_t {
    Disabled = 0,
    Unconfigured,
    Initializing,
    Connecting,
    WaitingForIp,
    Connected,
    AuthFailed,
    NoNetwork,
    ConnectFailed,
    Error,
};

/// @brief Snapshot of Wi-Fi state suitable for UI and controller use.
struct WifiStatus {
    WifiConnectionState state;
    bool credentials_present;
    int last_error;
    int link_status;
    std::array<char, 33> ssid;
    std::array<char, 16> ip_address;
};

/// @brief Semantic action currently assigned to a contextual softkey.
struct SoftKeyAction {
    const char* label;
    const char* route;
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
    LetterMode letter_mode;
    AlertSeverity alert_severity;
    SystemTestState test_state;
    BrightnessLevel panel_brightness;
    BrightnessLevel key_backlight_brightness;
    WifiStatus wifi_status;
    std::array<LampMode, static_cast<size_t>(LampId::Count)> lamps;
    SoftKeyMap softkeys;
};

/// @brief Returns the physical legends printed on a hard key.
const KeyLegend& key_legend(HardKeyId key);

/// @brief Builds a default console state for startup.
ConsoleState make_default_console_state();
