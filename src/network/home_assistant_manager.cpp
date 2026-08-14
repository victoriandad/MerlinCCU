#include "home_assistant_manager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#include "config_manager.h"
#include "debug_logging.h"
#include "home_assistant_weather_parser.h"
#include "http_response.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "mbedtls/ssl.h"
#include "open_meteo_parser.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
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

#if __has_include("weather_display_config.h")
#include "weather_display_config.h"
#else
inline constexpr char HOME_ASSISTANT_WEATHER_ENTITY_ID[] = "";
inline constexpr char HOME_ASSISTANT_SUN_ENTITY_ID[] = "";
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
inline constexpr const char* kWeatherEntityId = HOME_ASSISTANT_WEATHER_ENTITY_ID;
inline constexpr const char* kSunEntityId = HOME_ASSISTANT_SUN_ENTITY_ID;

constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kIoTimeoutMs = 4000;
constexpr uint32_t kTlsConnectTimeoutMs = 12000;
constexpr uint32_t kTlsIoTimeoutMs = 12000;
constexpr uint32_t kRetryDelayMs = 10000;
constexpr uint32_t kDirectWeatherRetryDelayMs = 5 * 60 * 1000;
constexpr uint32_t kRefreshIntervalMs = 5 * 60 * 1000;
constexpr uint8_t kTcpPollInterval = 2;
constexpr size_t kHttpTargetBufferSize = 640;
constexpr char kHttpSchemePrefix[] = "http://";
constexpr size_t kHttpSchemePrefixLength = sizeof(kHttpSchemePrefix) - 1;
constexpr char kHttpsSchemePrefix[] = "https://";
constexpr size_t kHttpsSchemePrefixLength = sizeof(kHttpsSchemePrefix) - 1;
constexpr bool kHomeAssistantRuntimeEnabled = true;
constexpr char kOpenMeteoHost[] = "api.open-meteo.com";
constexpr uint16_t kOpenMeteoPort = 80U;
constexpr bool kEnableDirectWeatherSerialDiagnostics = false;

/// @brief Returns true when the flash configuration owns Home Assistant fields.
bool runtime_home_assistant_config_present()
{
    const RuntimeConfig& config = config_manager::settings();
    return config.home_assistant_enabled || config.home_assistant_host[0] != '\0' ||
           config.home_assistant_token[0] != '\0' || config.home_assistant_entity_id[0] != '\0' ||
           config.weather_entity_id[0] != '\0';
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

const char* active_weather_entity_id()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.weather_entity_id.data()
                                                   : kWeatherEntityId;
}

