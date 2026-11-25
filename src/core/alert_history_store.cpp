#include "alert_history_store.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "alert_history.h"
#include "config_manager.h"
#include "hardware/flash.h"
#include "hardware/regs/addressmap.h"
#include "hardware/sync.h"
#include "pinter_store.h"

namespace alert_history_store
{

namespace
{

using alert_history::Image;

constexpr uint32_t kCopyBytes = kReservedFlashBytes / 2U;
// Directly below the Pinter store, which sits below the settings store.
constexpr uint32_t kStorageOffset = PICO_FLASH_SIZE_BYTES - config_manager::kReservedFlashBytes -
                                    pinter_store::kReservedFlashBytes - kReservedFlashBytes;
constexpr uint32_t kCopyOffsets[2] = {kStorageOffset, kStorageOffset + kCopyBytes};

static_assert(sizeof(Image) <= kCopyBytes, "an alert history image must fit its two sectors");
static_assert(kCopyBytes % FLASH_SECTOR_SIZE == 0U, "each copy must be whole sectors");

uint32_t g_save_sequence = 0U;

/// @brief One copy, read through the untranslated XIP alias: the boot ROM
/// maps only the running A/B slot at XIP_BASE (#154).
const Image& stored(uint32_t offset)
{
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return *reinterpret_cast<const Image*>(XIP_NOCACHE_NOALLOC_NOTRANSLATE_BASE + offset);
}

/// @brief Writes the image to one copy, a page at a time.
/// @details The image may be in PSRAM, which shares the QMI with flash and
/// cannot be read while flash is being written. Each page is copied to SRAM
/// first, with interrupts and XIP still on.
bool write_copy(uint32_t offset, const Image& image)
{
    alignas(4) static uint8_t page[FLASH_PAGE_SIZE] = {};
    const auto* bytes = reinterpret_cast<const uint8_t*>(&image);

    uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, kCopyBytes);
    restore_interrupts(interrupts);

    for (size_t done = 0U; done < sizeof(Image); done += FLASH_PAGE_SIZE)
    {
        const size_t length = std::min<size_t>(FLASH_PAGE_SIZE, sizeof(Image) - done);
        std::memset(page, 0xFF, sizeof(page));
        std::memcpy(page, bytes + done, length);
        interrupts = save_and_disable_interrupts();
        flash_range_program(offset + done, page, sizeof(page));
        restore_interrupts(interrupts);
    }

    const Image& written = stored(offset);
    return alert_history::image_valid(written) && written.save_sequence == image.save_sequence;
}

} // namespace

void load()
{
    const Image& first = stored(kCopyOffsets[0]);
    const Image& second = stored(kCopyOffsets[1]);
    const bool first_valid = alert_history::image_valid(first);
    const bool second_valid = alert_history::image_valid(second);
    std::printf("Alert history flash scan: copy0=%s copy1=%s\n", first_valid ? "valid" : "invalid",
                second_valid ? "valid" : "invalid");
    if (!first_valid && !second_valid)
    {
        return;
    }
    const Image& newest =
        !second_valid || (first_valid && first.save_sequence >= second.save_sequence) ? first
                                                                                      : second;
    if (alert_history::restore(newest))
    {
        g_save_sequence = newest.save_sequence;
        std::printf("Alert history loaded: %u entries, sequence %lu\n",
                    static_cast<unsigned>(alert_history::count()),
                    static_cast<unsigned long>(g_save_sequence));
    }
}

void flush(uint32_t now_ms)
{
    if (!alert_history::save_due(now_ms))
    {
        return;
    }
    const uint32_t sequence = g_save_sequence + 1U;
    const uint32_t offset = kCopyOffsets[sequence % 2U];
    const bool saved = write_copy(offset, alert_history::image_for_save(sequence));
    std::printf("Alert history save %lu (%u entries): %s\n", static_cast<unsigned long>(sequence),
                static_cast<unsigned>(alert_history::count()), saved ? "ok" : "verify failed");
    if (saved)
    {
        g_save_sequence = sequence;
    }
    // Marked saved either way: a failed write is retried with the next
    // change rather than every loop pass, which would wear the sector.
    alert_history::mark_saved(now_ms);
}

} // namespace alert_history_store
