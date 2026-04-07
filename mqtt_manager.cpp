#include "mqtt_manager.h"

#include <cstdio>
#include <cstring>

#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace {

#if __has_include("mqtt_credentials.h")
#include "mqtt_credentials.h"
constexpr bool kMqttConfigured = true;
#else
constexpr bool kMqttConfigured = false;
inline constexpr char HOME_ASSISTANT_MQTT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_MQTT_PORT = 1883;
inline constexpr char HOME_ASSISTANT_MQTT_USERNAME[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_PASSWORD[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_DISCOVERY_PREFIX[] = "homeassistant";
inline constexpr char HOME_ASSISTANT_MQTT_BASE_TOPIC[] = "merlinccu";
#endif

constexpr bool kMqttRuntimeEnabled = true;
constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr char kOnlineState[] = "online";
constexpr char kOfflineState[] = "offline";
constexpr char kUnknownState[] = "unknown";
constexpr char kNotConfiguredState[] = "not_configured";
constexpr char kFirmwareVersion[] = "0.1";
constexpr char kDeviceModel[] = "Raspberry Pi Pico 2 W";
constexpr char kManufacturer[] = "MerlinCCU";

enum class SensorId : uint8_t {
    Status = 0,
    WifiState,
    IpAddress,
    HomeAssistantState,
    HomeAssistantHttp,
    TrackedEntity,
    Count,
};

enum class PendingPublishType : uint8_t {
    None = 0,
    Availability,
    Discovery,
    State,
};

struct SensorDescriptor {
    const char* name;
    const char* slug;
    const char* icon;
    bool diagnostic;
    const char* options_json;
};

constexpr SensorDescriptor kSensorDescriptors[] = {
    {"Status", "status", "mdi:radio-tower", false, nullptr},
    {"Wi-Fi state",
     "wifi_state",
     "mdi:wifi",
     true,
     "[\"disabled\",\"unconfigured\",\"initializing\",\"scanning\",\"connecting\",\"waiting_for_ip\","
     "\"connected\",\"auth_failed\",\"no_network\",\"connect_failed\",\"error\"]"},
    {"IP address", "ip_address", "mdi:ip-network", true, nullptr},
    {"Home Assistant",
     "home_assistant_state",
     "mdi:home-assistant",
     true,
     "[\"disabled\",\"unconfigured\",\"waiting_for_wifi\",\"resolving\",\"connecting\",\"authorizing\","
     "\"connected\",\"unauthorized\",\"error\"]"},
    {"Home Assistant HTTP", "home_assistant_http", "mdi:web", true, nullptr},
    {"Tracked entity", "tracked_entity", "mdi:toggle-switch-outline", false, nullptr},
};

static_assert((sizeof(kSensorDescriptors) / sizeof(kSensorDescriptors[0])) == static_cast<size_t>(SensorId::Count),
              "MQTT sensor descriptors must match SensorId");

constexpr size_t sensor_index(SensorId id)
{
    return static_cast<size_t>(id);
}

template <size_t N>
void copy_text(std::array<char, N>& dst, const char* src)
{
    dst.fill('\0');
    if (src == nullptr) {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

void copy_cstr(char* dst, size_t dst_size, const char* src)
{
    if (dst == nullptr || dst_size == 0) {
        return;
    }

    dst[0] = '\0';
    if (src == nullptr) {
        return;
    }

    std::snprintf(dst, dst_size, "%s", src);
}

bool parse_port(const char* text, uint16_t* out_port)
{
    if (text == nullptr || *text == '\0' || out_port == nullptr) {
        return false;
    }

    unsigned long value = 0;
    for (const char* p = text; *p != '\0'; ++p) {
        if (*p < '0' || *p > '9') {
            return false;
        }
        value = (value * 10u) + static_cast<unsigned long>(*p - '0');
        if (value > 65535u) {
            return false;
        }
    }

    if (value == 0u) {
        return false;
    }

    *out_port = static_cast<uint16_t>(value);
    return true;
}

bool is_hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char to_lower_hex(char c)
{
    if (c >= 'A' && c <= 'F') {
        return static_cast<char>(c - 'A' + 'a');
    }
    return c;
}

const char* wifi_state_text(WifiConnectionState state)
{
    switch (state) {
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

const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state) {
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
uint16_t g_broker_port = HOME_ASSISTANT_MQTT_PORT;
bool g_config_valid = false;
char g_client_id[48] = {};
char g_device_id[32] = {};
char g_device_name[32] = {};
char g_availability_topic[96] = {};
char g_publish_buffer[1024] = {};
mqtt_connect_client_info_t g_client_info = {};
std::array<std::array<char, 64>, static_cast<size_t>(SensorId::Count)> g_sensor_unique_ids = {};
std::array<std::array<char, 96>, static_cast<size_t>(SensorId::Count)> g_sensor_state_topics = {};
std::array<std::array<char, 128>, static_cast<size_t>(SensorId::Count)> g_sensor_discovery_topics = {};
std::array<std::array<char, 32>, static_cast<size_t>(SensorId::Count)> g_current_sensor_values = {};
std::array<std::array<char, 32>, static_cast<size_t>(SensorId::Count)> g_published_sensor_values = {};
std::array<bool, static_cast<size_t>(SensorId::Count)> g_sensor_discovery_published = {};

void set_status(MqttConnectionState state, int last_error)
{
    g_status.state = state;
    g_status.last_error = last_error;
}

void clear_sensor_tracking()
{
    for (auto& unique_id : g_sensor_unique_ids) {
        unique_id.fill('\0');
    }
    for (auto& topic : g_sensor_state_topics) {
        topic.fill('\0');
    }
    for (auto& topic : g_sensor_discovery_topics) {
        topic.fill('\0');
    }
    for (auto& value : g_current_sensor_values) {
        value.fill('\0');
    }
    for (auto& value : g_published_sensor_values) {
        value.fill('\0');
    }
    g_sensor_discovery_published.fill(false);
    g_status.discovery_published = false;
}

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

    if (g_client != nullptr) {
        cyw43_arch_lwip_begin();
        if (mqtt_client_is_connected(g_client)) {
            mqtt_disconnect(g_client);
        }
        mqtt_client_free(g_client);
        cyw43_arch_lwip_end();
        g_client = nullptr;
    }

    if (reset_publish_state) {
        clear_sensor_tracking();
    } else {
        for (auto& value : g_published_sensor_values) {
            value.fill('\0');
        }
    }
}

bool parse_mqtt_endpoint()
{
    g_broker_host[0] = '\0';
    g_broker_port = HOME_ASSISTANT_MQTT_PORT;

    if (HOME_ASSISTANT_MQTT_HOST[0] == '\0') {
        return false;
    }

    const char* host_start = HOME_ASSISTANT_MQTT_HOST;
    if (std::strncmp(host_start, "mqtt://", 7) == 0) {
        host_start += 7;
    } else if (std::strncmp(host_start, "tcp://", 6) == 0) {
        host_start += 6;
    }

    const char* host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/') {
        ++host_end;
    }

    const size_t host_len = static_cast<size_t>(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(g_broker_host)) {
        std::printf("MQTT config host is empty or too long\n");
        return false;
    }

    std::memcpy(g_broker_host, host_start, host_len);
    g_broker_host[host_len] = '\0';

    if (*host_end == ':') {
        const char* port_start = host_end + 1;
        const char* port_end = port_start;
        while (*port_end != '\0' && *port_end != '/') {
            ++port_end;
        }

        char port_text[8] = {};
        const size_t port_len = static_cast<size_t>(port_end - port_start);
        if (port_len == 0 || port_len >= sizeof(port_text)) {
            std::printf("MQTT config port is invalid\n");
            return false;
        }

        std::memcpy(port_text, port_start, port_len);
        port_text[port_len] = '\0';
        if (!parse_port(port_text, &g_broker_port)) {
            std::printf("MQTT config port is invalid: %s\n", port_text);
            return false;
        }
    }

    return true;
}

void configure_identity_and_topics(const WifiStatus& wifi_status)
{
    char mac_hex[13] = {};
    size_t hex_len = 0;
    for (char c : wifi_status.mac_address) {
        if (c == '\0') {
            break;
        }
        if (is_hex_digit(c) && hex_len + 1 < sizeof(mac_hex)) {
            mac_hex[hex_len++] = to_lower_hex(c);
        }
    }
    mac_hex[hex_len] = '\0';

    if (hex_len != 12) {
        std::snprintf(mac_hex, sizeof(mac_hex), "unknown");
    }

    const char* base_topic = HOME_ASSISTANT_MQTT_BASE_TOPIC[0] ? HOME_ASSISTANT_MQTT_BASE_TOPIC : "merlinccu";
    const char* discovery_prefix =
        HOME_ASSISTANT_MQTT_DISCOVERY_PREFIX[0] ? HOME_ASSISTANT_MQTT_DISCOVERY_PREFIX : "homeassistant";
    const char* name_suffix = (std::strlen(mac_hex) >= 6) ? (mac_hex + std::strlen(mac_hex) - 6) : mac_hex;

    std::snprintf(g_device_id, sizeof(g_device_id), "merlinccu_%s", mac_hex);
    std::snprintf(g_client_id, sizeof(g_client_id), "merlinccu-%s", mac_hex);
    std::snprintf(g_device_name, sizeof(g_device_name), "MerlinCCU %s", name_suffix);
    std::snprintf(g_availability_topic, sizeof(g_availability_topic), "%s/%s/availability", base_topic, mac_hex);

    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i) {
        const SensorDescriptor& sensor = kSensorDescriptors[i];
        std::snprintf(g_sensor_unique_ids[i].data(), g_sensor_unique_ids[i].size(), "%s_%s", g_device_id, sensor.slug);
        std::snprintf(
            g_sensor_state_topics[i].data(), g_sensor_state_topics[i].size(), "%s/%s/%s/state", base_topic, mac_hex, sensor.slug);
        std::snprintf(g_sensor_discovery_topics[i].data(),
                      g_sensor_discovery_topics[i].size(),
                      "%s/sensor/%s/%s/config",
                      discovery_prefix,
                      g_device_id,
                      sensor.slug);
    }

    copy_text(g_status.device_id, g_device_id);
}

bool ensure_client()
{
    if (g_client != nullptr) {
        return true;
    }

    cyw43_arch_lwip_begin();
    g_client = mqtt_client_new();
    if (g_client != nullptr) {
        mqtt_set_inpub_callback(g_client, nullptr, nullptr, nullptr);
    }
    cyw43_arch_lwip_end();

    if (g_client == nullptr) {
        set_status(MqttConnectionState::Error, ERR_MEM);
        return false;
    }

    std::memset(&g_client_info, 0, sizeof(g_client_info));
    g_client_info.client_id = g_client_id;
    g_client_info.client_user = HOME_ASSISTANT_MQTT_USERNAME[0] ? HOME_ASSISTANT_MQTT_USERNAME : nullptr;
    g_client_info.client_pass = HOME_ASSISTANT_MQTT_PASSWORD[0] ? HOME_ASSISTANT_MQTT_PASSWORD : nullptr;
    g_client_info.keep_alive = 60;
    g_client_info.will_topic = g_availability_topic;
    g_client_info.will_msg = kOfflineState;
    g_client_info.will_msg_len = 0;
    g_client_info.will_qos = 0;
    g_client_info.will_retain = 1;
    return true;
}

bool build_discovery_payload(SensorId sensor_id)
{
    const SensorDescriptor& sensor = kSensorDescriptors[sensor_index(sensor_id)];
    char extras[512] = {};
    size_t extras_len = 0;

    if (sensor.icon != nullptr && sensor.icon[0] != '\0') {
        const int len = std::snprintf(extras + extras_len, sizeof(extras) - extras_len, ",\"icon\":\"%s\"", sensor.icon);
        if (len <= 0 || static_cast<size_t>(len) >= (sizeof(extras) - extras_len)) {
            return false;
        }
        extras_len += static_cast<size_t>(len);
    }

    if (sensor.diagnostic) {
        const int len = std::snprintf(extras + extras_len, sizeof(extras) - extras_len, ",\"entity_category\":\"diagnostic\"");
        if (len <= 0 || static_cast<size_t>(len) >= (sizeof(extras) - extras_len)) {
            return false;
        }
        extras_len += static_cast<size_t>(len);
    }

    if (sensor.options_json != nullptr) {
        const int len = std::snprintf(extras + extras_len,
                                      sizeof(extras) - extras_len,
                                      ",\"device_class\":\"enum\",\"options\":%s",
                                      sensor.options_json);
        if (len <= 0 || static_cast<size_t>(len) >= (sizeof(extras) - extras_len)) {
            return false;
        }
        extras_len += static_cast<size_t>(len);
    }

    const int len = std::snprintf(g_publish_buffer,
                                  sizeof(g_publish_buffer),
                                  "{\"name\":\"%s\",\"unique_id\":\"%s\","
                                  "\"state_topic\":\"%s\",\"availability_topic\":\"%s\","
                                  "\"payload_available\":\"%s\",\"payload_not_available\":\"%s\","
                                  "\"origin\":{\"name\":\"MerlinCCU\",\"sw_version\":\"%s\"},"
                                  "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"%s\","
                                  "\"manufacturer\":\"%s\",\"model\":\"%s\",\"sw_version\":\"%s\"}%s}",
                                  sensor.name,
                                  g_sensor_unique_ids[sensor_index(sensor_id)].data(),
                                  g_sensor_state_topics[sensor_index(sensor_id)].data(),
                                  g_availability_topic,
                                  kOnlineState,
                                  kOfflineState,
                                  kFirmwareVersion,
                                  g_device_id,
                                  g_device_name,
                                  kManufacturer,
                                  kDeviceModel,
                                  kFirmwareVersion,
                                  extras);
    return len > 0 && static_cast<size_t>(len) < sizeof(g_publish_buffer);
}

bool build_state_payload(SensorId sensor_id)
{
    copy_cstr(g_publish_buffer, sizeof(g_publish_buffer), g_current_sensor_values[sensor_index(sensor_id)].data());
    return g_publish_buffer[0] != '\0';
}

bool all_discovery_published()
{
    for (bool published : g_sensor_discovery_published) {
        if (!published) {
            return false;
        }
    }
    return true;
}

bool find_pending_discovery(SensorId* out_sensor)
{
    if (out_sensor == nullptr) {
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i) {
        if (!g_sensor_discovery_published[i]) {
            *out_sensor = static_cast<SensorId>(i);
            return true;
        }
    }
    return false;
}

bool find_pending_state(SensorId* out_sensor)
{
    if (out_sensor == nullptr) {
        return false;
    }

    for (size_t i = 0; i < static_cast<size_t>(SensorId::Count); ++i) {
        if (std::strcmp(g_current_sensor_values[i].data(), g_published_sensor_values[i].data()) != 0) {
            *out_sensor = static_cast<SensorId>(i);
            return true;
        }
    }
    return false;
}

void update_sensor_values(const WifiStatus& wifi_status,
                          const HomeAssistantStatus& home_assistant_status,
                          const TimeStatus& time_status)
{
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::Status)].data(),
              g_current_sensor_values[sensor_index(SensorId::Status)].size(),
              kOnlineState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::WifiState)].data(),
              g_current_sensor_values[sensor_index(SensorId::WifiState)].size(),
              wifi_state_text(wifi_status.state));
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::IpAddress)].data(),
              g_current_sensor_values[sensor_index(SensorId::IpAddress)].size(),
              wifi_status.ip_address[0] ? wifi_status.ip_address.data() : kUnknownState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::HomeAssistantState)].data(),
              g_current_sensor_values[sensor_index(SensorId::HomeAssistantState)].size(),
              home_assistant_state_text(home_assistant_status.state));

    char http_status_text[16] = {};
    if (home_assistant_status.last_http_status > 0) {
        std::snprintf(http_status_text, sizeof(http_status_text), "%d", home_assistant_status.last_http_status);
    } else {
        std::snprintf(http_status_text, sizeof(http_status_text), "%s", kUnknownState);
    }
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::HomeAssistantHttp)].data(),
              g_current_sensor_values[sensor_index(SensorId::HomeAssistantHttp)].size(),
              http_status_text);

    const char* tracked_entity_state =
        home_assistant_status.tracked_entity_id[0] == '\0'
            ? kNotConfiguredState
            : (home_assistant_status.tracked_entity_state[0] ? home_assistant_status.tracked_entity_state.data()
                                                             : kUnknownState);
    copy_cstr(g_current_sensor_values[sensor_index(SensorId::TrackedEntity)].data(),
              g_current_sensor_values[sensor_index(SensorId::TrackedEntity)].size(),
              tracked_entity_state);

    (void)time_status;
}

