#include "mqtt_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include "alert_controller.h"
#include "config_fields.h"
#include "config_manager.h"
#include "console_controller.h"
#include "debug_logging.h"
#include "firmware_version.h"
#include "framebuffer.h"
#include "ha_key_list.h"
#include "home_controls.h"
#include "house_weather.h"
#include "input.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "mqtt_commands.h"
#include "ota_manager.h"
#include "page_table.h"
#include "panel_config.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "pico/stdlib.h"
#include "png_encoder.h"
#include "psram.h"
#include "retry_backoff.h"
#include "text_utils.h"
#include "unit_fleet.h"

namespace
{

#if __has_include("mqtt_credentials.h")
#include "mqtt_credentials.h"
constexpr bool kMqttConfigured = true;
inline constexpr const char* kMqttHost = kHomeAssistantMqttHost;
inline constexpr uint16_t kMqttPort = kHomeAssistantMqttPort;
inline constexpr const char* kMqttUsername = kHomeAssistantMqttUsername;
inline constexpr const char* kMqttPassword = kHomeAssistantMqttPassword;
inline constexpr const char* kMqttDiscoveryPrefix = kHomeAssistantMqttDiscoveryPrefix;
inline constexpr const char* kMqttBaseTopic = kHomeAssistantMqttBaseTopic;
#else
constexpr bool kMqttConfigured = false;
inline constexpr char kMqttHost[] = "";
inline constexpr uint16_t kMqttPort = 1883;
inline constexpr char kMqttUsername[] = "";
inline constexpr char kMqttPassword[] = "";
inline constexpr char kMqttDiscoveryPrefix[] = "homeassistant";
inline constexpr char kMqttBaseTopic[] = "merlinccu";
#endif

/// @brief Normalizes MQTT settings to the repo's internal naming style.
/// @details The optional credentials header keeps explicit `HOME_ASSISTANT_*` names because
/// users edit those files directly. This translation layer either aliases those symbols or
/// provides local defaults so runtime constants read consistently throughout the implementation.
constexpr bool kMqttRuntimeEnabled = true;
constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr size_t kMacAddressHexDigitCount = 12;
constexpr size_t kMacAddressHexTextCapacity = kMacAddressHexDigitCount + 1;
constexpr size_t kDeviceNameSuffixLength = 6;
constexpr uint16_t kMqttKeepAliveSeconds = 60;
constexpr char kUnknownMacText[] = "unknown";
constexpr char kOnlineState[] = "online";
constexpr char kOfflineState[] = "offline";
constexpr char kUnknownState[] = "unknown";
constexpr char kNotConfiguredState[] = "not_configured";
constexpr char kDeviceModel[] = "Raspberry Pi Pico 2 W";
constexpr char kManufacturer[] = "MerlinCCU";

enum class SensorId : uint8_t
{
    Status = 0,
    WifiState,
    IpAddress,
    HomeAssistantState,
    HomeAssistantHttp,
    TrackedEntity,
    FirmwareVersion,
    FirmwareUpdate,
    SettingsChange,
    HaMessageState,
    // Long-run health (#155).
    LastReset,
    Uptime,
    StackFree,
    HeapArena,
    // The panel as a picture for dashboards (#159): an HA image entity whose
    // PNG is published by publish_screen_image(), not the value table.
    Screen,
    // Entities Home Assistant controls (#150), in mqtt_commands::Target order.
    AlertLamp,
    TestLamp,
    PanelBrightness,
    KeyBacklight,
    Display,
    Page,
    Message,
    Count,
};

enum class PendingPublishType : uint8_t
{
    None = 0,
    Availability,
    Discovery,
    State,
    KeyTrigger,
    KeyEvent,
    ScreenImage,
    ControlPress,
    /// This unit's summary for the other units (#210).
    UnitSummary,
    /// Emptying this unit's own UNIT OFFLINE acceptance as it connects.
    UnitAckClear,
    /// Telling the other units UNIT OFFLINE was accepted here.
    UnitAck,
};

/// @brief Describes one MQTT sensor entity published through Home Assistant discovery.
/// @details The discovery payload builder uses this metadata table to keep topic names,
/// icons, and option lists aligned with the `SensorId` enum instead of scattering that
/// information across multiple switch statements.
struct SensorDescriptor
{
    const char* name;
    const char* slug;
    const char* icon;
    bool diagnostic;
    const char* options_json;
    /// Command target for entities HA controls; Count for read-only sensors.
    /// Their name, slug, icon, platform and options come from mqtt_commands.
    mqtt_commands::Target command;
};

using mqtt_commands::Target;
constexpr Target kNoCommand = Target::Count;

/// @brief Static discovery metadata for every MQTT-backed sensor exposed by the firmware.
/// @details This table is kept in enum order so a `SensorId` can directly index into the
/// matching Home Assistant discovery settings.
constexpr SensorDescriptor kSensorDescriptors[] = {
    {"Status", "status", "mdi:radio-tower", false, nullptr, kNoCommand},
    {"Wi-Fi state", "wifi_state", "mdi:wifi", true,
     "[\"disabled\",\"unconfigured\",\"initializing\",\"scanning\",\"connecting\",\"waiting_for_"
     "ip\","
     "\"connected\",\"auth_failed\",\"no_network\",\"connect_failed\",\"error\"]",
     kNoCommand},
    {"IP address", "ip_address", "mdi:ip-network", true, nullptr, kNoCommand},
    {"Home Assistant", "home_assistant_state", "mdi:home-assistant", true,
     "[\"disabled\",\"unconfigured\",\"waiting_for_wifi\",\"resolving\",\"connecting\","
     "\"authorizing\","
     "\"connected\",\"unauthorized\",\"error\"]",
     kNoCommand},
    {"Home Assistant HTTP", "home_assistant_http", "mdi:web", true, nullptr, kNoCommand},
    {"Tracked entity", "tracked_entity", "mdi:toggle-switch-outline", false, nullptr, kNoCommand},
    // Firmware commit and update outcome (#154), so HA shows which build each
    // unit runs and flags one whose last update rolled back.
    {"Firmware", "firmware_version", "mdi:chip", true, nullptr, kNoCommand},
    {"Firmware update", "firmware_update", "mdi:update", false,
     "[\"confirmed\",\"on_trial\",\"rolled_back\",\"not_partitioned\"]", kNoCommand},
    // Last settings save since start-up and where it came from (#158).
    {"Last settings change", "settings_change", "mdi:cog-transfer", true, nullptr, kNoCommand},
    // Whether HA's last message is showing or was accepted on the panel (#150).
    {"Message state", "message_state", "mdi:message-check", false,
     "[\"none\",\"active\",\"acknowledged\"]", kNoCommand},
    // Why the unit last started, uptime, and worst-case stack and heap use
    // (#155), so HA can chart restarts and slow memory growth over a soak.
    // Options are filled from reset_reason::slug_options_json().
    {"Last reset", "last_reset", "mdi:restart-alert", true, nullptr, kNoCommand},
    {"Uptime", "uptime", "mdi:timer-outline", true, nullptr, kNoCommand},
    {"Stack free", "stack_free", "mdi:layers-outline", true, nullptr, kNoCommand},
    {"Heap arena", "heap_arena", "mdi:memory", true, nullptr, kNoCommand},
    {"Screen", "screen", "mdi:monitor-screenshot", false, nullptr, kNoCommand},
    {nullptr, nullptr, nullptr, false, nullptr, Target::AlertLamp},
    {nullptr, nullptr, nullptr, false, nullptr, Target::TestLamp},
    {nullptr, nullptr, nullptr, false, nullptr, Target::PanelBrightness},
    {nullptr, nullptr, nullptr, false, nullptr, Target::KeyBacklight},
    {nullptr, nullptr, nullptr, false, nullptr, Target::Display},
    {nullptr, nullptr, nullptr, false, nullptr, Target::Page},
    {nullptr, nullptr, nullptr, false, nullptr, Target::Message},
};

/// @brief An entity's slug, from its descriptor or its command target.
const char* entity_slug(const SensorDescriptor& sensor)
{
    return sensor.command != kNoCommand ? mqtt_commands::info(sensor.command).slug : sensor.slug;
}

static_assert((sizeof(kSensorDescriptors) / sizeof(kSensorDescriptors[0])) ==
                  static_cast<size_t>(SensorId::Count),
              "MQTT sensor descriptors must match SensorId");

/// @brief Converts a sensor enum into the matching descriptor and topic-array index.
/// @details Discovery and publish state are all stored in enum-ordered arrays, so this helper
/// keeps the indexing assumption obvious at the call site.
constexpr size_t sensor_index(SensorId id)
{
    return static_cast<size_t>(id);
}

using text_utils::copy_text;

/// @brief Copies a C string into a bounded destination buffer.
void copy_cstr(char* dst, size_t dst_size, const char* src)
{
    if (dst == nullptr || dst_size == 0)
    {
        return;
    }

    dst[0] = '\0';
    if (src == nullptr)
    {
        return;
    }

    std::snprintf(dst, dst_size, "%s", src);
}

/// @brief Returns whether a character is a hexadecimal digit.
bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

/// @brief Normalizes one hexadecimal digit to lower case.
char to_lower_hex(char c)
{
    if (c >= 'A' && c <= 'F')
    {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

/// @brief Returns the MQTT-published string for one Wi-Fi state.
const char* wifi_state_text(WifiConnectionState state)
{
    switch (state)
    {
    case WifiConnectionState::Disabled:
        return "disabled";
    case WifiConnectionState::Unconfigured:
        return "unconfigured";
    case WifiConnectionState::Initializing:
        return "initializing";
    case WifiConnectionState::Scanning:
        return "scanning";
    case WifiConnectionState::Connecting:
        return "connecting";
    case WifiConnectionState::WaitingForIp:
        return "waiting_for_ip";
    case WifiConnectionState::Connected:
        return "connected";
    case WifiConnectionState::AuthFailed:
        return "auth_failed";
    case WifiConnectionState::NoNetwork:
        return "no_network";
    case WifiConnectionState::ConnectFailed:
        return "connect_failed";
    case WifiConnectionState::Error:
        return "error";
    }

    return "error";
}

/// @brief Returns the MQTT-published string for one Home Assistant state.
const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state)
    {
    case HomeAssistantConnectionState::Disabled:
        return "disabled";
    case HomeAssistantConnectionState::Unconfigured:
        return "unconfigured";
    case HomeAssistantConnectionState::WaitingForWifi:
        return "waiting_for_wifi";
    case HomeAssistantConnectionState::Resolving:
        return "resolving";
    case HomeAssistantConnectionState::Connecting:
        return "connecting";
    case HomeAssistantConnectionState::Authorizing:
        return "authorizing";
    case HomeAssistantConnectionState::Connected:
        return "connected";
    case HomeAssistantConnectionState::Unauthorized:
        return "unauthorized";
    case HomeAssistantConnectionState::Error:
        return "error";
    }

    return "error";
}

MqttStatus g_status = {};
ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
bool g_connect_attempted = false;
bool g_connect_in_progress = false;
bool g_connected = false;
bool g_request_in_flight = false;
bool g_availability_published = false;
absolute_time_t g_dns_deadline = nil_time;
// After a refused, failed or dropped session, when to start a fresh one
// (#155). Nil until a failure is seen.
absolute_time_t g_next_session_at = nil_time;
constexpr uint32_t kReconnectInitialDelayMs = 5000U;
constexpr uint32_t kReconnectMaxDelayMs = 5U * 60U * 1000U;
RetryBackoff g_reconnect_backoff(kReconnectInitialDelayMs, kReconnectMaxDelayMs);
mqtt_client_t* g_client = nullptr;
PendingPublishType g_publish_type_in_flight = PendingPublishType::None;
SensorId g_sensor_in_flight = SensorId::Status;
char g_broker_host[config_fields::kHostLength + 1U] = {};
uint16_t g_broker_port = kMqttPort;
bool g_config_valid = false;
char g_client_id[48] = {};
char g_device_id[32] = {};
char g_device_name[32] = {};
char g_availability_topic[96] = {};
// "<base>/<mac>": state topics are "<prefix>/<slug>/state" and HA's commands
// arrive on "<prefix>/<slug>/set" (#150).
char g_topic_prefix[80] = {};
char g_command_filter[96] = {};
char g_discovery_prefix[40] = {};

// HA keys (#151): each has a retained device-trigger config so HA's
// automation editor offers it; a press is a non-retained "press" on
// "<prefix>/key/<slug>". g_triggers_published tracks which configs HA has,
// so keys taken off the list get their config cleared.
constexpr uint64_t kAllKeysMask =
    (uint64_t{1} << static_cast<unsigned>(ButtonId::Count)) - 1U;
uint64_t g_triggers_published = kAllKeysMask;
ButtonId g_key_in_flight = ButtonId::LeftTop;
bool g_key_trigger_removing = false;
bool g_subscribed = false;
bool g_subscribe_in_flight = false;

/// @brief One received command, copied out of the lwIP callbacks for the
/// main loop. A second command arriving before the first is applied is
/// dropped and logged; HA's commands are one-off user actions.
struct PendingCommand
{
    // Shared with the lwIP callbacks; the main loop reads it under the
    // cyw43 lwIP lock.
    bool ready;
    bool receiving;
    bool overflow;
    char topic[96];
    char payload[mqtt_commands::kMaxPayloadLength + 1U];
    size_t payload_length;
};
PendingCommand g_pending_command = {};

// House weather from HA (#153): retained "<base>/house/weather/{now,hourly,
// daily,warnings}" (warnings: #202), shared by every CCU on the broker. Each topic has its own
// buffer, so the three retained messages that arrive together on subscribe are all kept. The lwIP
// callbacks fill a buffer and set `ready`; the main loop reads it and clears `ready`, and the
// callbacks never touch a ready buffer.
char g_base_topic[48] = {};
static_assert(sizeof(g_base_topic) > config_fields::kMqttBaseTopicLength,
              "g_base_topic must hold the longest base topic the web page accepts");
char g_house_filter[96] = {};
bool g_house_subscribed = false;
bool g_house_subscribe_in_flight = false;
MERLIN_PSRAM_BSS char g_house_now[house_weather::kMaxPayloadBytes[0] + 1U];
MERLIN_PSRAM_BSS char g_house_hourly[house_weather::kMaxPayloadBytes[1] + 1U];
MERLIN_PSRAM_BSS char g_house_daily[house_weather::kMaxPayloadBytes[2] + 1U];
MERLIN_PSRAM_BSS char g_house_warnings[house_weather::kMaxPayloadBytes[3] + 1U];

/// @brief One topic's receive buffer, filled by the lwIP callbacks and read
/// by the main loop (house weather and Controls).
struct InboundBuffer
{
    char* data;
    size_t capacity;
    size_t length;
    bool ready;
};
std::array<InboundBuffer, house_weather::kTopicCount> g_house_buffers = {{
    {g_house_now, sizeof(g_house_now), 0U, false},
    {g_house_hourly, sizeof(g_house_hourly), 0U, false},
    {g_house_daily, sizeof(g_house_daily), 0U, false},
    {g_house_warnings, sizeof(g_house_warnings), 0U, false},
}};

// This unit's Controls list and HA's answers to its presses (#152), on
// "<prefix>/controls/{list,result}", indexed by home_controls::Topic.
// Presses go out on "<prefix>/controls/press".
char g_controls_filter[96] = {};
bool g_controls_subscribed = false;
bool g_controls_subscribe_in_flight = false;
MERLIN_PSRAM_BSS char g_controls_list[home_controls::kMaxListBytes + 1U];
char g_controls_result[home_controls::kMaxResultBytes + 1U] = {};
std::array<InboundBuffer, 2> g_controls_buffers = {{
    {g_controls_list, sizeof(g_controls_list), 0U, false},
    {g_controls_result, sizeof(g_controls_result), 0U, false},
}};
// The other units (#210): "<base>/+/+" brings every unit's availability,
// summary and offline_ack. The lwIP callbacks apply each publish to g_fleet;
// the main loop works out the alerts under the lwIP lock and keeps a copy for
// the console.
char g_fleet_filter[96] = {};
bool g_fleet_subscribed = false;
bool g_fleet_subscribe_in_flight = false;
char g_self_mac[unit_fleet::kMacLength + 1U] = {};
UnitFleet g_fleet = {};
UnitFleet g_fleet_snapshot = {};
unit_fleet::TopicRef g_fleet_receiving = {};
char g_fleet_payload[unit_fleet::kMaxPayloadBytes + 1U] = {};
size_t g_fleet_payload_length = 0U;
// This unit's summary as it should be, as the broker last took it, and as
// sent in the publish in flight.
char g_own_summary[unit_fleet::kMaxPayloadBytes + 1U] = {};
char g_published_summary[unit_fleet::kMaxPayloadBytes + 1U] = {};
char g_summary_in_flight[unit_fleet::kMaxPayloadBytes + 1U] = {};
// Emptied once per session, so an acceptance left from this unit's last
// outage cannot hide its next one.
bool g_own_ack_cleared = false;
char g_ack_mac_in_flight[unit_fleet::kMacLength + 1U] = {};

// Discovery JSON, and the panel PNG (#159). Pages are 0.5 to 2 KB as PNG; a
// bigger one (heavy dither) is skipped. The lwIP ring (lwipopts.h) holds
// this plus the topic.
constexpr size_t kScreenImageMaxBytes = 3584U;
MERLIN_PSRAM_BSS char g_publish_buffer[kScreenImageMaxBytes];
// The panel picture goes out when the screen has changed, at most this often,
// so a dashboard stays current without loading the broker or the loop.
constexpr uint32_t kScreenImageIntervalMs = 5000U;
absolute_time_t g_screen_image_due = nil_time;
// CRC of the frame HA has (or that was too big to send); `valid` is false
// until one is sent on this session.
bool g_screen_image_valid = false;
uint32_t g_screen_image_crc = 0U;
uint32_t g_screen_image_crc_in_flight = 0U;
// Frames skipped as too large since the last one that fitted. Only the first
// skip of a run is logged, and the count when one fits again.
// (A busy page that stays too large changes every few seconds, which made
// one line per frame.)
uint32_t g_screen_images_skipped = 0U;
mqtt_connect_client_info_t g_client_info = {};
std::array<std::array<char, 64>, static_cast<size_t>(SensorId::Count)> g_sensor_unique_ids = {};
std::array<std::array<char, 96>, static_cast<size_t>(SensorId::Count)> g_sensor_state_topics = {};
std::array<std::array<char, 128>, static_cast<size_t>(SensorId::Count)> g_sensor_discovery_topics =
    {};
std::array<std::array<char, 32>, static_cast<size_t>(SensorId::Count)> g_current_sensor_values = {};
std::array<std::array<char, 32>, static_cast<size_t>(SensorId::Count)> g_published_sensor_values =
    {};
std::array<bool, static_cast<size_t>(SensorId::Count)> g_sensor_discovery_published = {};

/// @brief Updates the public MQTT status snapshot.
void set_status(MqttConnectionState state, int last_error)
{
    g_status.state = state;
    g_status.last_error = last_error;
}

/// @brief Clears retained topic, value, and discovery bookkeeping.
void clear_sensor_tracking()
{
    for (auto& unique_id : g_sensor_unique_ids)
    {
        unique_id.fill('\0');
    }
    for (auto& topic : g_sensor_state_topics)
    {
        topic.fill('\0');
    }
    for (auto& topic : g_sensor_discovery_topics)
    {
        topic.fill('\0');
    }
    for (auto& value : g_current_sensor_values)
    {
        value.fill('\0');
    }
    for (auto& value : g_published_sensor_values)
    {
        value.fill('\0');
    }
    g_sensor_discovery_published.fill(false);
    g_status.discovery_published = false;
}

/// @brief Resets live MQTT session state and optionally publish bookkeeping.
void reset_runtime(bool reset_publish_state)
{
    g_dns_pending = false;
    g_dns_resolved = false;
    g_connect_attempted = false;
    g_connect_in_progress = false;
    g_connected = false;
    g_dns_deadline = nil_time;
    g_next_session_at = nil_time;
    g_request_in_flight = false;
    g_publish_type_in_flight = PendingPublishType::None;
    g_sensor_in_flight = SensorId::Status;
    g_availability_published = false;
    g_subscribed = false;
    g_subscribe_in_flight = false;
    g_house_subscribed = false;
    g_house_subscribe_in_flight = false;
    g_controls_subscribed = false;
    g_controls_subscribe_in_flight = false;
    g_fleet_subscribed = false;
    g_fleet_subscribe_in_flight = false;
    g_published_summary[0] = '\0';
    g_own_ack_cleared = false;
    // Unknown after a reconnect or restart: treat every key as published, so
    // configs for keys no longer on the list are cleared once (#151).
    g_triggers_published = kAllKeysMask;
    g_screen_image_valid = false;
    g_screen_image_due = nil_time;

    if (g_client != nullptr)
    {
        cyw43_arch_lwip_begin();
        if (mqtt_client_is_connected(g_client))
        {
            mqtt_disconnect(g_client);
        }
        mqtt_client_free(g_client);
        cyw43_arch_lwip_end();
        g_client = nullptr;
    }
    cyw43_arch_lwip_begin();
    (void)unit_fleet::set_listening(g_fleet, false);
    cyw43_arch_lwip_end();

    if (reset_publish_state)
    {
        clear_sensor_tracking();
    }
    else
    {
        for (auto& value : g_published_sensor_values)
        {
            value.fill('\0');
        }
    }
}

/// @brief Parses and normalizes the configured MQTT broker endpoint.
bool parse_mqtt_endpoint()
{
    g_broker_host[0] = '\0';
    const RuntimeConfig& config = config_manager::settings();
    const bool use_runtime = config.mqtt_enabled && config.mqtt_host[0] != '\0';
    const char* mqtt_host = use_runtime ? config.mqtt_host.data() : kMqttHost;
    g_broker_port = use_runtime ? config.mqtt_port : kMqttPort;

    // Parse the broker endpoint once at startup so the runtime state machine
    // only has to deal with connect/publish behaviour, not string cleanup.
    if (mqtt_host[0] == '\0')
    {
        return false;
    }

    if (!text_utils::parse_endpoint(mqtt_host, text_utils::kMqttSchemes, g_broker_port,
                                    g_broker_host, sizeof(g_broker_host), &g_broker_port))
    {
        std::printf("MQTT config host/port is invalid: %s\n", mqtt_host);
        return false;
    }

    return true;
}

/// @brief Derives MQTT identity strings and retained topic names from Wi-Fi identity.
void configure_identity_and_topics(const WifiStatus& wifi_status)
{
    char mac_hex[kMacAddressHexTextCapacity] = {};
    size_t hex_len = 0;
    // Derive stable MQTT identifiers from the Wi-Fi MAC so Home Assistant sees
    // the device as the same entity across reboots without extra storage.
    for (char c : wifi_status.mac_address)
    {
        if (c == '\0')
        {
            break;
        }
        if (is_hex_digit(c) && hex_len + 1 < sizeof(mac_hex))
        {
            mac_hex[hex_len++] = to_lower_hex(c);
        }
    }
    mac_hex[hex_len] = '\0';

    if (hex_len != kMacAddressHexDigitCount)
    {
        std::snprintf(mac_hex, sizeof(mac_hex), "%s", kUnknownMacText);
    }

    const RuntimeConfig& config = config_manager::settings();
    const char* base_topic = config.mqtt_base_topic[0] ? config.mqtt_base_topic.data()
                                                       : (kMqttBaseTopic[0] ? kMqttBaseTopic
                                                                            : "merlinccu");
    const char* discovery_prefix =
        config.mqtt_discovery_prefix[0]
            ? config.mqtt_discovery_prefix.data()
            : (kMqttDiscoveryPrefix[0] ? kMqttDiscoveryPrefix : "homeassistant");
    std::snprintf(g_discovery_prefix, sizeof(g_discovery_prefix), "%s", discovery_prefix);
    const char* name_suffix = (std::strlen(mac_hex) >= kDeviceNameSuffixLength)
                                  ? (mac_hex + std::strlen(mac_hex) - kDeviceNameSuffixLength)
                                  : mac_hex;

    std::snprintf(g_device_id, sizeof(g_device_id), "merlinccu_%s", mac_hex);
    std::snprintf(g_client_id, sizeof(g_client_id), "merlinccu-%s", mac_hex);
    // HA shows the display label (e.g. "Ground floor") so several units are
    // told apart by name; identity stays on the MAC above, so renaming a unit
    // never orphans its entities (#149).
    if (config.device_label[0] != '\0')
    {
        std::snprintf(g_device_name, sizeof(g_device_name), "%s", config.device_label.data());
        // The label is free text but goes into the discovery JSON unescaped.
        for (char& c : g_device_name)
        {
            if (c == '"' || c == '\\')
            {
                c = '\'';
            }
        }
    }
    else if (config.device_name[0] != '\0')
    {
        std::snprintf(g_device_name, sizeof(g_device_name), "%s", config.device_name.data());
    }
    else
    {
        std::snprintf(g_device_name, sizeof(g_device_name), "MerlinCCU %s", name_suffix);
    }
    std::snprintf(g_availability_topic, sizeof(g_availability_topic), "%s/%s/availability",
                  base_topic, mac_hex);
    std::snprintf(g_topic_prefix, sizeof(g_topic_prefix), "%s/%s", base_topic, mac_hex);
    std::snprintf(g_base_topic, sizeof(g_base_topic), "%s", base_topic);
    std::snprintf(g_self_mac, sizeof(g_self_mac), "%s", mac_hex);

    // Topic strings are precomputed once per session so the publish path can
    // focus on ordering and retry behaviour instead of repeated formatting.
    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i)
    {
        const SensorDescriptor& sensor = kSensorDescriptors[i];
        const char* slug = entity_slug(sensor);
        const char* component =
            sensor.command != kNoCommand
                ? mqtt_commands::info(sensor.command).component
                : (static_cast<SensorId>(i) == SensorId::Screen ? "image" : "sensor");
        std::snprintf(g_sensor_unique_ids[i].data(), g_sensor_unique_ids[i].size(), "%s_%s",
                      g_device_id, slug);
        std::snprintf(g_sensor_state_topics[i].data(), g_sensor_state_topics[i].size(),
                      "%s/%s/state", g_topic_prefix, slug);
        std::snprintf(g_sensor_discovery_topics[i].data(), g_sensor_discovery_topics[i].size(),
                      "%s/%s/%s/%s/config", discovery_prefix, component, g_device_id, slug);
    }

