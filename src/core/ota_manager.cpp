#include "ota_manager.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "pico.h"

#include "boot/bootrom_constants.h"
#include "boot/picobin.h"
#include "boot/picoboot_constants.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/structs/powman.h"
#include "hardware/sync.h"
#include "hardware/watchdog.h"
#include "ota_upload.h"
#include "pico/bootrom.h"
#include "pico/time.h"

#include "firmware_version.h"
#include "text_utils.h"

#if __has_include("ota_credentials.h")
#include "ota_credentials.h"
#else
inline constexpr char kOtaUploadToken[] = "";
#endif

namespace ota_manager
{

namespace
{

// An update in flight is recorded in a POWMAN scratch register, so the next
// boot can tell whether the boot ROM fell back to the old image. Watchdog
// scratch does not work for this: the boot ROM's flash-update reboot clears
// it. POWMAN scratch survives every reset except a power cut. Its writes
// carry a password in the top 16 bits, so the record is 16 bits: a marker
// byte and the target slot.
constexpr uint32_t kUpdateRecordScratch = 0U;
constexpr uint32_t kUpdateMarker = 0xA500U;
constexpr uint32_t kUpdateMarkerMask = 0xFF00U;
constexpr uint32_t kUpdateSlotMask = 0x00FFU;

/// @brief Trial-boot watchdog, just under the RP2350's ~16.7 s maximum.
/// Start-up (including the 10 s console delay) is fed through
/// feed_boot_watchdog(), then the main loop arms its own.
constexpr uint32_t kTrialBootWatchdogMs = 16000U;

/// @brief A trial image runs at least this long before confirming, so a crash
/// soon after start-up still falls back.
constexpr uint32_t kMinimumTrialRunMs = 15000U;

/// @brief A trial image that has not confirmed by now (from boot) gives up
/// and reboots into the previous image. Covers a slow Wi-Fi join with room
/// to spare.
constexpr uint32_t kConfirmDeadlineMs = 90000U;

/// @brief Time for the upload's HTTP reply to reach the browser before the
/// restart.
constexpr uint32_t kRebootDelayMs = 1500U;

/// @brief Scratch space rom_explicit_buy() needs to clear the trial flag.
constexpr size_t kBuyWorkareaBytes = 4096U;

/// @brief Partitions in the table; the boot ROM's own limit is 16.
constexpr uint32_t kMaxPartitions = 16U;

enum class UploadState : uint8_t
{
    Idle,
    Receiving,
    RebootPending,
};

/// @brief Where one partition sits in flash (physical offsets).
struct Slot
{
    int8_t index = -1;
    uint32_t offset = 0U;
    uint32_t bytes = 0U;
};

FirmwareStatus g_status = {};
Slot g_spare = {};
bool g_spare_has_image = false;
absolute_time_t g_boot_time = nil_time;
UploadState g_state = UploadState::Idle;
uint32_t g_expected_bytes = 0U;
uint32_t g_written_bytes = 0U;
absolute_time_t g_reboot_at = nil_time;
// Boot details kept for log_boot_state(); stdio is not up yet at boot_check().
uint8_t g_boot_type = 0U;
uint8_t g_tbyb_flags = 0U;
uint32_t g_boot_record = 0U;

/// @brief Reads flash at a physical offset, bypassing the boot ROM's address
/// translation (which maps only the running slot at XIP_BASE).
const uint8_t* flash_bytes(uint32_t offset)
{
    // XIP addresses are fixed hardware addresses; the cast is intentional.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<const uint8_t*>(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

/// @brief Looks up one partition's location in the boot ROM's resident table.
bool partition_slot(uint32_t index, Slot& out)
{
    uint32_t info[3] = {};
    constexpr uint32_t kFlags = PT_INFO_PARTITION_LOCATION_AND_FLAGS | PT_INFO_SINGLE_PARTITION;
    const int words = rom_get_partition_table_info(info, 3U, kFlags | (index << 24U));
    if (words < 2 || info[0] != kFlags)
    {
        return false;
    }
    const uint32_t first = (info[1] & PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_BITS) >>
                           PICOBIN_PARTITION_LOCATION_FIRST_SECTOR_LSB;
    const uint32_t last = (info[1] & PICOBIN_PARTITION_LOCATION_LAST_SECTOR_BITS) >>
                          PICOBIN_PARTITION_LOCATION_LAST_SECTOR_LSB;
    if (last < first)
    {
        return false;
    }
    out.index = static_cast<int8_t>(index);
    out.offset = first * FLASH_SECTOR_SIZE;
    out.bytes = (last + 1U - first) * FLASH_SECTOR_SIZE;
    return true;
}

/// @brief Finds the other half of the running slot's A/B pair.
bool find_spare_slot(int8_t running, Slot& out)
{
    if (running < 0)
    {
        return false;
    }
    const int b = rom_get_b_partition(static_cast<uint>(running));
    if (b >= 0)
    {
        return partition_slot(static_cast<uint32_t>(b), out);
    }
    // Running from a B slot: its A is the partition whose B it is.
    for (uint32_t a = 0U; a < kMaxPartitions; ++a)
    {
        if (rom_get_b_partition(a) == running)
        {
            return partition_slot(a, out);
        }
    }
    return false;
}

/// @brief Confirms the running trial image so the boot ROM keeps it.
bool buy()
{
    auto* workarea = static_cast<uint8_t*>(std::malloc(kBuyWorkareaBytes));
    if (workarea == nullptr)
    {
        std::printf("OTA: no memory to confirm the new firmware; will retry\n");
        return false;
    }
    const int rc = rom_explicit_buy(workarea, kBuyWorkareaBytes);
    std::free(workarea);
    if (rc != BOOTROM_OK)
    {
        std::printf("OTA: confirming the new firmware failed (%d); will retry\n", rc);
        return false;
    }
    return true;
}

uint32_t read_update_record()
{
    return powman_hw->scratch[kUpdateRecordScratch] & (kUpdateMarkerMask | kUpdateSlotMask);
}

void write_update_record(uint32_t record)
{
    powman_hw->scratch[kUpdateRecordScratch] = POWMAN_PASSWORD_BITS | record;
}

void clear_update_record()
{
    write_update_record(0U);
}

/// @brief Erases the start of the previous image once the new one is
/// confirmed.
/// @details Images carry no version number, so with two valid images the
/// boot ROM's choice on a normal boot is not the one just confirmed. With the
/// old image's first sector (and so its metadata block) gone, the confirmed
/// image is the only bootable one. Rolling back after confirmation is then a
/// fresh upload of the old build.
void retire_previous_image()
{
    if (g_spare.index < 0 || !g_spare_has_image)
    {
        return;
    }
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(g_spare.offset, FLASH_SECTOR_SIZE);
    restore_interrupts(interrupts);
    g_spare_has_image = false;
}

} // namespace

void boot_check()
{
    g_boot_time = get_absolute_time();
    g_status = {};
    g_status.valid = true;
    text_utils::copy_text(g_status.version, kFirmwareVersion);

    boot_info_t boot_info = {};
    const bool have_boot_info = rom_get_boot_info(&boot_info);
    g_status.slot = have_boot_info ? boot_info.partition : BOOT_PARTITION_NONE;
    g_status.partitioned = g_status.slot >= 0;
    g_status.trial = have_boot_info &&
                     (boot_info.tbyb_and_update_info & BOOT_TBYB_AND_UPDATE_FLAG_BUY_PENDING) != 0U;
    g_boot_type = have_boot_info ? boot_info.boot_type : 0U;
    g_tbyb_flags = have_boot_info ? boot_info.tbyb_and_update_info : 0U;
    g_boot_record = read_update_record();

    if ((g_boot_record & kUpdateMarkerMask) == kUpdateMarker)
    {
        const auto target = static_cast<int8_t>(g_boot_record & kUpdateSlotMask);
        if (g_status.slot != target)
        {
            g_status.rolled_back = true;
            clear_update_record();
        }
        else if (!g_status.trial)
        {
            // Booted the new slot and it is already confirmed: nothing to report.
            clear_update_record();
        }
        // Otherwise this is the trial boot itself; the record stays until it
        // confirms, so a fall-back on the next boot is noticed.
    }

    if (g_status.partitioned && find_spare_slot(g_status.slot, g_spare))
    {
        g_spare_has_image =
            ota_upload::has_image_block(flash_bytes(g_spare.offset), ota_upload::kSectorBytes);
    }

    if (g_status.trial)
    {
        watchdog_enable(kTrialBootWatchdogMs, true);
    }
}

void feed_boot_watchdog()
{
    if (g_status.trial)
    {
        watchdog_update();
    }
}

bool booted_after_update()
{
    return (g_boot_record & kUpdateMarkerMask) == kUpdateMarker;
}

bool booted_as_flash_update()
{
    constexpr uint8_t kChainedFlag = BOOT_TYPE_CHAINED_FLAG;
    return (g_boot_type & static_cast<uint8_t>(~kChainedFlag)) == BOOT_TYPE_FLASH_UPDATE;
}

void log_boot_state()
{
    std::printf("OTA: boot slot=%d boot_type=%u tbyb_flags=0x%02X update_record=0x%04lX "
                "trial=%d rolled_back=%d spare=%d (%s)\n",
                static_cast<int>(g_status.slot), static_cast<unsigned>(g_boot_type),
                static_cast<unsigned>(g_tbyb_flags), static_cast<unsigned long>(g_boot_record),
                g_status.trial ? 1 : 0, g_status.rolled_back ? 1 : 0,
                static_cast<int>(g_spare.index), g_spare_has_image ? "has image" : "empty");
}

FirmwareStatus status()
{
    return g_status;
}

const char* state_name()
{
    if (!g_status.partitioned)
    {
        return "not_partitioned";
    }
    if (g_status.trial)
    {
        return "on_trial";
    }
    return g_status.rolled_back ? "rolled_back" : "confirmed";
}

bool update(bool network_ready)
{
    if (g_state == UploadState::RebootPending && time_reached(g_reboot_at))
    {
        std::printf("OTA: restarting into slot %d\n", static_cast<int>(g_spare.index));
        write_update_record(kUpdateMarker |
                            (static_cast<uint32_t>(g_spare.index) & kUpdateSlotMask));
        // A flash-update boot makes the boot ROM prefer the slot at this
        // address and run it on trial.
        (void)rom_reboot(REBOOT2_FLAG_REBOOT_TYPE_FLASH_UPDATE | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS,
                         10U, XIP_BASE + g_spare.offset, 0U);
        while (true)
        {
            tight_loop_contents();
        }
    }

    if (!g_status.trial)
    {
        return false;
    }

    const int64_t run_ms = absolute_time_diff_us(g_boot_time, get_absolute_time()) / 1000;
    const bool ran_long_enough = run_ms >= static_cast<int64_t>(kMinimumTrialRunMs);
    if (ran_long_enough && (network_ready || !g_spare_has_image))
    {
        if (buy())
        {
            std::printf("OTA: new firmware confirmed in slot %d\n",
                        static_cast<int>(g_status.slot));
            g_status.trial = false;
            clear_update_record();
            retire_previous_image();
            return true;
        }
    }

    if (run_ms >= static_cast<int64_t>(kConfirmDeadlineMs))
    {
        std::printf("OTA: new firmware not confirmed after %lus; going back to the old one\n",
                    static_cast<unsigned long>(kConfirmDeadlineMs / 1000U));
        watchdog_reboot(0U, 0U, 10U);
        while (true)
        {
            tight_loop_contents();
        }
    }
    return false;
}

uint32_t spare_slot_bytes()
{
    if (!g_status.partitioned || g_status.trial || g_spare.index < 0)
    {
        return 0U;
    }
    return g_spare.bytes;
}

std::string_view upload_token()
{
    return kOtaUploadToken;
}

bool begin(uint32_t image_bytes, std::string_view& error)
{
    if (g_state != UploadState::Idle)
    {
        error = "An upload is already in progress.";
        return false;
    }
    if (!g_status.partitioned)
    {
        error = "This CCU has no A/B partition table; flash it over USB first (docs/ota.md).";
        return false;
    }
    if (g_status.trial)
    {
        error = "The running firmware is still on trial; wait for it to confirm.";
        return false;
    }
    if (g_spare.index < 0 || image_bytes > g_spare.bytes)
    {
        error = "The image does not fit the spare firmware slot.";
        return false;
    }

    g_state = UploadState::Receiving;
    g_expected_bytes = image_bytes;
    g_written_bytes = 0U;
    // The spare slot is about to be overwritten, so it no longer holds an
    // image to fall back to.
    g_spare_has_image = false;
    std::printf("OTA: receiving %lu bytes into slot %d at 0x%06lX\n",
                static_cast<unsigned long>(image_bytes), static_cast<int>(g_spare.index),
                static_cast<unsigned long>(g_spare.offset));
    return true;
}

bool write_sector(uint8_t* data, size_t length, std::string_view& error)
{
    if (g_state != UploadState::Receiving || data == nullptr || length == 0U ||
        length > ota_upload::kSectorBytes || g_written_bytes + length > g_expected_bytes)
    {
        error = "Upload out of step.";
        abort();
        return false;
    }
    if (length < ota_upload::kSectorBytes && g_written_bytes + length != g_expected_bytes)
    {
        error = "Upload out of step: short sector before the end.";
        abort();
        return false;
    }
    if (g_written_bytes == 0U && !ota_upload::has_image_block(data, length))
    {
        error = "Not an RP2350 firmware image: upload the .bin file.";
        abort();
        return false;
    }

    const size_t program_bytes = ota_upload::padded_to_page(length);
    std::memset(data + length, 0xFF, program_bytes - length);

    const uint32_t offset = g_spare.offset + g_written_bytes;
    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, data, program_bytes);
    restore_interrupts(interrupts);

    if (std::memcmp(flash_bytes(offset), data, program_bytes) != 0)
    {
        error = "Flash write did not read back correctly.";
        abort();
        return false;
    }

    g_written_bytes += static_cast<uint32_t>(length);
    return true;
}

bool finish(std::string_view& error)
{
    if (g_state != UploadState::Receiving || g_written_bytes != g_expected_bytes)
    {
        error = "Upload ended before the whole image arrived.";
        abort();
        return false;
    }
    std::printf("OTA: %lu bytes written to slot %d; restarting shortly\n",
                static_cast<unsigned long>(g_written_bytes), static_cast<int>(g_spare.index));
    g_state = UploadState::RebootPending;
    g_reboot_at = make_timeout_time_ms(kRebootDelayMs);
    return true;
}

void abort()
{
    if (g_state != UploadState::Receiving)
    {
        return;
    }
    std::printf("OTA: upload abandoned after %lu of %lu bytes\n",
                static_cast<unsigned long>(g_written_bytes),
                static_cast<unsigned long>(g_expected_bytes));
    g_state = UploadState::Idle;
    g_expected_bytes = 0U;
    g_written_bytes = 0U;
}

bool receiving()
{
    return g_state == UploadState::Receiving;
}

} // namespace ota_manager