void mqtt_request_cb(void* arg, err_t err)
{
    (void)arg;

    const PendingPublishType completed_type = g_publish_type_in_flight;
    const SensorId completed_sensor = g_sensor_in_flight;
    g_publish_type_in_flight = PendingPublishType::None;
    g_request_in_flight = false;

    if (err != ERR_OK) {
        set_status(MqttConnectionState::Error, err);
        std::printf("MQTT publish failed err=%d\n", static_cast<int>(err));
        return;
    }

    switch (completed_type) {
    case PendingPublishType::Availability:
        g_availability_published = true;
        std::printf("MQTT availability published topic=%s\n", g_availability_topic);
        break;
    case PendingPublishType::Discovery:
        g_sensor_discovery_published[sensor_index(completed_sensor)] = true;
        g_status.discovery_published = all_discovery_published();
        std::printf("MQTT discovery published topic=%s\n", g_sensor_discovery_topics[sensor_index(completed_sensor)].data());
        break;
    case PendingPublishType::State:
        copy_text(g_published_sensor_values[sensor_index(completed_sensor)],
                  g_current_sensor_values[sensor_index(completed_sensor)].data());
        std::printf("MQTT state published topic=%s value=%s\n",
                    g_sensor_state_topics[sensor_index(completed_sensor)].data(),
                    g_published_sensor_values[sensor_index(completed_sensor)].data());
        break;
    case PendingPublishType::None:
        break;
    }
}

