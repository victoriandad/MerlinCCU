#include "pinter_persistence.h"

#include "config_persistence.h"

namespace pinter_persistence
{

bool validate_slot(const PinterFlashSlot& slot)
{
    if (slot.magic != kPinterFlashMagic || slot.version != kPinterFlashVersion ||
        slot.payload_size != sizeof(slot.pinters))
    {
        return false;
    }

    const uint32_t expected = config_persistence::crc32(
        reinterpret_cast<const uint8_t*>(&slot.pinters), sizeof(slot.pinters));
    return slot.crc32 == expected;
}

const PinterFlashCandidate* choose_newest_candidate(const PinterFlashCandidate& a,
                                                    const PinterFlashCandidate& b)
{
    if (!a.valid && !b.valid)
    {
        return nullptr;
    }
    if (a.valid && b.valid)
    {
        return (b.sequence > a.sequence) ? &b : &a;
    }
    return a.valid ? &a : &b;
}

} // namespace pinter_persistence
