#include "home_assistant_manager.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <optional>

#include "config_manager.h"
#include "debug_logging.h"
#include "geo_coordinates.h"
#include "ha_request_plan.h"
#include "house_weather.h"
#include "http_response.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "mbedtls/ssl.h"
#include "mqtt_manager.h"
#include "open_meteo_parser.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "psram.h"
#include "text_utils.h"
#include "time_manager.h"
#include "weather_forecast_parser.h"
#include "weather_json_scan.h"
#include "weather_normalisation.h"

namespace
{

#if __has_include("home_assistant_credentials.h")
#include "home_assistant_credentials.h"
constexpr bool kHomeAssistantConfigured = true;
#else
constexpr bool kHomeAssistantConfigured = false;
inline constexpr char HOME_ASSISTANT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_PORT = 8123;
inline constexpr char HOME_ASSISTANT_TOKEN[] = "";
inline constexpr char HOME_ASSISTANT_ENTITY_ID[] = "";
inline constexpr char HOME_ASSISTANT_SELF_ENTITY_ID[] = "";
#endif

/// @brief Normalizes user-editable credential symbols to the repo's internal naming style.
/// @details The local configuration headers intentionally keep explicit
/// `HOME_ASSISTANT_*` names because users edit those files directly. The implementation uses
/// `kCamelCase` aliases so runtime constants read the same way as the rest of the codebase.
inline constexpr const char* kHomeAssistantHost = HOME_ASSISTANT_HOST;
inline constexpr uint16_t kHomeAssistantPort = HOME_ASSISTANT_PORT;
inline constexpr const char* kHomeAssistantToken = HOME_ASSISTANT_TOKEN;
inline constexpr const char* kTrackedEntityId = HOME_ASSISTANT_ENTITY_ID;
inline constexpr const char* kSelfEntityId = HOME_ASSISTANT_SELF_ENTITY_ID;

constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kIoTimeoutMs = 4000;
constexpr uint32_t kTlsConnectTimeoutMs = 12000;
constexpr uint32_t kTlsIoTimeoutMs = 12000;
constexpr uint32_t kRetryDelayMs = 10000;
constexpr uint32_t kDirectWeatherRetryDelayMs = 5 * 60 * 1000;
constexpr uint8_t kTcpPollInterval = 2;
constexpr size_t kHttpTargetBufferSize = 640;
constexpr char kOpenMeteoHost[] = "api.open-meteo.com";
constexpr uint16_t kOpenMeteoPort = 80U;
constexpr bool kEnableDirectWeatherSerialDiagnostics = false;

/// @brief Returns true when the flash configuration owns Home Assistant fields.
bool runtime_home_assistant_config_present()
{
    const RuntimeConfig& config = config_manager::settings();
    return config.home_assistant_enabled || config.home_assistant_host[0] != '\0' ||
           config.home_assistant_token[0] != '\0' || config.home_assistant_entity_id[0] != '\0';
}

bool active_home_assistant_enabled()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_enabled
                                                   : kHomeAssistantConfigured;
}

const char* active_home_assistant_host()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_host.data()
                                                   : kHomeAssistantHost;
}

uint16_t active_home_assistant_port()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_port
                                                   : kHomeAssistantPort;
}

const char* active_home_assistant_token()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_token.data()
                                                   : kHomeAssistantToken;
}

const char* active_tracked_entity_id()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_entity_id.data()
                                                   : kTrackedEntityId;
}

const char* active_self_entity_id()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.home_assistant_self_entity_id.data()
                                                   : kSelfEntityId;
}

WeatherSource active_weather_source()
{
    const WeatherSource source = config_manager::settings().weather_source;
    // MET Norway remains in the enum only so older flash settings can be read
    // safely. It is not selectable while the Pico TLS path is unreliable with
    // that service.
    return source == WeatherSource::MetNorway ? WeatherSource::OpenMeteo : source;
}

void active_weather_coordinate_signature(char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0)
    {
        return;
    }

    const RuntimeConfig& config = config_manager::settings();
    std::snprintf(output, output_size, "%s", config.weather_coordinates.data());
}

bool parse_location_coordinates(double* out_latitude, double* out_longitude)
{
    // One reading of a typed location for the web form, the panel and here (#87).
    const std::optional<geo_coordinates::GeoPoint> point =
        geo_coordinates::parse(config_manager::settings().weather_coordinates.data());
    if (!point)
    {
        return false;
    }
    *out_latitude = point->latitude;
    *out_longitude = point->longitude;
    return true;
}

using ha_request_plan::Chain;
using ha_request_plan::RequestKind;

/// @brief Where one chain's requests go.
struct Endpoint
{
    char host[48] = {};
    uint16_t port = 0U;
    bool tls = false;
};

/// @brief When one request chain runs next, and with which request (#165).
struct ChainSchedule
{
    RequestKind next_kind = RequestKind::ProbeApi;
    absolute_time_t next_attempt = nil_time;
    /// False until the chain's first request starts; it then runs at once.
    bool attempted = false;
};

HomeAssistantStatus g_status = {};
ha_request_plan::Plan g_plan = {};
Endpoint g_ha_endpoint = {};
Endpoint g_open_meteo_endpoint = {};
std::array<ChainSchedule, ha_request_plan::kChainCount> g_chains = {};
ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
altcp_pcb* g_pcb = nullptr;
struct altcp_tls_config* g_tls_config = nullptr;
size_t g_request_sent = 0;
size_t g_response_len = 0;
// Issue #104: g_response has no documented sizing rationale and aggregates
// several entity fetches per cycle (weather, calendar, sun, self-entity).
// Tracked here rather than guessed at before shrinking it.
size_t g_response_peak_len = 0;
char g_request[1024] = {};
char g_request_body[256] = {};
// PSRAM on boards that have it (#162): filled from pbufs and parsed by the CPU.
MERLIN_PSRAM_BSS char g_response[16384];
// The endpoint of the request in flight, copied from its chain's Endpoint.
char g_configured_host[48] = {};
char g_configured_location[128] = {};
uint16_t g_configured_port = kHomeAssistantPort;
bool g_request_uses_tls = false;
// Set once the socket connects, so the poll callback only resumes a send
// that has somewhere to go.
bool g_tcp_connected = false;
absolute_time_t g_deadline = nil_time;
RequestKind g_request_kind = RequestKind::ProbeApi;
char g_weather_temperature_unit = '\0';
char g_weather_wind_source_unit[8] = {};
WeatherSource g_active_weather_source = WeatherSource::HomeAssistant;
double g_provider_latitude = 0.0;
double g_provider_longitude = 0.0;

/// @brief Returns the chain of the request in flight (or last started).
Chain current_chain()
{
    return ha_request_plan::chain_of(g_request_kind);
}

/// @brief Returns the schedule of one chain.
ChainSchedule& schedule_of(Chain chain)
{
    return g_chains[static_cast<size_t>(chain)];
}

/// @brief Emits focused serial diagnostics for direct weather bring-up.
void direct_weather_log(const char* format, ...)
{
    if (!kEnableDirectWeatherSerialDiagnostics || current_chain() != Chain::OpenMeteo ||
        format == nullptr)
    {
        return;
    }

    std::printf("WX Open-Meteo: ");
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
}