void mqtt_connection_cb(mqtt_client_t* client, void* arg, mqtt_connection_status_t status)
{
    (void)client;
    (void)arg;

    g_connect_in_progress = false;

    if (status == MQTT_CONNECT_ACCEPTED) {
        g_connected = true;
        set_status(MqttConnectionState::Connected, 0);
        std::printf("MQTT connected to %s:%u\n", g_broker_host, static_cast<unsigned>(g_broker_port));
        return;
    }

    g_connected = false;
    g_request_in_flight = false;
    g_publish_type_in_flight = PendingPublishType::None;

    if (status == MQTT_CONNECT_REFUSED_USERNAME_PASS || status == MQTT_CONNECT_REFUSED_NOT_AUTHORIZED_) {
        set_status(MqttConnectionState::AuthFailed, static_cast<int>(status));
        std::printf("MQTT auth failed status=%d\n", static_cast<int>(status));
        return;
    }

    set_status(MqttConnectionState::Error, static_cast<int>(status));
    std::printf("MQTT connection failed status=%d\n", static_cast<int>(status));
}

bool start_mqtt_connect()
{
    if (!ensure_client()) {
        return false;
    }

    g_connect_in_progress = true;
    set_status(MqttConnectionState::Connecting, 0);

    cyw43_arch_lwip_begin();
    const err_t rc = mqtt_client_connect(g_client, &g_resolved_ip, g_broker_port, mqtt_connection_cb, nullptr, &g_client_info);
    cyw43_arch_lwip_end();

    if (rc == ERR_OK) {
        std::printf("MQTT connecting host=%s port=%u\n", g_broker_host, static_cast<unsigned>(g_broker_port));
        return true;
    }

    g_connect_in_progress = false;
    set_status(MqttConnectionState::Error, rc);
    std::printf("MQTT connect start failed err=%d\n", static_cast<int>(rc));
    return false;
}

