#include "share_price_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "config_manager.h"
#include "http_response.h"
#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "text_utils.h"

namespace share_price_manager
{

namespace
{

constexpr uint32_t kResolveTimeoutMs = 4000U;
constexpr uint32_t kConnectTimeoutMs = 5000U;
constexpr uint32_t kIoTimeoutMs = 5000U;
constexpr uint32_t kRetryDelayMs = 60U * 1000U;
constexpr uint32_t kRefreshIntervalMs = 5U * 60U * 1000U;
constexpr size_t kRequestBufferSize = 512U;
// The local feed's compact multi-share JSON (issue #42) is far smaller than
// Yahoo's single-symbol chart payload was; shrunk from 16KB to reclaim
// static RAM -- see docs/architecture.md's Memory budget tracking, headroom
// is tight.
constexpr size_t kResponseBufferSize = 4096U;
constexpr size_t kMaxParsedHistoryValues = 256U;
constexpr uint16_t kDefaultPort = 8123U; // Home Assistant's default port

ShareMarketStatus g_status = {};
char g_configured_host[64] = {};
uint16_t g_configured_port = kDefaultPort;
char g_configured_token[128] = {};
uint8_t g_configured_share_count = 0U;
std::array<WatchedShareConfig, kMaxWatchedShares> g_configured_watched_shares = {};
bool g_config_valid = false;

ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
altcp_pcb* g_pcb = nullptr;
size_t g_request_sent = 0U;
size_t g_response_len = 0U;
char g_request[kRequestBufferSize] = {};
char g_response[kResponseBufferSize] = {};
std::array<uint16_t, kMaxParsedHistoryValues> g_history_parse_values = {};
absolute_time_t g_deadline = nil_time;
absolute_time_t g_next_attempt = nil_time;
bool g_request_attempted = false;
bool g_completion_pending = false;
bool g_completion_success = false;
int g_completion_http_status = 0;
err_t g_completion_error = ERR_OK;
SharePeriod g_inflight_period = SharePeriod::Today;

using text_utils::copy_text;

/// @brief Seeds graph history for the selected placeholder period.
void seed_placeholder_history(ShareWatchEntry& share, SharePeriod period)
{
    switch (period)
    {
    case SharePeriod::Today:
        share.history_points = {1362U, 1364U, 1361U, 1365U, 1363U, 1366U, 1362U, 1367U,
                                1365U, 1368U, 1364U, 1369U, 1367U, 1370U, 1366U, 1371U,
                                1368U, 1372U, 1369U, 1373U, 1370U, 1374U, 1371U, 1372U};
        break;
    case SharePeriod::Week:
        share.history_points = {1348U, 1320U, 1356U, 1312U, 1370U, 1296U, 1382U, 1278U,
                                1364U, 1304U, 1392U, 1268U, 1352U, 1318U, 1376U, 1288U,
                                1400U, 1308U, 1368U, 1326U, 1388U, 1336U, 1378U, 1372U};
        break;
    case SharePeriod::Month:
        share.history_points = {1380U, 1344U, 1292U, 1238U, 1180U, 1120U, 1066U, 1014U,
                                970U, 1004U, 1048U, 1102U, 1160U, 1216U, 1264U, 1310U,
                                1358U, 1394U, 1360U, 1322U, 1280U, 1318U, 1356U, 1372U};
        break;
    case SharePeriod::Year:
        share.history_points = {720U, 840U, 780U, 980U, 920U, 1180U, 1040U, 1320U,
                                1110U, 1460U, 980U, 1260U, 890U, 1510U, 1080U, 1340U,
                                760U, 1220U, 940U, 1430U, 1160U, 1540U, 1280U, 1372U};
        break;
    case SharePeriod::AllTime:
        share.history_points = {120U, 160U, 220U, 310U, 280U, 430U, 520U, 460U,
                                640U, 760U, 690U, 880U, 1040U, 980U, 1180U, 1360U,
                                1280U, 1480U, 1700U, 1540U, 1820U, 2060U, 1880U, 1372U};
        break;
    }
}

/// @brief Marks seeded share data as local placeholder data, not a live quote.
void seed_placeholder_quote(ShareWatchEntry& share)
{
    copy_text(share.price_text, "Demo");
    copy_text(share.change_text, "No live");
}

/// @brief Rebuilds the watchlist from the user-configured symbols so the UI
/// reflects the current watched_share_count/watched_shares config on every
/// tick while the local feed is disabled or unconfigured. Once the feed is
/// active, symbol/display_name changes are instead picked up through
/// runtime_config_changed() -> init(), so a live fetch's real price/history
/// isn't wiped between successful refreshes -- see update()'s "!g_config_valid"
/// branch and the comment on g_configured_watched_shares.
void seed_from_config(ShareMarketStatus& status)
{
    const RuntimeConfig& config = config_manager::settings();
    const uint8_t count =
        std::min(config.watched_share_count, static_cast<uint8_t>(kMaxWatchedShares));
    status.share_count = count;
    for (size_t i = 0U; i < status.watched_shares.size(); ++i)
    {
        ShareWatchEntry& share = status.watched_shares[i];
        share.display_name.fill('\0');
        share.symbol.fill('\0');
        share.exchange.fill('\0');
        share.currency.fill('\0');
        share.price_text.fill('\0');
        share.change_text.fill('\0');
        share.history_points.fill(0U);
        if (i >= count)
        {
            continue;
        }

        copy_text(share.symbol, config.watched_shares[i].symbol.data());
        copy_text(share.display_name, config.watched_shares[i].display_name.data());
        seed_placeholder_quote(share);
        seed_placeholder_history(share, status.period);
    }
}

/// @brief Returns the local feed's period query-string value for one CCU period.
const char* period_query_value(SharePeriod period)
{
    switch (period)
    {
    case SharePeriod::Today:
        return "today";
    case SharePeriod::Week:
        return "week";
    case SharePeriod::Month:
        return "month";
    case SharePeriod::Year:
        return "year";
    case SharePeriod::AllTime:
        return "all_time";
    }

    return "today";
}

/// @brief Re-reads config and returns false if the feature isn't usable yet.
/// @details Reuses the configured Home Assistant host/port (issue #42's
/// proposed contract) rather than a dedicated host field -- the feed is
/// expected to live on the same box as Home Assistant (a REST command,
/// template, or small local proxy), not a separate third-party endpoint.
bool refresh_config()
{
    const RuntimeConfig& config = config_manager::settings();
    g_configured_share_count = config.watched_share_count;
    g_configured_watched_shares = config.watched_shares;
    if (!config.shares_feed_enabled || config.home_assistant_host[0] == '\0')
    {
        return false;
    }

    std::snprintf(g_configured_host, sizeof(g_configured_host), "%s",
                  config.home_assistant_host.data());
    g_configured_port =
        (config.home_assistant_port != 0U) ? config.home_assistant_port : kDefaultPort;
    std::snprintf(g_configured_token, sizeof(g_configured_token), "%s",
                  config.shares_feed_token.data());
    return true;
}

/// @brief Returns whether the active config differs from what was last used.
/// @details Also fires on a watchlist edit (issue #9), not just host/token
/// changes -- symbol/display_name are only re-seeded from config through
/// init(), see seed_from_config()'s comment.
bool runtime_config_changed()
{
    const RuntimeConfig& config = config_manager::settings();
    const bool was_configured = g_config_valid;
    const bool now_configured = config.shares_feed_enabled && config.home_assistant_host[0] != '\0';
    if (was_configured != now_configured)
    {
        return true;
    }
    if (config.watched_share_count != g_configured_share_count ||
        config.watched_shares != g_configured_watched_shares)
    {
        return true;
    }
    if (!now_configured)
    {
        return false;
    }

    return std::strcmp(g_configured_host, config.home_assistant_host.data()) != 0 ||
           g_configured_port != config.home_assistant_port ||
           std::strcmp(g_configured_token, config.shares_feed_token.data()) != 0;
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

/// @brief Resets one in-flight network attempt while preserving the last data snapshot.
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

// Status-line/header parsing, chunked decoding, and buffered-completion
// detection are shared across every network manager -- see http_response.h
// and docs/architecture.md's "Network helper ownership boundary" (issue
// #46). These thin wrappers keep the call sites below unchanged.

/// @brief Extracts the numeric HTTP status from the response head.
int partial_http_status()
{
    return http_response::partial_status(g_response);
}

/// @brief Dechunks the buffered response in place if needed, returning a
/// pointer to the (now contiguous) body, or nullptr if the headers aren't
/// complete yet or the chunk framing is malformed/incomplete.
/// @details Home Assistant's REST/template responses commonly use
/// `Transfer-Encoding: chunked` rather than a fixed Content-Length, so this
/// stays provider-agnostic infrastructure rather than something specific to
/// the old Yahoo path it was originally written for.
char* normalised_response_body()
{
    const char* headers_end_ptr = http_response::headers_end(g_response);
    if (headers_end_ptr == nullptr)
    {
        return nullptr;
    }
    if (!http_response::header_has_token(g_response, headers_end_ptr, "Transfer-Encoding:",
                                         "chunked"))
    {
        return const_cast<char*>(http_response::body(g_response));
    }
    if (!http_response::decode_chunked_body(g_response, &g_response_len))
    {
        return nullptr;
    }
    return const_cast<char*>(http_response::body(g_response));
}

/// @brief Records a failed fetch without wiping the last displayed share values.
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
    g_status.last_success_ms = to_ms_since_boot(get_absolute_time());
    schedule_next_attempt(kRefreshIntervalMs);
}

/// @brief Defers request completion until the main update loop is back in control.
/// @details lwIP callbacks must not close or replace the active PCB while
/// lwIP is still unwinding the callback. Period changes make that re-entrancy
/// much more likely, so callbacks only record the outcome here.
void defer_completion(bool success, err_t err, int http_status)
{
    g_completion_pending = true;
    g_completion_success = success;
    g_completion_error = err;
    g_completion_http_status = http_status;
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
    if (cursor == nullptr || cursor >= end || *cursor == '"')
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

/// @brief Formats a price compactly enough for one bracketed softkey line.
void format_price_text(double price, std::array<char, 12>& out)
{
    out.fill('\0');
    if (price >= 1000.0 && price < 10000.0)
    {
        const long tenths = std::lround(price * 10.0);
        const int whole = static_cast<int>(tenths / 10L);
        const int thousands = whole / 1000;
        const int remainder = whole % 1000;
        const int decimal = static_cast<int>(tenths % 10L);
        std::snprintf(out.data(), out.size(), "%d,%03d.%d", thousands, remainder, decimal);
        return;
    }

    std::snprintf(out.data(), out.size(), "%.1f", price);
}

/// @brief Converts a parsed price into the uint16 graph range used by the renderer.
uint16_t graph_value_from_price(double price)
{
    const long rounded = std::lround(price);
    if (rounded < 0L)
    {
        return 0U;
    }
    if (rounded > 65535L)
    {
        return 65535U;
    }

    return static_cast<uint16_t>(rounded);
}

/// @brief Downsamples arbitrary close values into the fixed 24-point CCU graph buffer.
void copy_history_points(size_t count, ShareWatchEntry& share, uint16_t fallback_price)
{
    if (count == 0U)
    {
        share.history_points.fill(fallback_price);
        return;
    }

    for (size_t i = 0U; i < share.history_points.size(); ++i)
    {
        const size_t source_index =
            (count == 1U) ? 0U : ((i * (count - 1U)) / (share.history_points.size() - 1U));
        share.history_points[i] = g_history_parse_values[source_index];
    }
}

/// @brief Parses one share object's bounded `"history":[...]` array into graph points.
bool parse_bounded_history(const char* obj_start, const char* obj_end, ShareWatchEntry& share,
                           double fallback_price)
{
    const char* cursor = find_bounded(obj_start, obj_end, "\"history\":[");
    if (cursor == nullptr)
    {
        copy_history_points(0U, share, graph_value_from_price(fallback_price));
        return false;
    }

    cursor += std::strlen("\"history\":[");
    size_t value_count = 0U;
    g_history_parse_values.fill(0U);

    while (cursor < obj_end)
    {
        cursor = skip_json_space(cursor);
        if (cursor >= obj_end || *cursor == ']')
        {
            break;
        }
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }

        char* end = nullptr;
        const double value = std::strtod(cursor, &end);
        if (end == cursor)
        {
            break;
        }
        if (value_count < g_history_parse_values.size())
        {
            g_history_parse_values[value_count++] = graph_value_from_price(value);
        }
        cursor = end;
    }

    copy_history_points(value_count, share, graph_value_from_price(fallback_price));
    return value_count > 0U;
}

/// @brief Finds the watched share entry matching `symbol`, or nullptr.
ShareWatchEntry* find_watched_share(const char* symbol)
{
    if (symbol == nullptr || symbol[0] == '\0')
    {
        return nullptr;
    }

    for (uint8_t i = 0U; i < g_status.share_count; ++i)
    {
        if (std::strcmp(g_status.watched_shares[i].symbol.data(), symbol) == 0)
        {
            return &g_status.watched_shares[i];
        }
    }

    return nullptr;
}

/// @brief Parses the `{"shares":[...]}` local-feed response (issue #42) and
/// updates every configured watched share found in it.
/// @details Only shares already present in the configured watchlist
/// (config_manager -- issue #9) are updated; entries the feed doesn't mention
/// keep whatever they last showed rather than being blanked, matching the
/// "malformed/stale/missing fields must not block navigation" requirement. A
/// per-share `"data_state"` other than `"live"` is treated the same as a
/// missing entry -- the feed is explicitly saying this value isn't
/// trustworthy right now, so the display is left alone rather than show it.
/// `price` accepts either a raw JSON number or a quoted numeric string (the
/// issue's own proposed contract shows the latter); `change` is copied
/// through as-is since the contract shows it already formatted for display
/// (e.g. "+0.02%"), unlike `price`, which is reformatted through
/// format_price_text() for the same thousands-separator style the rest of
/// the app uses.
bool parse_shares_feed_response()
{
    char* body = normalised_response_body();
    if (body == nullptr)
    {
        return false;
    }

    const char* array_marker = std::strstr(body, "\"shares\":[");
    if (array_marker == nullptr)
    {
        return false;
    }

    const char* buffer_end = g_response + g_response_len;
    const char* cursor = array_marker + std::strlen("\"shares\":[");
    bool updated_any = false;

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
            // keep whatever shares were already parsed.
            break;
        }