    copy_text(g_status.device_id, g_device_id);
}

/// @brief Where the payload of the publish being received goes.
enum class Receiving : uint8_t
{
    Nothing = 0,
    Command,
    /// House weather or Controls, into g_receiving_buffer.
    Buffer,
    /// Another unit's topic (#210), into g_fleet_payload.
    Fleet,
};

/// @brief Set by incoming_publish_cb for incoming_data_cb (lwIP context).
Receiving g_receiving = Receiving::Nothing;
InboundBuffer* g_receiving_buffer = nullptr;

/// @brief Starts receiving one payload into `buffer` (lwIP context).
/// @details One that is too big, or that arrives while the last of the same
/// topic still waits for the main loop, is dropped and logged. The main loop
/// empties buffers every pass, and HA republishes.
void start_buffer(InboundBuffer& buffer, const char* topic_name, u32_t tot_len)
{
    if (buffer.ready || tot_len > buffer.capacity - 1U)
    {
        g_receiving = Receiving::Nothing;
        std::printf("MQTT payload dropped: %s (%lu bytes)\n", topic_name,
                    static_cast<unsigned long>(tot_len));
        return;
    }
    g_receiving = Receiving::Buffer;
    g_receiving_buffer = &buffer;
    buffer.length = 0U;
}

/// @brief Starts receiving one publish from the broker (lwIP context).
/// @details House weather (#153) and this unit's Controls topics (#152) go
/// to their own buffers. Anything else is a command from HA. It is copied for
/// the main loop; one that is too long, or that arrives while the last is
/// still waiting, is dropped and logged.
void incoming_publish_cb(void* /*arg*/, const char* topic, u32_t tot_len)
{
    if (const std::optional<house_weather::Topic> house =
            house_weather::topic_for(topic, g_base_topic))
    {
        start_buffer(g_house_buffers[static_cast<size_t>(*house)], topic, tot_len);
        return;
    }
    if (const std::optional<home_controls::Topic> controls =
            home_controls::topic_for(topic, g_topic_prefix))
    {
        start_buffer(g_controls_buffers[static_cast<size_t>(*controls)], topic, tot_len);
        return;
    }
    // Everything "<base>/+/+" brings is another unit's, never a command; topics
    // it does not know, and oversized ones, are skipped.
    if (unit_fleet::in_fleet_filter(topic, g_base_topic))
    {
        const std::optional<unit_fleet::TopicRef> ref = unit_fleet::topic_for(topic, g_base_topic);
        g_receiving = Receiving::Nothing;
        if (ref.has_value() && tot_len <= unit_fleet::kMaxPayloadBytes)
        {
            g_fleet_receiving = *ref;
            g_fleet_payload_length = 0U;
            g_receiving = Receiving::Fleet;
        }
        return;
    }
    PendingCommand& command = g_pending_command;
    if (command.ready || tot_len > mqtt_commands::kMaxPayloadLength ||
        std::strlen(topic) >= sizeof(command.topic))
    {
        g_receiving = Receiving::Nothing;
        std::printf("MQTT command dropped: %s (%lu bytes)\n", topic,
                    static_cast<unsigned long>(tot_len));
        return;
    }
    g_receiving = Receiving::Command;
    std::snprintf(command.topic, sizeof(command.topic), "%s", topic);
    command.payload_length = 0U;
    command.receiving = true;
}

