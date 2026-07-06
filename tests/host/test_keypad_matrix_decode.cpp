#include "keypad_matrix_decode.h"

#include "test_framework.h"

using keypad_matrix_decode::hit_mask_shows_closure;
using keypad_matrix_decode::kMatrixButtons;
using keypad_matrix_decode::kObservedPanelPins;
using keypad_matrix_decode::observed_line_hit_bit;
using keypad_matrix_decode::observed_line_index_for_panel_pin;

namespace
{

/// @brief Builds a synthetic hit-mask snapshot showing exactly one closure.
std::array<uint16_t, kKeypadObservedLineCount> make_hits(uint8_t panel_pin_a, uint8_t panel_pin_b)
{
    std::array<uint16_t, kKeypadObservedLineCount> hits = {};
    const size_t index_a = observed_line_index_for_panel_pin(panel_pin_a);
    const size_t index_b = observed_line_index_for_panel_pin(panel_pin_b);
    if (index_a < kObservedPanelPins.size() && index_b < kObservedPanelPins.size())
    {
        hits[index_a] |= observed_line_hit_bit(index_b);
    }
    return hits;
}

} // namespace

HOST_TEST(observed_line_index_for_panel_pin_finds_known_pins)
{
    EXPECT_EQ(observed_line_index_for_panel_pin(5U), 0U);
    EXPECT_EQ(observed_line_index_for_panel_pin(22U), kObservedPanelPins.size() - 1U);
}

HOST_TEST(observed_line_index_for_panel_pin_rejects_unknown_pin)
{
    EXPECT_EQ(observed_line_index_for_panel_pin(99U), kObservedPanelPins.size());
}

HOST_TEST(hit_mask_shows_closure_is_false_with_no_bits_set)
{
    const std::array<uint16_t, kKeypadObservedLineCount> hits = {};
    EXPECT_FALSE(hit_mask_shows_closure(hits, 5U, 20U));
}

HOST_TEST(hit_mask_shows_closure_detects_either_direction)
{
    // Alert is confirmed on panel pins 5 x 20 -- a real scan can see the
    // closure from either driven direction, so both must decode identically.
    const auto hits_a_drives_b = make_hits(5U, 20U);
    EXPECT_TRUE(hit_mask_shows_closure(hits_a_drives_b, 5U, 20U));

    const auto hits_b_drives_a = make_hits(20U, 5U);
    EXPECT_TRUE(hit_mask_shows_closure(hits_b_drives_a, 5U, 20U));
}

HOST_TEST(hit_mask_shows_closure_rejects_unknown_pin)
{
    const auto hits = make_hits(5U, 20U);
    EXPECT_FALSE(hit_mask_shows_closure(hits, 5U, 99U));
}

HOST_TEST(every_matrix_button_decodes_uniquely_from_its_own_closure)
{
    // For each confirmed button, a hit-mask showing only that button's pin
    // pair must decode as pressed for that button and NOT for any other --
    // this is what would catch a copy-paste error (duplicate/overlapping pin
    // pair) in the 50-entry bench matrix table.
    for (const auto& button : kMatrixButtons)
    {
        const auto hits = make_hits(button.panel_pin_a, button.panel_pin_b);
        EXPECT_TRUE(hit_mask_shows_closure(hits, button.panel_pin_a, button.panel_pin_b));

        for (const auto& other : kMatrixButtons)
        {
            if (other.id == button.id)
            {
                continue;
            }
            EXPECT_FALSE(hit_mask_shows_closure(hits, other.panel_pin_a, other.panel_pin_b));
        }
    }
}
