#pragma once

/// @brief Local Wi-Fi credentials for Pico W bring-up.
/// @details Copy this file to `wifi_credentials.h`, fill in your real values,
/// and keep that local file out of version control.
///
/// The preferred simple form mirrors the official Pico W examples. If you need
/// to override the default auth or regulatory domain, define `WIFI_AUTH` or
/// `WIFI_COUNTRY` before the credentials.
///
/// Quick start:
/// 1. Copy this file to `wifi_credentials.h`.
/// 2. Replace `WIFI_SSID` and `WIFI_PASSWORD`.
/// 3. Leave the auth and country defaults alone unless your AP needs different
///    values.
/// 4. Only enable the static IP block if your router never completes DHCP.

#define WIFI_COUNTRY CYW43_COUNTRY_UK
#define WIFI_AUTH CYW43_AUTH_WPA2_MIXED_PSK

inline constexpr char WIFI_SSID[] = "your-ssid";
inline constexpr char WIFI_PASSWORD[] = "your-password";

// Optional static IPv4 configuration for APs that never complete DHCP.
// Uncomment all three required values together. DNS is optional.
// Use this only after confirming DHCP is the problem in the serial log.
// #define WIFI_STATIC_IP_ADDR "192.168.1.50"
// #define WIFI_STATIC_IP_NETMASK "255.255.255.0"
// #define WIFI_STATIC_IP_GATEWAY "192.168.1.1"
// #define WIFI_STATIC_IP_DNS "192.168.1.1"