/// @brief Collects a publish's payload, which may arrive in pieces (lwIP
/// context).
void incoming_data_cb(void* /*arg*/, const u8_t* data, u16_t len, u8_t flags)
{
    if (g_receiving == Receiving::Fleet)
    {
        const size_t count =
            std::min<size_t>(len, unit_fleet::kMaxPayloadBytes - g_fleet_payload_length);
        if (count > 0U)
        {
            std::memcpy(g_fleet_payload + g_fleet_payload_length, data, count);
            g_fleet_payload_length += count;
        }
        if ((flags & MQTT_DATA_FLAG_LAST) != 0U)
        {
            (void)unit_fleet::apply(g_fleet, g_fleet_receiving,
                                    std::string_view(g_fleet_payload, g_fleet_payload_length),
                                    to_ms_since_boot(get_absolute_time()), g_self_mac);
            g_receiving = Receiving::Nothing;
        }
        return;
    }
    if (g_receiving == Receiving::Buffer && g_receiving_buffer != nullptr)
    {
        InboundBuffer& buffer = *g_receiving_buffer;
        const size_t count = std::min<size_t>(len, buffer.capacity - 1U - buffer.length);
        std::memcpy(buffer.data + buffer.length, data, count);
        buffer.length += count;
        if ((flags & MQTT_DATA_FLAG_LAST) != 0U)
        {
            buffer.data[buffer.length] = '\0';
            buffer.ready = true;
            g_receiving = Receiving::Nothing;
        }
        return;
    }
    PendingCommand& command = g_pending_command;
    if (g_receiving != Receiving::Command || !command.receiving)
    {
        return;
    }
    const size_t room = sizeof(command.payload) - 1U - command.payload_length;
    const size_t count = len < room ? len : room;
    std::memcpy(command.payload + command.payload_length, data, count);
    command.payload_length += count;
    if ((flags & MQTT_DATA_FLAG_LAST) != 0U)
    {
        command.payload[command.payload_length] = '\0';
        command.receiving = false;
        command.ready = true;
    }
}