void dns_found(const char* name, const ip_addr_t* ipaddr, void* arg)
{
    (void)name;
    (void)arg;

    if (!g_dns_pending) {
        return;
    }

    g_dns_pending = false;
    if (ipaddr == nullptr) {
        set_status(MqttConnectionState::Error, ERR_TIMEOUT);
        std::printf("MQTT DNS resolution failed for host=%s\n", g_broker_host);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_resolved = true;
}

bool start_session()
{
    if (!ensure_client()) {
        return false;
    }

    g_connect_attempted = true;
    g_status.last_error = 0;

    ip_addr_t parsed = {};
    if (ipaddr_aton(g_broker_host, &parsed)) {
        g_resolved_ip = parsed;
        g_dns_resolved = false;
        return start_mqtt_connect();
    }

    cyw43_arch_lwip_begin();
    const err_t dns_rc = dns_gethostbyname(g_broker_host, &g_resolved_ip, dns_found, nullptr);
    cyw43_arch_lwip_end();

    if (dns_rc == ERR_OK) {
        return start_mqtt_connect();
    }

    if (dns_rc == ERR_INPROGRESS) {
        g_dns_pending = true;
        g_dns_deadline = make_timeout_time_ms(kResolveTimeoutMs);
        set_status(MqttConnectionState::Resolving, 0);
        std::printf("MQTT resolving host=%s\n", g_broker_host);
        return true;
    }

    set_status(MqttConnectionState::Error, dns_rc);
    std::printf("MQTT dns_gethostbyname failed err=%d host=%s\n", static_cast<int>(dns_rc), g_broker_host);
    return false;
}

bool publish(const char* topic, const char* payload, PendingPublishType publish_type, SensorId sensor_id)
{
    if (!g_connected || g_request_in_flight || g_client == nullptr || topic == nullptr || payload == nullptr) {
        return false;
    }

    const u16_t payload_length = static_cast<u16_t>(std::strlen(payload));
    const u8_t retain = 1;

    cyw43_arch_lwip_begin();
    const err_t rc = mqtt_publish(g_client, topic, payload, payload_length, 0, retain, mqtt_request_cb, nullptr);
    cyw43_arch_lwip_end();

    if (rc != ERR_OK) {
        set_status(MqttConnectionState::Error, rc);
        std::printf("MQTT publish start failed err=%d topic=%s\n", static_cast<int>(rc), topic);
        return false;
    }

    g_request_in_flight = true;
    g_publish_type_in_flight = publish_type;
    g_sensor_in_flight = sensor_id;
    return true;
}

bool start_next_publish()
{
    if (!g_connected || g_request_in_flight) {
        return false;
    }

    if (!g_availability_published) {
        return publish(g_availability_topic, kOnlineState, PendingPublishType::Availability, SensorId::Status);
    }

    SensorId sensor_id = SensorId::Status;
    if (find_pending_discovery(&sensor_id)) {
        if (!build_discovery_payload(sensor_id)) {
            set_status(MqttConnectionState::Error, ERR_BUF);
            return false;
        }
        return publish(g_sensor_discovery_topics[sensor_index(sensor_id)].data(),
                       g_publish_buffer,
                       PendingPublishType::Discovery,
                       sensor_id);
    }

    if (find_pending_state(&sensor_id)) {
        if (!build_state_payload(sensor_id)) {
            set_status(MqttConnectionState::Error, ERR_BUF);
            return false;
        }
        return publish(g_sensor_state_topics[sensor_index(sensor_id)].data(),
                       g_publish_buffer,
                       PendingPublishType::State,
                       sensor_id);
    }

    return false;
}

}  // namespace