/// @brief Returns whether the current request kind should drive connection-state updates.
/// @details The HA chain's probe stands for the HA link; its later requests
/// fail soft. Open-Meteo's single request is its whole chain.
bool request_updates_connection_state()
{
    return current_chain() == Chain::OpenMeteo || g_request_kind == RequestKind::ProbeApi;
}

/// @brief Returns the active request's I/O timeout budget.
/// @details TLS handshakes can take noticeably longer than plain TCP on the
/// Pico W stack, so HTTPS requests get a wider but still bounded deadline.
uint32_t active_connect_timeout_ms()
{
    return g_request_uses_tls ? kTlsConnectTimeoutMs : kConnectTimeoutMs;
}

/// @brief Returns the active request's post-connect read/write timeout budget.
uint32_t active_io_timeout_ms()
{
    return g_request_uses_tls ? kTlsIoTimeoutMs : kIoTimeoutMs;
}

/// @brief Returns the retry delay for the current chain after a failed request.
/// @details HA is on the LAN, so it is retried soon; a public provider is
/// left alone for longer.
uint32_t active_failure_retry_ms()
{
    return current_chain() == Chain::HomeAssistant ? kRetryDelayMs : kDirectWeatherRetryDelayMs;
}

using text_utils::copy_text;

/// @brief Returns the log-friendly name for one request kind.
const char* request_kind_name(RequestKind kind)
{
    switch (kind)
    {
    case RequestKind::ProbeApi:
        return "probe";
    case RequestKind::FetchTrackedEntity:
        return "entity";
    case RequestKind::PublishSelfEntity:
        return "publish";
    case RequestKind::FetchOpenMeteo:
        return "open-meteo";
    }

    return "unknown";
}

// The primitives below (status-line parsing, header lookup, Content-Length,
// chunked decoding, buffered-completion detection) are shared across every
// network manager -- see http_response.h and docs/architecture.md's "Network
// helper ownership boundary". These wrappers exist only to keep every call
// site in this file unchanged (same names, same signatures); the actual
// logic lives in src/network/http_response.cpp, extracted from what used to
// be this file's own implementation (issue #46) since it was the most
// mature of three independently-converged copies.

/// @brief Returns a pointer to the start of the current HTTP response body.
const char* response_body()
{
    return http_response::body(g_response);
}

/// @brief Returns a pointer to the end of the current HTTP header block.
const char* response_headers_end()
{
    return http_response::headers_end(g_response);
}

/// @brief Extracts the HTTP status code when only part of the response is available.
int partial_http_status()
{
    return http_response::partial_status(g_response);
}

/// @brief Parses the `Content-Length` header from the current response.
bool parse_response_content_length(size_t* out_length)
{
    return http_response::parse_content_length(g_response, response_headers_end(), out_length);
}

/// @brief Returns whether a named header contains the requested token text.
bool response_header_has_token(const char* header_name, const char* token)
{
    return http_response::header_has_token(g_response, response_headers_end(), header_name, token);
}

/// @brief Decodes the current chunked response body in place.
bool decode_chunked_response_body()
{
    return http_response::decode_chunked_body(g_response, &g_response_len);
}

/// @brief Returns true when the buffered response has enough body bytes to parse.
/// @details This avoids waiting for EOF on HTTPS servers that keep the TLS
/// session open after sending a complete Content-Length or chunked response.
bool buffered_response_complete()
{
    return http_response::is_complete(g_response, g_response_len);
}

/// @brief Validates that the current HTTP response is complete and supported.
/// @details This firmware expects a complete header block and either a
/// supported chunked body or a body length that matches `Content-Length` when
/// present. Failing here keeps truncated or unsupported HTTP responses out of
/// the JSON parsing path.
bool validate_http_response(int* http_status, err_t* protocol_error)
{
    if (http_status != nullptr)
    {
        *http_status = 0;
    }
    if (protocol_error != nullptr)
    {
        *protocol_error = ERR_VAL;
    }

    const char* headers_end = response_headers_end();
    if (headers_end == nullptr)
    {
        return false;
    }

    int parsed_status = 0;
    if (std::sscanf(g_response, "HTTP/%*d.%*d %d", &parsed_status) != 1)
    {
        return false;
    }

    const bool chunked = response_header_has_token("Transfer-Encoding:", "chunked");
    if (chunked && !decode_chunked_response_body())
    {
        return false;
    }

    const char* body = headers_end + 4;
    const size_t body_length = g_response_len - static_cast<size_t>(body - g_response);
    size_t content_length = 0;
    if (!chunked && parse_response_content_length(&content_length) && body_length != content_length)
    {
        if (protocol_error != nullptr)
        {
            *protocol_error = ERR_CLSD;
        }
        return false;
    }

    if (http_status != nullptr)
    {
        *http_status = parsed_status;
    }
    if (protocol_error != nullptr)
    {
        *protocol_error = ERR_OK;
    }
    return true;
}

// The JSON scanning, unit normalisation, condition mapping, forecast/metric
// aggregation, and HA/Open-Meteo entity parsing primitives below (through
// clear_runtime_data) are shared across the Home Assistant and Open-Meteo
// weather paths -- see weather_json_scan.h, weather_normalisation.h,
// weather_forecast_parser.h, home_assistant_weather_parser.h,
// open_meteo_parser.h and docs/architecture.md's "Network helper ownership
// boundary". These wrappers exist only to keep every call site in this file
// unchanged (same names, same signatures); the actual logic now lives in
// those modules' .cpp files, extracted from what used to be this file's own
// implementation (issue #47).

/// @brief Extracts one JSON string field by key from a simple response body.
/// @details Still called directly from this file (the tracked-entity `state`
/// lookup below); every other JSON-scanning/normalisation/condition-mapping
/// helper that used to live here was only ever called from the parsing logic
/// that moved into the modules above, so those wrapper stubs were deleted
/// rather than kept as unused pass-throughs.
bool extract_json_string_value(const char* json, const char* key, char* out, size_t out_size)
{
    return weather_json::extract_json_string_value(json, key, out, out_size);
}

/// @brief Clears typed current-weather values used by threshold alerts.
void clear_current_weather_metrics()
{
    weather_forecast::clear_current_weather_metrics(g_status);
}

/// @brief Clears provider-originated weather warning state.
void clear_weather_alert_status()
{
    weather_forecast::clear_weather_alert_status(g_status);
}

/// @brief Clears the cached hourly weather forecast rows.
void clear_weather_forecast()
{
    weather_forecast::clear_weather_forecast(g_status);
}

/// @brief Clears the cached daily weather forecast rows used by week mode.
void clear_weather_daily_forecast()
{
    weather_forecast::clear_weather_daily_forecast(g_status);
}

/// @brief Parses an Open-Meteo current/hourly/daily weather response.
bool parse_open_meteo_weather(const char* json)
{
    return open_meteo::parse_weather(json, g_status, g_weather_temperature_unit,
                                     g_weather_wind_source_unit,
                                     sizeof(g_weather_wind_source_unit));
}