        char symbol[10] = {};
        if (extract_bounded_string(cursor, obj_end, "\"symbol\"", symbol, sizeof(symbol)))
        {
            ShareWatchEntry* share = find_watched_share(symbol);
            char data_state[16] = {};
            const bool has_state =
                extract_bounded_string(cursor, obj_end, "\"data_state\"", data_state,
                                       sizeof(data_state));
            if (share != nullptr && (!has_state || std::strcmp(data_state, "live") == 0))
            {
                char name[24] = {};
                if (extract_bounded_string(cursor, obj_end, "\"name\"", name, sizeof(name)))
                {
                    copy_text(share->display_name, name);
                }

                char exchange[8] = {};
                if (extract_bounded_string(cursor, obj_end, "\"exchange\"", exchange,
                                           sizeof(exchange)))
                {
                    copy_text(share->exchange, exchange);
                }

                char currency[8] = {};
                if (extract_bounded_string(cursor, obj_end, "\"currency\"", currency,
                                           sizeof(currency)))
                {
                    copy_text(share->currency, currency);
                }

                double price = 0.0;
                bool have_price = extract_bounded_number(cursor, obj_end, "\"price\"", &price);
                if (!have_price)
                {
                    char price_text[16] = {};
                    if (extract_bounded_string(cursor, obj_end, "\"price\"", price_text,
                                               sizeof(price_text)))
                    {
                        char* end = nullptr;
                        price = std::strtod(price_text, &end);
                        have_price = end != price_text;
                    }
                }
                if (have_price)
                {
                    format_price_text(price, share->price_text);
                }

                char change_text[12] = {};
                if (extract_bounded_string(cursor, obj_end, "\"change\"", change_text,
                                           sizeof(change_text)))
                {
                    copy_text(share->change_text, change_text);
                }

                parse_bounded_history(cursor, obj_end, *share, price);
                updated_any = true;
            }
        }

