#include "pinter_store.h"

#include <cstdio>
#include <cstring>

#include "config_manager.h"
#include "config_persistence.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"
#include "pinter_persistence.h"

namespace pinter_store
{

namespace
{

using pinter_persistence::PinterFlashCandidate;
using pinter_persistence::PinterFlashSlot;

constexpr uint32_t kFlashSlotCount = 2;
constexpr uint32_t kPinterStorageBytes = FLASH_SECTOR_SIZE * kFlashSlotCount;
// Sits directly below the settings store's own reserved region so the two
// flash-backed persistence mechanisms can never overlap.
constexpr uint32_t kPinterStorageOffset =
    PICO_FLASH_SIZE_BYTES - config_manager::kReservedFlashBytes - kPinterStorageBytes;
constexpr uint32_t kSlot0Offset = kPinterStorageOffset;
constexpr uint32_t kSlot1Offset = kPinterStorageOffset + FLASH_SECTOR_SIZE;

static_assert(sizeof(PinterFlashSlot) <= FLASH_SECTOR_SIZE,
              "Pinter flash slot must fit in one flash sector");
static_assert(kPinterStorageBytes == pinter_store::kReservedFlashBytes,
              "pinter_store::kReservedFlashBytes must track this store's real reservation");

uint32_t g_sequence = 0;

/// @brief Returns a const pointer to one flash slot.
const PinterFlashSlot* flash_slot(uint32_t offset)
{
    // XIP flash is memory mapped on the RP2040/RP2350, so this hardware
    // address cast is intentional and cannot be expressed as an ordinary
    // object pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<const PinterFlashSlot*>(XIP_BASE + offset);
}

PinterFlashCandidate read_candidate(uint32_t offset)
{
    const PinterFlashSlot* slot = flash_slot(offset);
    if (pinter_persistence::validate_slot(*slot))
    {
        return {true, slot->sequence, slot->pinters};
    }

    return {false, 0, {}};
}

/// @brief Writes one complete slot to flash.
bool write_slot(uint32_t offset, const std::array<PinterStatus, kPinterCount>& pinters,
                uint32_t sequence)
{
    alignas(4) static uint8_t flash_buffer[FLASH_SECTOR_SIZE] = {};
    std::memset(flash_buffer, 0xFF, sizeof(flash_buffer));

    PinterFlashSlot slot = {};
    slot.magic = pinter_persistence::kPinterFlashMagic;
    slot.version = pinter_persistence::kPinterFlashVersion;
    slot.sequence = sequence;
    slot.payload_size = sizeof(slot.pinters);
    slot.pinters = pinters;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.pinters),
                                           sizeof(slot.pinters));
    std::memcpy(flash_buffer, &slot, sizeof(slot));

    std::printf("Pinter save: writing sequence %lu to flash offset 0x%08lX\n",
                static_cast<unsigned long>(sequence), static_cast<unsigned long>(offset));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, flash_buffer, sizeof(flash_buffer));
    restore_interrupts(interrupts);

    const PinterFlashSlot* stored = flash_slot(offset);
    const bool valid = pinter_persistence::validate_slot(*stored) && stored->sequence == sequence;
    std::printf("Pinter save: verify %s for sequence %lu\n", valid ? "ok" : "failed",
                static_cast<unsigned long>(sequence));
    return valid;
}

/// @brief Returns the next flash slot offset for a save operation.
uint32_t next_save_slot_offset()
{
    return (g_sequence % 2U) == 0U ? kSlot1Offset : kSlot0Offset;
}

} // namespace

bool load(std::array<PinterStatus, kPinterCount>* out_pinters)
{
    if (out_pinters == nullptr)
    {
        return false;
    }

    const PinterFlashCandidate slot0 = read_candidate(kSlot0Offset);
    const PinterFlashCandidate slot1 = read_candidate(kSlot1Offset);
    std::printf("Pinter flash scan: slot0=%s slot1=%s\n", slot0.valid ? "valid" : "invalid",
                slot1.valid ? "valid" : "invalid");

    const PinterFlashCandidate* chosen = pinter_persistence::choose_newest_candidate(slot0, slot1);
    if (chosen == nullptr)
    {
        return false;
    }

    *out_pinters = chosen->pinters;
    g_sequence = chosen->sequence;
    std::printf("Pinter state loaded from flash sequence %lu\n",
                static_cast<unsigned long>(chosen->sequence));
    return true;
}

bool save(const std::array<PinterStatus, kPinterCount>& pinters)
{
    const uint32_t next_sequence = g_sequence + 1U;
    if (!write_slot(next_save_slot_offset(), pinters, next_sequence))
    {
        std::printf("Pinter save failed\n");
        return false;
    }

    g_sequence = next_sequence;
    return true;
}

} // namespace pinter_store
