#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "console_model.h"

/// @brief Over-the-air firmware updates into the spare A/B slot (#154).
/// @details The RP2350 boot ROM does the hard parts: it boots whichever slot
/// holds the newer image, checks the image hash, and runs an uploaded image on
/// trial ("try before you buy") until the firmware confirms it. This module
/// writes the spare slot, restarts into it, and confirms or abandons the
/// trial. See docs/ota.md.
namespace ota_manager
{

/// @brief Reads the boot ROM's boot info. Call first thing in main().
/// @details On a trial boot this also starts a watchdog (the boot ROM's own
/// may already be running), so a hang during start-up still ends in a
/// reboot, which falls back to the previous image.
void boot_check();

/// @brief Feeds the trial-boot watchdog during long start-up steps.
/// @details Does nothing on a normal boot. The main loop arms its own
/// watchdog once start-up is done.
void feed_boot_watchdog();

/// @brief True when this boot follows an update restart (or its fall-back),
/// so the watchdog reset behind it is not a hang.
bool booted_after_update();

/// @brief Prints what boot_check() found, once stdio is up.
void log_boot_state();

/// @brief Running slot, trial state and last update outcome.
FirmwareStatus status();

/// @brief Confirms or abandons a trial, and restarts after an upload.
/// @param network_ready Wi-Fi is connected with an address.
/// @return true when status() changed.
/// @details A trial image confirms itself once it has run for a short while
/// and the network is up. If that has not happened by the deadline, it
/// reboots and the boot ROM goes back to the previous image. If the other
/// slot holds no image (a unit just moved to the A/B layout over USB), there
/// is nothing to fall back to, so it confirms without waiting for Wi-Fi.
bool update(bool network_ready);

/// @brief Size of the slot an upload would go into; 0 when uploads are not
/// possible (no partition table, or on trial).
uint32_t spare_slot_bytes();

/// @brief The built-in upload token; empty when uploads are off.
std::string_view upload_token();

/// @brief Starts an upload of `image_bytes` into the spare slot.
/// @param error set to a short reason on failure.
bool begin(uint32_t image_bytes, std::string_view& error);

/// @brief Writes the next sector of the image.
/// @param data the sector's bytes; the buffer must hold a whole sector
/// (ota_upload::kSectorBytes) because a short final sector is padded in place.
/// @param length bytes of image in `data`; a whole sector except for the last.
/// @details Erases and programs with interrupts off, as the settings store
/// does, then reads the sector back to check it.
bool write_sector(uint8_t* data, size_t length, std::string_view& error);

/// @brief Ends an upload once every byte is written, and schedules the
/// restart into the new image.
bool finish(std::string_view& error);

/// @brief Abandons an upload in progress. The running image is untouched.
void abort();

/// @brief True between begin() and finish() or abort().
bool receiving();

} // namespace ota_manager
