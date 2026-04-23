#include "mqtt_manager.h"

#include <cstdio>
#include <cstring>
#include <limits>

#include "config_manager.h"
#include "debug_logging.h"
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

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
constexpr unsigned long kMaxTcpPortValue = std::numeric_limits<uint16_t>::max();
constexpr char kMqttSchemePrefix[] = "mqtt://";
constexpr size_t kMqttSchemePrefixLength = sizeof(kMqttSchemePrefix) - 1;
constexpr char kTcpSchemePrefix[] = "tcp://";
constexpr size_t kTcpSchemePrefixLength = sizeof(kTcpSchemePrefix) - 1;
constexpr size_t kMacAddressHexDigitCount = 12;
constexpr size_t kMacAddressHexTextCapacity = kMacAddressHexDigitCount + 1;
constexpr size_t kDeviceNameSuffixLength = 6;
constexpr uint16_t kMqttKeepAliveSeconds = 60;
constexpr char kUnknownMacText[] = "unknown";
constexpr char kOnlineState[] = "online";
constexpr char kOfflineState[] = "offline";
constexpr char kUnknownState[] = "unknown";
constexpr char kNotConfiguredState[] = "not_configured";
constexpr char kFirmwareVersion[] = "0.1";
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
    Count,
};

enum class PendingPublishType : uint8_t
{
    None = 0,
    Availability,
    Discovery,
    State,
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
};

/// @brief Static discovery metadata for every MQTT-backed sensor exposed by the firmware.
/// @details This table is kept in enum order so a `SensorId` can directly index into the
/// matching Home Assistant discovery settings.
constexpr SensorDescriptor kSensorDescriptors[] = {
    {"Status", "status", "mdi:radio-tower", false, nullptr},
    {"Wi-Fi state", "wifi_state", "mdi:wifi", true,
     "[\"disabled\",\"unconfigured\",\"initializing\",\"scanning\",\"connecting\",\"waiting_for_"
     "ip\","
     "\"connected\",\"auth_failed\",\"no_network\",\"connect_failed\",\"error\"]"},
    {"IP address", "ip_address", "mdi:ip-network", true, nullptr},
    {"Home Assistant", "home_assistant_state", "mdi:home-assistant", true,
     "[\"disabled\",\"unconfigured\",\"waiting_for_wifi\",\"resolving\",\"connecting\","
     "\"authorizing\","
     "\"connected\",\"unauthorized\",\"error\"]"},
    {"Home Assistant HTTP", "home_assistant_http", "mdi:web", true, nullptr},
    {"Tracked entity", "tracked_entity", "mdi:toggle-switch-outline", false, nullptr},
};

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