        cursor = obj_end + 1;
    }

    return updated_any;
}

/// @brief Builds the HTTP GET request for the configured period.
bool build_request()
{
    char auth_header[176] = {};
    if (g_configured_token[0] != '\0')
    {
        std::snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s\r\n",
                      g_configured_token);
    }

    const int target_len = std::snprintf(
        g_request, sizeof(g_request),
        "GET /api/merlinccu/shares?period=%s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "User-Agent: MerlinCCU/0.1 (https://github.com/victoriandad/MerlinCCU)\r\n"
        "Accept: application/json\r\n"
        "Accept-Encoding: identity\r\n"
        "%s"
        "Connection: close\r\n"
        "\r\n",
        period_query_value(g_inflight_period), g_configured_host, auth_header);
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
    // Parsing can be surprisingly expensive on the Pico when the all-time
    // period returns a larger payload. Defer both parsing and socket close to
    // the update loop so lwIP callbacks stay short and non-reentrant.
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
        // The response didn't fit in our fixed-size buffer. Not a hard
        // failure -- parse_shares_feed_response() tolerates a truncated
        // final object and keeps whatever complete shares it already found.
        handle_response_complete();
        return ERR_OK;
    }
    if (http_response::is_complete(g_response, g_response_len))
    {
        // Some local feeds don't close promptly after a complete response;
        // acting as soon as the advertised body has arrived avoids idling
        // until the I/O deadline. Covers both chunked and Content-Length
        // delimited responses, not just the chunked case this used to check.
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
    g_inflight_period = g_status.period;
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

/// @brief Compares two snapshots without relying on struct padding bytes.
bool status_changed(const ShareMarketStatus& lhs, const ShareMarketStatus& rhs)
{
    return lhs.configured != rhs.configured || lhs.data_valid != rhs.data_valid ||
           lhs.last_error != rhs.last_error || lhs.last_http_status != rhs.last_http_status ||
           lhs.period != rhs.period || lhs.share_count != rhs.share_count ||
           lhs.watched_shares != rhs.watched_shares;
}

} // namespace