/// @brief Records the result of subscribing to the command topics.
void subscribe_cb(void* /*arg*/, err_t err)
{
    g_subscribe_in_flight = false;
    g_subscribed = err == ERR_OK;
    std::printf("MQTT command subscribe %s: %s\n", g_subscribed ? "ok" : "failed",
                g_command_filter);
}

/// @brief Records the result of subscribing to the house weather topics.
void house_subscribe_cb(void* /*arg*/, err_t err)
{
    g_house_subscribe_in_flight = false;
    g_house_subscribed = err == ERR_OK;
    std::printf("MQTT house weather subscribe %s: %s\n", g_house_subscribed ? "ok" : "failed",
                g_house_filter);
}

/// @brief Records the result of subscribing to this unit's Controls topics.
void controls_subscribe_cb(void* /*arg*/, err_t err)
{
    g_controls_subscribe_in_flight = false;
    g_controls_subscribed = err == ERR_OK;
    std::printf("MQTT controls subscribe %s: %s\n", g_controls_subscribed ? "ok" : "failed",
                g_controls_filter);
}

/// @brief Records the result of subscribing to the other units' topics; the
/// table is live from here (lwIP context, so under the lock).
void fleet_subscribe_cb(void* /*arg*/, err_t err)
{
    g_fleet_subscribe_in_flight = false;
    g_fleet_subscribed = err == ERR_OK;
    (void)unit_fleet::set_listening(g_fleet, g_fleet_subscribed);
    std::printf("MQTT units subscribe %s: %s\n", g_fleet_subscribed ? "ok" : "failed",
                g_fleet_filter);
}

/// @brief Subscribes to "<prefix>/+/set", then "<base>/house/weather/+",
/// then "<prefix>/controls/+", then "<base>/+/+", once per connection.
void subscribe_commands()
{
    if (g_subscribe_in_flight || g_house_subscribe_in_flight || g_controls_subscribe_in_flight ||
        g_fleet_subscribe_in_flight || !g_connected || g_client == nullptr)
    {
        return;
    }
    if (g_subscribed)
    {
        if (!g_house_subscribed)
        {
            std::snprintf(g_house_filter, sizeof(g_house_filter), "%s/house/weather/+",
                          g_base_topic);
            cyw43_arch_lwip_begin();
            const err_t rc =
                mqtt_subscribe(g_client, g_house_filter, 0, house_subscribe_cb, nullptr);
            cyw43_arch_lwip_end();
            g_house_subscribe_in_flight = rc == ERR_OK;
        }
        else if (!g_controls_subscribed)
        {
            std::snprintf(g_controls_filter, sizeof(g_controls_filter), "%s/controls/+",
                          g_topic_prefix);
            cyw43_arch_lwip_begin();
            const err_t rc =
                mqtt_subscribe(g_client, g_controls_filter, 0, controls_subscribe_cb, nullptr);
            cyw43_arch_lwip_end();
            g_controls_subscribe_in_flight = rc == ERR_OK;
        }
        else if (!g_fleet_subscribed)
        {
            std::snprintf(g_fleet_filter, sizeof(g_fleet_filter), "%s/+/+", g_base_topic);
            cyw43_arch_lwip_begin();
            const err_t rc =
                mqtt_subscribe(g_client, g_fleet_filter, 0, fleet_subscribe_cb, nullptr);
            cyw43_arch_lwip_end();
            g_fleet_subscribe_in_flight = rc == ERR_OK;
        }
        return;
    }
    std::snprintf(g_command_filter, sizeof(g_command_filter), "%s/+/set", g_topic_prefix);
    cyw43_arch_lwip_begin();
    const err_t rc = mqtt_subscribe(g_client, g_command_filter, 0, subscribe_cb, nullptr);
    cyw43_arch_lwip_end();
    g_subscribe_in_flight = rc == ERR_OK;
}

/// @brief Hands Controls payloads received since the last call to the
/// Controls page (main loop). The callbacks leave a ready buffer alone, so it
/// is read unlocked.
void apply_controls_payloads()
{
    for (size_t i = 0U; i < g_controls_buffers.size(); ++i)
    {
        InboundBuffer& buffer = g_controls_buffers[i];
        cyw43_arch_lwip_begin();
        const bool ready = buffer.ready;
        cyw43_arch_lwip_end();
        if (!ready)
        {
            continue;
        }
        const auto topic = static_cast<home_controls::Topic>(i);
        if (topic == home_controls::Topic::List &&
            !console_controller::apply_home_controls(topic, buffer.data))
        {
            std::printf("MQTT controls list refused (%u bytes)\n",
                        static_cast<unsigned>(buffer.length));
        }
        else if (topic == home_controls::Topic::Result)
        {
            (void)console_controller::apply_home_controls(topic, buffer.data);
        }
        cyw43_arch_lwip_begin();
        buffer.ready = false;
        cyw43_arch_lwip_end();
    }
}

