#pragma once

#include <cstdint>

/// @brief Keeps alert history (#203) in flash across restarts.
/// @details Two copies of alert_history::Image, two sectors each, directly
/// below the Pinter store, written alternately with a rising sequence like
/// the settings and Pinter stores: a power cut mid-save leaves the previous
/// copy intact. alert_history decides when a save is due; this only reads
/// and writes flash.
namespace alert_history_store
{

/// @brief Bytes reserved for the two copies.
/// @details alert_history_store.cpp statically asserts the image fits.
inline constexpr uint32_t kReservedFlashBytes = 4096U * 4U;

/// @brief Loads the newer valid copy into alert_history, if there is one.
/// Call once at start-up, after psram::init().
void load();

/// @brief Saves the history when alert_history says a save is due.
/// @details Call at the top of the main loop, outside any network callback:
/// erasing and programming flash disables interrupts for the duration.
void flush(uint32_t now_ms);

} // namespace alert_history_store
