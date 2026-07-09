#include "air_traffic_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config_manager.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "text_utils.h"

namespace air_traffic_manager
{

namespace
{

constexpr uint32_t kResolveTimeoutMs = 4000U;
constexpr uint32_t kConnectTimeoutMs = 5000U;
constexpr uint32_t kIoTimeoutMs = 6000U;
constexpr uint32_t kRetryDelayMs = 20U * 1000U;
// Aircraft positions update roughly every second at the source, but there is
// no need to refresh that fast for a small onboard display -- see
// docs/adsb-air-traffic-feature-design.md for the reasoning.
constexpr uint32_t kRefreshIntervalMs = 15U * 1000U;
constexpr size_t kRequestBufferSize = 320U;
// Bounded so a busy airspace + large configured radius cannot grow this
// without limit. If the real response doesn't fit, that fetch cycle simply
// fails and retries -- the same "truncation is failure" tradeoff already
// accepted elsewhere in this codebase (see share_price_manager.cpp), not a
// new one introduced here.
constexpr size_t kResponseBufferSize = 8192U;
constexpr uint16_t kDefaultPort = 80U;

AirTrafficStatus g_status = {};
char g_configured_host[64] = {};
uint16_t g_configured_port = kDefaultPort;
char g_configured_api_key[128] = {};
double g_home_latitude = 0.0;
double g_home_longitude = 0.0;
uint16_t g_configured_radius_nm = 30U;
bool g_config_valid = false;

ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
altcp_pcb* g_pcb = nullptr;
size_t g_request_sent = 0U;
size_t g_response_len = 0U;
char g_request[kRequestBufferSize] = {};
char g_response[kResponseBufferSize] = {};
absolute_time_t g_deadline = nil_time;
absolute_time_t g_next_attempt = nil_time;
bool g_request_attempted = false;
bool g_completion_pending = false;
bool g_completion_success = false;
int g_completion_http_status = 0;
err_t g_completion_error = ERR_OK;

using text_utils::copy_text;

/// @brief One nearby aircraft parsed from the provider response.
struct Candidate
{
    char callsign[16];
    double distance_nm;
    double bearing_deg;
    double altitude_ft;
    bool on_ground;
    bool has_altitude;
};

/// @brief Scans forward for the next signed decimal number in `*cursor`.
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

/// @brief Parses a free-text "lat,lon" home-location string.
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

/// @brief Re-reads config and returns false if the feature isn't usable yet.
bool refresh_config()
{
    const RuntimeConfig& config = config_manager::settings();
    if (!config.air_traffic_enabled || config.air_traffic_host[0] == '\0')
    {
        return false;
    }

    if (!parse_coordinates_text(config.air_traffic_coordinates.data(), &g_home_latitude,
                                &g_home_longitude))
    {
        return false;
    }

    std::snprintf(g_configured_host, sizeof(g_configured_host), "%s",
                  config.air_traffic_host.data());
    g_configured_port = (config.air_traffic_port != 0U) ? config.air_traffic_port : kDefaultPort;
    std::snprintf(g_configured_api_key, sizeof(g_configured_api_key), "%s",
                  config.air_traffic_api_key.data());
    g_configured_radius_nm = (config.air_traffic_radius_nm != 0U) ? config.air_traffic_radius_nm : 30U;
    return true;
}

/// @brief Returns whether the active config differs from what was last used.
bool runtime_config_changed()
{
    const RuntimeConfig& config = config_manager::settings();
    const bool was_configured = g_config_valid;
    const bool now_configured =
        config.air_traffic_enabled && config.air_traffic_host[0] != '\0' &&
        config.air_traffic_coordinates[0] != '\0';
    if (was_configured != now_configured)
    {
        return true;
    }
    if (!now_configured)
    {
        return false;
    }

    return std::strcmp(g_configured_host, config.air_traffic_host.data()) != 0 ||
           g_configured_port != config.air_traffic_port ||
           std::strcmp(g_configured_api_key, config.air_traffic_api_key.data()) != 0 ||
           g_configured_radius_nm != config.air_traffic_radius_nm;
}

/// @brief Removes callbacks from the current TCP control block before close/abort.
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

/// @brief Closes the active socket, if any.
void close_pcb()
{
    if (g_pcb == nullptr)
    {
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
}

/// @brief Resets one in-flight fetch attempt while preserving the last snapshot.
void reset_attempt_state()
{
    close_pcb();
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0U;
    g_response_len = 0U;
    g_request[0] = '\0';
    g_response[0] = '\0';
    g_deadline = nil_time;
    g_completion_pending = false;
    g_completion_success = false;
    g_completion_http_status = 0;
    g_completion_error = ERR_OK;
}

/// @brief Schedules the next fetch attempt after a bounded delay.
void schedule_next_attempt(uint32_t delay_ms)
{
    g_next_attempt = make_timeout_time_ms(delay_ms);
}

/// @brief Returns true when the retry/refresh timer allows a new request.
bool request_due()
{
    return !g_request_attempted ||
           (!is_nil_time(g_next_attempt) &&
            absolute_time_diff_us(get_absolute_time(), g_next_attempt) <= 0);
}

/// @brief Extracts the numeric HTTP status from the response head.
int partial_http_status()
{
    if (g_response_len < 12U)
    {
        return 0;
    }

    const char* http = std::strstr(g_response, "HTTP/1.");
    if (http == nullptr)
    {
        return 0;
    }

    const char* status_start = std::strchr(http, ' ');
    if (status_start == nullptr)
    {
        return 0;
    }

    return std::atoi(status_start + 1);
}

/// @brief Returns the start of the HTTP response body, if the headers are complete.
const char* response_body()
{
    const char* separator = std::strstr(g_response, "\r\n\r\n");
    return separator == nullptr ? nullptr : separator + 4;
}

/// @brief Returns true when the received headers announce chunked transfer encoding.
bool response_uses_chunked_encoding()
{
    const char* header_end = std::strstr(g_response, "\r\n\r\n");
    const char* chunked = std::strstr(g_response, "Transfer-Encoding: chunked");
    return chunked != nullptr && (header_end == nullptr || chunked < header_end);
}

/// @brief Returns true when the final zero-length HTTP chunk has arrived.
bool chunked_response_complete()
{
    const char* body = response_body();
    return body != nullptr && response_uses_chunked_encoding() &&
           std::strstr(body, "\r\n0\r\n") != nullptr;
}

/// @brief Skips JSON whitespace and returns the next meaningful character.
const char* skip_json_space(const char* cursor)
{
    while (cursor != nullptr &&
           (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n'))
    {
        ++cursor;
    }

    return cursor;
}

/// @brief Finds the first occurrence of `key` within `[start, end)`.
const char* find_bounded(const char* start, const char* end, const char* key)
{
    const size_t key_len = std::strlen(key);
    if (key_len == 0U || end <= start)
    {
        return nullptr;
    }

    const size_t range_len = static_cast<size_t>(end - start);
    if (key_len > range_len)
    {
        return nullptr;
    }

    for (const char* p = start; p <= end - key_len; ++p)
    {
        if (std::strncmp(p, key, key_len) == 0)
        {
            return p;
        }
    }

    return nullptr;
}

/// @brief Extracts one bounded JSON string scalar by key.
bool extract_bounded_string(const char* start, const char* end, const char* key, char* out,
                            size_t out_size)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_json_space(cursor + 1);
    if (cursor == nullptr || cursor >= end || *cursor != '"')
    {
        return false;
    }

    ++cursor;
    size_t write_index = 0U;
    while (cursor < end && *cursor != '"' && write_index + 1U < out_size)
    {
        if (*cursor == '\\' && (cursor + 1) < end)
        {
            ++cursor;
        }
        out[write_index++] = *cursor++;
    }

    out[write_index] = '\0';
    return write_index > 0U;
}

/// @brief Returns true when `key`'s value is a quoted string (e.g. adsb.lol's
/// `"alt_baro":"ground"` convention for grounded aircraft).
bool bounded_value_is_string(const char* start, const char* end, const char* key)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_json_space(cursor + 1);
    return cursor != nullptr && cursor < end && *cursor == '"';
}

/// @brief Extracts one bounded JSON numeric scalar by key.
bool extract_bounded_number(const char* start, const char* end, const char* key, double* out_value)
{
    const char* cursor = find_bounded(start, end, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    cursor = skip_json_space(cursor + 1);
    if (cursor == nullptr || cursor >= end)
    {
        return false;
    }

    char* parse_end = nullptr;
    const double value = std::strtod(cursor, &parse_end);
    if (parse_end == cursor)
    {
        return false;
    }

    *out_value = value;
    return true;
}

/// @brief Finds the closing `}` matching the `{` at `obj_start`, string-aware
/// so a brace inside a quoted value cannot desync the depth count.
const char* find_object_end(const char* obj_start, const char* buffer_end)
{
    if (obj_start >= buffer_end || *obj_start != '{')
    {
        return nullptr;
    }

    int depth = 0;
    bool in_string = false;
    for (const char* p = obj_start; p < buffer_end; ++p)
    {
        if (in_string)
        {
            if (*p == '\\' && (p + 1) < buffer_end)
            {
                ++p;
                continue;
            }
            if (*p == '"')
            {
                in_string = false;
            }
            continue;
        }

        if (*p == '"')
        {
            in_string = true;
        }
        else if (*p == '{')
        {
            ++depth;
        }
        else if (*p == '}')
        {
            --depth;
            if (depth == 0)
            {
                return p;
            }
        }
    }

    return nullptr;
}

/// @brief Inserts `candidate` into the bounded closest-N-by-distance array.
void insert_candidate(std::array<Candidate, kAirTrafficEntryCapacity>& best, uint8_t& count,
                      const Candidate& candidate)
{
    if (count < best.size())
    {
        best[count] = candidate;
        ++count;
    }
    else if (candidate.distance_nm < best[count - 1U].distance_nm)
    {
        best[count - 1U] = candidate;
    }
    else
    {
        return;
    }

    for (uint8_t i = count; i > 1U && best[i - 1U].distance_nm < best[i - 2U].distance_nm; --i)
    {
        std::swap(best[i - 1U], best[i - 2U]);
    }
}

/// @brief Parses the `"ac":[...]` aircraft array into the closest-N entries.
bool parse_aircraft_response()
{
    const char* body = response_body();
    if (body == nullptr)
    {
        return false;
    }

    const char* array_marker = std::strstr(body, "\"ac\":[");
    if (array_marker == nullptr)
    {
        return false;
    }

    const char* buffer_end = g_response + g_response_len;
    const char* cursor = array_marker + std::strlen("\"ac\":[");
    std::array<Candidate, kAirTrafficEntryCapacity> best = {};
    uint8_t best_count = 0U;

    while (cursor < buffer_end)
    {
        cursor = skip_json_space(cursor);
        if (cursor >= buffer_end || *cursor == ']')
        {
            break;
        }
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (*cursor != '{')
        {
            break;
        }

        const char* obj_end = find_object_end(cursor, buffer_end);
        if (obj_end == nullptr)
        {
            // Truncated final object (response didn't fit) -- stop here and
            // keep whatever closer aircraft were already found.
            break;
        }

        double dst = 0.0;
        if (extract_bounded_number(cursor, obj_end, "\"dst\"", &dst))
        {
            Candidate candidate = {};
            candidate.distance_nm = dst;

            double dir = 0.0;
            extract_bounded_number(cursor, obj_end, "\"dir\"", &dir);
            candidate.bearing_deg = dir;

            char callsign[16] = {};
            if (extract_bounded_string(cursor, obj_end, "\"flight\"", callsign, sizeof(callsign)))
            {
                size_t len = std::strlen(callsign);
                while (len > 0U && callsign[len - 1U] == ' ')
                {
                    callsign[--len] = '\0';
                }
            }
            if (callsign[0] == '\0')
            {
                extract_bounded_string(cursor, obj_end, "\"hex\"", callsign, sizeof(callsign));
            }
            std::snprintf(candidate.callsign, sizeof(candidate.callsign), "%s",
                          callsign[0] != '\0' ? callsign : "?");

            if (bounded_value_is_string(cursor, obj_end, "\"alt_baro\""))
            {
                candidate.on_ground = true;
                candidate.has_altitude = false;
            }
            else
            {
                double altitude = 0.0;
                candidate.has_altitude =
                    extract_bounded_number(cursor, obj_end, "\"alt_baro\"", &altitude);
                candidate.altitude_ft = altitude;
            }

            insert_candidate(best, best_count, candidate);
        }

        cursor = obj_end + 1;
    }

    g_status.aircraft_count = best_count;
    char text[16] = {};
    for (uint8_t i = 0U; i < best_count; ++i)
    {
        AirTrafficEntry& entry = g_status.aircraft[i];
        copy_text(entry.callsign, best[i].callsign);

        std::snprintf(text, sizeof(text), "%.1fnm", best[i].distance_nm);
        copy_text(entry.distance_text, text);

        if (best[i].on_ground)
        {
            copy_text(entry.altitude_text, "GND");
        }
        else if (best[i].has_altitude)
        {
            std::snprintf(text, sizeof(text), "%ldft", std::lround(best[i].altitude_ft));
            copy_text(entry.altitude_text, text);
        }
        else
        {
            copy_text(entry.altitude_text, "-");
        }

        const int bearing = ((static_cast<int>(std::lround(best[i].bearing_deg)) % 360) + 360) % 360;
        std::snprintf(text, sizeof(text), "%03d", bearing);
        copy_text(entry.bearing_text, text);
    }

    for (uint8_t i = best_count; i < kAirTrafficEntryCapacity; ++i)
    {
        g_status.aircraft[i] = {};
    }

    return true;
}

/// @brief Records a failed fetch without wiping the last displayed data.
void finish_failure(err_t err, int http_status)
{
    reset_attempt_state();
    g_status.data_valid = false;
    // lwIP defines err_t as signed char; preserving the negative protocol code
    // is the useful behaviour here, despite clang-tidy's generic char warning.
    // NOLINTNEXTLINE(bugprone-signed-char-misuse)
    g_status.last_error = static_cast<int>(err);
    g_status.last_http_status = http_status;
    schedule_next_attempt(kRetryDelayMs);
}

/// @brief Records a successful fetch and schedules the next refresh.
void finish_success(int http_status)
{
    reset_attempt_state();
    g_status.data_valid = true;
    g_status.last_error = 0;
    g_status.last_http_status = http_status;
    schedule_next_attempt(kRefreshIntervalMs);
}

/// @brief Defers request completion until the main update loop is back in control.
void defer_completion(bool success, err_t err, int http_status)
{
    g_completion_pending = true;
    g_completion_success = success;
    g_completion_error = err;
    g_completion_http_status = http_status;
}

/// @brief Builds the HTTP GET request for the configured point/radius query.
bool build_request()
{
    char auth_header[160] = {};
    if (g_configured_api_key[0] != '\0')
    {
        // adsb.lol does not require a key today; the exact header name for a
        // future gated tier is unconfirmed (see design doc's open questions),
        // so this is a best-effort placeholder that's easy to adjust later.
        std::snprintf(auth_header, sizeof(auth_header), "Api-Auth: %s\r\n", g_configured_api_key);
    }

    const int target_len = std::snprintf(
        g_request, sizeof(g_request),
        "GET /v2/point/%.6f/%.6f/%u HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: MerlinCCU/0.1 (https://github.com/victoriandad/MerlinCCU)\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        g_home_latitude, g_home_longitude, static_cast<unsigned>(g_configured_radius_nm),
        g_configured_host, auth_header);
    return target_len > 0 && static_cast<size_t>(target_len) < sizeof(g_request);
}

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
        defer_completion(false, err, 0);
        return ERR_OK;
    }

    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return try_send_request();
}

/// @brief Handles asynchronous lwIP connection errors.
void on_tcp_error(void* arg, err_t err)
{
    (void)arg;
    g_pcb = nullptr;
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0U;
    g_response_len = 0U;
    defer_completion(false, err, 0);
}

/// @brief Writes as much of the pending request as the TCP send buffer allows.
err_t try_send_request()
{
    if (g_pcb == nullptr)
    {
        return ERR_OK;
    }

    const size_t request_len = std::strlen(g_request);
    while (g_request_sent < request_len)
    {
        const u16_t available = altcp_sndbuf(g_pcb);
        if (available == 0U)
        {
            return ERR_OK;
        }

        const size_t remaining = request_len - g_request_sent;
        const size_t chunk = std::min(remaining, static_cast<size_t>(available));
        const err_t write_rc =
            altcp_write(g_pcb, g_request + g_request_sent, static_cast<u16_t>(chunk),
                        TCP_WRITE_FLAG_COPY |
                            ((g_request_sent + chunk < request_len) ? TCP_WRITE_FLAG_MORE : 0));
        if (write_rc != ERR_OK)
        {
            if (write_rc == ERR_MEM)
            {
                return ERR_OK;
            }
            defer_completion(false, write_rc, 0);
            return ERR_OK;
        }

        g_request_sent += chunk;
    }

    const err_t output_rc = altcp_output(g_pcb);
    if (output_rc != ERR_OK)
    {
        defer_completion(false, output_rc, 0);
    }
    return ERR_OK;
}

/// @brief Handles TCP send acknowledgements.
err_t on_tcp_sent(void* arg, altcp_pcb* pcb, u16_t len)
{
    (void)arg;
    (void)len;
    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    return try_send_request();
}

/// @brief Handles completed provider response data.
void handle_response_complete()
{
    defer_completion(true, ERR_OK, partial_http_status());
}

/// @brief Handles incoming response chunks and EOF.
err_t on_tcp_recv(void* arg, altcp_pcb* pcb, pbuf* p, err_t err)
{
    (void)arg;
    if (g_completion_pending)
    {
        if (p != nullptr)
        {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (pcb != g_pcb)
    {
        if (p != nullptr)
        {
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
        defer_completion(false, err, partial_http_status());
        return ERR_OK;
    }

    if (p == nullptr)
    {
        handle_response_complete();
        return ERR_OK;
    }

    altcp_recved(pcb, p->tot_len);

    const size_t received_len = static_cast<size_t>(p->tot_len);
    const size_t available = sizeof(g_response) - g_response_len - 1U;
    const size_t copy_len = std::min(received_len, available);
    if (copy_len > 0U)
    {
        pbuf_copy_partial(p, g_response + g_response_len, static_cast<u16_t>(copy_len), 0);
        g_response_len += copy_len;
        g_response[g_response_len] = '\0';
    }
    pbuf_free(p);

    if (copy_len < received_len)
    {
        defer_completion(false, ERR_BUF, partial_http_status());
        return ERR_OK;
    }
    if (chunked_response_complete())
    {
        handle_response_complete();
    }
    return ERR_OK;
}

/// @brief Retries send progress from lwIP poll callbacks.
err_t on_tcp_poll(void* arg, altcp_pcb* pcb)
{
    (void)arg;
    if (pcb != g_pcb)
    {
        return ERR_OK;
    }

    return try_send_request();
}

/// @brief DNS callback used by lwIP when hostname resolution completes asynchronously.
void dns_found(const char* name, const ip_addr_t* ipaddr, void* callback_arg)
{
    (void)name;
    (void)callback_arg;
    if (!g_dns_pending)
    {
        return;
    }

    if (ipaddr == nullptr)
    {
        g_dns_pending = false;
        defer_completion(false, ERR_TIMEOUT, 0);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_pending = false;
    g_dns_resolved = true;
}

/// @brief Opens the plain TCP socket and starts the request connection phase.
void start_socket_connect()
{
    if (!build_request())
    {
        finish_failure(ERR_VAL, 0);
        return;
    }

    cyw43_arch_lwip_begin();
    g_pcb = altcp_tcp_new_ip_type(IP_GET_TYPE(&g_resolved_ip));
    if (g_pcb == nullptr)
    {
        cyw43_arch_lwip_end();
        finish_failure(ERR_MEM, 0);
        return;
    }

    altcp_arg(g_pcb, nullptr);
    altcp_recv(g_pcb, on_tcp_recv);
    altcp_sent(g_pcb, on_tcp_sent);
    altcp_err(g_pcb, on_tcp_error);
    altcp_poll(g_pcb, on_tcp_poll, 2);
    g_deadline = make_timeout_time_ms(kConnectTimeoutMs);

    const err_t connect_rc = altcp_connect(g_pcb, &g_resolved_ip, g_configured_port, on_tcp_connected);
    cyw43_arch_lwip_end();
    if (connect_rc != ERR_OK)
    {
        finish_failure(connect_rc, 0);
    }
}

/// @brief Starts a new provider DNS lookup.
void start_request()
{
    reset_attempt_state();
    g_request_attempted = true;
    g_next_attempt = nil_time;
    g_deadline = make_timeout_time_ms(kResolveTimeoutMs);

    cyw43_arch_lwip_begin();
    const err_t dns_rc = dns_gethostbyname(g_configured_host, &g_resolved_ip, dns_found, nullptr);
    cyw43_arch_lwip_end();
    if (dns_rc == ERR_OK)
    {
        g_dns_resolved = true;
        g_dns_pending = false;
        return;
    }
    if (dns_rc == ERR_INPROGRESS)
    {
        g_dns_pending = true;
        return;
    }

    defer_completion(false, dns_rc, 0);
}

} // namespace

void init()
{
    g_status = {};
    g_status.configured = false;
    g_status.data_valid = false;
    g_config_valid = false;
    reset_attempt_state();
    g_next_attempt = nil_time;
    g_request_attempted = false;
}

bool update(const WifiStatus& wifi_status, bool fetch_enabled)
{
    const AirTrafficStatus previous = g_status;

    if (runtime_config_changed())
    {
        init();
    }

    g_config_valid = refresh_config();
    g_status.configured = g_config_valid;
    if (!g_config_valid)
    {
        if (g_pcb != nullptr || g_dns_pending || g_completion_pending || g_request_attempted)
        {
            reset_attempt_state();
            g_request_attempted = false;
            g_next_attempt = nil_time;
        }
        g_status.data_valid = false;
        return previous != g_status;
    }

    // Isolate network traffic to the page that actually shows it, matching
    // the pattern already used for share-price fetches.
    if (!fetch_enabled)
    {
        const bool had_active_attempt = g_pcb != nullptr || g_dns_pending || g_dns_resolved ||
                                        g_completion_pending || g_request_attempted ||
                                        g_request_sent != 0U || g_response_len != 0U ||
                                        !is_nil_time(g_next_attempt);
        if (had_active_attempt)
        {
            reset_attempt_state();
            g_request_attempted = false;
            g_next_attempt = nil_time;
        }
        return previous != g_status;
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready)
    {
        reset_attempt_state();
        return previous != g_status;
    }

    if (g_completion_pending)
    {
        const int completed_http_status = g_completion_http_status;
        if (g_completion_success && completed_http_status == 200 && parse_aircraft_response())
        {
            finish_success(completed_http_status);
        }
        else if (g_completion_success)
        {
            finish_failure(ERR_VAL, completed_http_status);
        }
        else
        {
            finish_failure(g_completion_error, completed_http_status);
        }
        return previous != g_status;
    }

    if (g_dns_pending && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        g_dns_pending = false;
        finish_failure(ERR_TIMEOUT, 0);
    }

    if (g_dns_resolved && g_pcb == nullptr)
    {
        g_dns_resolved = false;
        start_socket_connect();
    }

    if (g_pcb != nullptr && !is_nil_time(g_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0)
    {
        finish_failure(ERR_TIMEOUT, partial_http_status());
    }

    if (!g_dns_pending && g_pcb == nullptr && request_due())
    {
        start_request();
    }

    return previous != g_status;
}

const AirTrafficStatus& status()
{
    return g_status;
}

} // namespace air_traffic_manager