/// @brief Applies a command received since the last call (main loop).
void apply_pending_command()
{
    static PendingCommand command = {};
    cyw43_arch_lwip_begin();
    const bool ready = g_pending_command.ready;
    if (ready)
    {
        command = g_pending_command;
        g_pending_command.ready = false;
    }
    cyw43_arch_lwip_end();
    if (!ready)
    {
        return;
    }

    const std::string_view payload(command.payload, command.payload_length);
    const std::optional<Target> target =
        mqtt_commands::target_for_topic(command.topic, g_topic_prefix);
    const std::optional<mqtt_commands::Command> parsed =
        target.has_value() ? mqtt_commands::parse(*target, payload) : std::nullopt;
    if (!parsed.has_value())
    {
        std::printf("MQTT command refused: %s '%.*s'\n", command.topic,
                    static_cast<int>(std::min<size_t>(payload.size(), 40U)), payload.data());
        return;
    }
    std::printf("MQTT command: %s '%.*s'\n", command.topic,
                static_cast<int>(std::min<size_t>(payload.size(), 40U)), payload.data());
    (void)console_controller::apply_mqtt_command(*parsed);
}

/// @brief Allocates and configures the lwIP MQTT client on first use.
bool ensure_client()
{
    if (g_client != nullptr)
    {
        return true;
    }

    // The lwIP MQTT client is allocated lazily so the disabled/unconfigured
    // states do not hold network resources they never use.
    cyw43_arch_lwip_begin();
    g_client = mqtt_client_new();
    if (g_client != nullptr)
    {
        mqtt_set_inpub_callback(g_client, incoming_publish_cb, incoming_data_cb, nullptr);
    }
    cyw43_arch_lwip_end();

    if (g_client == nullptr)
    {
        set_status(MqttConnectionState::Error, ERR_MEM);
        return false;
    }

    std::memset(&g_client_info, 0, sizeof(g_client_info));
    const RuntimeConfig& config = config_manager::settings();
    g_client_info.client_id = g_client_id;
    g_client_info.client_user = config.mqtt_username[0]
                                    ? config.mqtt_username.data()
                                    : (kMqttUsername[0] ? kMqttUsername : nullptr);
    g_client_info.client_pass = config.mqtt_password[0]
                                    ? config.mqtt_password.data()
                                    : (kMqttPassword[0] ? kMqttPassword : nullptr);
    g_client_info.keep_alive = kMqttKeepAliveSeconds;
    g_client_info.will_topic = g_availability_topic;
    g_client_info.will_msg = kOfflineState;
    g_client_info.will_msg_len = 0;
    g_client_info.will_qos = 0;
    g_client_info.will_retain = 1;
    return true;
}

/// @brief Builds the Home Assistant discovery payload for one sensor.
bool build_discovery_payload(SensorId sensor_id)
{
    const SensorDescriptor& sensor = kSensorDescriptors[sensor_index(sensor_id)];
    const bool controllable = sensor.command != kNoCommand;
    const char* name = controllable ? mqtt_commands::info(sensor.command).name : sensor.name;
    const char* icon = controllable ? mqtt_commands::info(sensor.command).icon : sensor.icon;
    const char* options =
        controllable ? mqtt_commands::options_json(sensor.command) : sensor.options_json;
    if (sensor_id == SensorId::LastReset)
    {
        options = reset_reason::slug_options_json();
    }
    char extras[512] = {};
    size_t extras_len = 0;
    bool fits = true;
    const auto add = [&](const char* format, const char* value)
    {
        const int length =
            std::snprintf(extras + extras_len, sizeof(extras) - extras_len, format, value);
        if (length <= 0 || static_cast<size_t>(length) >= (sizeof(extras) - extras_len))
        {
            fits = false;
            return;
        }
        extras_len += static_cast<size_t>(length);
    };

    // Optional Home Assistant metadata is appended incrementally so each sensor
    // can stay compact while still advertising richer diagnostics where useful.
    if (icon != nullptr && icon[0] != '\0')
    {
        add(",\"icon\":\"%s\"", icon);
    }
    if (sensor.diagnostic)
    {
        add("%s", ",\"entity_category\":\"diagnostic\"");
    }
    if (controllable)
    {
        // HA sends commands to "<prefix>/<slug>/set" (#150); selects list
        // their options, a notify entity takes free text.
        char command_topic[112] = {};
        std::snprintf(command_topic, sizeof(command_topic), "%s/%s/set", g_topic_prefix,
                      entity_slug(sensor));
        add(",\"command_topic\":\"%s\"", command_topic);
        if (options != nullptr)
        {
            add(",\"options\":%s", options);
        }
    }
    else if (options != nullptr)
    {
        add(",\"device_class\":\"enum\",\"options\":%s", options);
    }
    // Numeric health sensors (#155) carry units so HA graphs them.
    switch (sensor_id)
    {
    case SensorId::Uptime:
        add("%s", ",\"device_class\":\"duration\",\"unit_of_measurement\":\"min\","
                  "\"state_class\":\"total_increasing\"");
        break;
    case SensorId::StackFree:
    case SensorId::HeapArena:
        add("%s", ",\"device_class\":\"data_size\",\"unit_of_measurement\":\"B\","
                  "\"state_class\":\"measurement\"");
        break;
    case SensorId::Screen:
        add("%s", ",\"content_type\":\"image/png\"");
        break;
    default:
        break;
    }
    if (!fits)
    {
        return false;
    }

    // The final payload is a retained self-description of the device and one
    // sensor entity. State values are published separately afterward. An
    // image entity takes its picture from "image_topic" instead.
    const char* topic_key = sensor_id == SensorId::Screen ? "image_topic" : "state_topic";
    const int kLen =
        std::snprintf(g_publish_buffer, sizeof(g_publish_buffer),
                      "{\"name\":\"%s\",\"unique_id\":\"%s\","
                      "\"%s\":\"%s\",\"availability_topic\":\"%s\","
                      "\"payload_available\":\"%s\",\"payload_not_available\":\"%s\","
                      "\"origin\":{\"name\":\"MerlinCCU\",\"sw_version\":\"%s\"},"
                      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                      "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}%s}",
                      name, g_sensor_unique_ids[sensor_index(sensor_id)].data(), topic_key,
                      g_sensor_state_topics[sensor_index(sensor_id)].data(), g_availability_topic,
                      kOnlineState, kOfflineState, kFirmwareVersion, g_device_id, g_device_name,
                      kManufacturer, kDeviceModel, kFirmwareVersion, extras);
    return kLen > 0 && static_cast<size_t>(kLen) < sizeof(g_publish_buffer);
}

/// @brief Builds the state payload for one sensor from the cached value table.
bool build_state_payload(SensorId sensor_id)
{
    copy_cstr(g_publish_buffer, sizeof(g_publish_buffer),
              g_current_sensor_values[sensor_index(sensor_id)].data());
    return g_publish_buffer[0] != '\0';
}

/// @brief Returns whether every sensor discovery message was published.
bool all_discovery_published()
{
    for (bool published : g_sensor_discovery_published)
    {
        if (!published)
        {
            return false;
        }
    }
    return true;
}

/// @brief Finds the next sensor still missing a discovery publish.
bool find_pending_discovery(SensorId* out_sensor)
{
    if (out_sensor == nullptr)
    {
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i)
    {
        if (!g_sensor_discovery_published[i])
        {
            *out_sensor = static_cast<SensorId>(i);
            return true;
        }
    }
    return false;
}

/// @brief Finds the next sensor whose state value changed since last publish.
bool find_pending_state(SensorId* out_sensor)
{
    if (out_sensor == nullptr)
    {
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i)
    {
        if (std::strcmp(g_current_sensor_values[i].data(), g_published_sensor_values[i].data()) !=
            0)
        {
            *out_sensor = static_cast<SensorId>(i);
            return true;
        }
    }
    return false;
}

