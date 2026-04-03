#pragma once

#include <stdint.h>

#include "console_model.h"

namespace time_manager {

/// @brief Initializes the local time tracking state.
void init();

/// @brief Advances the time state and reports whether visible status changed.
bool update();

/// @brief Returns the latest time status snapshot for the UI/controller.
const TimeStatus& status();

}  // namespace time_manager

#ifdef __cplusplus
extern "C" {
#endif

/// @brief Receives a UTC epoch update from the lwIP SNTP callback path.
void merlinccu_set_ntp_time(uint32_t sec);

#ifdef __cplusplus
}
#endif
