#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure flash-slot persistence logic for Pinter vessel tracking state
/// (issue #18 follow-up): CRC/version/sequence validation and
/// newest-candidate selection, mirroring config_persistence's proven
/// two-slot pattern so a power loss mid-write still leaves a previous
/// complete snapshot available. Knows nothing about actual flash reads or
/// writes -- pinter_store.cpp owns those.
namespace pinter_persistence
{

inline constexpr uint32_t kPinterFlashMagic = 0x50494E54U; // "PINT"
inline constexpr uint16_t kPinterFlashVersion = 1;

/// @brief One flash-backed Pinter tracking slot.
struct PinterFlashSlot
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    std::array<PinterStatus, kPinterCount> pinters;
};

/// @brief One read candidate from a flash slot, valid or not.
struct PinterFlashCandidate
{
    bool valid;
    uint32_t sequence;
    std::array<PinterStatus, kPinterCount> pinters;
};

/// @brief Returns true when a stored slot header and payload are valid.
bool validate_slot(const PinterFlashSlot& slot);

/// @brief Chooses the newer of two read candidates, or nullptr if neither is valid.
/// @details The returned pointer aliases whichever input candidate won; the
/// caller must keep both candidates alive for as long as the result is used.
const PinterFlashCandidate* choose_newest_candidate(const PinterFlashCandidate& a,
                                                    const PinterFlashCandidate& b);

} // namespace pinter_persistence