/// @brief Rebuilds the cached MQTT sensor values from subsystem snapshots.
void update_sensor_values(const WifiStatus& wifi_status,
                          const HomeAssistantStatus& home_assistant_status,
                          const TimeStatus& time_status)
{
    // Rebuild every tracked sensor value from the latest subsystem snapshots so
    // later comparison against the published copies is straightforward.
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::Status)].data(),
              g_current_sensor_values[sensor_index(SensorId::Status)].size(), kOnlineState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::WifiState)].data(),
              g_current_sensor_values[sensor_index(SensorId::WifiState)].size(),
              wifi_state_text(wifi_status.state));
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::IpAddress)].data(),
              g_current_sensor_values[sensor_index(SensorId::IpAddress)].size(),
              wifi_status.ip_address[0] ? wifi_status.ip_address.data() : kUnknownState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::HomeAssistantState)].data(),
              g_current_sensor_values[sensor_index(SensorId::HomeAssistantState)].size(),
              home_assistant_state_text(home_assistant_status.state));

    // HTTP status is surfaced separately because it is often the fastest clue
    // when Home Assistant is reachable but the API flow is still misconfigured.
    char http_status_text[16] = {};
    if (home_assistant_status.last_http_status > 0)
    {
        std::snprintf(http_status_text, sizeof(http_status_text), "%d",
                      home_assistant_status.last_http_status);
    }
    else
    {
        std::snprintf(http_status_text, sizeof(http_status_text), "%s", kUnknownState);
    }
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::HomeAssistantHttp)].data(),
              g_current_sensor_values[sensor_index(SensorId::HomeAssistantHttp)].size(),
              http_status_text);

    const char* tracked_entity_state =
        home_assistant_status.tracked_entity_id[0] == '\0'
            ? kNotConfiguredState
            : (home_assistant_status.tracked_entity_state[0]
                   ? home_assistant_status.tracked_entity_state.data()
                   : kUnknownState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::TrackedEntity)].data(),
              g_current_sensor_values[sensor_index(SensorId::TrackedEntity)].size(),
              tracked_entity_state);

    const FirmwareStatus firmware = ota_manager::status();
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::FirmwareVersion)].data(),
              g_current_sensor_values[sensor_index(SensorId::FirmwareVersion)].size(),
              firmware.version.data());
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::FirmwareUpdate)].data(),
              g_current_sensor_values[sensor_index(SensorId::FirmwareUpdate)].size(),
              ota_manager::state_name());

    const config_manager::ChangeRecord change = config_manager::last_change();
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::SettingsChange)].data(),
              g_current_sensor_values[sensor_index(SensorId::SettingsChange)].size(),
              change.valid ? change.text.data() : "none since start-up");

    // State of the entities HA controls (#150), so HA shows what the panel
    // really does, including changes made at the panel itself.
    const ConsoleState& console = console_controller::state();
    const auto set_value = [](SensorId id, const char* value)
    {
        if (value != nullptr)
        {
            copy_cstr(g_current_sensor_values[sensor_index(id)].data(),
                      g_current_sensor_values[sensor_index(id)].size(), value);
        }
    };
    set_value(SensorId::HaMessageState, console_controller::ha_message_state());

    // Uptime moves once a minute; stack and heap figures are worst cases
    // since boot, so they change rarely and a rise in the heap arena over a
    // soak points at a leak (#155).
    set_value(SensorId::LastReset, reset_reason::slug(console.boot_status.reset_reason));
    char number_text[16] = {};
    std::snprintf(number_text, sizeof(number_text), "%lu",
                  static_cast<unsigned long>(console.boot_status.uptime_minutes));
    set_value(SensorId::Uptime, number_text);
    std::snprintf(number_text, sizeof(number_text), "%lu",
                  static_cast<unsigned long>(console.stack_status.free_bytes));
    // Nothing is published until a figure exists: HA refuses non-numbers here.
    set_value(SensorId::StackFree, console.stack_status.valid ? number_text : "");
    std::snprintf(number_text, sizeof(number_text), "%lu",
                  static_cast<unsigned long>(console.heap_status.arena_bytes));
    set_value(SensorId::HeapArena, console.heap_status.valid ? number_text : "");

    set_value(SensorId::AlertLamp,
              mqtt_commands::option_name(Target::AlertLamp,
                                         static_cast<uint8_t>(console.lamp_overrides[0])));
    set_value(SensorId::TestLamp,
              mqtt_commands::option_name(Target::TestLamp,
                                         static_cast<uint8_t>(console.lamp_overrides[1])));
    set_value(SensorId::PanelBrightness,
              mqtt_commands::option_name(Target::PanelBrightness,
                                         static_cast<uint8_t>(console.panel_brightness)));
    set_value(SensorId::KeyBacklight,
              mqtt_commands::option_name(Target::KeyBacklight,
                                         static_cast<uint8_t>(console.key_backlight_brightness)));
    set_value(SensorId::Display, console_controller::screen_saver_active() ? "screen_saver"
                                                                           : "awake");
    // A page HA cannot pick (a Settings subpage, say) reports the nearest
    // page above it that it can.
    MenuPage page = console.active_page;
    std::optional<uint8_t> page_value = mqtt_commands::page_option(page);
    for (int depth = 0; !page_value.has_value() && depth < 4; ++depth)
    {
        page = page_table::describe(page).parent;
        page_value = mqtt_commands::page_option(page);
    }
    set_value(SensorId::Page, mqtt_commands::option_name(Target::Page, page_value.value_or(0U)));

    // Time is not published yet, but keeping the parameter in the updater makes
    // the future sensor expansion path obvious.
    (void)time_status;
}

/// @brief Handles completion of one asynchronous MQTT publish request.
void mqtt_request_cb(void* arg, err_t err)
{
    (void)arg;

    const PendingPublishType kCompletedType = g_publish_type_in_flight;
    const SensorId kCompletedSensor = g_sensor_in_flight;
    g_publish_type_in_flight = PendingPublishType::None;
    g_request_in_flight = false;

    // Only after the broker accepts the publish do we advance the retained
    // state-machine bookkeeping for discovery/state topics.
    if (err != ERR_OK)
    {
        set_status(MqttConnectionState::Error, err);
        std::printf("MQTT publish failed err=%d\n", static_cast<int>(err));
        return;
    }

    switch (kCompletedType)
    {
    case PendingPublishType::Availability:
        g_availability_published = true;
        PERIODIC_LOG("MQTT availability published topic=%s\n", g_availability_topic);
        break;
    case PendingPublishType::Discovery:
        g_sensor_discovery_published[sensor_index(kCompletedSensor)] = true;
        g_status.discovery_published = all_discovery_published();
        PERIODIC_LOG("MQTT discovery published topic=%s\n",
                     g_sensor_discovery_topics[sensor_index(kCompletedSensor)].data());
        break;
    case PendingPublishType::State:
        copy_text(g_published_sensor_values[sensor_index(kCompletedSensor)],
                  g_current_sensor_values[sensor_index(kCompletedSensor)].data());
        PERIODIC_LOG("MQTT state published topic=%s value=%s\n",
                     g_sensor_state_topics[sensor_index(kCompletedSensor)].data(),
                     g_published_sensor_values[sensor_index(kCompletedSensor)].data());
        break;
    case PendingPublishType::KeyTrigger:
        if (g_key_trigger_removing)
        {
            g_triggers_published &= ~ha_keys::bit(g_key_in_flight);
        }
        else
        {
            g_triggers_published |= ha_keys::bit(g_key_in_flight);
        }
        break;
    case PendingPublishType::ScreenImage:
        g_screen_image_valid = true;
        g_screen_image_crc = g_screen_image_crc_in_flight;
        break;
    case PendingPublishType::UnitSummary:
        std::snprintf(g_published_summary, sizeof(g_published_summary), "%s", g_summary_in_flight);
        break;
    case PendingPublishType::UnitAckClear:
        g_own_ack_cleared = true;
        break;
    case PendingPublishType::UnitAck:
        // lwIP context, so the table is already under the lock.
        for (size_t i = 0U; i < g_fleet.count; ++i)
        {
            if (std::strcmp(g_fleet.peers[i].mac.data(), g_ack_mac_in_flight) == 0)
            {
                g_fleet.peers[i].ack_to_publish = false;
            }
        }
        break;
    case PendingPublishType::KeyEvent:
    case PendingPublishType::ControlPress:
    case PendingPublishType::None:
        break;
    }
}

/// @brief Handles the result of an MQTT broker connection attempt.
void mqtt_connection_cb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status)
{
    (void)client;
    (void)arg;

    g_connect_in_progress = false;

    if (status == MQTT_CONNECT_ACCEPTED)
    {
        g_connected = true;
        g_reconnect_backoff.reset();
        set_status(MqttConnectionState::Connected, 0);
        PERIODIC_LOG("MQTT connected to %s:%u\n", g_broker_host,
                     static_cast<unsigned>(g_broker_port));
        return;
    }

    g_connected = false;
    g_request_in_flight = false;
    g_publish_type_in_flight = PendingPublishType::None;

    if (status == MQTT_CONNECT_REFUSED_USERNAME_PASS ||
        status == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_)
    {
        set_status(MqttConnectionState::AuthFailed, static_cast<int>(status));
        std::printf("MQTT auth failed status=%d\n", static_cast<int>(status));
        return;
    }

    set_status(MqttConnectionState::Error, static_cast<int>(status));
    std::printf("MQTT connection failed status=%d\n", static_cast<int>(status));
}

/// @brief Starts a broker connection using the resolved endpoint.
bool start_mqtt_connect()
{
    if (!ensure_client())
    {
        return false;
    }

    g_connect_in_progress = true;
    set_status(MqttConnectionState::Connecting, 0);

    cyw43_arch_lwip_begin();
    const err_t kRc = mqtt_client_connect(g_client, &g_resolved_ip, g_broker_port,
                                          mqtt_connection_cb, nullptr, &g_client_info);
    cyw43_arch_lwip_end();

    if (kRc == ERR_OK)
    {
        PERIODIC_LOG("MQTT connecting host=%s port=%u\n", g_broker_host,
                     static_cast<unsigned>(g_broker_port));
        return true;
    }

    g_connect_in_progress = false;
    set_status(MqttConnectionState::Error, kRc);
    std::printf("MQTT connect start failed err=%d\n", static_cast<int>(kRc));
    return false;
}

