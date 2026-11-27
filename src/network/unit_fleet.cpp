#include "unit_fleet.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace unit_fleet
{

namespace
{

constexpr std::string_view kAvailabilitySlug = "availability";
constexpr std::string_view kSummarySlug = "summary";
constexpr std::string_view kOfflineAckSlug = "offline_ack";
constexpr std::string_view kOnline = "online";
constexpr std::string_view kOffline = "offline";
/// @brief MAC digits shown in a peer's name before its summary arrives.
constexpr size_t kShortMacLength = 6U;

/// @brief Copies a view into a fixed array, cut to fit and terminated.
template <size_t N> void copy_view(std::array<char, N>& dest, std::string_view src)
{
    dest.fill('\0');
    const size_t count = std::min(src.size(), N - 1U);
    for (size_t i = 0U; i < count; ++i)
    {
        dest[i] = src[i];
    }
}

/// @brief Splits `<base>/<a>/<b>` into its two levels.
bool split_levels(std::string_view topic, std::string_view base, std::string_view& first,
                  std::string_view& second)
{
    if (base.empty() || topic.size() <= base.size() + 1U || topic.substr(0U, base.size()) != base ||
        topic[base.size()] != '/')
    {
        return false;
    }
    const std::string_view rest = topic.substr(base.size() + 1U);
    const size_t slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0U || slash + 1U >= rest.size())
    {
        return false;
    }
    first = rest.substr(0U, slash);
    second = rest.substr(slash + 1U);
    return second.find('/') == std::string_view::npos;
}

bool is_mac(std::string_view text)
{
    if (text.size() != kMacLength)
    {
        return false;
    }
    for (const char c : text)
    {
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')))
        {
            return false;
        }
    }
    return true;
}

/// @brief The slot for `mac`, taking a free one for a new unit.
std::optional<size_t> slot_for(UnitFleet& fleet, std::string_view mac)
{
    for (size_t i = 0U; i < fleet.count; ++i)
    {
        if (std::string_view(fleet.peers[i].mac.data()) == mac)
        {
            return i;
        }
    }
    if (fleet.count >= fleet.peers.size())
    {
        return std::nullopt;
    }
    PeerUnit& peer = fleet.peers[fleet.count];
    peer = {};
    copy_view(peer.mac, mac);
    return fleet.count++;
}

/// @brief Applies a summary's `key=value` lines; unknown keys are skipped.
void apply_summary(PeerUnit& peer, std::string_view payload)
{
    peer.name.fill('\0');
    peer.version.fill('\0');
    peer.top_alert.fill('\0');
    peer.alert_count = 0U;
    while (!payload.empty())
    {
        const size_t end = payload.find('\n');
        const std::string_view line = payload.substr(0U, end);
        payload = end == std::string_view::npos ? std::string_view() : payload.substr(end + 1U);
        const size_t equals = line.find('=');
        if (equals == std::string_view::npos)
        {
            continue;
        }
        const std::string_view key = line.substr(0U, equals);
        const std::string_view value = line.substr(equals + 1U);
        if (key == "name")
        {
            copy_view(peer.name, value);
        }
        else if (key == "version")
        {
            copy_view(peer.version, value);
        }
        else if (key == "top")
        {
            copy_view(peer.top_alert, value);
        }
        else if (key == "alerts")
        {
            std::array<char, 4> digits = {};
            copy_view(digits, value);
            const unsigned long count = std::strtoul(digits.data(), nullptr, 10);
            peer.alert_count = static_cast<uint8_t>(std::min<unsigned long>(count, UINT8_MAX));
        }
    }
}

} // namespace

bool in_fleet_filter(std::string_view topic, std::string_view base)
{
    std::string_view first;
    std::string_view second;
    return split_levels(topic, base, first, second);
}

std::optional<TopicRef> topic_for(std::string_view topic, std::string_view base)
{
    std::string_view mac;
    std::string_view slug;
    if (!split_levels(topic, base, mac, slug) || !is_mac(mac))
    {
        return std::nullopt;
    }
    TopicRef ref = {};
    copy_view(ref.mac, mac);
    if (slug == kAvailabilitySlug)
    {
        ref.kind = Kind::Availability;
    }
    else if (slug == kSummarySlug)
    {
        ref.kind = Kind::Summary;
    }
    else if (slug == kOfflineAckSlug)
    {
        ref.kind = Kind::OfflineAck;
    }
    else
    {
        return std::nullopt;
    }
    return ref;
}

bool format_summary(const OwnSummary& summary, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return false;
    }
    const int length = std::snprintf(
        out, out_size, "name=%s\nversion=%s\nboard=%s\nalerts=%u\ntop=%s\n",
        summary.name != nullptr ? summary.name : "",
        summary.version != nullptr ? summary.version : "",
        summary.board != nullptr ? summary.board : "", static_cast<unsigned>(summary.alert_count),
        summary.top_alert != nullptr ? summary.top_alert : "");
    return length > 0 && static_cast<size_t>(length) < out_size &&
           static_cast<size_t>(length) <= kMaxPayloadBytes;
}

