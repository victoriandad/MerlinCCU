#pragma once

/// @brief Local Home Assistant API settings.
/// @details Copy this file to `config/home_assistant_credentials.h`, fill in your
/// real values, and keep that local file out of version control.
///
/// This initial integration uses plain HTTP on your local network and performs
/// an authenticated `GET /api/` probe with a long-lived access token.
///
/// Quick start:
/// 1. Copy this file to `config/home_assistant_credentials.h`.
/// 2. Set `HOME_ASSISTANT_HOST` to your HA box: a LAN DNS name such as
///    `ha.lan`, `homeassistant.local`, or a fixed LAN IP.
/// 3. Set `HOME_ASSISTANT_TOKEN` to a Home Assistant long-lived access token.
/// 4. Optionally set `HOME_ASSISTANT_ENTITY_ID` if you want MerlinCCU to read
///    one existing HA entity, such as a Tapo switch.
/// 5. Leave `HOME_ASSISTANT_SELF_ENTITY_ID` enabled if you want the firmware to
///    publish a simple MerlinCCU status entity over REST.
///
/// `HOME_ASSISTANT_HOST` may be either:
/// - a bare host name or IPv4 address such as `ha.lan` or `192.168.1.20`;
///   this uses `HOME_ASSISTANT_PORT`
/// - a URL such as `http://ha.lan/` or `http://homeassistant.local:8123`;
///   a URL brings its own port (`http://` is 80, `https://` is 443) unless it
///   ends in `:port`, and `HOME_ASSISTANT_PORT` is then ignored
///
/// HA's own default port is 8123. An HA served behind a proxy on port 80,
/// such as `http://ha.lan/`, needs either the URL form or port 80 here.
/// HTTPS certificates are not validated yet (#17), so prefer plain HTTP on the
/// LAN. If mDNS is unreliable on your network, prefer a router DNS name or a
/// fixed LAN IP address.

inline constexpr char HOME_ASSISTANT_HOST[] = "homeassistant.local";
inline constexpr uint16_t HOME_ASSISTANT_PORT = 8123;
inline constexpr char HOME_ASSISTANT_TOKEN[] = "your-long-lived-access-token";

// Optional entity to read from Home Assistant using GET /api/states/<entity_id>.
// For a Tapo P110 this is often a switch or sensor entity, but the exact ID
// depends on your HA setup. Example:
// inline constexpr char HOME_ASSISTANT_ENTITY_ID[] = "switch.tapo_p110";
inline constexpr char HOME_ASSISTANT_ENTITY_ID[] = "";

// Optional entity for MerlinCCU to publish back into Home Assistant using
// POST /api/states/<entity_id>. This creates a simple REST-backed entity, not
// a full Home Assistant device registry entry.
inline constexpr char HOME_ASSISTANT_SELF_ENTITY_ID[] = "sensor.merlinccu_status";