/// @brief Clears all runtime data fetched from Home Assistant.
void clear_runtime_data()
{
    // Runtime data is cleared independently from static configuration so losing
    // connectivity wipes stale weather/entity values without forgetting what to
    // request once the connection returns.
    g_status.self_entity_published = false;
    g_status.tracked_entity_state.fill('\0');
    g_status.weather_source_hint.fill('\0');
    g_status.weather_condition.fill('\0');
    g_status.weather_temperature.fill('\0');
    g_status.weather_wind_unit.fill('\0');
    g_status.sunrise_text.fill('\0');
    g_status.sunset_text.fill('\0');
    clear_current_weather_metrics();
    clear_weather_alert_status();
    clear_weather_forecast();
    clear_weather_daily_forecast();
    g_weather_temperature_unit = '\0';
    g_weather_wind_source_unit[0] = '\0';
}

/// @brief Returns whether the supplied HTTP status counts as success for a request kind.
bool request_kind_success(RequestKind kind, int http_status)
{
    switch (kind)
    {
    case RequestKind::ProbeApi:
    case RequestKind::FetchTrackedEntity:
    case RequestKind::FetchOpenMeteo:
        return http_status == 200;
    case RequestKind::PublishSelfEntity:
        return http_status == 200 || http_status == 201;
    }

    return false;
}

/// @brief Puts a chain back to its first request, due after `delay_ms`, or
/// never when `delay_ms` is 0 (a rejected token is not retried).
void schedule_chain_restart(Chain chain, uint32_t delay_ms)
{
    ChainSchedule& schedule = schedule_of(chain);
    schedule.next_kind = ha_request_plan::first_request(chain, g_plan).value_or(g_request_kind);
    schedule.next_attempt = (delay_ms > 0U) ? make_timeout_time_ms(delay_ms) : nil_time;
}

/// @brief Moves the current chain on after a request finished, successfully
/// or with a soft failure.
/// @details The next request in the chain runs straight away; after the last
/// one the chain waits for the next refresh.
void advance_request_kind()
{
    const Chain chain = current_chain();
    if (const std::optional<RequestKind> next = ha_request_plan::next_request(g_request_kind, g_plan))
    {
        ChainSchedule& schedule = schedule_of(chain);
        schedule.next_kind = *next;
        schedule.next_attempt = get_absolute_time();
        return;
    }
    schedule_chain_restart(chain, home_assistant_manager::kRefreshIntervalMs);
}

/// @brief Parses and normalizes the configured Home Assistant endpoint.
/// @return Whether HA REST is enabled with a usable host.
bool parse_home_assistant_endpoint()
{
    g_ha_endpoint = {};
    g_ha_endpoint.port = active_home_assistant_port();

    // Endpoint normalisation happens once at init because this client supports
    // host[:port] targets with optional http/https schemes, not arbitrary base
    // paths.
    if (!active_home_assistant_enabled() || active_home_assistant_host()[0] == '\0')
    {
        return false;
    }

    const char* host_end = nullptr;
    if (!text_utils::parse_endpoint(active_home_assistant_host(), text_utils::kHttpSchemes,
                                    active_home_assistant_port(), g_ha_endpoint.host,
                                    sizeof(g_ha_endpoint.host), &g_ha_endpoint.port,
                                    &g_ha_endpoint.tls, &host_end))
    {
        std::printf("HA config host/port is invalid: %s\n", active_home_assistant_host());
        g_ha_endpoint.host[0] = '\0';
        return false;
    }

    if (*host_end == '/' && std::strcmp(host_end, "/") != 0)
    {
        std::printf("HA config should not include a path; using host root only\n");
    }

    return true;
}

/// @brief Reads the saved location for Open-Meteo.
/// @return Whether Open-Meteo is the weather source and the location is valid.
bool parse_open_meteo_endpoint()
{
    g_open_meteo_endpoint = {};
    if (g_active_weather_source != WeatherSource::OpenMeteo)
    {
        return false;
    }

    if (!parse_location_coordinates(&g_provider_latitude, &g_provider_longitude))
    {
        std::printf("Weather config requires direct weather coordinates for Open-Meteo; got '%s'\n",
                    g_configured_location);
        return false;
    }

    std::snprintf(g_open_meteo_endpoint.host, sizeof(g_open_meteo_endpoint.host), "%s",
                  kOpenMeteoHost);
    g_open_meteo_endpoint.port = kOpenMeteoPort;
    return true;
}

/// @brief Reads the settings into the request plan and both endpoints.
void load_plan()
{
    active_weather_coordinate_signature(g_configured_location, sizeof(g_configured_location));
    g_active_weather_source = active_weather_source();

    g_plan = {};
    g_plan.home_assistant =
        parse_home_assistant_endpoint() && active_home_assistant_token()[0] != '\0';
    g_plan.tracked_entity = active_tracked_entity_id()[0] != '\0';
    g_plan.self_entity = active_self_entity_id()[0] != '\0';
    g_plan.open_meteo = parse_open_meteo_endpoint();
}

/// @brief Updates the status of the current chain.
/// @details The HA chain owns the HA link fields. The Open-Meteo chain only
/// touches the weather state, so its failures never mark HA down (#165).
/// With HA as the weather source the weather state follows MQTT instead
/// (see update_house_weather_state()).
void set_status(HomeAssistantConnectionState state, int last_error, int last_http_status)
{
    if (current_chain() == Chain::OpenMeteo)
    {
        g_status.weather_state = state;
        return;
    }

    g_status.state = state;
    g_status.last_error = last_error;
    g_status.last_http_status = last_http_status;
}

/// @brief Sets both chains to the same idle state, leaving an inactive
/// chain as Unconfigured.
void set_idle_states(HomeAssistantConnectionState state)
{
    g_status.state = g_plan.home_assistant ? state : HomeAssistantConnectionState::Unconfigured;
    if (g_active_weather_source == WeatherSource::OpenMeteo)
    {
        g_status.weather_state =
            g_plan.open_meteo ? state : HomeAssistantConnectionState::Unconfigured;
    }
}

/// @brief Maps the MQTT link onto the weather state, for HA weather (#153).
HomeAssistantConnectionState weather_state_from_mqtt(MqttConnectionState state)
{
    switch (state)
    {
    case MqttConnectionState::Disabled:
        return HomeAssistantConnectionState::Disabled;
    case MqttConnectionState::Unconfigured:
        return HomeAssistantConnectionState::Unconfigured;
    case MqttConnectionState::WaitingForWifi:
        return HomeAssistantConnectionState::WaitingForWifi;
    case MqttConnectionState::Resolving:
        return HomeAssistantConnectionState::Resolving;
    case MqttConnectionState::Connecting:
        return HomeAssistantConnectionState::Connecting;
    case MqttConnectionState::Connected:
        return HomeAssistantConnectionState::Connected;
    case MqttConnectionState::AuthFailed:
        return HomeAssistantConnectionState::Unauthorized;
    case MqttConnectionState::Error:
        return HomeAssistantConnectionState::Error;
    }
    return HomeAssistantConnectionState::Error;
}

