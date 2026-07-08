#include "wifi_manager.h"

#include "config_manager.h"
#include "debug_logging.h"
#include "lwip/apps/netbiosns.h"
#include "lwip/apps/sntp.h"
#include "lwip/dhcp.h"
#include "lwip/dns.h"
#include "lwip/ip4_addr.h"
#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "pico/cyw43_arch.h"
#include "pico/error.h"
#include "pico/stdlib.h"
#include "text_utils.h"
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace
{

struct WifiCredential
{
    const char* ssid;
    const char* password;
};

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
#if defined(WIFI_STATIC_IP_ADDR) && defined(WIFI_STATIC_IP_NETMASK) &&                             \
    defined(WIFI_STATIC_IP_GATEWAY)
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
constexpr uint8_t kInternetFailureRoamThreshold = 3U;
constexpr char kInternetProbeHostName[] = "dns.google";
constexpr char kAdvertisedHostName[] = "MerlinCCU";
constexpr char kNtpServerHostName[] = "pool.ntp.org";
constexpr size_t kNoWifiCredentialIndex = static_cast<size_t>(-1);

#if __has_include("wifi_credentials.h")
#if defined(WIFI_CREDENTIALS_COUNT) && defined(WIFI_CREDENTIALS)
inline constexpr const WifiCredential* kWifiCredentials = WIFI_CREDENTIALS;
constexpr size_t kWifiCredentialCount = WIFI_CREDENTIALS_COUNT;
#else
constexpr WifiCredential kDefaultWifiCredentials[] = {
    {kWifiSsid, kWifiPassword},
};
inline constexpr const WifiCredential* kWifiCredentials = kDefaultWifiCredentials;
constexpr size_t kWifiCredentialCount =
    sizeof(kDefaultWifiCredentials) / sizeof(kDefaultWifiCredentials[0]);
#endif
#else
constexpr WifiCredential kDefaultWifiCredentials[] = {
    {"", ""},
};
inline constexpr const WifiCredential* kWifiCredentials = kDefaultWifiCredentials;
constexpr size_t kWifiCredentialCount =
    sizeof(kDefaultWifiCredentials) / sizeof(kDefaultWifiCredentials[0]);
#endif

// The Wi-Fi manager keeps a small amount of explicit timing state rather than
// relying on blocking waits everywhere. That lets the main loop stay responsive
// while DHCP, DNS, and retry behaviour are still visible on the status page.
WifiStatus g_status = {};
bool g_cyw43_initialized = false;
int g_last_observed_link_status = CYW43_LINK_DOWN;
absolute_time_t g_next_retry = nil_time;
absolute_time_t g_wait_for_ip_deadline = nil_time;
// Guards the async join request itself: if cyw43_wifi_link_status() never
// leaves its pre-connect value (a silent radio-level failure), this is the
// only thing that notices and moves on to the next credential/retry. Once
// any link-status change is observed, update() clears this and the existing
// WaitingForIp/DHCP deadline takes over.
absolute_time_t g_connect_deadline = nil_time;
absolute_time_t g_next_probe = nil_time;
absolute_time_t g_probe_started_at = nil_time;
absolute_time_t g_probe_deadline = nil_time;
bool g_probe_in_flight = false;
bool g_netbios_started = false;
bool g_sntp_started = false;
size_t g_next_credential_index = kNoWifiCredentialIndex;
uint8_t g_internet_probe_failures = 0U;
char g_runtime_host_name[32] = {};

/// @brief Returns whether one configured Wi-Fi credential has a usable SSID.
bool credential_valid(const WifiCredential& credential)
{
    return credential.ssid && credential.ssid[0] != '\0';
}

/// @brief Returns whether the web-configured Wi-Fi SSID should be tried first.
bool runtime_wifi_configured()
{
    const RuntimeConfig& config = config_manager::settings();
    return config.wifi_ssid[0] != '\0';
}

/// @brief Counts every compile-time candidate slot plus the optional runtime fallback.
size_t credential_candidate_count()
{
    return kWifiCredentialCount + (runtime_wifi_configured() ? 1U : 0U);
}

/// @brief Returns a credential by ordered candidate index, skipping invalid entries.
/// @details Compile-time credentials keep their definition order. The saved
/// web-config SSID is appended as a fallback so stale runtime settings cannot
/// unexpectedly override the local preferred AP list.
const WifiCredential* credential_at_index(size_t index)
{
    if (index < kWifiCredentialCount)
    {
        if (!credential_valid(kWifiCredentials[index]))
        {
            return nullptr;
        }

        return &kWifiCredentials[index];
    }

    index -= kWifiCredentialCount;
    if (index != 0U || !runtime_wifi_configured())
    {
        return nullptr;
    }

    const RuntimeConfig& config = config_manager::settings();
    static WifiCredential runtime_credential = {};
    runtime_credential = {config.wifi_ssid.data(), config.wifi_password.data()};
    return credential_valid(runtime_credential) ? &runtime_credential : nullptr;
}

/// @brief Finds the first usable Wi-Fi credential candidate.
size_t first_valid_credential_index()
{
    const size_t candidate_count = credential_candidate_count();
    for (size_t i = 0; i < candidate_count; ++i)
    {
        if (credential_at_index(i) != nullptr)
        {
            return i;
        }
    }

    return kNoWifiCredentialIndex;
}

/// @brief Finds the next usable Wi-Fi credential after the supplied candidate index.
size_t next_valid_credential_index_after(size_t current_index)
{
    const size_t candidate_count = credential_candidate_count();
    for (size_t i = current_index + 1U; i < candidate_count; ++i)
    {
        if (credential_at_index(i) != nullptr)
        {
            return i;
        }
    }

    return kNoWifiCredentialIndex;
}

/// @brief Returns whether at least one usable Wi-Fi credential is configured.
bool credentials_available()
{
    return first_valid_credential_index() != kNoWifiCredentialIndex;
}

/// @brief Counts usable credentials so roaming only runs when there is another AP to try.
size_t valid_credential_count()
{
    size_t count = 0;
    const size_t candidate_count = credential_candidate_count();
    for (size_t i = 0; i < candidate_count; ++i)
    {
        if (credential_at_index(i) != nullptr)
        {
            ++count;
        }
    }

    return count;
}

/// @brief Returns whether failing over to a different configured AP is possible.
bool alternate_credentials_available()
{
    return valid_credential_count() > 1U;
}

/// @brief Builds a DNS/NetBIOS-safe hostname from the configured device name.
const char* advertised_host_name()
{
    const char* source = config_manager::device_name();
    size_t out = 0;

    for (size_t i = 0; source[i] != '\0' && out + 1 < sizeof(g_runtime_host_name); ++i)
    {
        const char c = source[i];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))
        {
            g_runtime_host_name[out++] = c;
        }
        else if (c == '-' || c == '_')
        {
            g_runtime_host_name[out++] = '-';
        }
    }

    if (out == 0)
    {
        std::snprintf(g_runtime_host_name, sizeof(g_runtime_host_name), "%s", kAdvertisedHostName);
    }
    else
    {
        g_runtime_host_name[out] = '\0';
    }

    return g_runtime_host_name;
}

