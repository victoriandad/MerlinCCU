#include "wifi_manager.h"

#include <cstdio>
#include <cstring>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "cyw43.h"

namespace {

#if __has_include("wifi_credentials.h")
#include "wifi_credentials.h"
constexpr const char* kWifiSsid = WIFI_SSID;
constexpr const char* kWifiPassword = WIFI_PASSWORD;
#else
constexpr const char* kWifiSsid = "";
constexpr const char* kWifiPassword = "";
#endif

constexpr uint32_t kRetryDelayMs = 10000;

WifiStatus g_status = {};
bool g_cyw43_initialized = false;
bool g_connect_in_progress = false;
int g_last_observed_link_status = CYW43_LINK_DOWN;
absolute_time_t g_next_retry = nil_time;

bool credentials_present()
{
    return kWifiSsid[0] != '\0';
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

void update_ip_address()
{
    g_status.ip_address.fill('\0');

    if (netif_default) {
        const char* ip_text = ip4addr_ntoa(netif_ip4_addr(netif_default));
        copy_ip_text(g_status.ip_address, ip_text);
    }
}

uint32_t auth_mode()
{
    return (kWifiPassword[0] != '\0') ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN;
}

void apply_link_status(int link_status)
{
    g_status.link_status = link_status;
    g_status.last_error = 0;

    switch (link_status) {
    case CYW43_LINK_DOWN:
        g_status.state = g_connect_in_progress ? WifiConnectionState::Connecting : WifiConnectionState::ConnectFailed;
        g_status.ip_address.fill('\0');
        if (!g_connect_in_progress) {
            g_next_retry = make_timeout_time_ms(kRetryDelayMs);
        }
        break;
    case CYW43_LINK_JOIN:
        g_status.state = WifiConnectionState::Connecting;
        g_status.ip_address.fill('\0');
        break;
    case CYW43_LINK_NOIP:
        g_status.state = WifiConnectionState::WaitingForIp;
        update_ip_address();
        break;
    case CYW43_LINK_UP:
        g_status.state = WifiConnectionState::Connected;
        update_ip_address();
        g_connect_in_progress = false;
        break;
    case CYW43_LINK_BADAUTH:
        g_status.state = WifiConnectionState::AuthFailed;
        g_status.last_error = link_status;
        g_connect_in_progress = false;
        g_next_retry = nil_time;
        break;
    case CYW43_LINK_NONET:
        g_status.state = WifiConnectionState::NoNetwork;
        g_status.last_error = link_status;
        g_connect_in_progress = false;
        g_next_retry = make_timeout_time_ms(kRetryDelayMs);
        break;
    case CYW43_LINK_FAIL:
    default:
        g_status.state = WifiConnectionState::ConnectFailed;
        g_status.last_error = link_status;
        g_connect_in_progress = false;
        g_next_retry = make_timeout_time_ms(kRetryDelayMs);
        break;
    }
}

bool start_connect()
{
    const int rc = cyw43_arch_wifi_connect_async(kWifiSsid, kWifiPassword, auth_mode());
    if (rc != 0) {
        g_status.state = WifiConnectionState::Error;
        g_status.last_error = rc;
        g_connect_in_progress = false;
        std::printf("WiFi connect start failed: %d\n", rc);
        return true;
    }

    g_connect_in_progress = true;
    g_status.state = WifiConnectionState::Connecting;
    g_status.last_error = 0;
    g_status.ip_address.fill('\0');
    std::printf("WiFi connecting to SSID '%s'\n", g_status.ssid.data());
    return true;
}

}  // namespace

namespace wifi_manager {

void init()
{
    g_status = {};
    g_status.state = WifiConnectionState::Disabled;
    g_status.credentials_present = credentials_present();
    g_status.last_error = 0;
    g_status.link_status = CYW43_LINK_DOWN;
    copy_text(g_status.ssid, kWifiSsid);
    g_status.ip_address.fill('\0');
    g_cyw43_initialized = false;
    g_connect_in_progress = false;
    g_last_observed_link_status = CYW43_LINK_DOWN;
    g_next_retry = nil_time;

    if (!g_status.credentials_present) {
        g_status.state = WifiConnectionState::Unconfigured;
        std::printf("WiFi disabled: no credentials configured\n");
        return;
    }

    g_status.state = WifiConnectionState::Initializing;

    const int rc = cyw43_arch_init();
    if (rc != 0) {
        g_status.state = WifiConnectionState::Error;
        g_status.last_error = rc;
        std::printf("WiFi init failed: %d\n", rc);
        return;
    }

    g_cyw43_initialized = true;
    cyw43_arch_enable_sta_mode();
    start_connect();
}

bool update()
{
    if (!g_cyw43_initialized) {
        return false;
    }

    bool changed = false;
    const int link_status = cyw43_wifi_link_status(&cyw43_state, CYW43_ITF_STA);

    if (link_status != g_last_observed_link_status) {
        g_last_observed_link_status = link_status;
        apply_link_status(link_status);
        std::printf("WiFi link status changed: %d\n", link_status);
        changed = true;
    } else if (link_status == CYW43_LINK_UP) {
        WifiStatus previous = g_status;
        update_ip_address();
        changed = changed || (previous.ip_address != g_status.ip_address);
    }

    if (!g_connect_in_progress &&
        (g_status.state == WifiConnectionState::ConnectFailed ||
         g_status.state == WifiConnectionState::NoNetwork ||
         g_status.state == WifiConnectionState::Connecting) &&
        !is_nil_time(g_next_retry) &&
        absolute_time_diff_us(get_absolute_time(), g_next_retry) <= 0) {
        changed = start_connect() || changed;
    }

    return changed;
}

const WifiStatus& status()
{
    return g_status;
}

}  // namespace wifi_manager
