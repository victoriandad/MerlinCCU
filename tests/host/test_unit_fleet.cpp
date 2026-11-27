#include "unit_fleet.h"

#include <cstring>
#include <string>

#include "alert_controller.h"
#include "console_controller.h"
#include "test_framework.h"

namespace
{

constexpr char kBase[] = "merlinccu";
constexpr char kSelf[] = "28cdc11a896c";
constexpr char kPeerA[] = "88a29e0b828a";
constexpr char kPeerB[] = "d83add000001";
constexpr uint32_t kMinute = 60U * 1000U;

unit_fleet::TopicRef ref(const char* mac, unit_fleet::Kind kind)
{
    unit_fleet::TopicRef out = {};
    std::strncpy(out.mac.data(), mac, out.mac.size() - 1U);
    out.kind = kind;
    return out;
}

void send(UnitFleet& fleet, const char* mac, unit_fleet::Kind kind, const char* payload,
          uint32_t now_ms)
{
    (void)unit_fleet::apply(fleet, ref(mac, kind), payload, now_ms, kSelf);
}

/// @brief A fleet that is listening, with one peer online on `version`.
UnitFleet with_peer(const char* version)
{
    UnitFleet fleet = {};
    (void)unit_fleet::set_listening(fleet, true);
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "online", 0U);
    const std::string summary = std::string("name=merlinccu2\nversion=") + version +
                                "\nboard=pico2_w\nalerts=2\ntop=HEAP LOW\n";
    send(fleet, kPeerA, unit_fleet::Kind::Summary, summary.c_str(), 0U);
    return fleet;
}

} // namespace

HOST_TEST(fleet_topics_are_read_and_others_refused)
{
    const auto availability = unit_fleet::topic_for("merlinccu/88a29e0b828a/availability", kBase);
    EXPECT_TRUE(availability.has_value());
    EXPECT_TRUE(availability->kind == unit_fleet::Kind::Availability);
    EXPECT_TRUE(std::strcmp(availability->mac.data(), kPeerA) == 0);
    EXPECT_TRUE(unit_fleet::topic_for("merlinccu/88a29e0b828a/summary", kBase)->kind ==
                unit_fleet::Kind::Summary);
    EXPECT_TRUE(unit_fleet::topic_for("merlinccu/88a29e0b828a/offline_ack", kBase)->kind ==
                unit_fleet::Kind::OfflineAck);
    EXPECT_FALSE(unit_fleet::topic_for("merlinccu/88a29e0b828a/screen", kBase).has_value());
    EXPECT_FALSE(unit_fleet::topic_for("merlinccu/house/summary", kBase).has_value());
    EXPECT_FALSE(unit_fleet::topic_for("merlinccu/88A29E0B828A/summary", kBase).has_value());
    EXPECT_FALSE(unit_fleet::topic_for("other/88a29e0b828a/summary", kBase).has_value());
    EXPECT_FALSE(unit_fleet::topic_for("merlinccu/88a29e0b828a/status/state", kBase).has_value());

    // The filter "<base>/+/+" also brings topics that are not a unit's;
    // they are recognised as the filter's so they are never taken as commands.
    EXPECT_TRUE(unit_fleet::in_fleet_filter("merlinccu/house/summary", kBase));
    EXPECT_FALSE(unit_fleet::in_fleet_filter("merlinccu/88a29e0b828a/page/set", kBase));
    EXPECT_FALSE(unit_fleet::in_fleet_filter("merlinccu/house/weather/now", kBase));
}

HOST_TEST(fleet_summary_round_trips_and_own_publishes_are_ignored)
{
    unit_fleet::OwnSummary own = {};
    own.name = "merlinccu3";
    own.version = "76c0214";
    own.board = "pimoroni_pico_plus2_w_rp2350";
    own.alert_count = 3U;
    own.top_alert = "UPDATE FAILED";
    char payload[unit_fleet::kMaxPayloadBytes + 1U] = {};
    EXPECT_TRUE(unit_fleet::format_summary(own, payload, sizeof(payload)));

    UnitFleet fleet = {};
    send(fleet, kSelf, unit_fleet::Kind::Summary, payload, 0U);
    EXPECT_EQ(fleet.count, 0U);

    send(fleet, kPeerB, unit_fleet::Kind::Summary, payload, 0U);
    EXPECT_EQ(fleet.count, 1U);
    const PeerUnit& peer = fleet.peers[0];
    EXPECT_TRUE(std::strcmp(peer.name.data(), "merlinccu3") == 0);
    EXPECT_TRUE(std::strcmp(peer.version.data(), "76c0214") == 0);
    EXPECT_TRUE(std::strcmp(peer.top_alert.data(), "UPDATE FAILED") == 0);
    EXPECT_EQ(peer.alert_count, 3U);
    EXPECT_FALSE(peer.availability_known);
}