/// @brief Applies one house weather payload from MQTT (main loop).
/// @details The data's age comes from the payload's own timestamp once the
/// clock is set, so data HA kept retained while it was cut off shows its
/// true age (and goes stale) rather than looking fresh on arrival.
void apply_house_weather(house_weather::Topic topic, const char* json)
{
    if (g_active_weather_source != WeatherSource::HomeAssistant)
    {
        return;
    }
    const std::optional<uint32_t> timestamp = house_weather::apply(topic, json, g_status);
    if (!timestamp.has_value())
    {
        std::printf("HA weather payload refused (topic %u)\n", static_cast<unsigned>(topic));
        return;
    }
    if (topic != house_weather::Topic::Now)
    {
        return;
    }
    const uint32_t now_ms = to_ms_since_boot(get_absolute_time());
    uint32_t age_ms = 0U;
    if (const std::optional<uint32_t> utc = time_manager::utc_now();
        utc.has_value() && *utc > *timestamp)
    {
        constexpr uint32_t kMaxAgeSeconds = UINT32_MAX / 1000U;
        age_ms = std::min(*utc - *timestamp, kMaxAgeSeconds) * 1000U;
    }
    // 0 means "never fetched", so very old data is pinned to boot + 1 ms.
    g_status.weather_last_success_ms = (age_ms < now_ms) ? now_ms - age_ms : 1U;
    PERIODIC_LOG("HA weather over MQTT: %s %s, %lu s old\n",
                 g_status.weather_condition[0] ? g_status.weather_condition.data() : "?",
                 g_status.weather_temperature[0] ? g_status.weather_temperature.data() : "-",
                 static_cast<unsigned long>(age_ms / 1000U));
}

/// @brief Keeps HA weather in step with MQTT: applies new payloads and
/// mirrors the link state.
void update_house_weather()
{
    mqtt_manager::take_house_weather(apply_house_weather);
    if (g_active_weather_source == WeatherSource::HomeAssistant)
    {
        g_status.weather_state = weather_state_from_mqtt(mqtt_manager::status().state);
    }
}

/// @brief Removes all lwIP callbacks from an application TCP control block.
void clear_connection_callbacks(altcp_pcb* pcb)
{
    if (pcb == nullptr)
    {
        return;
    }

    altcp_arg(pcb, nullptr);
    altcp_recv(pcb, nullptr);
    altcp_sent(pcb, nullptr);
    altcp_err(pcb, nullptr);
    altcp_poll(pcb, nullptr, 0);
}

/// @brief Closes the current TCP/TLS control block if one is active.
void close_pcb()
{
    if (g_pcb == nullptr)
    {
        if (g_tls_config != nullptr)
        {
            altcp_tls_free_config(g_tls_config);
            g_tls_config = nullptr;
        }
        return;
    }

    altcp_pcb* pcb = g_pcb;
    g_pcb = nullptr;
    cyw43_arch_lwip_begin();
    clear_connection_callbacks(pcb);
    const err_t close_rc = altcp_close(pcb);
    if (close_rc != ERR_OK)
    {
        altcp_abort(pcb);
    }
    cyw43_arch_lwip_end();

    if (g_tls_config != nullptr)
    {
        altcp_tls_free_config(g_tls_config);
        g_tls_config = nullptr;
    }
}

/// @brief Clears all transient request, socket, and response state.
void reset_attempt_state()
{
    close_pcb();
    g_dns_pending = false;
    g_dns_resolved = false;
    g_tcp_connected = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_request_body[0] = '\0';
    g_response[0] = '\0';
    g_deadline = nil_time;
}

/// @brief Finishes a failed request: records the state and restarts the
/// current chain after `retry_ms` (never when 0).
void finish_request(HomeAssistantConnectionState state, int last_error, int last_http_status,
                    uint32_t retry_ms)
{
    set_status(state, last_error, last_http_status);
    schedule_chain_restart(current_chain(), retry_ms);
    reset_attempt_state();
}

/// @brief Finalizes a successful request and advances the polling sequence.
void finish_request_success(int http_status)
{
    set_status(HomeAssistantConnectionState::Connected, 0, http_status);
    reset_attempt_state();
    advance_request_kind();
}

/// @brief Converts an optional request failure into cleared data and continued polling.
void finish_optional_request_soft_failure(err_t err)
{
    const int http_status = partial_http_status();

    switch (g_request_kind)
    {
    case RequestKind::FetchTrackedEntity:
        g_status.tracked_entity_state.fill('\0');
        break;
    case RequestKind::PublishSelfEntity:
        g_status.self_entity_published = false;
        break;
    case RequestKind::ProbeApi:
    case RequestKind::FetchOpenMeteo:
        // These drive their chain's state, so they never fail soft.
        break;
    }

    reset_attempt_state();
    set_status(HomeAssistantConnectionState::Connected, 0,
               http_status > 0 ? http_status : g_status.last_http_status);
    advance_request_kind();
    std::printf("HA %s soft-failed err=%d status=%d\n", request_kind_name(g_request_kind),
                static_cast<int>(err), http_status);
}

/// @brief Builds the HTTP request buffer for the current request kind.
bool build_request()
{
    g_request_body[0] = '\0';
    const char* method = "GET";
    const char* target = "/";
    char target_buffer[kHttpTargetBufferSize] = {};

    if (g_request_kind == RequestKind::FetchOpenMeteo)
    {
        const int target_len = std::snprintf(
            target_buffer, sizeof(target_buffer),
            "/v1/forecast?latitude=%.4f&longitude=%.4f&hourly=temperature_2m,wind_speed_10m,"
            "wind_direction_10m,weather_code&daily=sunrise,sunset,temperature_2m_max,"
            "temperature_2m_min,wind_speed_10m_max,wind_direction_10m_dominant,weather_code"
            "&current=temperature_2m,weather_code,wind_speed_10m,wind_direction_10m&timezone=auto"
            "&temperature_unit=celsius&wind_speed_unit=mph&forecast_hours=%u&forecast_days=%u",
            g_provider_latitude, g_provider_longitude,
            static_cast<unsigned>(kWeatherForecastEntryCount),
            static_cast<unsigned>(kWeatherDailyForecastEntryCount));
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            finish_request(HomeAssistantConnectionState::Error, -1, 0, active_failure_retry_ms());
            return false;
        }

        target = target_buffer;
    }
    // Each request kind maps to one explicit REST call so the receive path can
    // infer how to parse the response from the current state machine position.
    if (g_request_kind == RequestKind::FetchTrackedEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.tracked_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            finish_request(HomeAssistantConnectionState::Error, -1, 0, active_failure_retry_ms());
            return false;
        }
        target = target_buffer;
    }
    else if (g_request_kind == RequestKind::PublishSelfEntity)
    {
        method = "POST";
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.self_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            finish_request(HomeAssistantConnectionState::Error, -1, 0, active_failure_retry_ms());
            return false;
        }

        const int body_len =
            std::snprintf(g_request_body, sizeof(g_request_body),
                          "{\"state\":\"online\",\"attributes\":{\"friendly_name\":\"MerlinCCU\","
                          "\"integration\":\"rest_api\",\"status\":\"connected\"}}");
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            finish_request(HomeAssistantConnectionState::Error, -1, 0, active_failure_retry_ms());
            return false;
        }
        target = target_buffer;
    }

    // The request buffer is assembled as one contiguous HTTP message because
    // the send path may need to write it in multiple TCP chunks later.
    int len = 0;
    if (current_chain() == Chain::HomeAssistant)
    {
        len = std::snprintf(g_request, sizeof(g_request),
                            "%s %s HTTP/1.1\r\n"
                            "Host: %s:%u\r\n"
                            "Authorization: Bearer %s\r\n"
                            "Content-Type: application/json\r\n"
                            "Content-Length: %u\r\n"
                            "Connection: close\r\n"
                            "\r\n"
                            "%s",
                            method, target, g_configured_host,
                            static_cast<unsigned>(g_configured_port), active_home_assistant_token(),
                            static_cast<unsigned>(std::strlen(g_request_body)), g_request_body);
    }
    else
    {
        // Public providers are reached on their default ports, so keep the
        // Host header in its canonical form. The explicit User-Agent keeps
        // direct-provider traffic identifiable without adding account details.
        len = std::snprintf(g_request, sizeof(g_request),
                            "%s %s HTTP/1.1\r\n"
                            "Host: %s\r\n"
                            "User-Agent: MerlinCCU/0.1 "
                            "(https://github.com/victoriandad/MerlinCCU)\r\n"
                            "Accept: application/json\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            method, target, g_configured_host);
    }
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(g_request))
    {
        finish_request(HomeAssistantConnectionState::Error, -1, 0, active_failure_retry_ms());
        return false;
    }
    direct_weather_log("request kind=%s len=%d target=%s\n", request_kind_name(g_request_kind), len,
                       target);
    return true;
}

