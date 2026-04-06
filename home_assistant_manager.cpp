#include "home_assistant_manager.h"

#include <cstdio>
#include <cstring>

#include "lwip/dns.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#include "pico/stdlib.h"

namespace {

#if __has_include("home_assistant_credentials.h")
#include "home_assistant_credentials.h"
constexpr bool kHomeAssistantConfigured = true;
#else
constexpr bool kHomeAssistantConfigured = false;
inline constexpr char HOME_ASSISTANT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_PORT = 8123;
inline constexpr char HOME_ASSISTANT_TOKEN[] = "";
#endif

constexpr uint32_t kResolveTimeoutMs = 4000;
constexpr uint32_t kConnectTimeoutMs = 4000;
constexpr uint32_t kIoTimeoutMs = 4000;
constexpr uint32_t kSuccessProbeIntervalMs = 30000;
constexpr uint32_t kFailureRetryIntervalMs = 10000;
constexpr uint8_t kTcpPollInterval = 2;

HomeAssistantStatus g_status = {};
ip_addr_t g_resolved_ip = {};
bool g_dns_pending = false;
bool g_dns_resolved = false;
tcp_pcb* g_pcb = nullptr;
size_t g_request_sent = 0;
size_t g_response_len = 0;
char g_request[512] = {};
char g_response[256] = {};
char g_configured_host[48] = {};
uint16_t g_configured_port = HOME_ASSISTANT_PORT;
bool g_config_valid = false;
absolute_time_t g_deadline = nil_time;
absolute_time_t g_next_attempt = nil_time;

void copy_text(std::array<char, 48>& dst, const char* src)
{
    dst.fill('\0');
    if (!src) {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
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

bool parse_home_assistant_endpoint()
{
    g_configured_host[0] = '\0';
    g_configured_port = HOME_ASSISTANT_PORT;

    if (HOME_ASSISTANT_HOST[0] == '\0') {
        return false;
    }

    const char* host_start = HOME_ASSISTANT_HOST;
    if (std::strncmp(host_start, "http://", 7) == 0) {
        host_start += 7;
    } else if (std::strncmp(host_start, "https://", 8) == 0) {
        std::printf("HA config uses https:// but only plain HTTP is supported\n");
        return false;
    }

    const char* host_end = host_start;
    while (*host_end != '\0' && *host_end != ':' && *host_end != '/') {
        ++host_end;
    }

    const size_t host_len = static_cast<size_t>(host_end - host_start);
    if (host_len == 0 || host_len >= sizeof(g_configured_host)) {
        std::printf("HA config host is empty or too long\n");
        return false;
    }

    std::memcpy(g_configured_host, host_start, host_len);
    g_configured_host[host_len] = '\0';

    if (*host_end == ':') {
        const char* port_start = host_end + 1;
        const char* port_end = port_start;
        while (*port_end != '\0' && *port_end != '/') {
            ++port_end;
        }

        char port_text[8] = {};
        const size_t port_len = static_cast<size_t>(port_end - port_start);
        if (port_len == 0 || port_len >= sizeof(port_text)) {
            std::printf("HA config port is invalid\n");
            return false;
        }

        std::memcpy(port_text, port_start, port_len);
        port_text[port_len] = '\0';
        if (!parse_port(port_text, &g_configured_port)) {
            std::printf("HA config port is invalid: %s\n", port_text);
            return false;
        }

        host_end = port_end;
    }

    if (*host_end == '/' && std::strcmp(host_end, "/") != 0) {
        std::printf("HA config should not include a path; using host root only\n");
    }

    return true;
}

void schedule_retry(uint32_t delay_ms)
{
    g_next_attempt = make_timeout_time_ms(delay_ms);
}

void set_status(HomeAssistantConnectionState state, int last_error, int last_http_status)
{
    g_status.state = state;
    g_status.last_error = last_error;
    g_status.last_http_status = last_http_status;
}

void clear_tcp_callbacks(tcp_pcb* pcb)
{
    if (pcb == nullptr) {
        return;
    }

    tcp_arg(pcb, nullptr);
    tcp_recv(pcb, nullptr);
    tcp_sent(pcb, nullptr);
    tcp_err(pcb, nullptr);
    tcp_poll(pcb, nullptr, 0);
}

void close_pcb()
{
    if (g_pcb == nullptr) {
        return;
    }

    tcp_pcb* pcb = g_pcb;
    g_pcb = nullptr;
    clear_tcp_callbacks(pcb);

    const err_t close_rc = tcp_close(pcb);
    if (close_rc != ERR_OK) {
        tcp_abort(pcb);
    }
}

void reset_attempt_state()
{
    close_pcb();
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_response[0] = '\0';
    g_deadline = nil_time;
}

void finish_request(HomeAssistantConnectionState state, int last_error, int last_http_status, uint32_t retry_ms)
{
    set_status(state, last_error, last_http_status);
    schedule_retry(retry_ms);
    reset_attempt_state();
}

bool build_request()
{
    const int len = std::snprintf(g_request,
                                  sizeof(g_request),
                                  "GET /api/ HTTP/1.1\r\n"
                                  "Host: %s:%u\r\n"
                                  "Authorization: Bearer %s\r\n"
                                  "Content-Type: application/json\r\n"
                                  "Connection: close\r\n"
                                  "\r\n",
                                  g_configured_host,
                                  static_cast<unsigned>(g_configured_port),
                                  HOME_ASSISTANT_TOKEN);
    if (len <= 0 || static_cast<size_t>(len) >= sizeof(g_request)) {
        set_status(HomeAssistantConnectionState::Error, -1, 0);
        schedule_retry(kFailureRetryIntervalMs);
        return false;
    }
    return true;
}

err_t try_send_request();

err_t on_tcp_connected(void* arg, tcp_pcb* pcb, err_t err)
{
    (void)arg;

    if (pcb != g_pcb) {
        return ERR_OK;
    }

    if (err != ERR_OK) {
        finish_request(HomeAssistantConnectionState::Error, err, 0, kFailureRetryIntervalMs);
        std::printf("HA connect failed err=%d\n", static_cast<int>(err));
        return ERR_OK;
    }

    set_status(HomeAssistantConnectionState::Authorizing, 0, 0);
    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return try_send_request();
}

void on_tcp_error(void* arg, err_t err)
{
    (void)arg;
    g_pcb = nullptr;
    set_status(HomeAssistantConnectionState::Error, err, g_status.last_http_status);
    g_dns_pending = false;
    g_dns_resolved = false;
    g_request_sent = 0;
    g_response_len = 0;
    g_response[0] = '\0';
    g_deadline = nil_time;
    schedule_retry(kFailureRetryIntervalMs);
    std::printf("HA tcp error err=%d\n", static_cast<int>(err));
}

void handle_http_status(int http_status)
{
    g_status.last_http_status = http_status;

    if (http_status == 200) {
        finish_request(HomeAssistantConnectionState::Connected, 0, http_status, kSuccessProbeIntervalMs);
        std::printf("HA API probe ok host=%s port=%u status=%d\n",
                    g_configured_host,
                    static_cast<unsigned>(g_configured_port),
                    http_status);
        return;
    }

    if (http_status == 401) {
        finish_request(HomeAssistantConnectionState::Unauthorized, 0, http_status, kFailureRetryIntervalMs);
        std::printf("HA API probe unauthorized host=%s port=%u\n",
                    g_configured_host,
                    static_cast<unsigned>(g_configured_port));
        return;
    }

    finish_request(HomeAssistantConnectionState::Error, 0, http_status, kFailureRetryIntervalMs);
    std::printf("HA API probe unexpected status=%d host=%s port=%u\n",
                http_status,
                g_configured_host,
                static_cast<unsigned>(g_configured_port));
}

err_t on_tcp_recv(void* arg, tcp_pcb* pcb, pbuf* p, err_t err)
{
    (void)arg;

    if (pcb != g_pcb) {
        if (p != nullptr) {
            tcp_recved(pcb, p->tot_len);
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (err != ERR_OK) {
        if (p != nullptr) {
            pbuf_free(p);
        }
        finish_request(HomeAssistantConnectionState::Error, err, g_status.last_http_status, kFailureRetryIntervalMs);
        return ERR_OK;
    }

    if (p == nullptr) {
        finish_request(HomeAssistantConnectionState::Error, ERR_CLSD, g_status.last_http_status, kFailureRetryIntervalMs);
        return ERR_OK;
    }

    const uint16_t received_len = p->tot_len;
    const size_t space_left = sizeof(g_response) - g_response_len - 1;
    const uint16_t copy_len = static_cast<uint16_t>((received_len < space_left) ? received_len : space_left);

    if (copy_len > 0) {
        pbuf_copy_partial(p, g_response + g_response_len, copy_len, 0);
        g_response_len += copy_len;
        g_response[g_response_len] = '\0';
        g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    }

    tcp_recved(pcb, received_len);
    pbuf_free(p);

    const char* line_end = std::strstr(g_response, "\r\n");
    if (line_end == nullptr) {
        if (g_response_len + 1 >= sizeof(g_response)) {
            finish_request(HomeAssistantConnectionState::Error, ERR_BUF, 0, kFailureRetryIntervalMs);
        }
        return ERR_OK;
    }

    int http_status = 0;
    if (std::sscanf(g_response, "HTTP/%*d.%*d %d", &http_status) != 1) {
        http_status = 0;
    }

    handle_http_status(http_status);
    return ERR_OK;
}

err_t try_send_request()
{
    if (g_pcb == nullptr) {
        return ERR_CLSD;
    }

    const size_t request_len = std::strlen(g_request);
    while (g_request_sent < request_len) {
        const u16_t sndbuf = tcp_sndbuf(g_pcb);
        if (sndbuf == 0) {
            return ERR_OK;
        }

        const size_t remaining = request_len - g_request_sent;
        const u16_t chunk = static_cast<u16_t>((remaining < sndbuf) ? remaining : sndbuf);
        if (chunk == 0) {
            return ERR_OK;
        }

        const err_t write_rc = tcp_write(g_pcb,
                                         g_request + g_request_sent,
                                         chunk,
                                         TCP_WRITE_FLAG_COPY | ((g_request_sent + chunk < request_len) ? TCP_WRITE_FLAG_MORE : 0));
        if (write_rc != ERR_OK) {
            if (write_rc == ERR_MEM) {
                return ERR_OK;
            }

            finish_request(HomeAssistantConnectionState::Error, write_rc, 0, kFailureRetryIntervalMs);
            return write_rc;
        }

        g_request_sent += chunk;
    }

    const err_t output_rc = tcp_output(g_pcb);
    if (output_rc != ERR_OK) {
        finish_request(HomeAssistantConnectionState::Error, output_rc, 0, kFailureRetryIntervalMs);
        return output_rc;
    }

    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return ERR_OK;
}

err_t on_tcp_sent(void* arg, tcp_pcb* pcb, u16_t len)
{
    (void)arg;
    (void)len;

    if (pcb != g_pcb) {
        return ERR_OK;
    }

    g_deadline = make_timeout_time_ms(kIoTimeoutMs);
    return try_send_request();
}

err_t on_tcp_poll(void* arg, tcp_pcb* pcb)
{
    (void)arg;

    if (pcb != g_pcb) {
        return ERR_OK;
    }

    if (g_status.state == HomeAssistantConnectionState::Authorizing && g_request_sent < std::strlen(g_request)) {
        return try_send_request();
    }

    return ERR_OK;
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
        set_status(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0);
        schedule_retry(kFailureRetryIntervalMs);
        std::printf("HA DNS resolution failed for host=%s\n", g_configured_host);
        return;
    }

    g_resolved_ip = *ipaddr;
    g_dns_resolved = true;
}

bool start_socket_connect()
{
    if (!build_request()) {
        return false;
    }

    g_pcb = tcp_new_ip_type(IP_GET_TYPE(&g_resolved_ip));
    if (g_pcb == nullptr) {
        set_status(HomeAssistantConnectionState::Error, ERR_MEM, 0);
        schedule_retry(kFailureRetryIntervalMs);
        return false;
    }

    tcp_arg(g_pcb, nullptr);
    tcp_recv(g_pcb, on_tcp_recv);
    tcp_sent(g_pcb, on_tcp_sent);
    tcp_err(g_pcb, on_tcp_error);
    tcp_poll(g_pcb, on_tcp_poll, kTcpPollInterval);

    const err_t rc = tcp_connect(g_pcb, &g_resolved_ip, g_configured_port, on_tcp_connected);
    if (rc == ERR_OK) {
        set_status(HomeAssistantConnectionState::Connecting, 0, 0);
        g_deadline = make_timeout_time_ms(kConnectTimeoutMs);
        return true;
    }

    set_status(HomeAssistantConnectionState::Error, rc, 0);
    reset_attempt_state();
    schedule_retry(kFailureRetryIntervalMs);
    std::printf("HA connect start failed err=%d\n", static_cast<int>(rc));
    return false;
}

bool start_probe()
{
    reset_attempt_state();
    g_status.last_error = 0;
    g_status.last_http_status = 0;

    ip_addr_t parsed = {};
    if (ipaddr_aton(g_configured_host, &parsed)) {
        g_resolved_ip = parsed;
        g_dns_resolved = true;
        return start_socket_connect();
    }

    const err_t dns_rc = dns_gethostbyname(g_configured_host, &g_resolved_ip, dns_found, nullptr);
    if (dns_rc == ERR_OK) {
        g_dns_resolved = true;
        set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        return start_socket_connect();
    }

    if (dns_rc == ERR_INPROGRESS) {
        g_dns_pending = true;
        set_status(HomeAssistantConnectionState::Resolving, 0, 0);
        g_deadline = make_timeout_time_ms(kResolveTimeoutMs);
        std::printf("HA resolving host=%s\n", g_configured_host);
        return true;
    }

    set_status(HomeAssistantConnectionState::Error, dns_rc, 0);
    schedule_retry(kFailureRetryIntervalMs);
    std::printf("HA dns_gethostbyname failed err=%d host=%s\n", static_cast<int>(dns_rc), g_configured_host);
    return false;
}

}  // namespace

namespace home_assistant_manager {

void init()
{
    g_config_valid = parse_home_assistant_endpoint();
    g_status = {};
    g_status.configured = kHomeAssistantConfigured &&
                          g_config_valid &&
                          HOME_ASSISTANT_TOKEN[0] != '\0';
    copy_text(g_status.host, g_configured_host);
    g_status.last_error = 0;
    g_status.last_http_status = 0;
    g_status.state = g_status.configured ? HomeAssistantConnectionState::WaitingForWifi
                                         : HomeAssistantConnectionState::Unconfigured;
    reset_attempt_state();
    g_next_attempt = nil_time;
}

bool update(const WifiStatus& wifi_status)
{
    const HomeAssistantStatus previous = g_status;

    if (!g_status.configured) {
        g_status.state = HomeAssistantConnectionState::Unconfigured;
        return previous.state != g_status.state || previous.configured != g_status.configured;
    }

    const bool wifi_ready = wifi_status.state == WifiConnectionState::Connected && wifi_status.ip_address[0] != '\0';
    if (!wifi_ready) {
        reset_attempt_state();
        g_status.state = HomeAssistantConnectionState::WaitingForWifi;
        g_status.last_error = 0;
        g_status.last_http_status = 0;
        g_next_attempt = nil_time;
        return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
    }

    if (g_dns_pending && !is_nil_time(g_deadline) && absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0) {
        g_dns_pending = false;
        set_status(HomeAssistantConnectionState::Error, ERR_TIMEOUT, 0);
        schedule_retry(kFailureRetryIntervalMs);
    }

    if (g_dns_resolved && g_pcb == nullptr) {
        g_dns_resolved = false;
        start_socket_connect();
    }

    if (g_pcb != nullptr && !is_nil_time(g_deadline) && absolute_time_diff_us(get_absolute_time(), g_deadline) <= 0) {
        set_status(HomeAssistantConnectionState::Error, ERR_TIMEOUT, g_status.last_http_status);
        reset_attempt_state();
        schedule_retry(kFailureRetryIntervalMs);
    }

    if (!g_dns_pending && g_pcb == nullptr &&
        (is_nil_time(g_next_attempt) || absolute_time_diff_us(get_absolute_time(), g_next_attempt) <= 0)) {
        g_next_attempt = nil_time;
        start_probe();
    }

    return std::memcmp(&previous, &g_status, sizeof(g_status)) != 0;
}

const HomeAssistantStatus& status()
{
    return g_status;
}

}  // namespace home_assistant_manager