HOST_TEST(fleet_holds_at_most_its_table_of_peers)
{
    UnitFleet fleet = {};
    char mac[unit_fleet::kMacLength + 1U] = {};
    for (unsigned i = 0U; i < unit_fleet::kMaxPeers + 2U; ++i)
    {
        std::snprintf(mac, sizeof(mac), "aabbccddee%02x", i);
        send(fleet, mac, unit_fleet::Kind::Availability, "online", 0U);
    }
    EXPECT_EQ(fleet.count, unit_fleet::kMaxPeers);
}

HOST_TEST(unit_offline_needs_thirty_minutes_and_only_while_listening)
{
    UnitFleet fleet = with_peer("76c0214");
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 1000U);
    (void)unit_fleet::update(fleet, 1000U + (29U * kMinute), "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);
    (void)unit_fleet::update(fleet, 1000U + unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 1U);

    // A repeated retained "offline" (a reconnect) keeps the original time.
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 40U * kMinute);
    (void)unit_fleet::update(fleet, 41U * kMinute, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 1U);

    (void)unit_fleet::set_listening(fleet, false);
    (void)unit_fleet::update(fleet, 42U * kMinute, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);

    (void)unit_fleet::set_listening(fleet, true);
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "online", 43U * kMinute);
    (void)unit_fleet::update(fleet, 43U * kMinute, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);
}

HOST_TEST(accepting_unit_offline_holds_until_the_unit_is_back)
{
    UnitFleet fleet = with_peer("76c0214");
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 0U);
    (void)unit_fleet::update(fleet, unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_TRUE(unit_fleet::acknowledge_offline(fleet));
    EXPECT_TRUE(fleet.peers[0].offline_acknowledged);
    const auto to_publish = unit_fleet::next_ack_to_publish(fleet);
    EXPECT_TRUE(to_publish.has_value() && *to_publish == 0U);

    (void)unit_fleet::update(fleet, 2U * unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);

    // Back again: the acceptance is spent, and a queued one is dropped.
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "online", 70U * kMinute);
    EXPECT_FALSE(fleet.peers[0].offline_acknowledged);
    EXPECT_FALSE(unit_fleet::next_ack_to_publish(fleet).has_value());

    // Its next outage raises the alert again.
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 80U * kMinute);
    (void)unit_fleet::update(fleet, 80U * kMinute + unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 1U);
}

HOST_TEST(an_acceptance_from_another_unit_clears_unit_offline_here)
{
    UnitFleet fleet = with_peer("76c0214");
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 0U);
    (void)unit_fleet::update(fleet, unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 1U);

    send(fleet, kPeerA, unit_fleet::Kind::OfflineAck, "1", unit_fleet::kOfflineAlertMs);
    (void)unit_fleet::update(fleet, unit_fleet::kOfflineAlertMs + 1U, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);
    EXPECT_FALSE(fleet.peers[0].ack_to_publish);

    // The returning unit empties its acceptance topic.
    send(fleet, kPeerA, unit_fleet::Kind::OfflineAck, "", 50U * kMinute);
    EXPECT_FALSE(fleet.peers[0].offline_acknowledged);
}

HOST_TEST(an_acceptance_arriving_before_the_unit_is_seen_still_counts)
{
    UnitFleet fleet = {};
    (void)unit_fleet::set_listening(fleet, true);
    send(fleet, kPeerB, unit_fleet::Kind::OfflineAck, "1", 0U);
    send(fleet, kPeerB, unit_fleet::Kind::Availability, "offline", 0U);
    (void)unit_fleet::update(fleet, 2U * unit_fleet::kOfflineAlertMs, "76c0214");
    EXPECT_EQ(fleet.offline_alert_mask, 0U);

    // An emptied acceptance for an unknown unit does not take a slot.
    send(fleet, kPeerA, unit_fleet::Kind::OfflineAck, "", 0U);
    EXPECT_EQ(fleet.count, 1U);
}

