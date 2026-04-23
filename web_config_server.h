#pragma once

#include "console_model.h"

namespace web_config_server
{

/// @brief Initializes the local intranet configuration server state.
void init();

/// @brief Advances the web server state and starts/stops it with Wi-Fi.
bool update(const WifiStatus& wifi_status);

} // namespace web_config_server