namespace mqtt_manager {

void init()
{
    g_config_valid = parse_mqtt_endpoint();
    g_status = {};
    g_status.configured = kMqttConfigured && g_config_valid;
    copy_text(g_status.broker, g_broker_host);
    g_status.state = !g_status.configured ? MqttConnectionState::Unconfigured
                                          : (kMqttRuntimeEnabled ? MqttConnectionState::WaitingForWifi
                                                                 : MqttConnectionState::Disabled);
    g_status.last_error = 0;
    g_status.discovery_published = false;
    g_status.device_id.fill('\0');
    clear_sensor_tracking();
    reset_runtime(false);
}

bool update(const WifiStatus& wifi_status,
            const HomeAssistantStatus& home_assistant_status,
            const TimeStatus& time_status)
{
    const MqttStatus previous = g_status;

    if (!g_status.configured) {
        g_status.state = MqttConnectionState::Unconfigured;
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    if (!kMqttRuntimeEnabled) {
        g_status.state = MqttConnectionState::Disabled;
        g_status.last_error = 0;
        reset_runtime(true);
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready) {
        reset_runtime(true);
        g_status.state = MqttConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    configure_identity_and_topics(wifi_status);
    update_sensor_values(wifi_status, home_assistant_status, time_status);

    if (g_dns_pending && !is_nil_time(g_dns_deadline) && absolute_time_diff_us(get_absolute_time(), g_dns_deadline) <= 0) {
        g_dns_pending = false;
        set_status(MqttConnectionState::Error, ERR_TIMEOUT);
    }

    if (!g_connect_attempted) {
        start_session();
    } else if (g_dns_resolved && !g_connect_in_progress && !g_connected) {
        g_dns_resolved = false;
        start_mqtt_connect();
    }

    if (g_connected && !g_request_in_flight) {
        start_next_publish();
    }

    return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
}

const MqttStatus& status()
{
    return g_status;
}

}  // namespace mqtt_manager
