#pragma once

/// @brief Local Home Assistant API settings.
/// @details Copy this file to `home_assistant_credentials.h`, fill in your
/// real values, and keep that local file out of version control.
///
/// This initial integration uses plain HTTP on your local network and performs
/// an authenticated `GET /api/` probe with a long-lived access token.
///
/// `HOME_ASSISTANT_HOST` may be either:
/// - a bare host name or IPv4 address such as `homeassistant.local` or `192.168.1.20`
/// - an `http://` URL such as `http://homeassistant.local:8123`
///
/// HTTPS is not supported by the current firmware, so do not use `https://`.
/// If mDNS is unreliable on your network, prefer a fixed LAN IP address.

inline constexpr char HOME_ASSISTANT_HOST[] = "homeassistant.local";
inline constexpr uint16_t HOME_ASSISTANT_PORT = 8123;
inline constexpr char HOME_ASSISTANT_TOKEN[] = "your-long-lived-access-token";
