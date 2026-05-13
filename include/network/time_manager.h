#pragma once

#include <stddef.h>
#include <stdint.h>

#include "console_model.h"

namespace time_manager
{

/// @brief Initializes the local time tracking state.
/// @details The actual wall-clock source comes from lwIP SNTP via
/// `merlinccu_set_ntp_time`.
void init();

/// @brief Advances the time state and reports whether visible status changed.
/// @details This formats the latest synced time for UI display.
bool update();

/// @brief Returns the latest time status snapshot for the UI/controller.
const TimeStatus& status();

/// @brief Formats an ISO-8601 time in the configured display time zone.
/// @details Inputs without an explicit `Z` or numeric offset are treated as
/// already-local provider timestamps and are copied as `HH:MM`.
bool format_local_time_from_iso8601(const char* iso_datetime, char* out, size_t out_size);

} // namespace time_manager

#ifdef __cplusplus
extern "C"
{
#endif

    /// @brief Receives a UTC epoch update from the lwIP SNTP callback path.
    void merlinccu_set_ntp_time(uint32_t sec);

#ifdef __cplusplus
}
#endif
