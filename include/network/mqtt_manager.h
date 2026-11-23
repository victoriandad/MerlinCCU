#pragma once

#include "console_model.h"
#include "house_weather.h"

namespace mqtt_manager
{

/// @brief Initializes the MQTT discovery state.
/// @details This parses the broker settings and prepares the retained discovery
/// and state topics used to surface MerlinCCU as a Home Assistant MQTT device.
void init();

/// @brief Advances the MQTT discovery state machine.
/// @details The manager publishes retained availability, discovery payloads,
/// and a small set of state topics once Wi-Fi and broker connectivity exist.
/// @return `true` when the MQTT status visible to the UI changed.
bool update(const WifiStatus& wifi_status, const HomeAssistantStatus& home_assistant_status,
            const TimeStatus& time_status);

/// @brief Returns the latest MQTT status snapshot for the UI/controller.
const MqttStatus& status();

/// @brief Receives one house weather payload (null-terminated JSON).
using HouseWeatherHandler = void (*)(house_weather::Topic topic, const char* json);

/// @brief Hands each house weather payload received since the last call to
/// `handler`, then frees its buffer for the next one (#153).
/// @details Main loop only. The payload is valid only during the call.
void take_house_weather(HouseWeatherHandler handler);

} // namespace mqtt_manager
