#include "pinter_persistence.h"

#include "config_persistence.h"
#include "test_framework.h"

using pinter_persistence::PinterFlashCandidate;
using pinter_persistence::PinterFlashSlot;

namespace
{

std::array<PinterStatus, kPinterCount> make_sample_pinters()
{
    std::array<PinterStatus, kPinterCount> pinters = {};
    pinters[0].state = PinterState::Brewing;
    pinters[0].brew_index = 3U;
    pinters[0].brew_start_day = 100U;
    pinters[0].planned_brewing_days = 7U;
    return pinters;
}

/// @brief Builds a Pinter flash slot with a correct header and CRC.
PinterFlashSlot make_valid_slot(const std::array<PinterStatus, kPinterCount>& pinters,
                                uint32_t sequence)
{
    PinterFlashSlot slot = {};
    slot.magic = pinter_persistence::kPinterFlashMagic;
    slot.version = pinter_persistence::kPinterFlashVersion;
    slot.sequence = sequence;
    slot.payload_size = sizeof(slot.pinters);
    slot.pinters = pinters;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.pinters),
                                           sizeof(slot.pinters));
    return slot;
}

} // namespace

HOST_TEST(pinter_flash_validate_slot_accepts_a_correctly_built_slot)
{
    const PinterFlashSlot slot = make_valid_slot(make_sample_pinters(), 7U);
    EXPECT_TRUE(pinter_persistence::validate_slot(slot));
}

HOST_TEST(pinter_flash_validate_slot_rejects_wrong_magic)
{
    PinterFlashSlot slot = make_valid_slot(make_sample_pinters(), 1U);
    slot.magic = 0xDEADBEEFU;
    EXPECT_FALSE(pinter_persistence::validate_slot(slot));
}

HOST_TEST(pinter_flash_validate_slot_rejects_wrong_version)
{
    PinterFlashSlot slot = make_valid_slot(make_sample_pinters(), 1U);
    slot.version = pinter_persistence::kPinterFlashVersion + 1U;
    EXPECT_FALSE(pinter_persistence::validate_slot(slot));
}

HOST_TEST(pinter_flash_validate_slot_rejects_a_flipped_bit_in_the_payload)
{
    // Simulates a torn/partial flash write or bit rot: header is intact but
    // the payload no longer matches its stored CRC.
    PinterFlashSlot slot = make_valid_slot(make_sample_pinters(), 1U);
    slot.pinters[0].brew_start_day ^= 0x1U;
    EXPECT_FALSE(pinter_persistence::validate_slot(slot));
}

HOST_TEST(pinter_flash_validate_slot_rejects_wrong_payload_size)
{
    PinterFlashSlot slot = make_valid_slot(make_sample_pinters(), 1U);
    slot.payload_size = sizeof(slot.pinters) - 1U;
    EXPECT_FALSE(pinter_persistence::validate_slot(slot));
}

HOST_TEST(pinter_flash_choose_newest_candidate_prefers_higher_sequence_when_both_valid)
{
    const auto pinters = make_sample_pinters();
    const PinterFlashCandidate older = {true, 3U, pinters};
    const PinterFlashCandidate newer = {true, 4U, pinters};

    const PinterFlashCandidate* chosen = pinter_persistence::choose_newest_candidate(older, newer);
    EXPECT_TRUE(chosen == &newer);

    const PinterFlashCandidate* chosen_reversed =
        pinter_persistence::choose_newest_candidate(newer, older);
    EXPECT_TRUE(chosen_reversed == &newer);
}

HOST_TEST(pinter_flash_choose_newest_candidate_falls_back_to_whichever_slot_is_valid)
{
    const auto pinters = make_sample_pinters();
    const PinterFlashCandidate valid = {true, 1U, pinters};
    const PinterFlashCandidate invalid = {false, 0U, {}};

    EXPECT_TRUE(pinter_persistence::choose_newest_candidate(valid, invalid) == &valid);
    EXPECT_TRUE(pinter_persistence::choose_newest_candidate(invalid, valid) == &valid);
}

HOST_TEST(pinter_flash_choose_newest_candidate_returns_null_when_neither_slot_is_valid)
{
    const PinterFlashCandidate a = {false, 0U, {}};
    const PinterFlashCandidate b = {false, 0U, {}};
    EXPECT_TRUE(pinter_persistence::choose_newest_candidate(a, b) == nullptr);
}