const char* active_sun_entity_id()
{
    const RuntimeConfig& config = config_manager::settings();
    return runtime_home_assistant_config_present() ? config.sun_entity_id.data() : kSunEntityId;
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

bool parse_next_double(const char** cursor, double* out_value)
{
    if (cursor == nullptr || *cursor == nullptr || out_value == nullptr)
    {
        return false;
    }

    const char* scan = *cursor;
    while (*scan != '\0')
    {
        const bool could_start_number =
            (*scan >= '0' && *scan <= '9') || *scan == '-' || *scan == '+' || *scan == '.';
        if (could_start_number)
        {
            char* end = nullptr;
            const double value = std::strtod(scan, &end);
            if (end != scan)
            {
                *cursor = end;
                *out_value = value;
                return true;
            }
        }
        ++scan;
    }

    return false;
}

bool parse_coordinates_text(const char* location_text, double* out_latitude, double* out_longitude)
{
    if (location_text == nullptr || location_text[0] == '\0' || out_latitude == nullptr ||
        out_longitude == nullptr)
    {
        return false;
    }

    const char* cursor = location_text;
    double latitude = 0.0;
    double longitude = 0.0;
    if (!parse_next_double(&cursor, &latitude) || !parse_next_double(&cursor, &longitude))
    {
        return false;
    }

    if (std::isnan(latitude) || std::isnan(longitude) || latitude < -90.0 || latitude > 90.0 ||
        longitude < -180.0 || longitude > 180.0)
    {
        return false;
    }

    *out_latitude = latitude;
    *out_longitude = longitude;
    return true;
}

bool parse_location_coordinates(double* out_latitude, double* out_longitude)
{
    return parse_coordinates_text(config_manager::settings().weather_coordinates.data(),
                                  out_latitude, out_longitude);
}

enum class RequestKind : uint8_t
{
    ProbeApi = 0,
    FetchTrackedEntity,
    FetchWeatherEntity,
    FetchWeatherForecast,
    FetchWeatherDailyForecast,
    FetchSunEntity,
    PublishSelfEntity,
};

HomeAssistantStatus g_status = {};
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
char g_response[16384] = {};
char g_configured_host[48] = {};
char g_configured_location[128] = {};
uint16_t g_configured_port = kHomeAssistantPort;
bool g_config_valid = false;
bool g_request_uses_tls = false;
absolute_time_t g_deadline = nil_time;
absolute_time_t g_next_attempt = nil_time;
bool g_probe_attempted = false;
bool g_sequence_complete = false;
RequestKind g_request_kind = RequestKind::ProbeApi;
char g_weather_temperature_unit = '\0';
char g_weather_wind_source_unit[8] = {};
WeatherSource g_active_weather_source = WeatherSource::HomeAssistant;
double g_provider_latitude = 0.0;
double g_provider_longitude = 0.0;

/// @brief Returns a short provider label for temporary serial diagnostics.
const char* active_weather_source_log_name()
{
    switch (g_active_weather_source)
    {
    case WeatherSource::HomeAssistant:
        return "HA";
    case WeatherSource::OpenMeteo:
    case WeatherSource::MetNorway:
        return "Open-Meteo";
    }

    return "?";
}

/// @brief Emits focused serial diagnostics for direct weather bring-up.
void direct_weather_log(const char* format, ...)
{
    if (!kEnableDirectWeatherSerialDiagnostics ||
        g_active_weather_source == WeatherSource::HomeAssistant || format == nullptr)
    {
        return;
    }

    std::printf("WX %s: ", active_weather_source_log_name());
    va_list args;
    va_start(args, format);
    std::vprintf(format, args);
    va_end(args);
}

/// @brief Returns whether the current request kind should drive connection-state updates.
bool request_updates_connection_state()
{
    if (g_active_weather_source == WeatherSource::HomeAssistant)
    {
        return g_request_kind == RequestKind::ProbeApi;
    }

    return g_request_kind == RequestKind::FetchWeatherEntity;
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

/// @brief Returns retry delay for the active source after a failed request.
uint32_t active_failure_retry_ms()
{
    return g_active_weather_source == WeatherSource::HomeAssistant ? kRetryDelayMs
                                                                   : kDirectWeatherRetryDelayMs;
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
    case RequestKind::FetchWeatherEntity:
        return "weather";
    case RequestKind::FetchWeatherForecast:
        return "hourly";
    case RequestKind::FetchWeatherDailyForecast:
        return "daily";
    case RequestKind::FetchSunEntity:
        return "sun";
    case RequestKind::PublishSelfEntity:
        return "publish";
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

/// @brief Finds the value for a named HTTP response header.
const char* find_response_header_value(const char* header_name)
{
    return http_response::find_header_value(g_response, response_headers_end(), header_name);
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

/// @brief Updates sunrise and sunset display strings from the sun entity payload.
void update_sun_times_from_json(const char* json)
{
    home_assistant_weather::update_sun_times_from_json(json, g_status);
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

/// @brief Parses the hourly weather forecast response into display rows.
bool parse_hourly_forecast_response(const char* json)
{
    return weather_forecast::parse_hourly_forecast_response(json, g_status,
                                                             g_weather_temperature_unit,
                                                             g_weather_wind_source_unit);
}

/// @brief Parses a daily weather forecast response into week-mode rows.
bool parse_daily_forecast_response(const char* json)
{
    return weather_forecast::parse_daily_forecast_response(json, g_status,
                                                            g_weather_temperature_unit,
                                                            g_weather_wind_source_unit);
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
    if (g_active_weather_source != WeatherSource::HomeAssistant)
    {
        return (kind == RequestKind::FetchWeatherEntity) && (http_status == 200);
    }

    switch (kind)
    {
    case RequestKind::ProbeApi:
    case RequestKind::FetchTrackedEntity:
    case RequestKind::FetchWeatherEntity:
    case RequestKind::FetchWeatherForecast:
    case RequestKind::FetchWeatherDailyForecast:
    case RequestKind::FetchSunEntity:
        return http_status == 200;
    case RequestKind::PublishSelfEntity:
        return http_status == 200 || http_status == 201;
    }

    return false;
}

/// @brief Advances the serialized Home Assistant polling sequence to its next step.
void advance_request_kind()
{
    if (g_active_weather_source != WeatherSource::HomeAssistant)
    {
        g_request_kind = RequestKind::FetchWeatherEntity;
        g_sequence_complete = true;
        g_next_attempt = make_timeout_time_ms(kRefreshIntervalMs);
        return;
    }

    // The Home Assistant session is intentionally serialized: prove the API is
    // reachable first, then fetch optional entities in priority order, then
    // wait for the next refresh interval.
    switch (g_request_kind)
    {
    case RequestKind::ProbeApi:
        if (g_status.tracked_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchTrackedEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.weather_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchWeatherEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchTrackedEntity:
        if (g_status.weather_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::FetchWeatherEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchWeatherEntity:
        g_request_kind = RequestKind::FetchWeatherForecast;
        g_sequence_complete = false;
        g_next_attempt = get_absolute_time();
        return;
    case RequestKind::FetchWeatherForecast:
        g_request_kind = RequestKind::FetchWeatherDailyForecast;
        g_sequence_complete = false;
        g_next_attempt = get_absolute_time();
        return;
    case RequestKind::FetchWeatherDailyForecast:
        if (active_sun_entity_id()[0] != '\0')
        {
            g_request_kind = RequestKind::FetchSunEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::FetchSunEntity:
        if (g_status.self_entity_id[0] != '\0')
        {
            g_request_kind = RequestKind::PublishSelfEntity;
            g_sequence_complete = false;
            g_next_attempt = get_absolute_time();
            return;
        }
        break;
    case RequestKind::PublishSelfEntity:
        break;
    }

    g_request_kind = RequestKind::ProbeApi;
    g_sequence_complete = true;
    g_next_attempt = make_timeout_time_ms(kRefreshIntervalMs);
}

/// @brief Parses and normalizes the configured Home Assistant endpoint.
bool parse_home_assistant_endpoint()
{
    g_configured_host[0] = '\0';
    active_weather_coordinate_signature(g_configured_location, sizeof(g_configured_location));
    g_configured_port = active_home_assistant_port();
    g_active_weather_source = active_weather_source();
    g_request_uses_tls = false;

    if (g_active_weather_source == WeatherSource::OpenMeteo)
    {
        if (!parse_location_coordinates(&g_provider_latitude, &g_provider_longitude))
        {
            std::printf(
                "Weather config requires direct weather coordinates for Open-Meteo; got '%s'\n",
                g_configured_location);
            return false;
        }

        std::snprintf(g_configured_host, sizeof(g_configured_host), "%s", kOpenMeteoHost);
        g_configured_port = kOpenMeteoPort;
        direct_weather_log("config coords=%.4f,%.4f host=%s port=%u tls=%u\n", g_provider_latitude,
                           g_provider_longitude, g_configured_host,
                           static_cast<unsigned>(g_configured_port), g_request_uses_tls ? 1U : 0U);
        return true;
    }

    // Endpoint normalisation happens once at init because this client supports
    // host[:port] targets with optional http/https schemes, not arbitrary base
    // paths.
    if (!active_home_assistant_enabled() || active_home_assistant_host()[0] == '\0')
    {
        return false;
    }

    const char* host_start = active_home_assistant_host();
    if (std::strncmp(host_start, kHttpSchemePrefix, kHttpSchemePrefixLength) == 0)
    {
        host_start += kHttpSchemePrefixLength;
    }
    else if (std::strncmp(host_start, kHttpsSchemePrefix, kHttpsSchemePrefixLength) == 0)
    {
        host_start += kHttpsSchemePrefixLength;
        g_request_uses_tls = true;
    }

    const char* host_end = nullptr;
    if (!text_utils::parse_host_and_optional_port(host_start, g_configured_host,
                                                   sizeof(g_configured_host), &g_configured_port,
                                                   &host_end))
    {
        std::printf("HA config host/port is invalid: %s\n", host_start);
        return false;
    }

    if (*host_end == '/' && std::strcmp(host_end, "/") != 0)
    {
        std::printf("HA config should not include a path; using host root only\n");
    }

    return true;
}

/// @brief Updates the public Home Assistant status snapshot.
void set_status(HomeAssistantConnectionState state, int last_error, int last_http_status)
{
    g_status.state = state;
    g_status.last_error = last_error;
    g_status.last_http_status = last_http_status;
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
    g_request_sent = 0;
    g_response_len = 0;
    g_request_body[0] = '\0';
    g_response[0] = '\0';
    g_deadline = nil_time;
}

/// @brief Finishes the current request and schedules the next retry or refresh.
void finish_request(HomeAssistantConnectionState state, int last_error, int last_http_status,
                    uint32_t retry_ms)
{
    set_status(state, last_error, last_http_status);
    g_next_attempt = (retry_ms > 0) ? make_timeout_time_ms(retry_ms) : nil_time;
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
    case RequestKind::FetchWeatherEntity:
        g_status.weather_source_hint.fill('\0');
        g_status.weather_condition.fill('\0');
        g_status.weather_temperature.fill('\0');
        g_status.weather_wind_unit.fill('\0');
        g_weather_wind_source_unit[0] = '\0';
        break;
    case RequestKind::FetchWeatherForecast:
        if (!parse_hourly_forecast_response(response_body()))
        {
            clear_weather_forecast();
        }
        break;
    case RequestKind::FetchWeatherDailyForecast:
        if (!parse_daily_forecast_response(response_body()))
        {
            clear_weather_daily_forecast();
        }
        break;
    case RequestKind::FetchSunEntity:
        g_status.sunrise_text.fill('\0');
        g_status.sunset_text.fill('\0');
        break;
    case RequestKind::PublishSelfEntity:
        g_status.self_entity_published = false;
        break;
    case RequestKind::ProbeApi:
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

    if (g_active_weather_source == WeatherSource::OpenMeteo)
    {
        if (g_request_kind != RequestKind::FetchWeatherEntity)
        {
            g_request_kind = RequestKind::FetchWeatherEntity;
        }

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
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }

        target = target_buffer;
    }
    // Each request kind maps to one explicit REST call so the receive path can
    // infer how to parse the response from the current state machine position.
    if (g_active_weather_source == WeatherSource::HomeAssistant &&
        g_request_kind == RequestKind::FetchTrackedEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.tracked_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_active_weather_source == WeatherSource::HomeAssistant &&
             g_request_kind == RequestKind::FetchWeatherEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.weather_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_active_weather_source == WeatherSource::HomeAssistant &&
             g_request_kind == RequestKind::FetchWeatherForecast)
    {
        method = "POST";
        target = "/api/services/weather/get_forecasts?return_response";

        const int body_len = std::snprintf(g_request_body, sizeof(g_request_body),
                                           "{\"entity_id\":\"%s\",\"type\":\"hourly\"}",
                                           g_status.weather_entity_id.data());
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
    }
    else if (g_active_weather_source == WeatherSource::HomeAssistant &&
             g_request_kind == RequestKind::FetchWeatherDailyForecast)
    {
        method = "POST";
        target = "/api/services/weather/get_forecasts?return_response";

        const int body_len = std::snprintf(g_request_body, sizeof(g_request_body),
                                           "{\"entity_id\":\"%s\",\"type\":\"daily\"}",
                                           g_status.weather_entity_id.data());
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
    }
    else if (g_active_weather_source == WeatherSource::HomeAssistant &&
             g_request_kind == RequestKind::FetchSunEntity)
    {
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             active_sun_entity_id());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }
    else if (g_active_weather_source == WeatherSource::HomeAssistant &&
             g_request_kind == RequestKind::PublishSelfEntity)
    {
        method = "POST";
        const int target_len = std::snprintf(target_buffer, sizeof(target_buffer), "/api/states/%s",
                                             g_status.self_entity_id.data());
        if (target_len <= 0 || static_cast<size_t>(target_len) >= sizeof(target_buffer))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }

        const int body_len =
            std::snprintf(g_request_body, sizeof(g_request_body),
                          "{\"state\":\"online\",\"attributes\":{\"friendly_name\":\"MerlinCCU\","
                          "\"integration\":\"rest_api\",\"status\":\"connected\"}}");
        if (body_len <= 0 || static_cast<size_t>(body_len) >= sizeof(g_request_body))
        {
            set_status(HomeAssistantConnectionState::Error, -1, 0);
            g_sequence_complete = true;
            g_next_attempt = nil_time;
            return false;
        }
        target = target_buffer;
    }

    // The request buffer is assembled as one contiguous HTTP message because
    // the send path may need to write it in multiple TCP chunks later.
    int len = 0;
    if (g_active_weather_source == WeatherSource::HomeAssistant)
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
        set_status(HomeAssistantConnectionState::Error, -1, 0);
        g_sequence_complete = true;
        g_next_attempt = nil_time;
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
        g_sequence_complete = true;
        finish_request(HomeAssistantConnectionState::Error, err, 0, active_failure_retry_ms());
        std::printf("HA connect failed err=%d\n", static_cast<int>(err));
        return ERR_OK;
    }

    direct_weather_log("connect callback ok tls=%u\n", g_request_uses_tls ? 1U : 0U);
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
    set_status(HomeAssistantConnectionState::Error, err, g_status.last_http_status);
    g_sequence_complete = true;
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_response[0] = '\0';
    g_deadline = nil_time;
    g_next_attempt = make_timeout_time_ms(active_failure_retry_ms());
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

    if (g_active_weather_source == WeatherSource::OpenMeteo)
    {
        if (http_status == 200 && parse_open_meteo_weather(response_body()))
        {
            copy_text(g_status.weather_source_hint, "Open-Meteo");
            g_status.last_http_status = http_status;
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
        copy_text(g_status.weather_condition, "No data");
        g_status.weather_temperature.fill('\0');
        clear_current_weather_metrics();
        clear_weather_alert_status();
        g_weather_temperature_unit = '\0';
        g_weather_wind_source_unit[0] = '\0';
        g_status.last_http_status = http_status;
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
        else if (g_request_kind == RequestKind::FetchWeatherEntity)
        {
            if (home_assistant_weather::parse_current_weather_entity(
                    response_body(), g_status, g_weather_temperature_unit,
                    g_weather_wind_source_unit, sizeof(g_weather_wind_source_unit)))
            {
                g_status.weather_last_success_ms = to_ms_since_boot(get_absolute_time());
            }

            PERIODIC_LOG("HA weather %s=%s %s\n", g_status.weather_entity_id.data(),
                         g_status.weather_condition[0] ? g_status.weather_condition.data() : "?",
                         g_status.weather_temperature[0] ? g_status.weather_temperature.data()
                                                         : "-");
        }
        else if (g_request_kind == RequestKind::FetchWeatherForecast)
        {
            if (parse_hourly_forecast_response(response_body()))
            {
                PERIODIC_LOG("HA hourly forecast %s count=%u\n", g_status.weather_entity_id.data(),
                             static_cast<unsigned>(g_status.weather_forecast_count));
            }
            else
            {
                clear_weather_forecast();
                PERIODIC_LOG("HA hourly forecast parse failed %s\n",
                             g_status.weather_entity_id.data());
            }
        }
        else if (g_request_kind == RequestKind::FetchWeatherDailyForecast)
        {
            if (parse_daily_forecast_response(response_body()))
            {
                PERIODIC_LOG("HA daily forecast %s count=%u\n", g_status.weather_entity_id.data(),
                             static_cast<unsigned>(g_status.weather_daily_forecast_count));
            }
            else
            {
                clear_weather_daily_forecast();
                PERIODIC_LOG("HA daily forecast parse failed %s\n",
                             g_status.weather_entity_id.data());
            }
        }
        else if (g_request_kind == RequestKind::FetchSunEntity)
        {
            update_sun_times_from_json(response_body());
            PERIODIC_LOG("HA sun %s rise=%s set=%s\n", active_sun_entity_id(),
                         g_status.sunrise_text[0] ? g_status.sunrise_text.data() : "-",
                         g_status.sunset_text[0] ? g_status.sunset_text.data() : "-");
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
        g_sequence_complete = true;
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

    if (g_request_kind == RequestKind::FetchWeatherEntity)
    {
        g_status.weather_source_hint.fill('\0');
        g_status.weather_condition.fill('\0');
        g_status.weather_temperature.fill('\0');
        g_status.weather_wind_unit.fill('\0');
        clear_current_weather_metrics();
        clear_weather_alert_status();
        g_weather_temperature_unit = '\0';
        g_weather_wind_source_unit[0] = '\0';
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA weather entity unavailable %s status=%d\n",
                     g_status.weather_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchWeatherForecast)
    {
        clear_weather_forecast();
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA hourly forecast unavailable %s status=%d\n",
                     g_status.weather_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchWeatherDailyForecast)
    {
        clear_weather_daily_forecast();
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA daily forecast unavailable %s status=%d\n",
                     g_status.weather_entity_id.data(), http_status);
        return;
    }

    if (g_request_kind == RequestKind::FetchSunEntity)
    {
        g_status.sunrise_text.fill('\0');
        g_status.sunset_text.fill('\0');
        reset_attempt_state();
        set_status(HomeAssistantConnectionState::Connected, 0, http_status);
        advance_request_kind();
        PERIODIC_LOG("HA sun entity unavailable %s status=%d\n", active_sun_entity_id(),
                     http_status);
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
    g_sequence_complete = true;
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
        g_sequence_complete = true;
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
                g_sequence_complete = true;
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
            g_sequence_complete = true;
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
            g_sequence_complete = true;
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
        g_sequence_complete = true;
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

    if (g_status.state == HomeAssistantConnectionState::Authorizing &&
        g_request_sent < std::strlen(g_request))
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
        g_sequence_complete = true;
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
        g_sequence_complete = true;
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

    g_sequence_complete = true;
    direct_weather_log("altcp_connect failed err=%d\n", static_cast<int>(rc));
    finish_request(HomeAssistantConnectionState::Error, rc, 0, active_failure_retry_ms());
    std::printf("HA connect start failed err=%d\n", static_cast<int>(rc));
    return false;
}

/// @brief Starts the next Home Assistant request in the polling sequence.
bool start_probe()
{
    g_probe_attempted = true;
    reset_attempt_state();
    g_status.last_error = 0;
    if (request_updates_connection_state())
    {
        g_status.last_http_status = 0;
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

    g_sequence_complete = true;
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
    g_config_valid = parse_home_assistant_endpoint();
    g_status = {};
    g_status.configured = false;
    if (g_active_weather_source == WeatherSource::HomeAssistant)
    {
        g_status.configured = active_home_assistant_enabled() && g_config_valid &&
                              active_home_assistant_token()[0] != '\0';
    }
    else
    {
        g_status.configured = g_config_valid;
    }

    copy_text(g_status.host, g_configured_host);
    if (g_active_weather_source == WeatherSource::HomeAssistant)
    {
        copy_text(g_status.tracked_entity_id, active_tracked_entity_id());
        copy_text(g_status.weather_entity_id, active_weather_entity_id());
        copy_text(g_status.self_entity_id, active_self_entity_id());
    }
    else
    {
        g_status.tracked_entity_id.fill('\0');
        copy_text(g_status.weather_entity_id, g_configured_location);
        g_status.self_entity_id.fill('\0');
    }
    clear_runtime_data();
    if (g_active_weather_source != WeatherSource::HomeAssistant)
    {
        copy_text(g_status.weather_source_hint, "Open-Meteo");
    }
    g_status.last_error = 0;
    g_status.last_http_status = 0;
    g_status.state = !g_status.configured ? HomeAssistantConnectionState::Unconfigured
                                          : (kHomeAssistantRuntimeEnabled
                                                 ? HomeAssistantConnectionState::WaitingForWifi
                                                 : HomeAssistantConnectionState::Disabled);
    reset_attempt_state();
    g_next_attempt = nil_time;
    g_probe_attempted = false;
    g_sequence_complete = false;
    g_request_kind = (g_active_weather_source == WeatherSource::HomeAssistant)
                         ? RequestKind::ProbeApi
                         : RequestKind::FetchWeatherEntity;
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

/// @brief Advances the Home Assistant resolve/connect/request state machine.
bool update(const WifiStatus& wifi_status)
{
    const HomeAssistantStatus previous = g_status;
    if (runtime_weather_config_changed())
    {
        init();
        return previous != g_status;
    }

    // Handle disabled and unconfigured cases first so the remaining logic can
    // assume Home Assistant is supposed to be active.
    if (!g_status.configured)
    {
        g_status.state = HomeAssistantConnectionState::Unconfigured;
        return previous.state != g_status.state || previous.configured != g_status.configured;
    }

    if (!kHomeAssistantRuntimeEnabled)
    {
        g_status.state = HomeAssistantConnectionState::Disabled;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        clear_runtime_data();
        reset_attempt_state();
        g_next_attempt = nil_time;
        g_probe_attempted = false;
        g_sequence_complete = false;
        g_request_kind = (g_active_weather_source == WeatherSource::HomeAssistant)
                             ? RequestKind::ProbeApi
                             : RequestKind::FetchWeatherEntity;
        return previous != g_status;
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready)
    {
        reset_attempt_state();
        g_status.state = HomeAssistantConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        clear_runtime_data();
        g_next_attempt = nil_time;
        g_probe_attempted = false;
        g_sequence_complete = false;
        g_request_kind = (g_active_weather_source == WeatherSource::HomeAssistant)
                             ? RequestKind::ProbeApi
                             : RequestKind::FetchWeatherEntity;
        return previous != g_status;
    }

    // Resolver and socket operations are asynchronous, so deadlines are checked
    // here in the update loop rather than buried inside lwIP callbacks.
    if (g_dns_pending && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        direct_weather_log("dns timeout host=%s\n", g_configured_host);
        g_dns_pending = false;
        g_sequence_complete = true;
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
        g_sequence_complete = true;
        const int timeout_http_status =
            g_status.last_http_status > 0 ? g_status.last_http_status : partial_http_status();
        direct_weather_log("io timeout state=%u sent=%u recv=%u partial_http=%d retry_ms=%u\n",
                           static_cast<unsigned>(g_status.state),
                           static_cast<unsigned>(g_request_sent),
                           static_cast<unsigned>(g_response_len), timeout_http_status,
                           static_cast<unsigned>(active_failure_retry_ms()));
        finish_request(HomeAssistantConnectionState::Error, ERR_TIMEOUT, timeout_http_status,
                       active_failure_retry_ms());
    }

    // New requests are only launched when the current step is idle and either
    // the initial probe has not happened yet or the retry/refresh timer expired.
    if (!g_dns_pending && g_pcb == nullptr &&
        (!g_probe_attempted || (!is_nil_time(g_next_attempt) &&
                                absolute_time_diff_us(get_absolute_time(), g_next_attempt) <= 0)))
    {
        g_next_attempt = nil_time;
        start_probe();
    }

    return previous != g_status;
}

const HomeAssistantStatus& status()
{
    return g_status;
}

} // namespace home_assistant_manager