/// @brief Writes as much of the pending HTTP request as lwIP currently allows.
err_t try_send_request();

/// @brief Handles completion of the TCP connect step.
err_t on_tcp_connected(void* arg, altcp_pcb* pcb, err_t err)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        direct_weather_log("connect callback err=%d\n", static_cast<int>(err));
        finish_request(HomeAssistantConnectionState::Error, err, 0, active_failure_retry_ms());
        std::printf("HA connect failed err=%d\n", static_cast<int>(err));
        return ERR_OK;
    }

    direct_weather_log("connect callback ok tls=%u\n", g_request_uses_tls ? 1U : 0U);
    g_tcp_connected = true;
    if (request_updates_connection_state())
    {
        set_status(HomeAssistantConnectionState::Authorizing, 0, 0);
    }
    else
    {
        g_status.last_error = 0;
    }
    g_deadline = make_timeout_time_ms(active_io_timeout_ms());
    return try_send_request();
}

/// @brief Handles asynchronous lwIP TCP errors for the active request.
void on_tcp_error(void* arg, err_t err)
{
    (void)arg;
    direct_weather_log("tcp error err=%d http=%d sent=%u recv=%u\n", static_cast<int>(err),
                       g_status.last_http_status, static_cast<unsigned>(g_request_sent),
                       static_cast<unsigned>(g_response_len));
    g_pcb = nullptr;
    if (g_tls_config != nullptr)
    {
        altcp_tls_free_config(g_tls_config);
        g_tls_config = nullptr;
    }
    // lwIP has already freed the pcb, so reset_attempt_state() (via
    // finish_request) finds nothing left to close.
    finish_request(HomeAssistantConnectionState::Error, err, g_status.last_http_status,
                   active_failure_retry_ms());
    std::printf("HA tcp error err=%d\n", static_cast<int>(err));
}

/// @brief Handles the completed HTTP status for the current request kind.
void handle_http_status(int http_status)
{
    // Unconditional (unlike direct_weather_log below, which only covers the
    // direct-weather-source path): every RequestKind cycled by
    // advance_request_kind() shares this one response buffer, so peak usage
    // has to be observed across all of them, not just weather fetches -- see
    // issue #104.
    PERIODIC_LOG("home_assistant_manager: response complete, kind=%s peak=%uB of %uB\n",
                 request_kind_name(g_request_kind), static_cast<unsigned>(g_response_peak_len),
                 static_cast<unsigned>(sizeof(g_response)));

    if (g_request_kind == RequestKind::FetchOpenMeteo)
    {
        g_status.weather_http_status = http_status;
        if (http_status == 200 && parse_open_meteo_weather(response_body()))
        {
            copy_text(g_status.weather_source_hint, "Open-Meteo");
            g_status.weather_last_success_ms = to_ms_since_boot(get_absolute_time());
            direct_weather_log("http=%d parse ok forecast=%u bytes=%u\n", http_status,
                               static_cast<unsigned>(g_status.weather_forecast_count),
                               static_cast<unsigned>(g_response_len));
            finish_request_success(http_status);
            return;
        }

        clear_weather_forecast();
        clear_weather_daily_forecast();
        g_status.sunrise_text.fill('\0');
        g_status.sunset_text.fill('\0');
        // Leave the condition empty rather than writing a placeholder: the
        // Weather page and the WeatherUnavailable alert both read an empty
        // payload as a failed fetch.
        g_status.weather_condition.fill('\0');
        g_status.weather_temperature.fill('\0');
        clear_current_weather_metrics();
        clear_weather_alert_status();
        g_weather_temperature_unit = '\0';
        g_weather_wind_source_unit[0] = '\0';
        direct_weather_log("http=%d parse failed bytes=%u body=%u\n", http_status,
                           static_cast<unsigned>(g_response_len),
                           response_body() != nullptr ? 1U : 0U);
        finish_request(HomeAssistantConnectionState::Error, ERR_VAL, http_status,
                       active_failure_retry_ms());
        return;
    }

    // Successful responses update only the fields associated with the current
    // request kind, then advance the serialized polling sequence.
    if (request_kind_success(g_request_kind, http_status))
    {
        if (g_request_kind == RequestKind::FetchTrackedEntity)
        {
            char entity_state[sizeof(g_status.tracked_entity_state)] = {};
            if (extract_json_string_value(response_body(), "\"state\":\"", entity_state,
                                          sizeof(entity_state)))
            {
                copy_text(g_status.tracked_entity_state, entity_state);
            }
            else
            {
                copy_text(g_status.tracked_entity_state, "?");
            }
            PERIODIC_LOG("HA entity state %s=%s\n", g_status.tracked_entity_id.data(),
                         g_status.tracked_entity_state.data());
        }
        else if (g_request_kind == RequestKind::PublishSelfEntity)
        {
            g_status.self_entity_published = true;
            PERIODIC_LOG("HA self entity posted %s status=%d\n", g_status.self_entity_id.data(),
                         http_status);
        }
        else
        {
            PERIODIC_LOG("HA API probe ok host=%s port=%u status=%d\n", g_configured_host,
                         static_cast<unsigned>(g_configured_port), http_status);
        }

        g_status.last_http_status = http_status;
        finish_request_success(http_status);
        return;
    }

    // Authorization failure is treated as terminal because retrying with the
    // same bearer token would only spam the server and hide the real problem.
    if (http_status == 401)
    {
        g_status.last_http_status = http_status;
        finish_request(HomeAssistantConnectionState::Unauthorized, 0, http_status, 0);
        PERIODIC_LOG("HA %s request unauthorized host=%s port=%u\n",
                     request_kind_name(g_request_kind), g_configured_host,
                     static_cast<unsigned>(g_configured_port));
        return;
    }

    // Optional requests fail soft so the rest of the sequence can still
    // complete and the UI can keep whatever data remains available.
    if (g_request_kind == RequestKind::FetchTrackedEntity)
    {
        g_status.tracked_entity_state.fill('\0');
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA tracked entity unavailable %s status=%d\n",
                     g_status.tracked_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::PublishSelfEntity)
    {
        g_status.self_entity_published = false;
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA self entity unavailable %s status=%d\n", g_status.self_entity_id.data(),
                     http_status);
        return;
    }

    g_status.last_http_status = http_status;
    finish_request(HomeAssistantConnectionState::Error, 0, http_status, active_failure_retry_ms());
    PERIODIC_LOG("HA %s request unexpected status=%d host=%s port=%u\n",
                 request_kind_name(g_request_kind), http_status, g_configured_host,
                 static_cast<unsigned>(g_configured_port));
}

