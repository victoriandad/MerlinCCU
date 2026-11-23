#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "console_model.h"

/// @brief Home Assistant commands to the CCU over MQTT (#150), kept free of
/// Pico SDK calls so host tests can cover them.
/// @details Each controllable entity has a command topic
/// `<base>/<mac>/<slug>/set`. A command is checked and bounded here; anything
/// unknown or malformed is refused (and logged by the caller), never guessed
/// at. The firmware then applies it through console_controller.
namespace mqtt_commands
{

/// @brief The entity a command is for. Order matches kTargets.
enum class Target : uint8_t
{
    AlertLamp = 0,
    TestLamp,
    PanelBrightness,
    KeyBacklight,
    Display,
    Page,
    Message,
    Count,
};

/// @brief HA's setting for a lamp; option order matches.
using LampSetting = LampOverride;

/// @brief HA's display request.
enum class DisplaySetting : uint8_t
{
    Awake = 0,
    ScreenSaver,
};

/// @brief Longest message title, matching the alert list's label room.
inline constexpr size_t kMaxTitleLength = 20U;
/// @brief Longest message text accepted; the alert detail holds 319.
inline constexpr size_t kMaxMessageLength = 300U;
/// @brief Longest command payload accepted at all.
inline constexpr size_t kMaxPayloadLength = 512U;

/// @brief A message for the CCU's alert list. Empty text clears it.
struct Message
{
    AlertSeverity severity = AlertSeverity::Message;
    std::array<char, kMaxTitleLength + 1U> title = {};
    std::array<char, kMaxMessageLength + 1U> text = {};
};

/// @brief One accepted command.
struct Command
{
    Target target = Target::AlertLamp;
    /// LampSetting, BrightnessLevel, DisplaySetting or MenuPage, by target.
    uint8_t value = 0U;
    Message message = {};
};

/// @brief Per-entity details for discovery and state.
struct TargetInfo
{
    const char* slug;      ///< Topic part and unique-id suffix.
    const char* name;      ///< HA entity name.
    const char* component; ///< HA platform: "select" or "notify".
    const char* icon;
};

/// @brief Discovery details for one target.
const TargetInfo& info(Target target);

/// @brief The select options as a JSON array, e.g. `["auto","off"]`;
/// nullptr for the message target.
const char* options_json(Target target);

/// @brief The option name for a value, e.g. "flash_slow"; nullptr if out of
/// range.
const char* option_name(Target target, uint8_t value);

/// @brief The page a Page command's value names.
MenuPage page_for(uint8_t value);

/// @brief The Page option for a page, or nullopt for pages HA cannot pick.
std::optional<uint8_t> page_option(MenuPage page);

/// @brief Finds the target a command topic is for.
/// @param topic the full topic, e.g. "merlinccu/28cdc11a896c/page/set".
/// @param prefix the unit's topic prefix, e.g. "merlinccu/28cdc11a896c".
std::optional<Target> target_for_topic(std::string_view topic, std::string_view prefix);

/// @brief Checks a payload for a target.
/// @details Select targets take one of their option names. The message
/// target takes plain text, or JSON with "message", optional "title" and
/// optional "severity" ("message", "warning" or "alert").
std::optional<Command> parse(Target target, std::string_view payload);

} // namespace mqtt_commands