/// @brief Completes one asynchronous DNS lookup for the MQTT broker.
void dns_found(const char* name, const ip_addr_t* ipaddr, void* arg)
{
    (void)name;
    (void)arg;

    if (!g_dns_pending)
    {
        return;
    }

    g_dns_pending = false;
    if (ipaddr == nullptr)
    {
        set_status(MqttConnectionState::Error, ERR_TIMEOUT);
        std::printf("MQTT DNS resolution failed for host=%s\n", g_broker_host);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_resolved = true;
}

/// @brief Starts a new MQTT session, resolving DNS if necessary.
bool start_session()
{
    if (!ensure_client())
    {
        return false;
    }

    g_connect_attempted = true;
    g_status.last_error = 0;

    // Accept literal IP addresses directly so local testing can bypass DNS
    // when the broker name is not yet resolvable on the target network.
    ip_addr_t parsed = {};
    if (ipaddr_aton(g_broker_host, &parsed))
    {
        g_resolved_ip = parsed;
        g_dns_resolved = false;
        return start_mqtt_connect();
    }

    cyw43_arch_lwip_begin();
    const err_t kDnsRc = dns_gethostbyname(g_broker_host, &g_resolved_ip, dns_found, nullptr);
    cyw43_arch_lwip_end();

    if (kDnsRc == ERR_OK)
    {
        return start_mqtt_connect();
    }

    if (kDnsRc == ERR_INPROGRESS)
    {
        g_dns_pending = true;
        g_dns_deadline = make_timeout_time_ms(kResolveTimeoutMs);
        set_status(MqttConnectionState::Resolving, 0);
        PERIODIC_LOG("MQTT resolving host=%s\n", g_broker_host);
        return true;
    }

    set_status(MqttConnectionState::Error, kDnsRc);
    std::printf("MQTT dns_gethostbyname failed err=%d host=%s\n", static_cast<int>(kDnsRc),
                g_broker_host);
    return false;
}

/// @brief Starts one MQTT publish of `length` bytes and records its in-flight
/// metadata. Everything but key presses is retained.
bool publish_bytes(const char* topic, const void* payload, u16_t length,
                   PendingPublishType publish_type, SensorId sensor_id)
{
    if (!g_connected || g_request_in_flight || g_client == nullptr || topic == nullptr ||
        payload == nullptr)
    {
        return false;
    }

    const u16_t kPayloadLength = length;
    // Key presses are moments, not state: a retained one would fire HA
    // automations again on every reconnect, or a light again (#152).
    const u8_t kRetain = (publish_type == PendingPublishType::KeyEvent ||
                          publish_type == PendingPublishType::ControlPress)
                             ? 0U
                             : 1U;

    cyw43_arch_lwip_begin();
    const err_t kRc = mqtt_publish(g_client, topic, payload, kPayloadLength, 0, kRetain,
                                   mqtt_request_cb, nullptr);
    cyw43_arch_lwip_end();

    if (kRc != ERR_OK)
    {
        set_status(MqttConnectionState::Error, kRc);
        std::printf("MQTT publish start failed err=%d topic=%s\n", static_cast<int>(kRc), topic);
        return false;
    }

    g_request_in_flight = true;
    g_publish_type_in_flight = publish_type;
    g_sensor_in_flight = sensor_id;
    return true;
}

/// @brief Starts one MQTT publish of a text payload.
bool publish(const char* topic, const char* payload, PendingPublishType publish_type,
             SensorId sensor_id)
{
    return payload != nullptr &&
           publish_bytes(topic, payload, static_cast<u16_t>(std::strlen(payload)), publish_type,
                         sensor_id);
}

/// @brief Publishes the panel as a PNG to the Screen image entity (#159)
/// when the picture changed and the last one is old enough.
/// @details Checked every kScreenImageIntervalMs, so the 10 KB CRC and the
/// encode run at most that often. A page too big for kScreenImageMaxBytes is
/// skipped, and HA keeps the previous picture; a run of skips is logged
/// once at its start and once at its end.
bool publish_screen_image()
{
    if (!g_sensor_discovery_published[sensor_index(SensorId::Screen)] ||
        (!is_nil_time(g_screen_image_due) && !time_reached(g_screen_image_due)))
    {
        return false;
    }
    g_screen_image_due = make_timeout_time_ms(kScreenImageIntervalMs);

    const uint8_t* frame = framebuffer::front();
    if (frame == nullptr)
    {
        return false;
    }
    const uint32_t crc = png_encoder::crc32(frame, static_cast<size_t>(kUiFbSize));
    if (g_screen_image_valid && crc == g_screen_image_crc)
    {
        return false;
    }
    const size_t length = png_encoder::encode_1bpp(
        frame, kUiWidth, kUiHeight, kUiStride, png_encoder::kPanelPaper, png_encoder::kPanelInk,
        reinterpret_cast<uint8_t*>(g_publish_buffer), sizeof(g_publish_buffer));
    if (length == 0U)
    {
        if (g_screen_images_skipped == 0U)
        {
            std::printf("MQTT screen image too large for %u bytes; skipping until one fits\n",
                        static_cast<unsigned>(sizeof(g_publish_buffer)));
        }
        ++g_screen_images_skipped;
        g_screen_image_valid = true;
        g_screen_image_crc = crc;
        return false;
    }
    if (g_screen_images_skipped > 0U)
    {
        std::printf("MQTT screen image fits again after %lu skipped\n",
                    static_cast<unsigned long>(g_screen_images_skipped));
        g_screen_images_skipped = 0U;
    }
    g_screen_image_crc_in_flight = crc;
    return publish_bytes(g_sensor_state_topics[sensor_index(SensorId::Screen)].data(),
                         g_publish_buffer, static_cast<u16_t>(length),
                         PendingPublishType::ScreenImage, SensorId::Screen);
}

/// @brief Starts the next queued availability, discovery, or state publish.
bool start_next_publish()
{
    if (!g_connected || g_request_in_flight)
    {
        return false;
    }

    // Publish ordering matters: availability first, then discovery, then live
    // state. That way Home Assistant learns about the entity before any values
    // arrive and retained state never races ahead of retained discovery.
    if (!g_availability_published)
    {
        return publish(g_availability_topic, kOnlineState, PendingPublishType::Availability,
                       SensorId::Status);
    }

    SensorId sensor_id = SensorId::Status;
    if (find_pending_discovery(&sensor_id))
    {
        if (!build_discovery_payload(sensor_id))
        {
            set_status(MqttConnectionState::Error, ERR_BUF);
            return false;
        }
        return publish(g_sensor_discovery_topics[sensor_index(sensor_id)].data(), g_publish_buffer,
                       PendingPublishType::Discovery, sensor_id);
    }

    // The other units (#210): an acceptance made here goes out first, then
    // this unit's stale acceptance is emptied, then its summary.
    char unit_topic[112] = {};
    cyw43_arch_lwip_begin();
    const std::optional<size_t> ack = unit_fleet::next_ack_to_publish(g_fleet);
    if (ack.has_value())
    {
        std::snprintf(g_ack_mac_in_flight, sizeof(g_ack_mac_in_flight), "%s",
                      g_fleet.peers[*ack].mac.data());
    }
    cyw43_arch_lwip_end();
    if (ack.has_value())
    {
        std::snprintf(unit_topic, sizeof(unit_topic), "%s/%s/offline_ack", g_base_topic,
                      g_ack_mac_in_flight);
        std::printf("MQTT unit offline accepted: %s\n", unit_topic);
        return publish(unit_topic, "1", PendingPublishType::UnitAck, SensorId::Status);
    }
    if (!g_own_ack_cleared)
    {
        std::snprintf(unit_topic, sizeof(unit_topic), "%s/offline_ack", g_topic_prefix);
        return publish(unit_topic, "", PendingPublishType::UnitAckClear, SensorId::Status);
    }
    if (g_own_summary[0] != '\0' && std::strcmp(g_own_summary, g_published_summary) != 0)
    {
        std::snprintf(g_summary_in_flight, sizeof(g_summary_in_flight), "%s", g_own_summary);
        std::snprintf(unit_topic, sizeof(unit_topic), "%s/summary", g_topic_prefix);
        return publish(unit_topic, g_summary_in_flight, PendingPublishType::UnitSummary,
                       SensorId::Status);
    }

    // Key presses are time-critical (#151), so they go ahead of trigger
    // configs and state updates.
    if (const std::optional<ButtonId> key = console_controller::pop_ha_key_press())
    {
        char slug[24] = {};
        ha_keys::slug(input::button_name(*key), slug, sizeof(slug));
        char topic[112] = {};
        std::snprintf(topic, sizeof(topic), "%s/key/%s", g_topic_prefix, slug);
        g_key_in_flight = *key;
        std::printf("MQTT key press: %s\n", topic);
        return publish(topic, "press", PendingPublishType::KeyEvent, SensorId::Status);
    }

    // A Controls key (#152) is someone waiting at the panel, so it goes next.
    // A press that cannot be sent times out on the page as no reply.
    char press[24] = {};
    if (console_controller::take_home_control_press(press, sizeof(press)))
    {
        char topic[112] = {};
        std::snprintf(topic, sizeof(topic), "%s/controls/press", g_topic_prefix);
        std::printf("MQTT control press: %s\n", press);
        return publish(topic, press, PendingPublishType::ControlPress, SensorId::Status);
    }

    const uint64_t wanted = config_manager::settings().ha_key_mask;
    const uint64_t changed = wanted ^ g_triggers_published;
    if (changed != 0U)
    {
        auto key = ButtonId::LeftTop;
        for (size_t i = 0U; i < static_cast<size_t>(ButtonId::Count); ++i)
        {
            if ((changed & ha_keys::bit(static_cast<ButtonId>(i))) != 0U)
            {
                key = static_cast<ButtonId>(i);
                break;
            }
        }
        const char* legend = input::button_name(key);
        char slug[24] = {};
        ha_keys::slug(legend, slug, sizeof(slug));
        char topic[160] = {};
        std::snprintf(topic, sizeof(topic), "%s/device_automation/%s/key_%s/config",
                      g_discovery_prefix, g_device_id, slug);
        g_key_in_flight = key;
        g_key_trigger_removing = !ha_keys::contains(wanted, key);
        if (g_key_trigger_removing)
        {
            // An empty retained config removes the trigger from HA.
            g_publish_buffer[0] = '\0';
        }
        else
        {
            std::snprintf(g_publish_buffer, sizeof(g_publish_buffer),
                          "{\"automation_type\":\"trigger\",\"topic\":\"%s/key/%s\","
                          "\"type\":\"button_short_press\",\"subtype\":\"%s\","
                          "\"payload\":\"press\",\"device\":{\"identifiers\":[\"%s\"]}}",
                          g_topic_prefix, slug, legend, g_device_id);
        }
        return publish(topic, g_publish_buffer, PendingPublishType::KeyTrigger, SensorId::Status);
    }

    if (find_pending_state(&sensor_id))
    {
        if (!build_state_payload(sensor_id))
        {
            set_status(MqttConnectionState::Error, ERR_BUF);
            return false;
        }
        return publish(g_sensor_state_topics[sensor_index(sensor_id)].data(), g_publish_buffer,
                       PendingPublishType::State, sensor_id);
    }

    // The picture goes last: it is the largest message and the least urgent.
    return publish_screen_image();
}

/// @brief Brings the other units' table up to date (main loop, #210).
/// @details Applies an acceptance of UNIT OFFLINE made on this panel, works
/// out the alerts for the time now, keeps a copy for the console, and
/// rebuilds the summary this unit publishes.
void update_unit_fleet()
{
    const ConsoleState& console = console_controller::state();
    const char* own_version = console.firmware_status.version.data();
    const bool accepted = alert_controller::acknowledged(AlertCode::UnitOffline);
    cyw43_arch_lwip_begin();
    if (accepted && unit_fleet::acknowledge_offline(g_fleet))
    {
        std::printf("Units: UNIT OFFLINE accepted here\n");
    }
    (void)unit_fleet::update(g_fleet, to_ms_since_boot(get_absolute_time()), own_version);
    g_fleet_snapshot = g_fleet;
    cyw43_arch_lwip_end();

    const ActiveAlert* top = nullptr;
    const uint8_t alert_count = alert_controller::own_alerts(console, &top);
    unit_fleet::OwnSummary summary = {};
    summary.name = console.wifi_status.host_name[0] != '\0' ? console.wifi_status.host_name.data()
                                                            : config_manager::device_name();
    summary.version = own_version;
    summary.board = kFirmwareBoard;
    summary.alert_count = alert_count;
    summary.top_alert = top != nullptr ? top->summary.data() : "";
    if (!unit_fleet::format_summary(summary, g_own_summary, sizeof(g_own_summary)))
    {
        g_own_summary[0] = '\0';
    }
}

} // namespace