template <size_t N>
/// @brief Copies text into a fixed-size MQTT status buffer.
void copy_text(std::array<char, N>& dst, const char* src)
{
    dst.fill('\0');
    if (src == nullptr)
    {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

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

/// @brief Parses a decimal TCP port string.
bool parse_port(const char* text, uint16_t* out_port)
{
    if (text == nullptr || *text == '\0' || out_port == nullptr)
    {
        return false;
    }

    unsigned long value = 0;
    for (const char* p = text; *p != '\0'; ++p)
    {
        if (*p < '0' || *p > '9')
        {
            return false;
        }
        value = (value * 10U) + static_cast<unsigned long>(*p - '0');
        if (value > kMaxTcpPortValue)
        {
            return false;
        }
    }

    if (value == 0U)
    {
        return false;
    }

    *out_port = static_cast<uint16_t>(value);
    return true;
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
mqtt_client_t* g_client = nullptr;
PendingPublishType g_publish_type_in_flight = PendingPublishType::None;
SensorId g_sensor_in_flight = SensorId::Status;
char g_broker_host[48] = {};
uint16_t g_broker_port = kMqttPort;
bool g_config_valid = false;
char g_client_id[48] = {};
char g_device_id[32] = {};
char g_device_name[32] = {};
char g_availability_topic[96] = {};
char g_publish_buffer[1024] = {};
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
    g_request_in_flight = false;
    g_publish_type_in_flight = PendingPublishType::None;
    g_sensor_in_flight = SensorId::Status;
    g_availability_published = false;

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
    // only has to deal with connect/publish behavior, not string cleanup.
    if (mqtt_host[0] == '\0')
    {
        return false;
    }

    const char* host_start = mqtt_host;
    if (std::strncmp(host_start, kMqttSchemePrefix, kMqttSchemePrefixLength) == 0)
    {
        host_start += kMqttSchemePrefixLength;
    }
    else if (std::strncmp(host_start, kTcpSchemePrefix, kTcpSchemePrefixLength) == 0)
    {
        host_start += kTcpSchemePrefixLength;
    }

    const char* host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/')
    {
        ++host_end;
    }

    const size_t kHostLen = static_cast<size_t>(host_end - host_start);
    if (kHostLen == 0 || kHostLen >= sizeof(g_broker_host))
    {
        std::printf("MQTT config host is empty or too long\n");
        return false;
    }

    std::memcpy(g_broker_host, host_start, kHostLen);
    g_broker_host[kHostLen] = '\0';

    if (*host_end == ':')
    {
        const char* port_start = host_end + 1;
        const char* port_end = port_start;
        while (*port_end != '\0' && *port_end != '/')
        {
            ++port_end;
        }

        char port_text[8] = {};
        const size_t kPortLen = static_cast<size_t>(port_end - port_start);
        if (kPortLen == 0 || kPortLen >= sizeof(port_text))
        {
            std::printf("MQTT config port is invalid\n");
            return false;
        }

        std::memcpy(port_text, port_start, kPortLen);
        port_text[kPortLen] = '\0';
        if (!parse_port(port_text, &g_broker_port))
        {
            std::printf("MQTT config port is invalid: %s\n", port_text);
            return false;
        }
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
    const char* name_suffix = (std::strlen(mac_hex) >= kDeviceNameSuffixLength)
                                  ? (mac_hex + std::strlen(mac_hex) - kDeviceNameSuffixLength)
                                  : mac_hex;

    std::snprintf(g_device_id, sizeof(g_device_id), "merlinccu_%s", mac_hex);
    std::snprintf(g_client_id, sizeof(g_client_id), "merlinccu-%s", mac_hex);
    if (config.device_name[0] != '\0')
    {
        std::snprintf(g_device_name, sizeof(g_device_name), "%s", config.device_name.data());
    }
    else
    {
        std::snprintf(g_device_name, sizeof(g_device_name), "MerlinCCU %s", name_suffix);
    }
    std::snprintf(g_availability_topic, sizeof(g_availability_topic), "%s/%s/availability",
                  base_topic, mac_hex);

    // Topic strings are precomputed once per session so the publish path can
    // focus on ordering and retry behavior instead of repeated formatting.
    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i)
    {
        const SensorDescriptor& sensor = kSensorDescriptors[i];
        std::snprintf(g_sensor_unique_ids[i].data(), g_sensor_unique_ids[i].size(), "%s_%s",
                      g_device_id, sensor.slug);
        std::snprintf(g_sensor_state_topics[i].data(), g_sensor_state_topics[i].size(),
                      "%s/%s/%s/state", base_topic, mac_hex, sensor.slug);
        std::snprintf(g_sensor_discovery_topics[i].data(), g_sensor_discovery_topics[i].size(),
                      "%s/sensor/%s/%s/config", discovery_prefix, g_device_id, sensor.slug);
    }

    copy_text(g_status.device_id, g_device_id);
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
        mqtt_set_inpub_callback(g_client, nullptr, nullptr, nullptr);
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
    char extras[512] = {};
    size_t extras_len = 0;

    // Optional Home Assistant metadata is appended incrementally so each sensor
    // can stay compact while still advertising richer diagnostics where useful.
    if (sensor.icon != nullptr && sensor.icon[0] != '\0')
    {
        const int kLen = std::snprintf(extras + extras_len, sizeof(extras) - extras_len,
                                       ",\"icon\":\"%s\"", sensor.icon);
        if (kLen <= 0 || static_cast<size_t>(kLen) >= (sizeof(extras) - extras_len))
        {
            return false;
        }
        extras_len += static_cast<size_t>(kLen);
    }

    if (sensor.diagnostic)
    {
        const int kLen = std::snprintf(extras + extras_len, sizeof(extras) - extras_len,
                                       ",\"entity_category\":\"diagnostic\"");
        if (kLen <= 0 || static_cast<size_t>(kLen) >= (sizeof(extras) - extras_len))
        {
            return false;
        }
        extras_len += static_cast<size_t>(kLen);
    }

    if (sensor.options_json != nullptr)
    {
        const int kLen =
            std::snprintf(extras + extras_len, sizeof(extras) - extras_len,
                          ",\"device_class\":\"enum\",\"options\":%s", sensor.options_json);
        if (kLen <= 0 || static_cast<size_t>(kLen) >= (sizeof(extras) - extras_len))
        {
            return false;
        }
        extras_len += static_cast<size_t>(kLen);
    }

    // The final payload is a retained self-description of the device and one
    // sensor entity. State values are published separately afterward.
    const int kLen =
        std::snprintf(g_publish_buffer, sizeof(g_publish_buffer),
                      "{\"name\":\"%s\",\"unique_id\":\"%s\","
                      "\"state_topic\":\"%s\",\"availability_topic\":\"%s\","
                      "\"payload_available\":\"%s\",\"payload_not_available\":\"%s\","
                      "\"origin\":{\"name\":\"MerlinCCU\",\"sw_version\":\"%s\"},"
                      "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                      "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}%s}",
                      sensor.name, g_sensor_unique_ids[sensor_index(sensor_id)].data(),
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

/// @brief Starts one retained MQTT publish and records its in-flight metadata.
bool publish(const char* topic, const char* payload, PendingPublishType publish_type,
             SensorId sensor_id)
{
    if (!g_connected || g_request_in_flight || g_client == nullptr || topic == nullptr ||
        payload == nullptr)
    {
        return false;
    }

    const u16_t kPayloadLength = static_cast<u16_t>(std::strlen(payload));
    const u8_t kRetain = 1;

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

    return false;
}

} // namespace

namespace mqtt_manager
{

/// @brief Initializes MQTT configuration and clears runtime state.
void init()
{
    // Snapshot the config once up front so runtime behavior is driven by a
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

    // Configuration and runtime-disable paths are handled first so the rest of
    // the function can assume MQTT is actually supposed to run.
    if (!g_status.configured)
    {
        g_status.state = MqttConnectionState::Unconfigured;
        return std::memcmp(&kPrevious, &g_status, sizeof(g_status)) != 0;
    }

    if (!kMqttRuntimeEnabled)
    {
        g_status.state = MqttConnectionState::Disabled;
        g_status.last_error = 0;
        reset_runtime(true);
        return std::memcmp(&kPrevious, &g_status, sizeof(g_status)) != 0;
    }

    const bool kWifiReady = wifi_status.ip_address[0] != '\0';
    if (!kWifiReady)
    {
        reset_runtime(true);
        g_status.state = MqttConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        return std::memcmp(&kPrevious, &g_status, sizeof(g_status)) != 0;
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
    if (g_connected && !g_request_in_flight)
    {
        start_next_publish();
    }

    return std::memcmp(&kPrevious, &g_status, sizeof(g_status)) != 0;
}

const MqttStatus& status()
{
    return g_status;
}

} // namespace mqtt_manager
