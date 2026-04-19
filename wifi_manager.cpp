#include "wifi_manager.h"

#include <cstdio>
#include <cstring>
#include "debug_logging.h"
#include "lwip/apps/netbiosns.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/apps/sntp.h"
#include "pico/cyw43_arch.h"
#include "pico/error.h"
#include "pico/stdlib.h"

namespace {

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
#if defined(WIFI_COUNTRY)
constexpr uint32_t kWifiCountry = WIFI_COUNTRY;
#else
constexpr uint32_t kWifiCountry = CYW43_COUNTRY_UK;
#endif
#if defined(WIFI_AUTH)
constexpr uint32_t kConfiguredWifiAuth = WIFI_AUTH;
#else
constexpr uint32_t kConfiguredWifiAuth = CYW43_AUTH_WPA2_MIXED_PSK;
#endif
#if defined(WIFI_STATIC_IP_ADDR) && defined(WIFI_STATIC_IP_NETMASK) && defined(WIFI_STATIC_IP_GATEWAY)
constexpr bool kUseStaticIp = true;
constexpr const char* kStaticIpAddress = WIFI_STATIC_IP_ADDR;
constexpr const char* kStaticIpNetmask = WIFI_STATIC_IP_NETMASK;
constexpr const char* kStaticIpGateway = WIFI_STATIC_IP_GATEWAY;
#if defined(WIFI_STATIC_IP_DNS)
constexpr const char* kStaticIpDns = WIFI_STATIC_IP_DNS;
#else
constexpr const char* kStaticIpDns = nullptr;
#endif
#else
constexpr bool kUseStaticIp = false;
constexpr const char* kStaticIpAddress = nullptr;
constexpr const char* kStaticIpNetmask = nullptr;
constexpr const char* kStaticIpGateway = nullptr;
constexpr const char* kStaticIpDns = nullptr;
#endif
#else
constexpr uint32_t kWifiCountry = CYW43_COUNTRY_UK;
constexpr uint32_t kConfiguredWifiAuth = CYW43_AUTH_WPA2_MIXED_PSK;
constexpr bool kUseStaticIp = false;
constexpr const char* kStaticIpAddress = nullptr;
constexpr const char* kStaticIpNetmask = nullptr;
constexpr const char* kStaticIpGateway = nullptr;
constexpr const char* kStaticIpDns = nullptr;
#endif

constexpr uint32_t kConnectTimeoutMs = 30000;
constexpr uint32_t kRetryDelayMs = 10000;
constexpr uint32_t kDhcpWaitTimeoutMs = 30000;
constexpr uint32_t kInternetProbeIntervalMs = 20000;
constexpr uint32_t kInternetProbeTimeoutMs = 5000;
constexpr char kInternetProbeHostName[] = "dns.google";
constexpr char kAdvertisedHostName[] = "MerlinCCU";
constexpr char kNtpServerHostName[] = "pool.ntp.org";

struct WifiCredential {
    const char* ssid;
    const char* password;
};

#if __has_include("wifi_credentials.h")
#if defined(WIFI_CREDENTIALS_COUNT)
constexpr size_t kWifiCredentialCount = WIFI_CREDENTIALS_COUNT;
#else
constexpr WifiCredential WIFI_CREDENTIALS[] = {
    {WIFI_SSID, WIFI_PASSWORD},
};
constexpr size_t kWifiCredentialCount = 1;
#endif
#else
constexpr WifiCredential WIFI_CREDENTIALS[] = {
    {"", ""},
};
constexpr size_t kWifiCredentialCount = 1;
#endif

WifiStatus g_status = {};
bool g_cyw43_initialized = false;
int g_last_observed_link_status = CYW43_LINK_DOWN;
absolute_time_t g_next_retry = nil_time;
absolute_time_t g_wait_for_ip_deadline = nil_time;
absolute_time_t g_next_probe = nil_time;
absolute_time_t g_probe_started_at = nil_time;
absolute_time_t g_probe_deadline = nil_time;
bool g_probe_in_flight = false;
bool g_netbios_started = false;
bool g_sntp_started = false;

bool credential_valid(const WifiCredential& credential)
{
    return credential.ssid && credential.ssid[0] != '\0';
}

const WifiCredential* active_credential()
{
    for (size_t i = 0; i < kWifiCredentialCount; ++i) {
        if (credential_valid(WIFI_CREDENTIALS[i])) {
            return &WIFI_CREDENTIALS[i];
        }
    }

    return nullptr;
}

const char* credential_password(const WifiCredential& credential)
{
    return (credential.password && credential.password[0] != '\0') ? credential.password : nullptr;
}

void copy_text(std::array<char, 33>& dst, const char* src)
{
    dst.fill('\0');
    if (!src) {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

void copy_ip_text(std::array<char, 16>& dst, const char* src)
{
    dst.fill('\0');
    if (!src) {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

void copy_mac_text(const uint8_t mac[6])
{
    g_status.mac_address.fill('\0');
    std::snprintf(g_status.mac_address.data(),
                  g_status.mac_address.size(),
                  "%02X:%02X:%02X:%02X:%02X:%02X",
                  mac[0],
                  mac[1],
                  mac[2],
                  mac[3],
                  mac[4],
                  mac[5]);
}

const char* auth_mode_text(uint32_t auth_mode)
{
    switch (auth_mode) {
    case CYW43_AUTH_OPEN:
        return "OPEN";
    case CYW43_AUTH_WPA_TKIP_PSK:
        return "WPA";
    case CYW43_AUTH_WPA2_AES_PSK:
        return "WPA2";
    case CYW43_AUTH_WPA2_MIXED_PSK:
        return "WPA2-MIX";
    default:
        return "OTHER";
    }
}

uint32_t configured_auth_mode(const WifiCredential& credential)
{
    return (credential.password && credential.password[0] != '\0') ? kConfiguredWifiAuth : CYW43_AUTH_OPEN;
}

void copy_auth_mode(uint32_t auth_mode)
{
    g_status.auth_mode.fill('\0');
    std::snprintf(g_status.auth_mode.data(), g_status.auth_mode.size(), "%s", auth_mode_text(auth_mode));
}

void clear_ip_address()
{
    g_status.ip_address.fill('\0');
}

void reset_internet_probe_status()
{
    g_status.internet_reachable = false;
    g_status.internet_probe_pending = false;
    g_status.internet_rtt_ms = -1;
}

void reset_internet_probe_timers()
{
    g_probe_in_flight = false;
    g_probe_started_at = nil_time;
    g_probe_deadline = nil_time;
    g_next_probe = nil_time;
}

void start_sntp_if_needed()
{
    if (g_sntp_started) {
        return;
    }

    cyw43_arch_lwip_begin();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, kNtpServerHostName);
    sntp_init();
    cyw43_arch_lwip_end();

    g_sntp_started = true;
    std::printf("WiFi SNTP started with server %s\n", kNtpServerHostName);
}

void stop_sntp_if_started()
{
    if (!g_sntp_started) {
        return;
    }

    cyw43_arch_lwip_begin();
    sntp_stop();
    cyw43_arch_lwip_end();

    g_sntp_started = false;
}

void update_ip_address()
{
    clear_ip_address();

    cyw43_arch_lwip_begin();
    if (netif_default) {
        const char* ip_text = ip4addr_ntoa(netif_ip4_addr(netif_default));
        copy_ip_text(g_status.ip_address, ip_text);
    }
    cyw43_arch_lwip_end();
}

bool has_ipv4_address()
{
    bool has_address = false;

    cyw43_arch_lwip_begin();
    if (netif_default) {
        const ip4_addr_t* address = netif_ip4_addr(netif_default);
        has_address = address && !ip4_addr_isany_val(*address);
    }
    cyw43_arch_lwip_end();

    return has_address;
}

void update_mac_address()
{
    uint8_t mac[6] = {};
    if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac) == 0) {
        copy_mac_text(mac);
    } else {
        g_status.mac_address.fill('\0');
    }
}

bool parse_ip4_or_log(const char* text, ip4_addr_t* out, const char* label)
{
    if (!text || !ip4addr_aton(text, out)) {
        std::printf("WiFi static IP config invalid %s='%s'\n", label, text ? text : "(null)");
        return false;
    }

    return true;
}

bool apply_static_ip_config()
{
    if (!kUseStaticIp) {
        return false;
    }

    ip4_addr_t address;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    if (!parse_ip4_or_log(kStaticIpAddress, &address, "addr") ||
        !parse_ip4_or_log(kStaticIpNetmask, &netmask, "netmask") ||
        !parse_ip4_or_log(kStaticIpGateway, &gateway, "gateway")) {
        return false;
    }

    cyw43_arch_lwip_begin();
    if (!netif_default) {
        cyw43_arch_lwip_end();
        std::printf("WiFi static IP config skipped: no default netif\n");
        return false;
    }

    dhcp_stop(netif_default);
    netif_set_addr(netif_default, &address, &netmask, &gateway);
    if (kStaticIpDns && kStaticIpDns[0] != '\0') {
        ip_addr_t dns;
        if (ipaddr_aton(kStaticIpDns, &dns)) {
            dns_setserver(0, &dns);
        } else {
            std::printf("WiFi static IP config invalid dns='%s'\n", kStaticIpDns);
        }
    }
    cyw43_arch_lwip_end();

    update_ip_address();
    std::printf("WiFi static IP applied: ip=%s gw=%s mask=%s\n",
                kStaticIpAddress,
                kStaticIpGateway,
                kStaticIpNetmask);
    return true;
}

WifiConnectionState state_from_link_status(int link_status)
{
    switch (link_status) {
    case CYW43_LINK_JOIN:
    case CYW43_LINK_NOIP:
        return WifiConnectionState::WaitingForIp;
    case CYW43_LINK_UP:
        return WifiConnectionState::Connected;
    case CYW43_LINK_BADAUTH:
        return WifiConnectionState::AuthFailed;
    case CYW43_LINK_NONET:
        return WifiConnectionState::NoNetwork;
    case CYW43_LINK_FAIL:
    case CYW43_LINK_DOWN:
    default:
        return WifiConnectionState::ConnectFailed;
    }
}

void schedule_retry()
{
    g_next_retry = make_timeout_time_ms(kRetryDelayMs);
}

void schedule_wait_for_ip_deadline()
{
    g_wait_for_ip_deadline = make_timeout_time_ms(kDhcpWaitTimeoutMs);
}

void advertise_hostname()
{
    cyw43_arch_lwip_begin();
    if (netif_default) {
        netif_set_hostname(netif_default, kAdvertisedHostName);
        if (!g_netbios_started) {
            netbiosns_init();
            netbiosns_set_name(kAdvertisedHostName);
            g_netbios_started = true;
        }
    }
    cyw43_arch_lwip_end();
}

void dns_probe_found(const char* name, const ip_addr_t* ipaddr, void* callback_arg)
{
    (void)name;
    (void)callback_arg;
    if (!g_probe_in_flight) {
        return;
    }

    g_status.internet_reachable = (ipaddr != nullptr);
    g_status.internet_probe_pending = false;
    g_status.internet_rtt_ms = -1;
    g_probe_in_flight = false;
    g_probe_deadline = nil_time;
    g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
    PERIODIC_LOG("WiFi internet probe %s host=%s\n", ipaddr ? "ok" : "failed", kInternetProbeHostName);
}

bool start_dns_probe()
{
    ip_addr_t resolved = {};

    cyw43_arch_lwip_begin();
    const err_t result = dns_gethostbyname(kInternetProbeHostName, &resolved, dns_probe_found, nullptr);
    cyw43_arch_lwip_end();

    if (result == ERR_OK) {
        g_probe_in_flight = true;
        g_probe_started_at = get_absolute_time();
        dns_probe_found(kInternetProbeHostName, &resolved, nullptr);
        return true;
    }

    if (result != ERR_INPROGRESS) {
        std::printf("WiFi internet probe start failed: %d\n", static_cast<int>(result));
        g_status.internet_reachable = false;
        g_status.internet_probe_pending = false;
        g_status.internet_rtt_ms = -1;
        g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
        return false;
    }

    g_probe_in_flight = true;
    g_probe_started_at = get_absolute_time();
    g_probe_deadline = make_timeout_time_ms(kInternetProbeTimeoutMs);
    g_status.internet_probe_pending = true;
    PERIODIC_LOG("WiFi internet probe resolving %s\n", kInternetProbeHostName);
    return true;
}

bool update_internet_probe()
{
    if (g_status.state != WifiConnectionState::Connected || !g_status.ip_address[0]) {
        if (g_status.internet_probe_pending || g_status.internet_reachable || g_status.internet_rtt_ms >= 0) {
            reset_internet_probe_status();
            reset_internet_probe_timers();
            return true;
        }
        return false;
    }

    if (g_probe_in_flight) {
        if (!is_nil_time(g_probe_deadline) && absolute_time_diff_us(get_absolute_time(), g_probe_deadline) <= 0) {
            g_probe_in_flight = false;
            g_probe_deadline = nil_time;
            g_status.internet_probe_pending = false;
            g_status.internet_reachable = false;
            g_status.internet_rtt_ms = -1;
            g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
            PERIODIC_LOG("WiFi internet probe timeout for %s\n", kInternetProbeHostName);
            return true;
        }
        return false;
    }

    if (is_nil_time(g_next_probe) || absolute_time_diff_us(get_absolute_time(), g_next_probe) <= 0) {
        g_next_probe = nil_time;
        return start_dns_probe();
    }

    return false;
}

bool attempt_connect()
{
    const WifiCredential* credential = active_credential();
    if (!credential || !credential_valid(*credential)) {
        return false;
    }

    const uint32_t auth_mode = configured_auth_mode(*credential);
    copy_text(g_status.ssid, credential->ssid);
    copy_auth_mode(auth_mode);
    clear_ip_address();
    g_status.state = WifiConnectionState::Connecting;
    g_status.last_error = 0;

    std::printf("WiFi connecting to SSID '%s' auth=%s timeout=%lums\n",
                credential->ssid,
                auth_mode_text(auth_mode),
                static_cast<unsigned long>(kConnectTimeoutMs));

    const int rc = cyw43_arch_wifi_connect_timeout_ms(credential->ssid,
                                                      credential_password(*credential),
                                                      auth_mode,
                                                      kConnectTimeoutMs);

    const int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    g_last_observed_link_status = link_status;
    g_status.link_status = link_status;
    g_status.last_error = rc;
    const bool ip_ready = has_ipv4_address();

    if (rc == PICO_OK && (link_status == CYW43_LINK_UP || ip_ready)) {
        apply_static_ip_config();
        update_ip_address();
        g_status.state = WifiConnectionState::Connected;
        start_sntp_if_needed();
        g_next_retry = nil_time;
        g_wait_for_ip_deadline = nil_time;
        g_next_probe = get_absolute_time();
        PERIODIC_LOG("WiFi connected to '%s' ip=%s\n",
                     credential->ssid,
                     g_status.ip_address[0] ? g_status.ip_address.data() : "-");
        return true;
    }

    if (rc == PICO_OK && (link_status == CYW43_LINK_JOIN || link_status == CYW43_LINK_NOIP)) {
        if (kUseStaticIp && apply_static_ip_config()) {
            g_status.state = WifiConnectionState::Connected;
            start_sntp_if_needed();
            g_next_retry = nil_time;
            g_wait_for_ip_deadline = nil_time;
            g_next_probe = get_absolute_time();
            PERIODIC_LOG("WiFi treating joined link as up after static IP assignment\n");
            return true;
        }
        g_status.state = WifiConnectionState::WaitingForIp;
        g_next_retry = nil_time;
        schedule_wait_for_ip_deadline();
        PERIODIC_LOG("WiFi joined '%s'; waiting for DHCP (link=%d)\n", credential->ssid, link_status);
        return true;
    }

    if (rc == PICO_ERROR_BADAUTH) {
        g_status.state = WifiConnectionState::AuthFailed;
    } else if (rc == PICO_ERROR_TIMEOUT && link_status == CYW43_LINK_NONET) {
        g_status.state = WifiConnectionState::NoNetwork;
    } else if (rc == PICO_ERROR_TIMEOUT || rc == PICO_ERROR_CONNECT_FAILED) {
        g_status.state = WifiConnectionState::ConnectFailed;
    } else {
        g_status.state = state_from_link_status(link_status);
    }

    std::printf("WiFi connect failed for '%s' rc=%d link=%d auth=%s\n",
                credential->ssid,
                rc,
                link_status,
                auth_mode_text(auth_mode));
    g_wait_for_ip_deadline = nil_time;
    reset_internet_probe_status();
    reset_internet_probe_timers();
    stop_sntp_if_started();
    schedule_retry();
    return true;
}

}  // namespace

namespace wifi_manager {

void init()
{
    g_status = {};
    g_status.state = WifiConnectionState::Initializing;
    g_status.credentials_present = (active_credential() != nullptr);
    g_status.link_status = CYW43_LINK_DOWN;
    g_status.last_error = 0;
    reset_internet_probe_status();
    reset_internet_probe_timers();
    clear_ip_address();
    g_status.auth_mode.fill('\0');
    g_status.ssid.fill('\0');

    if (!g_status.credentials_present) {
        g_status.state = WifiConnectionState::Unconfigured;
        std::printf("WiFi disabled: no credentials configured\n");
        return;
    }

    const WifiCredential* credential = active_credential();
    copy_text(g_status.ssid, credential->ssid);
    copy_auth_mode(configured_auth_mode(*credential));

    const int rc = cyw43_arch_init_with_country(kWifiCountry);
    if (rc != 0) {
        g_status.state = WifiConnectionState::Error;
        g_status.last_error = rc;
        std::printf("WiFi init failed: %d\n", rc);
        return;
    }

    g_cyw43_initialized = true;
    cyw43_arch_enable_sta_mode();
    update_mac_address();
    advertise_hostname();
    std::printf("WiFi country code set to 0x%08lx\n", static_cast<unsigned long>(kWifiCountry));
    std::printf("WiFi MAC %s host=%s\n",
                g_status.mac_address[0] ? g_status.mac_address.data() : "-",
                kAdvertisedHostName);

    attempt_connect();
}

bool update()
{
    if (!g_cyw43_initialized) {
        return false;
    }

    bool changed = false;

    const int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    const bool ip_ready = has_ipv4_address();
    if (link_status != g_last_observed_link_status) {
        g_last_observed_link_status = link_status;
        g_status.link_status = link_status;
        PERIODIC_LOG("WiFi link status changed: %d\n", link_status);

        if (link_status == CYW43_LINK_UP || ip_ready) {
            apply_static_ip_config();
            update_ip_address();
            g_status.state = WifiConnectionState::Connected;
            start_sntp_if_needed();
            g_next_retry = nil_time;
            g_wait_for_ip_deadline = nil_time;
            g_next_probe = get_absolute_time();
        } else if (link_status == CYW43_LINK_JOIN || link_status == CYW43_LINK_NOIP) {
            if (kUseStaticIp && apply_static_ip_config()) {
                g_status.state = WifiConnectionState::Connected;
                start_sntp_if_needed();
                g_next_retry = nil_time;
                g_wait_for_ip_deadline = nil_time;
                g_next_probe = get_absolute_time();
                changed = true;
                return changed;
            }
            g_status.state = WifiConnectionState::WaitingForIp;
            if (is_nil_time(g_wait_for_ip_deadline)) {
                schedule_wait_for_ip_deadline();
            }
        } else {
            clear_ip_address();
            g_status.state = state_from_link_status(link_status);
            g_wait_for_ip_deadline = nil_time;
            reset_internet_probe_status();
            reset_internet_probe_timers();
            stop_sntp_if_started();
            schedule_retry();
        }

        changed = true;
    }

    if (ip_ready && (g_status.state != WifiConnectionState::Connected || !g_status.ip_address[0])) {
        g_status.link_status = link_status;
        g_last_observed_link_status = link_status;
        apply_static_ip_config();
        update_ip_address();
        g_status.state = WifiConnectionState::Connected;
        start_sntp_if_needed();
        g_next_retry = nil_time;
        g_wait_for_ip_deadline = nil_time;
        g_next_probe = get_absolute_time();
        changed = true;
    }

    if (!is_nil_time(g_wait_for_ip_deadline) && absolute_time_diff_us(get_absolute_time(), g_wait_for_ip_deadline) <= 0) {
        PERIODIC_LOG("WiFi DHCP wait expired; retrying connection\n");
        g_wait_for_ip_deadline = nil_time;
        schedule_retry();
        changed = true;
    }

    if (!is_nil_time(g_next_retry) && absolute_time_diff_us(get_absolute_time(), g_next_retry) <= 0) {
        g_next_retry = nil_time;
        changed = attempt_connect() || changed;
    }

    changed = update_internet_probe() || changed;

    return changed;
}

const WifiStatus& status()
{
    return g_status;
}

}  // namespace wifi_manager