HOST_TEST(mixed_firmware_needs_ten_minutes_of_online_disagreement)
{
    UnitFleet fleet = with_peer("9271096");
    (void)unit_fleet::update(fleet, 0U, "76c0214");
    EXPECT_FALSE(fleet.mixed_firmware);
    (void)unit_fleet::update(fleet, unit_fleet::kMixedFirmwareMs - 1U, "76c0214");
    EXPECT_FALSE(fleet.mixed_firmware);
    (void)unit_fleet::update(fleet, unit_fleet::kMixedFirmwareMs, "76c0214");
    EXPECT_TRUE(fleet.mixed_firmware);

    // An offline unit's version no longer counts.
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 11U * kMinute);
    (void)unit_fleet::update(fleet, 11U * kMinute, "76c0214");
    EXPECT_FALSE(fleet.mixed_firmware);
    EXPECT_FALSE(fleet.mixed_since_ms.has_value());
}

HOST_TEST(a_peer_is_named_by_its_mac_until_its_summary_arrives)
{
    UnitFleet fleet = {};
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "online", 0U);
    char name[24] = {};
    unit_fleet::display_name(fleet.peers[0], name, sizeof(name));
    EXPECT_TRUE(std::strcmp(name, "unit 0b828a") == 0);
}

HOST_TEST(unit_alerts_are_raised_from_the_fleet_and_left_out_of_the_summary)
{
    alert_controller::reset();
    ConsoleState state = {};
    state.wifi_status.state = WifiConnectionState::Connected;
    state.time_status.synced = true;
    std::snprintf(state.firmware_status.version.data(), state.firmware_status.version.size(),
                  "76c0214");
    state.unit_fleet = with_peer("9271096");
    send(state.unit_fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 0U);
    (void)unit_fleet::update(state.unit_fleet, unit_fleet::kOfflineAlertMs, "76c0214");
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_EQ(state.active_alerts[0].code, static_cast<uint8_t>(AlertCode::UnitOffline));
    EXPECT_TRUE(std::strstr(state.active_alerts[0].detail.data(), "merlinccu2 has been offline") !=
                nullptr);

    const ActiveAlert* top = nullptr;
    EXPECT_EQ(alert_controller::own_alerts(state, &top), 0U);
    EXPECT_TRUE(top == nullptr);

    // Accepting it is seen as acknowledged, which the MQTT side relays.
    alert_controller::accept_alert(state, 0U);
    EXPECT_TRUE(alert_controller::acknowledged(AlertCode::UnitOffline));
    EXPECT_TRUE(unit_fleet::acknowledge_offline(state.unit_fleet));
    alert_controller::sync(state);
    EXPECT_FALSE(alert_controller::acknowledged(AlertCode::UnitOffline));
    EXPECT_EQ(state.alert_count, 0U);

    // Back online on other firmware for ten minutes: MIXED FIRMWARE.
    send(state.unit_fleet, kPeerA, unit_fleet::Kind::Availability, "online", 40U * kMinute);
    (void)unit_fleet::update(state.unit_fleet, 40U * kMinute, "76c0214");
    (void)unit_fleet::update(state.unit_fleet, 50U * kMinute, "76c0214");
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);
    EXPECT_EQ(state.active_alerts[0].code, static_cast<uint8_t>(AlertCode::MixedFirmware));
    EXPECT_TRUE(std::strstr(state.active_alerts[0].detail.data(), "merlinccu2 runs 9271096") !=
                nullptr);
    alert_controller::reset();
}

HOST_TEST(an_alert_raised_with_no_key_pressed_lights_the_alert_lamp)
{
    console_controller::init();
    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connected;
    (void)console_controller::set_wifi_status(wifi);
    TimeStatus time = {};
    time.synced = true;
    (void)console_controller::set_time_status(time);
    console_controller::sample_alerts();
    const auto alert_lamp = []
    { return console_controller::state().lamps[static_cast<size_t>(LampId::AlertLamp)]; };
    EXPECT_TRUE(alert_lamp() == LampMode::Off);

    // UNIT OFFLINE arrives from MQTT; only the one-second sample follows.
    UnitFleet fleet = with_peer("76c0214");
    send(fleet, kPeerA, unit_fleet::Kind::Availability, "offline", 0U);
    (void)unit_fleet::update(fleet, unit_fleet::kOfflineAlertMs, "76c0214");
    (void)console_controller::set_unit_fleet(fleet);
    console_controller::sample_alerts();
    EXPECT_TRUE(alert_lamp() == LampMode::FlashSlow);
    console_controller::init();
}