/// @brief Handles inbound TCP data and connection close events.
err_t on_tcp_recv(void* arg, altcp_pcb* pcb, pbuf* p, err_t err)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        if (p != nullptr)
        {
            altcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (err != ERR_OK)
    {
        if (p != nullptr)
        {
            pbuf_free(p);
        }
        direct_weather_log("recv callback err=%d http=%d sent=%u recv=%u\n", static_cast<int>(err),
                           g_status.last_http_status, static_cast<unsigned>(g_request_sent),
                           static_cast<unsigned>(g_response_len));
        finish_request(HomeAssistantConnectionState::Error, err, g_status.last_http_status,
                       active_failure_retry_ms());
        return ERR_OK;
    }

    if (p == nullptr)
    {
        int http_status = 0;
        err_t protocol_error = ERR_VAL;
        if (!validate_http_response(&http_status, &protocol_error))
        {
            direct_weather_log("eof invalid response err=%d partial_http=%d bytes=%u\n",
                               static_cast<int>(protocol_error), partial_http_status(),
                               static_cast<unsigned>(g_response_len));
            // Why we fail here:
            // This is the boundary between raw network input and application
            // state. If the response is malformed, incomplete, or uses an HTTP
            // feature this client does not understand, we stop here and treat
            // it like a failed request. That is easier to reason about than
            // letting deeper parsing code guess what the response meant.
            if (request_updates_connection_state())
            {
                finish_request(HomeAssistantConnectionState::Error, protocol_error,
                               partial_http_status(), active_failure_retry_ms());
            }
            else
            {
                finish_optional_request_soft_failure(protocol_error);
            }
            return ERR_OK;
        }

        handle_http_status(http_status);
        return ERR_OK;
    }

    // Accumulate response bytes into one contiguous buffer. Responses are
    // parsed once the advertised body is present or the provider closes the
    // connection.
    const uint16_t received_len = p->tot_len;
    const size_t space_left = sizeof(g_response) - g_response_len - 1;
    const uint16_t copy_len =
        static_cast<uint16_t>((received_len < space_left) ? received_len : space_left);

    if (copy_len > 0)
    {
        pbuf_copy_partial(p, g_response + g_response_len, copy_len, 0);
        g_response_len += copy_len;
        g_response[g_response_len] = '\0';
        g_response_peak_len = std::max(g_response_peak_len, g_response_len);
        g_deadline = make_timeout_time_ms(active_io_timeout_ms());
    }

    altcp_recved(pcb, received_len);
    pbuf_free(p);
    direct_weather_log("recv bytes=%u copied=%u total=%u peak=%u partial_http=%d\n",
                       static_cast<unsigned>(received_len), static_cast<unsigned>(copy_len),
                       static_cast<unsigned>(g_response_len),
                       static_cast<unsigned>(g_response_peak_len), partial_http_status());
    if (g_response_len + 1 >= sizeof(g_response))
    {
        if (request_updates_connection_state())
        {
            direct_weather_log("response buffer full total=%u partial_http=%d\n",
                               static_cast<unsigned>(g_response_len), partial_http_status());
            finish_request(HomeAssistantConnectionState::Error, ERR_BUF, partial_http_status(),
                           active_failure_retry_ms());
        }
        else
        {
            finish_optional_request_soft_failure(ERR_BUF);
        }
        return ERR_OK;
    }

    int http_status = 0;
    err_t protocol_error = ERR_VAL;
    if (buffered_response_complete() && validate_http_response(&http_status, &protocol_error))
    {
        // Public HTTPS APIs do not always close promptly after a complete
        // response. Once the advertised body is present, parse it immediately
        // instead of burning the TLS I/O timeout waiting for EOF.
        handle_http_status(http_status);
        return ERR_OK;
    }

    return ERR_OK;
}

/// @brief Writes as much of the pending HTTP request as lwIP currently allows.
err_t try_send_request()
{
    if (g_pcb == nullptr)
    {
        return ERR_CLSD;
    }

    const size_t request_len = std::strlen(g_request);
    // TCP send buffer space can be smaller than the request, so the request is
    // streamed out in chunks and resumed by later callbacks when necessary.
    while (g_request_sent < request_len)
    {
        const u16_t sndbuf = altcp_sndbuf(g_pcb);
        if (sndbuf == 0)
        {
            return ERR_OK;
        }

        const size_t remaining = request_len - g_request_sent;
        const u16_t chunk = static_cast<u16_t>((remaining < sndbuf) ? remaining : sndbuf);
        if (chunk == 0)
        {
            return ERR_OK;
        }

        const err_t write_rc =
            altcp_write(g_pcb, g_request + g_request_sent, chunk,
                        TCP_WRITE_FLAG_COPY |
                            ((g_request_sent + chunk < request_len) ? TCP_WRITE_FLAG_MORE : 0));
        if (write_rc != ERR_OK)
        {
            if (write_rc == ERR_MEM)
            {
                direct_weather_log(
                    "write deferred sent=%u/%u sndbuf=%u\n", static_cast<unsigned>(g_request_sent),
                    static_cast<unsigned>(request_len), static_cast<unsigned>(sndbuf));
                return ERR_OK;
            }

            direct_weather_log("write failed err=%d sent=%u/%u sndbuf=%u\n",
                               static_cast<int>(write_rc), static_cast<unsigned>(g_request_sent),
                               static_cast<unsigned>(request_len), static_cast<unsigned>(sndbuf));
            finish_request(HomeAssistantConnectionState::Error, write_rc, 0,
                           active_failure_retry_ms());
            return write_rc;
        }

        g_request_sent += chunk;
        direct_weather_log("write chunk=%u sent=%u/%u sndbuf=%u\n", static_cast<unsigned>(chunk),
                           static_cast<unsigned>(g_request_sent),
                           static_cast<unsigned>(request_len), static_cast<unsigned>(sndbuf));
    }

    const err_t output_rc = altcp_output(g_pcb);
    if (output_rc != ERR_OK)
    {
        direct_weather_log("output failed err=%d sent=%u/%u\n", static_cast<int>(output_rc),
                           static_cast<unsigned>(g_request_sent),
                           static_cast<unsigned>(request_len));
        finish_request(HomeAssistantConnectionState::Error, output_rc, 0,
                       active_failure_retry_ms());
        return output_rc;
    }

    direct_weather_log("output ok sent=%u/%u\n", static_cast<unsigned>(g_request_sent),
                       static_cast<unsigned>(request_len));
    g_deadline = make_timeout_time_ms(active_io_timeout_ms());
    return ERR_OK;
}