/// @brief Returns the credential password or `nullptr` for open networks.
const char* credential_password(const WifiCredential& credential)
{
    return (credential.password && credential.password[0] != '\0') ? credential.password : nullptr;
}

// The UI model stores fixed-size text snapshots so screen rendering never
// depends on borrowed pointers into lwIP or temporary parse buffers.
using text_utils::copy_text;

/// @brief Copies an IPv4 string into the fixed-size status buffer.
void copy_ip_text(std::array<char, 16>& dst, const char* src)
{
    dst.fill('\0');
    if (!src)
    {
        return;
    }

    std::snprintf(dst.data(), dst.size(), "%s", src);
}

/// @brief Formats the station MAC address into human-readable text.
void copy_mac_text(const uint8_t mac[6])
{
    g_status.mac_address.fill('\0');
    std::snprintf(g_status.mac_address.data(), g_status.mac_address.size(),
                  "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/// @brief Returns the compact text label for a CYW43 auth mode.
const char* auth_mode_text(uint32_t auth_mode)
{
    switch (auth_mode)
    {
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

/// @brief Chooses the auth mode implied by the configured credential.
uint32_t configured_auth_mode(const WifiCredential& credential)
{
    return (credential.password && credential.password[0] != '\0') ? kConfiguredWifiAuth
                                                                   : CYW43_AUTH_OPEN;
}

/// @brief Copies the current auth-mode label into the status snapshot.
void copy_auth_mode(uint32_t auth_mode)
{
    g_status.auth_mode.fill('\0');
    std::snprintf(g_status.auth_mode.data(), g_status.auth_mode.size(), "%s",
                  auth_mode_text(auth_mode));
}

/// @brief Clears the cached IP address text.
void clear_ip_address()
{
    g_status.ip_address.fill('\0');
}

/// @brief Clears the public internet probe status fields.
void reset_internet_probe_status()
{
    g_status.internet_reachable = false;
    g_status.internet_probe_pending = false;
    g_status.internet_rtt_ms = -1;
}

/// @brief Clears all internal timers associated with internet probing.
void reset_internet_probe_timers()
{
    g_probe_in_flight = false;
    g_probe_started_at = nil_time;
    g_probe_deadline = nil_time;
    g_next_probe = nil_time;
}

/// @brief Clears the consecutive upstream-probe failure counter.
void reset_internet_probe_failures()
{
    g_internet_probe_failures = 0U;
}

/// @brief Starts SNTP once the station has a usable network path.
/// @details Time sync is deferred until Wi-Fi is genuinely up so connection
/// failures remain attributable to association or DHCP rather than later NTP noise.
void start_sntp_if_needed()
{
    if (g_sntp_started)
    {
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

/// @brief Stops SNTP if time synchronization was previously started.
void stop_sntp_if_started()
{
    if (!g_sntp_started)
    {
        return;
    }

    cyw43_arch_lwip_begin();
    sntp_stop();
    cyw43_arch_lwip_end();

    g_sntp_started = false;
}

/// @brief Refreshes the cached IP address from lwIP.
void update_ip_address()
{
    clear_ip_address();

    cyw43_arch_lwip_begin();
    if (netif_default)
    {
        const char* ip_text = ip4addr_ntoa(netif_ip4_addr(netif_default));
        copy_ip_text(g_status.ip_address, ip_text);
    }
    cyw43_arch_lwip_end();
}

/// @brief Prints the current station IP address to the serial console.
/// @details Headless setups rely on the console log to discover the web UI
/// address when no display is attached.
void log_ip_address()
{
    std::printf("WiFi IP address: %s\n", g_status.ip_address[0] ? g_status.ip_address.data()
                                                                : "(none)");
}

/// @brief Returns whether the default netif currently has a usable IPv4 address.
bool has_ipv4_address()
{
    bool has_address = false;

    cyw43_arch_lwip_begin();
    if (netif_default)
    {
        const ip4_addr_t* address = netif_ip4_addr(netif_default);
        has_address = address && !ip4_addr_isany_val(*address);
    }
    cyw43_arch_lwip_end();

    return has_address;
}

/// @brief Refreshes the cached station MAC address.
void update_mac_address()
{
    uint8_t mac[6] = {};
    if (cyw43_wifi_get_mac(&cyw43_state, CYW43_ITF_STA, mac) == 0)
    {
        copy_mac_text(mac);
    }
    else
    {
        g_status.mac_address.fill('\0');
    }
}

/// @brief Parses one IPv4 configuration field and logs failures with context.
bool parse_ip4_or_log(const char* text, ip4_addr_t* out, const char* label)
{
    if (!text || !ip4addr_aton(text, out))
    {
        std::printf("WiFi static IP config invalid %s='%s'\n", label, text ? text : "(null)");
        return false;
    }

    return true;
}

/// @brief Applies the configured static IPv4 settings to the active netif.
/// @details This is only used as a recovery path for networks where association
/// succeeds but DHCP never completes, so the firmware still defaults to DHCP.
bool apply_static_ip_config()
{
    if (!kUseStaticIp)
    {
        return false;
    }

    // Static IP is treated as an explicit recovery path for networks where the
    // radio joins successfully but DHCP never converges. It is not the default
    // path because DHCP keeps the firmware more portable between networks.
    ip4_addr_t address;
    ip4_addr_t netmask;
    ip4_addr_t gateway;
    if (!parse_ip4_or_log(kStaticIpAddress, &address, "addr") ||
        !parse_ip4_or_log(kStaticIpNetmask, &netmask, "netmask") ||
        !parse_ip4_or_log(kStaticIpGateway, &gateway, "gateway"))
    {
        return false;
    }

    cyw43_arch_lwip_begin();
    if (!netif_default)
    {
        cyw43_arch_lwip_end();
        std::printf("WiFi static IP config skipped: no default netif\n");
        return false;
    }

    dhcp_stop(netif_default);
    netif_set_addr(netif_default, &address, &netmask, &gateway);
    if (kStaticIpDns && kStaticIpDns[0] != '\0')
    {
        ip_addr_t dns;
        if (ipaddr_aton(kStaticIpDns, &dns))
        {
            dns_setserver(0, &dns);
        }
        else
        {
            std::printf("WiFi static IP config invalid dns='%s'\n", kStaticIpDns);
        }
    }
    cyw43_arch_lwip_end();

    update_ip_address();
    std::printf("WiFi static IP applied: ip=%s gw=%s mask=%s\n", kStaticIpAddress, kStaticIpGateway,
                kStaticIpNetmask);
    return true;
}

WifiConnectionState state_from_link_status(int link_status)
{
    switch (link_status)
    {
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

/// @brief Schedules the next Wi-Fi reconnect attempt.
void schedule_retry()
{
    g_next_retry = make_timeout_time_ms(kRetryDelayMs);
}

/// @brief Starts a fresh pass through the configured credential list after a pause.
void schedule_credential_cycle_retry()
{
    g_next_credential_index = first_valid_credential_index();
    schedule_retry();
}

/// @brief Moves to the next configured credential, or delays before restarting the list.
void schedule_next_credential_or_retry(size_t current_index)
{
    g_connect_deadline = nil_time;

    if (current_index == kNoWifiCredentialIndex)
    {
        schedule_credential_cycle_retry();
        return;
    }

    const size_t next_index = next_valid_credential_index_after(current_index);
    if (next_index != kNoWifiCredentialIndex)
    {
        g_next_credential_index = next_index;
        g_next_retry = get_absolute_time();
        PERIODIC_LOG("WiFi trying next configured credential\n");
        return;
    }

    schedule_credential_cycle_retry();
}

/// @brief Moves immediately to the next available credential when the current AP has no internet.
void schedule_next_credential_for_roam(size_t current_index)
{
    const size_t next_index = next_valid_credential_index_after(current_index);
    g_next_credential_index =
        (next_index != kNoWifiCredentialIndex) ? next_index : first_valid_credential_index();
    g_next_retry = get_absolute_time();
}

/// @brief Starts the wait-for-DHCP deadline timer.
void schedule_wait_for_ip_deadline()
{
    g_wait_for_ip_deadline = make_timeout_time_ms(kDhcpWaitTimeoutMs);
}

/// @brief Advertises the device name through both lwIP hostname and NetBIOS.
/// @details Using both mechanisms makes the Pico easier to discover from mixed
/// desktop and mobile clients during local-network bring-up.
void advertise_hostname()
{
    const char* host_name = advertised_host_name();
    cyw43_arch_lwip_begin();
    if (netif_default)
    {
        netif_set_hostname(netif_default, host_name);
        if (!g_netbios_started)
        {
            netbiosns_init();
            netbiosns_set_name(host_name);
            g_netbios_started = true;
        }
    }
    cyw43_arch_lwip_end();
}

/// @brief Records one internet probe outcome and optionally roams to the next AP.
/// @details Association and DHCP can succeed on a hotspot that has no usable
/// upstream path. After several consecutive DNS probe failures, the manager
/// leaves that AP and tries the next configured credential. Single-network
/// installs stay associated so local diagnostics and the web UI remain reachable.
void record_internet_probe_result(bool reachable)
{
    if (reachable)
    {
        reset_internet_probe_failures();
        g_status.internet_reachable = true;
        g_status.internet_probe_pending = false;
        g_status.internet_rtt_ms = -1;
        return;
    }

    g_status.internet_reachable = false;
    g_status.internet_probe_pending = false;
    g_status.internet_rtt_ms = -1;

    if (g_internet_probe_failures < kInternetFailureRoamThreshold)
    {
        ++g_internet_probe_failures;
    }

    if (g_internet_probe_failures < kInternetFailureRoamThreshold ||
        !alternate_credentials_available())
    {
        return;
    }

    const size_t previous_index = g_next_credential_index;
    schedule_next_credential_for_roam(previous_index);
    if (g_next_credential_index == kNoWifiCredentialIndex ||
        g_next_credential_index == previous_index)
    {
        return;
    }

    std::printf("WiFi internet probe failed %u times on '%s'; trying next configured credential\n",
                static_cast<unsigned int>(g_internet_probe_failures),
                g_status.ssid[0] ? g_status.ssid.data() : "-");

    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    clear_ip_address();
    g_status.state = WifiConnectionState::ConnectFailed;
    g_status.link_status = CYW43_LINK_DOWN;
    g_last_observed_link_status = CYW43_LINK_DOWN;
    g_wait_for_ip_deadline = nil_time;
    reset_internet_probe_failures();
    reset_internet_probe_status();
    reset_internet_probe_timers();
    stop_sntp_if_started();
}

/// @brief Completes one asynchronous internet reachability probe.
/// @details A successful DNS lookup is treated as a lightweight signal that the
/// device has a usable upstream path, not just a local Wi-Fi association.
void dns_probe_found(const char* name, const ip_addr_t* ipaddr, void* callback_arg)
{
    (void)name;
    (void)callback_arg;
    if (!g_probe_in_flight)
    {
        return;
    }

    g_probe_in_flight = false;
    g_probe_deadline = nil_time;
    g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
    record_internet_probe_result(ipaddr != nullptr);
    PERIODIC_LOG("WiFi internet probe %s host=%s\n", ipaddr ? "ok" : "failed",
                 kInternetProbeHostName);
}

/// @brief Launches one asynchronous DNS-based internet reachability probe.
/// @details DNS is used here because lwIP already exposes the resolver path and
/// it is lighter than introducing an HTTP or ICMP-specific health check.
bool start_dns_probe()
{
    ip_addr_t resolved = {};

    cyw43_arch_lwip_begin();
    const err_t kResult =
        dns_gethostbyname(kInternetProbeHostName, &resolved, dns_probe_found, nullptr);
    cyw43_arch_lwip_end();

    if (kResult == ERR_OK)
    {
        g_probe_in_flight = true;
        g_probe_started_at = get_absolute_time();
        dns_probe_found(kInternetProbeHostName, &resolved, nullptr);
        return true;
    }

    if (kResult != ERR_INPROGRESS)
    {
        std::printf("WiFi internet probe start failed: %d\n", static_cast<int>(kResult));
        g_probe_in_flight = false;
        g_probe_deadline = nil_time;
        g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
        record_internet_probe_result(false);
        return true;
    }

    g_probe_in_flight = true;
    g_probe_started_at = get_absolute_time();
    g_probe_deadline = make_timeout_time_ms(kInternetProbeTimeoutMs);
    g_status.internet_probe_pending = true;
    PERIODIC_LOG("WiFi internet probe resolving %s\n", kInternetProbeHostName);
    return true;
}

/// @brief Advances the periodic internet reachability probe state.
/// @details Probe state is explicitly reset when Wi-Fi or IP state drops so the
/// UI never shows stale "internet ok" information after a disconnect.
bool update_internet_probe()
{
    if (g_status.state != WifiConnectionState::Connected || !g_status.ip_address[0])
    {
        // Probe state is cleared immediately when link/IP state drops so the
        // status page never shows stale "internet ok" information after disconnect.
        if (g_status.internet_probe_pending || g_status.internet_reachable ||
            g_status.internet_rtt_ms >= 0)
        {
            reset_internet_probe_status();
            reset_internet_probe_timers();
            return true;
        }
        return false;
    }

    if (g_probe_in_flight)
    {
        if (!is_nil_time(g_probe_deadline) &&
            absolute_time_diff_us(get_absolute_time(), g_probe_deadline) <= 0)
        {
            g_probe_in_flight = false;
            g_probe_deadline = nil_time;
            g_next_probe = make_timeout_time_ms(kInternetProbeIntervalMs);
            record_internet_probe_result(false);
            PERIODIC_LOG("WiFi internet probe timeout for %s\n", kInternetProbeHostName);
            return true;
        }
        return false;
    }

    if (is_nil_time(g_next_probe) || absolute_time_diff_us(get_absolute_time(), g_next_probe) <= 0)
    {
        g_next_probe = nil_time;
        return start_dns_probe();
    }

    return false;
}

/// @brief Attempts one Wi-Fi connection using the current ordered credential candidate.
/// @details The status model is populated before association completes so the UI
/// can explain what network and auth mode the firmware is currently trying.
bool attempt_connect()
{
    if (g_next_credential_index == kNoWifiCredentialIndex)
    {
        g_next_credential_index = first_valid_credential_index();
    }

    const size_t credential_index = g_next_credential_index;
    const WifiCredential* credential = credential_at_index(credential_index);
    if (!credential || !credential_valid(*credential))
    {
        return false;
    }

    const uint32_t kAuthMode = configured_auth_mode(*credential);
    copy_text(g_status.ssid, credential->ssid);
    copy_auth_mode(kAuthMode);
    clear_ip_address();
    g_status.state = WifiConnectionState::Connecting;
    g_status.last_error = 0;

    std::printf("WiFi connecting to SSID '%s' auth=%s timeout=%lums\n", credential->ssid,
                auth_mode_text(kAuthMode), static_cast<unsigned long>(kConnectTimeoutMs));

    // This only starts the join; it must not block. The main loop's update()
    // already polls cyw43_wifi_link_status() every iteration and drives the
    // Connecting -> WaitingForIp/Connected/failed transitions from there (see
    // the kLinkStatus != g_last_observed_link_status branch below). Blocking
    // here for up to kConnectTimeoutMs previously starved the whole main loop
    // -- including input handling and any future watchdog pet -- for as long
    // as 30 seconds on every reconnect.
    const int kRc =
        cyw43_arch_wifi_connect_async(credential->ssid, credential_password(*credential), kAuthMode);
    if (kRc != 0)
    {
        // The driver refused to even start the join (e.g. busy). Fail this
        // attempt immediately rather than leaving the state machine parked in
        // Connecting with nothing to ever move it forward.
        g_status.last_error = kRc;
        g_status.state = WifiConnectionState::ConnectFailed;
        std::printf("WiFi connect request failed to start for '%s' rc=%d\n", credential->ssid,
                    kRc);
        g_connect_deadline = nil_time;
        g_wait_for_ip_deadline = nil_time;
        reset_internet_probe_status();
        reset_internet_probe_timers();
        stop_sntp_if_started();
        schedule_next_credential_or_retry(credential_index);
        return true;
    }

    // Arm a deadline for the join itself: if cyw43_wifi_link_status() never
    // reports any change (a silent radio-level failure), update()'s top-level
    // deadline check is the only thing that notices and moves on.
    g_connect_deadline = make_timeout_time_ms(kConnectTimeoutMs);
    return true;
}

} // namespace

namespace wifi_manager
{

/// @brief Initializes the Wi-Fi manager and starts the first connect attempt.
void init()
{
    g_status = {};
    g_status.state = WifiConnectionState::Initializing;
    g_next_credential_index = first_valid_credential_index();
    g_status.credentials_present = credentials_available();
    g_status.link_status = CYW43_LINK_DOWN;
    g_status.last_error = 0;
    reset_internet_probe_status();
    reset_internet_probe_timers();
    clear_ip_address();
    g_status.auth_mode.fill('\0');
    g_status.ssid.fill('\0');

    // Bail out early if there is nothing to connect to; the rest of the
    // manager assumes at least one candidate credential exists.
    if (!g_status.credentials_present)
    {
        g_status.state = WifiConnectionState::Unconfigured;
        std::printf("WiFi disabled: no credentials configured\n");
        return;
    }

    const WifiCredential* credential = credential_at_index(g_next_credential_index);
    copy_text(g_status.ssid, credential->ssid);
    copy_auth_mode(configured_auth_mode(*credential));

    // Bring up the radio before publishing any identity details, because the
    // MAC address and hostname advertisement depend on cyw43 being active.
    const int kRc = cyw43_arch_init_with_country(kWifiCountry);
    if (kRc != 0)
    {
        g_status.state = WifiConnectionState::Error;
        g_status.last_error = kRc;
        std::printf("WiFi init failed: %d\n", kRc);
        return;
    }

    g_cyw43_initialized = true;
    cyw43_arch_enable_sta_mode();
    update_mac_address();
    advertise_hostname();
    std::printf("WiFi country code set to 0x%08lx\n", static_cast<unsigned long>(kWifiCountry));
    std::printf("WiFi MAC %s host=%s\n",
                g_status.mac_address[0] ? g_status.mac_address.data() : "-",
                advertised_host_name());

    attempt_connect();
}

/// @brief Advances the Wi-Fi connection state machine.
bool update()
{
    if (!g_cyw43_initialized)
    {
        return false;
    }

    bool changed = false;

    // Poll both cyw43 link state and lwIP IP state every loop because they do
    // not always transition at the same moment.
    const int kLinkStatus = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);
    const bool kIpReady = has_ipv4_address();
    if (kLinkStatus != g_last_observed_link_status)
    {
        g_last_observed_link_status = kLinkStatus;
        g_status.link_status = kLinkStatus;
        PERIODIC_LOG("WiFi link status changed: %d\n", kLinkStatus);

        // cyw43 link notifications and lwIP IP state do not always advance in
        // lockstep, so the state machine deliberately cross-checks both before
        // deciding whether the firmware is truly connected.
        if (kLinkStatus == CYW43_LINK_UP || kIpReady)
        {
            g_connect_deadline = nil_time;
            apply_static_ip_config();
            update_ip_address();
            g_status.state = WifiConnectionState::Connected;
            start_sntp_if_needed();
            g_next_retry = nil_time;
            g_wait_for_ip_deadline = nil_time;
            reset_internet_probe_failures();
            g_next_probe = get_absolute_time();
        }
        else if (kLinkStatus == CYW43_LINK_JOIN || kLinkStatus == CYW43_LINK_NOIP)
        {
            g_connect_deadline = nil_time;
            if (kUseStaticIp && apply_static_ip_config())
            {
                g_status.state = WifiConnectionState::Connected;
                start_sntp_if_needed();
                g_next_retry = nil_time;
                g_wait_for_ip_deadline = nil_time;
                reset_internet_probe_failures();
                g_next_probe = get_absolute_time();
                changed = true;
                return changed;
            }
            g_status.state = WifiConnectionState::WaitingForIp;
            if (is_nil_time(g_wait_for_ip_deadline))
            {
                schedule_wait_for_ip_deadline();
            }
        }
        else
        {
            clear_ip_address();
            g_status.state = state_from_link_status(kLinkStatus);
            g_wait_for_ip_deadline = nil_time;
            reset_internet_probe_status();
            reset_internet_probe_timers();
            stop_sntp_if_started();
            schedule_next_credential_or_retry(g_next_credential_index);
        }

        changed = true;
    }

    if (kIpReady && (g_status.state != WifiConnectionState::Connected || !g_status.ip_address[0]))
    {
        // This reconciliation path exists because DHCP can complete after the
        // initial link transition has already been observed and logged.
        g_status.link_status = kLinkStatus;
        g_last_observed_link_status = kLinkStatus;
        apply_static_ip_config();
        update_ip_address();
        g_status.state = WifiConnectionState::Connected;
        start_sntp_if_needed();
        log_ip_address();
        g_next_retry = nil_time;
        g_wait_for_ip_deadline = nil_time;
        reset_internet_probe_failures();
        g_next_probe = get_absolute_time();
        changed = true;
    }

    if (!is_nil_time(g_connect_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_connect_deadline) <= 0)
    {
        // cyw43_wifi_link_status() never moved off its pre-connect value for
        // this whole window -- a silent radio-level failure the change-based
        // branch above cannot see because there was never a change to detect.
        PERIODIC_LOG("WiFi connect attempt timed out with no link-status change; retrying\n");
        g_connect_deadline = nil_time;
        schedule_next_credential_or_retry(g_next_credential_index);
        changed = true;
    }

    if (!is_nil_time(g_wait_for_ip_deadline) &&
        absolute_time_diff_us(get_absolute_time(), g_wait_for_ip_deadline) <= 0)
    {
        PERIODIC_LOG("WiFi DHCP wait expired; retrying connection\n");
        g_wait_for_ip_deadline = nil_time;
        schedule_next_credential_or_retry(g_next_credential_index);
        changed = true;
    }

    if (!is_nil_time(g_next_retry) && absolute_time_diff_us(get_absolute_time(), g_next_retry) <= 0)
    {
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

} // namespace wifi_manager
