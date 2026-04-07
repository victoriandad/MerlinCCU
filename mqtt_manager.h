#pragma once

#include "console_model.h"

namespace mqtt_manager {

/// @brief Initializes the MQTT discovery state.
void init();

/// @brief Advances the MQTT discovery state machine.
bool update(const WifiStatus& wifi_status,
            const HomeAssistantStatus& home_assistant_status,
            const TimeStatus& time_status);

/// @brief Returns the latest MQTT status snapshot for the UI/controller.
const MqttStatus& status();

}  // namespace mqtt_manager