/// @brief Handles lwIP send acknowledgements for the active request.
err_t on_tcp_sent(void* arg, altcp_pcb* pcb, u16_t len)
{
    (void)arg;
    (void)len;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    direct_weather_log("sent ack len=%u sent=%u/%u\n", static_cast<unsigned>(len),
                       static_cast<unsigned>(g_request_sent),
                       static_cast<unsigned>(std::strlen(g_request)));
    g_deadline = make_timeout_time_ms(active_io_timeout_ms());
    return try_send_request();
}

/// @brief Handles lwIP poll callbacks for the active request.
err_t on_tcp_poll(void* arg, altcp_pcb* pcb)
{
    (void)arg;

    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    if (g_tcp_connected && g_request_sent < std::strlen(g_request))
    {
        return try_send_request();
    }

    return ERR_OK;
}

/// @brief Handles completion of the Home Assistant DNS lookup.
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
        direct_weather_log("dns callback timeout/null host=%s\n", g_configured_host);
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0,
                       active_failure_retry_ms());
        std::printf("HA DNS resolution failed for host=%s\n", g_configured_host);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_resolved = true;
    char ip_text[24] = {};
    ipaddr_ntoa_r(&g_resolved_ip, ip_text, sizeof(ip_text));
    direct_weather_log("dns callback host=%s ip=%s\n", g_configured_host, ip_text);
}

/// @brief Opens the TCP socket and starts the HTTP request connect phase.
bool start_socket_connect()
{
    if (!build_request())
    {
        return false;
    }

    char ip_text[24] = {};
    ipaddr_ntoa_r(&g_resolved_ip, ip_text, sizeof(ip_text));
    direct_weather_log("connect start ip=%s port=%u tls=%u\n", ip_text,
                       static_cast<unsigned>(g_configured_port), g_request_uses_tls ? 1U : 0U);

    cyw43_arch_lwip_begin();
    if (g_request_uses_tls)
    {
        // The first TLS implementation intentionally uses the platform TLS
        // stack without a CA bundle. Certificate verification can be added
        // later as a separate trust-store concern.
        g_tls_config = altcp_tls_create_config_client(nullptr, 0);
        if (g_tls_config != nullptr)
        {
            direct_weather_log("tls config ok\n");
            g_pcb = altcp_tls_new(g_tls_config, IP_GET_TYPE(&g_resolved_ip));
            if (g_pcb != nullptr)
            {
                auto* ssl = static_cast<mbedtls_ssl_context*>(altcp_tls_context(g_pcb));
                if (ssl == nullptr || mbedtls_ssl_set_hostname(ssl, g_configured_host) != 0)
                {
                    direct_weather_log("tls hostname failed ssl=%u host=%s\n",
                                       ssl != nullptr ? 1U : 0U, g_configured_host);
                    altcp_abort(g_pcb);
                    g_pcb = nullptr;
                }
                else
                {
                    direct_weather_log("tls hostname set host=%s\n", g_configured_host);
                }
            }
        }
    }
    else
    {
        g_pcb = altcp_tcp_new_ip_type(IP_GET_TYPE(&g_resolved_ip));
    }

    if (g_pcb == nullptr)
    {
        direct_weather_log("pcb allocation failed tls=%u\n", g_request_uses_tls ? 1U : 0U);
        if (g_tls_config != nullptr)
        {
            altcp_tls_free_config(g_tls_config);
            g_tls_config = nullptr;
        }
        cyw43_arch_lwip_end();
        finish_request(HomeAssistantConnectionState::Error, ERR_MEM, 0, active_failure_retry_ms());
        return false;
    }

    altcp_arg(g_pcb, nullptr);
    altcp_recv(g_pcb, on_tcp_recv);
    altcp_sent(g_pcb, on_tcp_sent);
    altcp_err(g_pcb, on_tcp_error);
    altcp_poll(g_pcb, on_tcp_poll, kTcpPollInterval);

    const err_t rc = altcp_connect(g_pcb, &g_resolved_ip, g_configured_port, on_tcp_connected);
    cyw43_arch_lwip_end();
    if (rc == ERR_OK)
    {
        direct_weather_log("altcp_connect ok deadline=%u\n",
                           static_cast<unsigned>(active_connect_timeout_ms()));
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Connecting, 0, 0);
        }
        else
        {
            g_status.last_error = 0;
        }
        g_deadline = make_timeout_time_ms(active_connect_timeout_ms());
        return true;
    }

    direct_weather_log("altcp_connect failed err=%d\n", static_cast<int>(rc));
    finish_request(HomeAssistantConnectionState::Error, rc, 0, active_failure_retry_ms());
    std::printf("HA connect start failed err=%d\n", static_cast<int>(rc));
    return false;
}

