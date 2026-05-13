#include "share_price_manager.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lwip/altcp.h"
#include "lwip/altcp_tcp.h"
#include "lwip/altcp_tls.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "mbedtls/ssl.h"
#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"

namespace share_price_manager
{

namespace
{

constexpr char kProviderHost[] = "query1.finance.yahoo.com";
constexpr uint16_t kProviderPort = 443U;
constexpr uint32_t kResolveTimeoutMs = 4000U;
constexpr uint32_t kConnectTimeoutMs = 6000U;
constexpr uint32_t kIoTimeoutMs = 6000U;
constexpr uint32_t kRetryDelayMs = 60U * 1000U;
constexpr uint32_t kRefreshIntervalMs = 5U * 60U * 1000U;
constexpr size_t kRequestBufferSize = 768U;
// Keep the response buffer comfortably above one-symbol chart payload sizes
// while avoiding unnecessary pressure on shared heap headroom.
constexpr size_t kResponseBufferSize = 16384U;
constexpr size_t kMaxParsedHistoryValues = 256U;
constexpr const char* kWatchedSymbol = "BA.L";
/// @brief Temporary safety switch while stabilising the live share fetch path.
constexpr bool kEnableLiveShareFetch = true;

ShareMarketStatus g_status = {};
ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
altcp_pcb* g_pcb = nullptr;
struct altcp_tls_config* g_tls_config = nullptr;
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

template <size_t N>
/// @brief Copies provider text into a fixed-size UI buffer.
void copy_text(std::array<char, N>& dst, const char* src)
{
    dst.fill('\0');
    if (src == nullptr)
    {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

/// @brief Seeds the first watched row so the UI has stable labels before Wi-Fi is ready.
void seed_bae_placeholder(ShareMarketStatus& status)
{
    status.share_count = 1U;
    for (auto& share : status.watched_shares)
    {
        share.display_name.fill('\0');
        share.symbol.fill('\0');
        share.exchange.fill('\0');
        share.currency.fill('\0');
        share.price_text.fill('\0');
        share.change_text.fill('\0');
        share.history_points.fill(0U);
    }

    ShareWatchEntry& bae = status.watched_shares[0];
    copy_text(bae.display_name, "BAE SYSTEMS");
    copy_text(bae.symbol, kWatchedSymbol);
    copy_text(bae.exchange, "LSE");
    copy_text(bae.currency, "GBX");
    copy_text(bae.price_text, "1,372.0");
    copy_text(bae.change_text, "+0.0%");
    bae.history_points = {1320U, 1324U, 1318U, 1328U, 1336U, 1332U, 1340U, 1346U,
                          1341U, 1348U, 1352U, 1349U, 1355U, 1351U, 1344U, 1348U,
                          1356U, 1360U, 1354U, 1358U, 1364U, 1362U, 1368U, 1372U};
}

/// @brief Returns Yahoo chart range/interval parameters for one CCU period.
const char* period_query(SharePeriod period)
{
    switch (period)
    {
    case SharePeriod::Today:
        return "range=1d&interval=5m";
    case SharePeriod::Week:
        return "range=5d&interval=15m";
    case SharePeriod::Month:
        return "range=1mo&interval=1d";
    case SharePeriod::Year:
        return "range=1y&interval=1wk";
    case SharePeriod::AllTime:
        return "range=max&interval=1mo";
    }

    return "range=1d&interval=5m";
}

/// @brief Removes callbacks from the current TCP/TLS control block before close/abort.
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

/// @brief Closes the active socket and releases its TLS configuration.
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

/// @brief Decodes an HTTP chunked response body in-place.
/// @details Yahoo currently returns chart JSON with `Transfer-Encoding:
/// chunked`. The Pico parser wants a contiguous JSON body, so this strips the
/// chunk framing once the connection closes.
char* normalised_response_body()
{
    char* body = const_cast<char*>(response_body());
    if (body == nullptr)
    {
        return nullptr;
    }

    if (!response_uses_chunked_encoding())
    {
        return body;
    }

    char* read = body;
    char* write = body;
    const char* response_end = g_response + g_response_len;
    while (read < response_end)
    {
        char* line_end = std::strstr(read, "\r\n");
        if (line_end == nullptr || line_end > response_end)
        {
            return nullptr;
        }

        char* parse_end = nullptr;
        const unsigned long chunk_size = std::strtoul(read, &parse_end, 16);
        if (parse_end == read)
        {
            return nullptr;
        }

        char* chunk_data = line_end + 2;
        if (chunk_size == 0UL)
        {
            *write = '\0';
            g_response_len = static_cast<size_t>(write - g_response);
            return body;
        }

        if (chunk_data + chunk_size > response_end)
        {
            return nullptr;
        }

        std::memmove(write, chunk_data, chunk_size);
        write += chunk_size;
        read = chunk_data + chunk_size;
        if (read + 2 <= response_end && read[0] == '\r' && read[1] == '\n')
        {
            read += 2;
        }
    }

    return nullptr;
}

/// @brief Records a failed fetch without wiping the last displayed share values.
void finish_failure(err_t err, int http_status)
{
    reset_attempt_state();
    g_status.data_valid = false;
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
/// @details lwIP callbacks must not close or replace the active TLS PCB while
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
    while (cursor != nullptr && (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
                                *cursor == '\n'))
    {
        ++cursor;
    }

    return cursor;
}

/// @brief Extracts one unescaped JSON string scalar by key.
bool extract_json_string(const char* json, const char* key, char* out, size_t out_size)
{
    if (json == nullptr || key == nullptr || out == nullptr || out_size == 0U)
    {
        return false;
    }

    const char* cursor = std::strstr(json, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = skip_json_space(cursor + 1);
    if (cursor == nullptr || *cursor != '"')
    {
        return false;
    }

    ++cursor;
    size_t write_index = 0U;
    while (*cursor != '\0' && *cursor != '"' && write_index + 1U < out_size)
    {
        if (*cursor == '\\' && cursor[1] != '\0')
        {
            ++cursor;
        }
        out[write_index++] = *cursor++;
    }

    out[write_index] = '\0';
    return write_index > 0U;
}

/// @brief Extracts one JSON numeric scalar by key.
bool extract_json_number(const char* json, const char* key, double* out_value)
{
    if (json == nullptr || key == nullptr || out_value == nullptr)
    {
        return false;
    }

    const char* cursor = std::strstr(json, key);
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = std::strchr(cursor, ':');
    if (cursor == nullptr)
    {
        return false;
    }

    cursor = skip_json_space(cursor + 1);
    char* end = nullptr;
    const double value = std::strtod(cursor, &end);
    if (end == cursor)
    {
        return false;
    }

    *out_value = value;
    return true;
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

/// @brief Formats percentage movement from current price and previous close.
void format_change_text(double price, double previous_close, std::array<char, 12>& out)
{
    out.fill('\0');
    if (previous_close <= 0.0)
    {
        std::snprintf(out.data(), out.size(), "%s", "-");
        return;
    }

    const double change_percent = ((price - previous_close) / previous_close) * 100.0;
    std::snprintf(out.data(), out.size(), "%+.1f%%", change_percent);
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

/// @brief Parses Yahoo's `indicators.quote[0].close` array into graph points.
bool parse_close_history(const char* json, ShareWatchEntry& share, double fallback_price)
{
    const char* cursor = std::strstr(json, "\"close\":[");
    if (cursor == nullptr)
    {
        return false;
    }

    cursor += std::strlen("\"close\":[");
    size_t value_count = 0U;
    g_history_parse_values.fill(0U);

    while (*cursor != '\0' && *cursor != ']')
    {
        cursor = skip_json_space(cursor);
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (std::strncmp(cursor, "null", 4) == 0)
        {
            cursor += 4;
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

/// @brief Parses Yahoo chart JSON into the single watched BAE Systems row.
bool parse_yahoo_chart_response()
{
    const char* body = normalised_response_body();
    if (body == nullptr)
    {
        return false;
    }

    double price = 0.0;
    if (!extract_json_number(body, "\"regularMarketPrice\"", &price))
    {
        return false;
    }

    double previous_close = 0.0;
    if (!extract_json_number(body, "\"chartPreviousClose\"", &previous_close))
    {
        (void)extract_json_number(body, "\"previousClose\"", &previous_close);
    }

    ShareWatchEntry& share = g_status.watched_shares[0];
    copy_text(share.display_name, "BAE SYSTEMS");

    char text[32] = {};
    if (extract_json_string(body, "\"symbol\"", text, sizeof(text)))
    {
        copy_text(share.symbol, text);
    }
    else
    {
        copy_text(share.symbol, kWatchedSymbol);
    }

    if (extract_json_string(body, "\"exchangeName\"", text, sizeof(text)))
    {
        copy_text(share.exchange, text);
    }
    else
    {
        copy_text(share.exchange, "LSE");
    }

    if (extract_json_string(body, "\"currency\"", text, sizeof(text)))
    {
        copy_text(share.currency, std::strcmp(text, "GBp") == 0 ? "GBX" : text);
    }
    else
    {
        copy_text(share.currency, "GBX");
    }

    format_price_text(price, share.price_text);
    format_change_text(price, previous_close, share.change_text);
    return parse_close_history(body, share, price);
}

/// @brief Builds the HTTPS request for the selected Yahoo chart period.
bool build_request()
{
    const int target_len =
        std::snprintf(g_request, sizeof(g_request),
                      "GET /v8/finance/chart/%s?%s HTTP/1.1\r\n"
                      "Host: %s\r\n"
                      "User-Agent: MerlinCCU/0.1 "
                      "(https://github.com/victoriandad/MerlinCCU)\r\n"
                      "Accept: application/json\r\n"
                      "Accept-Encoding: identity\r\n"
                      "Connection: close\r\n"
                      "\r\n",
                      kWatchedSymbol, period_query(g_inflight_period), kProviderHost);
    return target_len > 0 && static_cast<size_t>(target_len) < sizeof(g_request);
}

err_t try_send_request();

/// @brief Handles completion of the TCP/TLS connect step.
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
    if (g_tls_config != nullptr)
    {
        altcp_tls_free_config(g_tls_config);
        g_tls_config = nullptr;
    }
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

/// @brief Opens the TLS socket and starts the request connection phase.
void start_socket_connect()
{
    if (!build_request())
    {
        finish_failure(ERR_VAL, 0);
        return;
    }

    cyw43_arch_lwip_begin();
    g_tls_config = altcp_tls_create_config_client(nullptr, 0);
    if (g_tls_config != nullptr)
    {
        g_pcb = altcp_tls_new(g_tls_config, IP_GET_TYPE(&g_resolved_ip));
        if (g_pcb != nullptr)
        {
            auto* ssl = static_cast<mbedtls_ssl_context*>(altcp_tls_context(g_pcb));
            if (ssl == nullptr || mbedtls_ssl_set_hostname(ssl, kProviderHost) != 0)
            {
                altcp_abort(g_pcb);
                g_pcb = nullptr;
            }
        }
    }
    if (g_pcb == nullptr)
    {
        if (g_tls_config != nullptr)
        {
            altcp_tls_free_config(g_tls_config);
            g_tls_config = nullptr;
        }
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

    const err_t connect_rc = altcp_connect(g_pcb, &g_resolved_ip, kProviderPort, on_tcp_connected);
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
    const err_t dns_rc = dns_gethostbyname(kProviderHost, &g_resolved_ip, dns_found, nullptr);
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
    seed_bae_placeholder(g_status);
    reset_attempt_state();
    g_inflight_period = g_status.period;
    g_next_attempt = nil_time;
    g_request_attempted = false;
}

bool update(const WifiStatus& wifi_status, SharePeriod active_period, bool fetch_enabled)
{
    const ShareMarketStatus previous = g_status;

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
    // traffic elsewhere avoids coupling unrelated menus to market-provider I/O.
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

    // Keep the share UI responsive while the live fetch path is being
    // hardened. The placeholder row remains visible with static values.
    if (!kEnableLiveShareFetch)
    {
        reset_attempt_state();
        g_request_attempted = false;
        g_next_attempt = nil_time;
        g_status.data_valid = true;
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

        if (g_completion_success && completed_http_status == 200 && parse_yahoo_chart_response())
        {
            finish_success(completed_http_status);
        }
        else
        {
            finish_failure(g_completion_success ? ERR_VAL : g_completion_error,
                           completed_http_status);
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
