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

inline constexpr char HOME_ASSISTANT_MQTT_HOST[] = "";
inline constexpr uint16_t HOME_ASSISTANT_MQTT_PORT = 1883;
inline constexpr char HOME_ASSISTANT_MQTT_USERNAME[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_PASSWORD[] = "";
inline constexpr char HOME_ASSISTANT_MQTT_DISCOVERY_PREFIX[] = "homeassistant";
inline constexpr char HOME_ASSISTANT_MQTT_BASE_TOPIC[] = "merlinccu";