/// @brief Starts a chain's next request, pointing the client at that chain's endpoint.
bool start_probe(Chain chain)
{
    ChainSchedule& schedule = schedule_of(chain);
    schedule.attempted = true;
    schedule.next_attempt = nil_time;
    g_request_kind = schedule.next_kind;

    const Endpoint& endpoint =
        (chain == Chain::HomeAssistant) ? g_ha_endpoint : g_open_meteo_endpoint;
    std::snprintf(g_configured_host, sizeof(g_configured_host), "%s", endpoint.host);
    g_configured_port = endpoint.port;
    g_request_uses_tls = endpoint.tls;

    reset_attempt_state();
    if (chain == Chain::HomeAssistant)
    {
        g_status.last_error = 0;
        if (request_updates_connection_state())
        {
            g_status.last_http_status = 0;
        }
    }
    PERIODIC_LOG("HA starting %s request\n", request_kind_name(g_request_kind));
    direct_weather_log("start request kind=%s host=%s port=%u tls=%u\n",
                       request_kind_name(g_request_kind), g_configured_host,
                       static_cast<unsigned>(g_configured_port), g_request_uses_tls ? 1U : 0U);

    // Literal IP targets skip DNS so local integration testing can work even
    // when name resolution is not ready on the current network.
    ip_addr_t parsed = {};
    if (ipaddr_aton(g_configured_host, &parsed))
    {
        g_resolved_ip = parsed;
        g_dns_resolved = true;
        direct_weather_log("literal ip target host=%s\n", g_configured_host);
        return start_socket_connect();
    }

    cyw43_arch_lwip_begin();
    const err_t dns_rc = dns_gethostbyname(g_configured_host, &g_resolved_ip, dns_found, nullptr);
    cyw43_arch_lwip_end();
    if (dns_rc == ERR_OK)
    {
        g_dns_resolved = true;
        char ip_text[24] = {};
        ipaddr_ntoa_r(&g_resolved_ip, ip_text, sizeof(ip_text));
        direct_weather_log("dns immediate host=%s ip=%s\n", g_configured_host, ip_text);
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        }
        return start_socket_connect();
    }

    if (dns_rc == ERR_INPROGRESS)
    {
        g_dns_pending = true;
        if (request_updates_connection_state())
        {
            set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        }
        g_deadline = make_timeout_time_ms(kResolveTimeoutMs);
        PERIODIC_LOG("HA resolving host=%s\n", g_configured_host);
        direct_weather_log("dns pending host=%s deadline=%u\n", g_configured_host,
                           static_cast<unsigned>(kResolveTimeoutMs));
        return true;
    }

    direct_weather_log("dns start failed err=%d host=%s\n", static_cast<int>(dns_rc),
                       g_configured_host);
    finish_request(HomeAssistantConnectionState::Error, dns_rc, 0, active_failure_retry_ms());
    std::printf("HA dns_gethostbyname failed err=%d host=%s\n", static_cast<int>(dns_rc),
                g_configured_host);
    return false;
}

} // namespace

namespace home_assistant_manager
{

/// @brief Initializes Home Assistant configuration and runtime state.
void init()
{
    // Configuration is normalized once here so the periodic update loop only
    // has to decide whether to wait, connect, or parse responses.
    load_plan();
    g_status = {};
    g_status.configured = g_plan.home_assistant;
    copy_text(g_status.host, g_ha_endpoint.host);
    // HA presence and the tracked entity no longer depend on the weather
    // source (#165), so these are set whenever HA is.
    copy_text(g_status.tracked_entity_id, active_tracked_entity_id());
    copy_text(g_status.self_entity_id, active_self_entity_id());
    clear_runtime_data();
    if (g_active_weather_source == WeatherSource::HomeAssistant)
    {
        // HA weather arrives over MQTT (#153); the Status page shows the broker.
        copy_text(g_status.weather_host, config_manager::settings().mqtt_host.data());
    }
    else
    {
        // The Weather page's setup hint reads the saved location from here.
        copy_text(g_status.weather_entity_id, g_configured_location);
        copy_text(g_status.weather_host, kOpenMeteoHost);
        copy_text(g_status.weather_source_hint, "Open-Meteo");
    }
    set_idle_states(HomeAssistantConnectionState::WaitingForWifi);
    reset_attempt_state();
    for (const Chain chain : {Chain::HomeAssistant, Chain::OpenMeteo})
    {
        schedule_of(chain) = {};
        schedule_chain_restart(chain, 0U);
    }
    g_request_kind = RequestKind::ProbeApi;
}

/// @brief Returns true when weather source settings changed after initialisation.
/// @details The web UI and front panel can save display-related settings at
/// runtime, so direct weather providers must not rely on reboot-only config.
bool runtime_weather_config_changed()
{
    if (active_weather_source() != g_active_weather_source)
    {
        return true;
    }

    if (g_active_weather_source != WeatherSource::HomeAssistant)
    {
        char coordinate_signature[sizeof(g_configured_location)] = {};
        active_weather_coordinate_signature(coordinate_signature, sizeof(coordinate_signature));
        if (std::strcmp(coordinate_signature, g_configured_location) != 0)
        {
            return true;
        }
    }

    return false;
}

/// @brief Returns whether a chain is set up and its next request is due.
bool chain_due(Chain chain)
{
    if (!ha_request_plan::chain_active(chain, g_plan))
    {
        return false;
    }
    const ChainSchedule& schedule = schedule_of(chain);
    if (!schedule.attempted)
    {
        return true;
    }
    return !is_nil_time(schedule.next_attempt) &&
           absolute_time_diff_us(get_absolute_time(), schedule.next_attempt) <= 0;
}

/// @brief Advances the Home Assistant resolve/connect/request state machine.
bool update(const WifiStatus& wifi_status)
{
    // Static: the status carries the weather forecast tables, and a stack copy
    // took about 2 KB of the 4 KB core-0 stack on every loop pass (#164).
    // update() only runs from the main loop, so one copy is never shared.
    static HomeAssistantStatus previous = {};
    previous = g_status;
    if (runtime_weather_config_changed())
    {
        init();
        return previous != g_status;
    }
    update_house_weather();

    // With neither chain set up there is nothing to fetch; init() has
    // already marked both Unconfigured.
    if (!ha_request_plan::chain_active(Chain::HomeAssistant, g_plan) &&
        !ha_request_plan::chain_active(Chain::OpenMeteo, g_plan))
    {
        return previous != g_status;
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready)
    {
        reset_attempt_state();
        set_idle_states(HomeAssistantConnectionState::WaitingForWifi);
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        g_status.weather_http_status = 0;
        clear_runtime_data();
        for (const Chain chain : {Chain::HomeAssistant, Chain::OpenMeteo})
        {
            schedule_of(chain) = {};
            schedule_chain_restart(chain, 0U);
        }
        return previous != g_status;
    }

    // Resolver and socket operations are asynchronous, so deadlines are checked
    // here in the update loop rather than buried inside lwIP callbacks.
    if (g_dns_pending && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        direct_weather_log("dns timeout host=%s\n", g_configured_host);
        g_dns_pending = false;
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0,
                       active_failure_retry_ms());
    }

    // After DNS completes, the next update tick actually opens the socket so
    // the callback path stays simple and one-way.
    if (g_dns_resolved && g_pcb == nullptr)
    {
        g_dns_resolved = false;
        start_socket_connect();
    }

    // Socket operations also share the same deadline mechanism so hung connects
    // and hung reads both converge on the same retry path.
    if (g_pcb != nullptr && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        const int timeout_http_status =
            g_status.last_http_status > 0 ? g_status.last_http_status : partial_http_status();
        direct_weather_log("io timeout sent=%u recv=%u partial_http=%d retry_ms=%u\n",
                           static_cast<unsigned>(g_request_sent),
                           static_cast<unsigned>(g_response_len), timeout_http_status,
                           static_cast<unsigned>(active_failure_retry_ms()));
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, timeout_http_status,
                       active_failure_retry_ms());
    }

    // One request at a time shares the client and its response buffer. When
    // idle, the first chain that is due runs; HA goes first because its
    // requests are quick LAN calls.
    if (!g_dns_pending && g_pcb == nullptr)
    {
        for (const Chain chain : {Chain::HomeAssistant, Chain::OpenMeteo})
        {
            if (chain_due(chain))
            {
                start_probe(chain);
                break;
            }
        }
    }

    return previous != g_status;
}

const HomeAssistantStatus& status()
{
    return g_status;
}

} // namespace home_assistant_manager
