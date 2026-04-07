#pragma once

/// @brief Local MQTT broker settings for Home Assistant discovery.
/// @details Copy this file to `mqtt_credentials.h`, fill in the broker details,
/// and keep that local file out of version control.
///
/// This integration follows Home Assistant MQTT discovery. The default discovery
/// prefix is `homeassistant`, and MerlinCCU will publish retained discovery
/// configuration plus retained online state.
///
/// A Home Assistant MQTT integration and broker must already be set up.
///
/// Quick start:
/// 1. Copy this file to `mqtt_credentials.h`.
/// 2. Set `HOME_ASSISTANT_MQTT_HOST` to your broker host or IP.
/// 3. Leave the port at `1883` unless your broker uses a different plain MQTT
///    listener.
/// 4. Set `HOME_ASSISTANT_MQTT_USERNAME` and `HOME_ASSISTANT_MQTT_PASSWORD` to
///    broker credentials.
/// 5. Leave the discovery prefix as `homeassistant` unless your broker and HA
///    are configured to use a different discovery root.
///
/// These are broker credentials, not the Home Assistant REST token used by
/// `home_assistant_credentials.h`.

inline constexpr char HOME_ASSISTANT_MQTT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_MQTT_PORT = 1883;
inline constexpr char HOME_ASSISTANT_MQTT_USERNAME[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_PASSWORD[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_DISCOVERY_PREFIX[] = "homeassistant";
inline constexpr char HOME_ASSISTANT_MQTT_BASE_TOPIC[] = "merlinccu";