void init()
{
    g_status = {};
    g_status.configured = true;
    g_status.data_valid = false;
    g_status.last_error = 0;
    g_status.last_http_status = 0;
    g_status.period = SharePeriod::Today;
    g_config_valid = false;
    seed_from_config(g_status);
    reset_attempt_state();
    g_inflight_period = g_status.period;
    g_next_attempt = nil_time;
    g_request_attempted = false;
}

bool update(const WifiStatus& wifi_status, SharePeriod active_period, bool fetch_enabled)
{
    const ShareMarketStatus previous = g_status;

    if (runtime_config_changed())
    {
        init();
    }

    if (g_status.period != active_period)
    {
        g_status.period = active_period;
        g_status.data_valid = false;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        g_request_attempted = false;
        g_next_attempt = nil_time;
    }

    // The share workflow can be isolated to its own pages. Pausing network
    // traffic elsewhere avoids coupling unrelated menus to feed I/O.
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
        return status_changed(previous, g_status);
    }

    g_config_valid = refresh_config();
    if (!g_config_valid)
    {
        // No feed configured (or disabled) -- keep the demo watchlist fresh
        // and don't attempt any network I/O.
        if (g_pcb != nullptr || g_dns_pending || g_completion_pending || g_request_attempted)
        {
            reset_attempt_state();
            g_request_attempted = false;
            g_next_attempt = nil_time;
        }
        seed_from_config(g_status);
        g_status.data_valid = false;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        return status_changed(previous, g_status);
    }

    const bool wifi_ready = wifi_status.ip_address[0] != '\0';
    if (!wifi_ready)
    {
        reset_attempt_state();
        return status_changed(previous, g_status);
    }

    if (g_completion_pending)
    {
        const int completed_http_status = g_completion_http_status;
        if (g_completion_success && g_inflight_period != g_status.period)
        {
            reset_attempt_state();
            g_status.data_valid = false;
            g_status.last_error = 0;
            g_status.last_http_status = completed_http_status;
            g_next_attempt = nil_time;
            g_request_attempted = false;
            return status_changed(previous, g_status);
        }

        if (g_completion_success && completed_http_status == 200 && parse_shares_feed_response())
        {
            finish_success(completed_http_status);
        }
        else
        {
            if (g_completion_success)
            {
                finish_failure(ERR_VAL, completed_http_status);
            }
            else
            {
                finish_failure(g_completion_error, completed_http_status);
            }
        }
        return status_changed(previous, g_status);
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

    return status_changed(previous, g_status);
}

const ShareMarketStatus& status()
{
    return g_status;
}

} // namespace share_price_manager
