#pragma once

/// @brief Local Wi-Fi credentials for Pico W bring-up.
/// @details Copy this file to `config/wifi_credentials.h`, fill in your real values,
/// and keep that local file out of version control.
///
/// The preferred form is an ordered list. The firmware tries entries from top
/// to bottom; if all fail, it waits briefly and starts again from the first
/// entry. An AP that associates but repeatedly fails the internet probe is also
/// skipped when another credential is available. Leave the password empty for an
/// open network.
///
/// Quick start:
/// 1. Copy this file to `config/wifi_credentials.h`.
/// 2. Replace the SSIDs and passwords in `kLocalWifiCredentials`.
/// 3. Leave the auth and country defaults alone unless your AP needs different
///    values.
/// 4. Only enable the static IP block if your router never completes DHCP.

#define WIFI_COUNTRY CYW43_COUNTRY_UK
#define WIFI_AUTH CYW43_AUTH_WPA2_MIXED_PSK

inline constexpr WifiCredential kLocalWifiCredentials[] = {
    {"home-ssid", "home-password"},
    {"phone-hotspot-ssid", "phone-hotspot-password"},
};

#define WIFI_CREDENTIALS kLocalWifiCredentials
#define WIFI_CREDENTIALS_COUNT (sizeof(kLocalWifiCredentials) / sizeof(kLocalWifiCredentials[0]))

// Legacy single-network form is still supported by `wifi_manager.cpp`:
// inline constexpr char kWifiSsid[] = "your-ssid";
// inline constexpr char kWifiPassword[] = "your-password";

// Optional static IPv4 configuration for APs that never complete DHCP.
// Uncomment all three required values together. DNS is optional.
// Use this only after confirming DHCP is the problem in the serial log.
// #define WIFI_STATIC_IP_ADDR "192.168.1.50"
// #define WIFI_STATIC_IP_NETMASK "255.255.255.0"
// #define WIFI_STATIC_IP_GATEWAY "192.168.1.1"
// #define WIFI_STATIC_IP_DNS "192.168.1.1"
