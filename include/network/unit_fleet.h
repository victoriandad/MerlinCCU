#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

/// @brief The other CCUs on the same MQTT broker and base topic (#210).
/// @details Every unit publishes, retained, under `<base>/<mac>/`:
/// - `availability`: `online`, or `offline` from the broker's last will
///   (also Home Assistant's availability topic);
/// - `summary`: `key=value` lines with its name, version, board, and the
///   count and most serious of its own alerts;
/// - `offline_ack`: `1` once someone has accepted UNIT OFFLINE for it on any
///   unit, emptied by the unit itself when it next connects.
///
/// Each unit subscribes to `<base>/+/+` and keeps the others in a fixed
/// table, so nothing needs configuring. Schema: docs/mqtt-household-data.md.
/// Pure logic, so host tests cover it.
namespace unit_fleet
{

/// @brief Most other units one CCU tracks: the house has three, plus room
/// for a spare on the bench. More are ignored, and logged.
inline constexpr size_t kMaxPeers = 4U;
/// @brief Hex digits in a MAC, the units' MQTT identity.
inline constexpr size_t kMacLength = 12U;
/// @brief Longest name a unit advertises (wifi_manager::kMaxHostNameLength).
inline constexpr size_t kNameLength = 15U;
/// @brief Longest version text (FirmwareStatus::version).
inline constexpr size_t kVersionLength = 15U;
/// @brief Longest alert summary carried (ActiveAlert::summary, trimmed to fit
/// the Units page).
inline constexpr size_t kAlertLength = 20U;
/// @brief Largest summary payload accepted, in bytes.
inline constexpr size_t kMaxPayloadBytes = 128U;
/// @brief How long a unit must be offline before UNIT OFFLINE is raised. Long
/// enough that a restart, an update or a moved plug never raises it.
inline constexpr uint32_t kOfflineAlertMs = 30U * 60U * 1000U;
/// @brief How long the units must disagree on firmware before MIXED FIRMWARE
/// is raised: an update of every unit takes a few minutes.
inline constexpr uint32_t kMixedFirmwareMs = 10U * 60U * 1000U;

} // namespace unit_fleet

/// @brief What this unit knows of one other unit.
struct PeerUnit
{
    std::array<char, unit_fleet::kMacLength + 1U> mac;
    /// Its network name, from its summary; empty until one arrives.
    std::array<char, unit_fleet::kNameLength + 1U> name;
    std::array<char, unit_fleet::kVersionLength + 1U> version;
    /// Its most serious alert of its own, or empty.
    std::array<char, unit_fleet::kAlertLength + 1U> top_alert;
    uint8_t alert_count;
    /// An availability message has arrived, so `online` means something.
    bool availability_known;
    bool online;
    /// When this unit learnt the peer was offline (ms since this unit's boot).
    uint32_t offline_since_ms;
    /// UNIT OFFLINE for this peer was accepted on some unit.
    bool offline_acknowledged;
    /// This unit accepted it and still has to tell the others.
    bool ack_to_publish;
};

/// @brief The other units, and the alerts they raise.
struct UnitFleet
{
    /// Subscribed to the other units' topics, so the table is live.
    bool listening;
    uint8_t count;
    std::array<PeerUnit, unit_fleet::kMaxPeers> peers;
    /// Peers offline for kOfflineAlertMs and not yet accepted; drives UNIT
    /// OFFLINE.
    uint8_t offline_alert_mask;
    /// Since when an online peer has run different firmware from this unit;
    /// empty while they agree.
    std::optional<uint32_t> mixed_since_ms;
    /// They have disagreed for kMixedFirmwareMs; drives MIXED FIRMWARE.
    bool mixed_firmware;
};

/// @brief Compares two peers field by field.
inline bool operator==(const PeerUnit& lhs, const PeerUnit& rhs)
{
    return lhs.mac == rhs.mac && lhs.name == rhs.name && lhs.version == rhs.version &&
           lhs.top_alert == rhs.top_alert && lhs.alert_count == rhs.alert_count &&
           lhs.availability_known == rhs.availability_known && lhs.online == rhs.online &&
           lhs.offline_since_ms == rhs.offline_since_ms &&
           lhs.offline_acknowledged == rhs.offline_acknowledged &&
           lhs.ack_to_publish == rhs.ack_to_publish;
}

/// @brief Compares two fleet snapshots field by field.
inline bool operator==(const UnitFleet& lhs, const UnitFleet& rhs)
{
    return lhs.listening == rhs.listening && lhs.count == rhs.count && lhs.peers == rhs.peers &&
           lhs.offline_alert_mask == rhs.offline_alert_mask &&
           lhs.mixed_since_ms == rhs.mixed_since_ms && lhs.mixed_firmware == rhs.mixed_firmware;
}

inline bool operator!=(const UnitFleet& lhs, const UnitFleet& rhs)
{
    return !(lhs == rhs);
}

namespace unit_fleet
{

/// @brief Which of a unit's topics a publish is.
enum class Kind : uint8_t
{
    Availability,
    Summary,
    OfflineAck,
};

/// @brief One unit's topic: whose, and which.
struct TopicRef
{
    std::array<char, kMacLength + 1U> mac;
    Kind kind;
};

/// @brief True for any `<base>/<one level>/<one level>` topic: the filter
/// `<base>/+/+` delivers these, and they are never commands.
bool in_fleet_filter(std::string_view topic, std::string_view base);

/// @brief Reads `<base>/<mac>/{availability,summary,offline_ack}`; nullopt for
/// anything else, including a MAC that is not 12 lower-case hex digits.
std::optional<TopicRef> topic_for(std::string_view topic, std::string_view base);

/// @brief This unit's own summary, as it publishes it.
struct OwnSummary
{
    const char* name;
    const char* version;
    const char* board;
    uint8_t alert_count;
    const char* top_alert;
};

/// @brief Formats a summary payload; false when `out` is too small.
bool format_summary(const OwnSummary& summary, char* out, size_t out_size);

/// @brief Applies one publish from another unit. Publishes from `self_mac`
/// are ignored. A new unit takes a free slot; with none free it is dropped.
/// @return True when the table changed.
bool apply(UnitFleet& fleet, const TopicRef& ref, std::string_view payload, uint32_t now_ms,
           std::string_view self_mac);

/// @brief Recomputes the alert flags for `now_ms`, against this unit's own
/// firmware version.
/// @return True when the fleet changed.
bool update(UnitFleet& fleet, uint32_t now_ms, std::string_view own_version);

/// @brief Records that UNIT OFFLINE was accepted here: every peer it covers
/// is marked accepted, and queued to tell the others.
/// @return True when any peer was covered.
bool acknowledge_offline(UnitFleet& fleet);

/// @brief The next peer whose acceptance must be published, if any. A queued
/// acceptance for a peer that has since come back is dropped.
std::optional<size_t> next_ack_to_publish(UnitFleet& fleet);

/// @brief Records whether the table is live (subscribed); losing the
/// subscription clears the alert flags until it is back.
/// @return True when the fleet changed.
bool set_listening(UnitFleet& fleet, bool listening);

/// @brief A peer's name for the page and alert texts: its network name, or
/// `unit` and the end of its MAC before its summary arrives.
void display_name(const PeerUnit& peer, char* out, size_t out_size);

} // namespace unit_fleet