bool apply(UnitFleet& fleet, const TopicRef& ref, std::string_view payload, uint32_t now_ms,
           std::string_view self_mac)
{
    if (std::string_view(ref.mac.data()) == self_mac)
    {
        return false;
    }
    const bool clears = payload.empty();
    bool known = false;
    for (size_t i = 0U; i < fleet.count; ++i)
    {
        known = known || std::string_view(fleet.peers[i].mac.data()) == ref.mac.data();
    }
    // An emptied retained topic, such as a cleared acceptance, is no reason
    // to start tracking a unit.
    if (!known && clears)
    {
        return false;
    }
    const std::optional<size_t> slot = slot_for(fleet, ref.mac.data());
    if (!slot.has_value())
    {
        std::printf("Units: table full, ignoring %s\n", ref.mac.data());
        return false;
    }

    PeerUnit& peer = fleet.peers[*slot];
    const PeerUnit before = peer;
    switch (ref.kind)
    {
    case Kind::Availability:
        if (payload == kOnline)
        {
            peer.online = true;
            peer.availability_known = true;
            // Back again: the next outage needs accepting afresh.
            peer.offline_acknowledged = false;
            peer.ack_to_publish = false;
        }
        else if (payload == kOffline)
        {
            if (!peer.availability_known || peer.online)
            {
                peer.offline_since_ms = now_ms;
            }
            peer.online = false;
            peer.availability_known = true;
        }
        break;
    case Kind::Summary:
        apply_summary(peer, payload);
        break;
    case Kind::OfflineAck:
        // Only an offline unit's acceptance counts: one left behind for a unit
        // that is back would hide its next outage.
        peer.offline_acknowledged = !clears && !(peer.availability_known && peer.online);
        break;
    }
    return !(before == peer);
}

bool update(UnitFleet& fleet, uint32_t now_ms, std::string_view own_version)
{
    const uint8_t old_mask = fleet.offline_alert_mask;
    const std::optional<uint32_t> old_since = fleet.mixed_since_ms;
    const bool old_mixed = fleet.mixed_firmware;

    uint8_t mask = 0U;
    bool mixed_now = false;
    if (fleet.listening)
    {
        for (size_t i = 0U; i < fleet.count; ++i)
        {
            const PeerUnit& peer = fleet.peers[i];
            if (peer.availability_known && !peer.online && !peer.offline_acknowledged &&
                now_ms - peer.offline_since_ms >= kOfflineAlertMs)
            {
                mask = static_cast<uint8_t>(mask | (1U << i));
            }
            if (peer.availability_known && peer.online && peer.version[0] != '\0' &&
                !own_version.empty() && own_version != peer.version.data())
            {
                mixed_now = true;
            }
        }
    }
    fleet.offline_alert_mask = mask;
    if (!mixed_now)
    {
        fleet.mixed_since_ms.reset();
    }
    else if (!fleet.mixed_since_ms.has_value())
    {
        fleet.mixed_since_ms = now_ms;
    }
    fleet.mixed_firmware =
        fleet.mixed_since_ms.has_value() && now_ms - *fleet.mixed_since_ms >= kMixedFirmwareMs;

    return old_mask != fleet.offline_alert_mask || old_since != fleet.mixed_since_ms ||
           old_mixed != fleet.mixed_firmware;
}

bool acknowledge_offline(UnitFleet& fleet)
{
    bool any = false;
    for (size_t i = 0U; i < fleet.count; ++i)
    {
        if ((fleet.offline_alert_mask & (1U << i)) != 0U)
        {
            fleet.peers[i].offline_acknowledged = true;
            fleet.peers[i].ack_to_publish = true;
            any = true;
        }
    }
    fleet.offline_alert_mask = 0U;
    return any;
}

std::optional<size_t> next_ack_to_publish(UnitFleet& fleet)
{
    for (size_t i = 0U; i < fleet.count; ++i)
    {
        PeerUnit& peer = fleet.peers[i];
        if (!peer.ack_to_publish)
        {
            continue;
        }
        if (peer.online)
        {
            peer.ack_to_publish = false;
            continue;
        }
        return i;
    }
    return std::nullopt;
}

bool set_listening(UnitFleet& fleet, bool listening)
{
    if (fleet.listening == listening)
    {
        return false;
    }
    fleet.listening = listening;
    if (!listening)
    {
        fleet.offline_alert_mask = 0U;
        fleet.mixed_since_ms.reset();
        fleet.mixed_firmware = false;
    }
    return true;
}

void display_name(const PeerUnit& peer, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    if (peer.name[0] != '\0')
    {
        std::snprintf(out, out_size, "%s", peer.name.data());
        return;
    }
    const std::string_view mac(peer.mac.data());
    const std::string_view tail =
        mac.size() > kShortMacLength ? mac.substr(mac.size() - kShortMacLength) : mac;
    std::snprintf(out, out_size, "unit %.*s", static_cast<int>(tail.size()), tail.data());
}

} // namespace unit_fleet