namespace mqtt_manager
{

/// @brief Initializes MQTT configuration and clears runtime state.
void init()
{
    // Snapshot the config once up front so runtime behaviour is driven by a
    // simple "configured or not" flag instead of repeatedly reparsing strings.
    g_config_valid = parse_mqtt_endpoint();
    g_status = {};
    const RuntimeConfig& config = config_manager::settings();
    const bool runtime_configured = config.mqtt_enabled && config.mqtt_host[0] != '\0';
    g_status.configured = (runtime_configured || kMqttConfigured) && g_config_valid;
    copy_text(g_status.broker, g_broker_host);
    g_status.state = !g_status.configured
                         ? MqttConnectionState::Unconfigured
                         : (kMqttRuntimeEnabled ? MqttConnectionState::WaitingForWifi
                                                : MqttConnectionState::Disabled);
    g_status.last_error = 0;
    g_status.discovery_published = false;
    g_status.device_id.fill('\0');
    clear_sensor_tracking();
    reset_runtime(false);
}

/// @brief Advances the MQTT connection and publish state machine.
bool update(const WifiStatus& wifi_status, const HomeAssistantStatus& home_assistant_status,
            const TimeStatus& time_status)
{
    const MqttStatus kPrevious = g_status;

    update_unit_fleet();

    // Controls keys act only while a press can reach HA (#152).
    (void)console_controller::update_home_controls(g_connected && g_controls_subscribed,
                                                   to_ms_since_boot(get_absolute_time()));

    // A key press HA cannot hear now is dropped, not sent late (#151).
    if (!g_connected)
    {
        while (console_controller::pop_ha_key_press().has_value())
        {
        }
    }

    // Configuration and runtime-disable paths are handled first so the rest of
    // the function can assume MQTT is actually supposed to run.
    if (!g_status.configured)
    {
        g_status.state = MqttConnectionState::Unconfigured;
        return kPrevious != g_status;
    }

    if (!kMqttRuntimeEnabled)
    {
        g_status.state = MqttConnectionState::Disabled;
        g_status.last_error = 0;
        reset_runtime(true);
        return kPrevious != g_status;
    }

    const bool kWifiReady = wifi_status.ip_address[0] != '\0';
    if (!kWifiReady)
    {
        // A new network link gets a prompt first attempt.
        g_reconnect_backoff.reset();
        reset_runtime(true);
        g_status.state = MqttConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        return kPrevious != g_status;
    }

    configure_identity_and_topics(wifi_status);
    update_sensor_values(wifi_status, home_assistant_status, time_status);

    // DNS resolution is allowed to complete asynchronously, but it still has a
    // deadline so the UI can escape a stuck resolver state.
    if (g_dns_pending && !is_nil_time(g_dns_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_dns_deadline) <= 0)
    {
        g_dns_pending = false;
        set_status(MqttConnectionState::Error, ERR_TIMEOUT);
    }

    // A refused, failed or dropped session (a broker or HA restart) is torn
    // down and started again after a growing delay. Nothing else retries it
    // while Wi-Fi stays up (#155). The full reset republishes availability,
    // discovery and every state, since a restarted broker may have lost them.
    const bool kSessionDown = g_connect_attempted && !g_connected && !g_connect_in_progress &&
                              !g_dns_pending && !g_dns_resolved;
    if (kSessionDown)
    {
        if (is_nil_time(g_next_session_at))
        {
            const uint32_t kDelayMs = g_reconnect_backoff.next_delay_ms(get_rand_32());
            g_next_session_at = make_timeout_time_ms(kDelayMs);
            std::printf("MQTT reconnecting in %lus (failure %lu)\n",
                        static_cast<unsigned long>(kDelayMs / 1000U),
                        static_cast<unsigned long>(g_reconnect_backoff.failures()));
        }
        else if (time_reached(g_next_session_at))
        {
            reset_runtime(true);
        }
    }

    // The connect flow is split across multiple updates: begin session, react
    // to DNS completion, then hand over to the broker callbacks.
    if (!g_connect_attempted)
    {
        start_session();
    }
    else if (g_dns_resolved && !g_connect_in_progress && !g_connected)
    {
        g_dns_resolved = false;
        start_mqtt_connect();
    }

    // Once connected, the publisher drains one pending retained message at a
    // time so the callback path can track exactly what completed.
    if (g_connected)
    {
        subscribe_commands();
        apply_pending_command();
        apply_controls_payloads();
    }
    if (g_connected && !g_request_in_flight)
    {
        start_next_publish();
    }

    return kPrevious != g_status;
}

const MqttStatus& status()
{
    return g_status;
}

const UnitFleet& unit_fleet()
{
    return g_fleet_snapshot;
}

void take_house_weather(HouseWeatherHandler handler)
{
    for (size_t i = 0; i < g_house_buffers.size(); ++i)
    {
        InboundBuffer& buffer = g_house_buffers[i];
        cyw43_arch_lwip_begin();
        const bool ready = buffer.ready;
        cyw43_arch_lwip_end();
        if (!ready)
        {
            continue;
        }
        // The callbacks leave a ready buffer alone, so it is read unlocked.
        handler(static_cast<house_weather::Topic>(i), buffer.data);
        cyw43_arch_lwip_begin();
        buffer.ready = false;
        cyw43_arch_lwip_end();
    }
}

} // namespace mqtt_manager
