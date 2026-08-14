#include "pinter_controller.h"

#include "test_framework.h"

namespace
{

ConsoleState make_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::Pinter;
    state.time_status.synced = true;
    state.time_status.local_epoch_day = 20000U;
    return state;
}

} // namespace

HOST_TEST(brew_catalogue_count_is_nonzero_and_bounded)
{
    EXPECT_TRUE(pinter_controller::brew_catalogue_count() > 0U);
    EXPECT_TRUE(pinter_controller::brew_catalogue_count() <= 255U);
}

HOST_TEST(brew_timing_out_of_range_index_falls_back_to_default)
{
    const auto& in_range = pinter_controller::brew_timing(0U);
    const auto& out_of_range =
        pinter_controller::brew_timing(static_cast<uint8_t>(pinter_controller::brew_catalogue_count() + 10U));
    EXPECT_TRUE(in_range.name != nullptr);
    EXPECT_TRUE(out_of_range.name != nullptr);
}

HOST_TEST(list_page_count_rounds_up)
{
    EXPECT_EQ(pinter_controller::list_page_count(0U), 1U);
    EXPECT_EQ(pinter_controller::list_page_count(8U), 1U);
    EXPECT_EQ(pinter_controller::list_page_count(9U), 2U);
    EXPECT_EQ(pinter_controller::list_page_count(16U), 2U);
    EXPECT_EQ(pinter_controller::list_page_count(17U), 3U);
}

HOST_TEST(clamp_list_page_pulls_back_a_now_out_of_range_page)
{
    uint8_t page = 5U;
    pinter_controller::clamp_list_page(page, 9U); // 2 pages (0,1) for 9 items
    EXPECT_EQ(page, 1U);
}

HOST_TEST(select_slot_returns_false_for_out_of_range_or_already_selected)
{
    ConsoleState state = make_state();
    state.selected_pinter_index = 0U;
    EXPECT_FALSE(pinter_controller::select_slot(state, 0U)); // already selected
    EXPECT_TRUE(pinter_controller::select_slot(state, 2U));
    EXPECT_EQ(static_cast<int>(state.selected_pinter_index), 2);
    EXPECT_FALSE(pinter_controller::select_slot(state, static_cast<uint8_t>(state.pinters.size())));
}

HOST_TEST(can_start_requires_idle_and_dock_space)
{
    ConsoleState state = make_state();
    for (auto& pinter : state.pinters)
    {
        pinter.state = PinterState::Idle;
    }
    EXPECT_TRUE(pinter_controller::can_start(state));

    for (size_t i = 0; i < kPinterBrewDockCapacity; ++i)
    {
        state.pinters[i].state = PinterState::Brewing;
    }
    // Selected pinter (index 0) is now Brewing, not Idle -- can't start regardless of dock space.
    EXPECT_FALSE(pinter_controller::can_start(state));
}

HOST_TEST(confirm_start_transitions_idle_pinter_to_brewing_with_pending_fields)
{
    ConsoleState state = make_state();
    state.selected_pinter_index = 0U;
    state.pinters[0].state = PinterState::Idle;
    state.pinter_pending_brew_index = 3U;
    state.pinter_pending_brewing_days = 7U;
    state.pinter_pending_cold_crash_days = 1U;
    state.pinter_pending_conditioning_days = 4U;

    EXPECT_TRUE(pinter_controller::confirm_start(state));

    const PinterStatus& pinter = state.pinters[0];
    EXPECT_TRUE(pinter.state == PinterState::Brewing);
    EXPECT_EQ(pinter.planned_brewing_days, 7U);
    EXPECT_EQ(pinter.planned_cold_crash_days, 1U);
    EXPECT_EQ(pinter.planned_conditioning_days, 4U);
    EXPECT_FALSE(pinter.cold_crash_used);
    EXPECT_TRUE(state.active_page == MenuPage::Pinter);
}

HOST_TEST(confirm_start_fails_when_selected_pinter_is_not_idle)
{
    ConsoleState state = make_state();
    state.pinters[0].state = PinterState::Ready;
    EXPECT_FALSE(pinter_controller::confirm_start(state));
}

HOST_TEST(apply_primary_action_does_nothing_for_an_idle_vessel)
{
    ConsoleState state = make_state();
    state.pinters[0].state = PinterState::Idle;
    EXPECT_FALSE(pinter_controller::apply_primary_action(state));
    EXPECT_TRUE(state.pinters[0].state == PinterState::Idle);
}

HOST_TEST(apply_primary_action_advances_conditioning_to_ready)
{
    ConsoleState state = make_state();
    state.pinters[0].state = PinterState::Conditioning;
    EXPECT_TRUE(pinter_controller::apply_primary_action(state));
    EXPECT_TRUE(state.pinters[0].state == PinterState::Ready);
}

HOST_TEST(reset_selected_clears_a_brewing_pinter_back_to_idle)
{
    ConsoleState state = make_state();
    state.pinters[0].state = PinterState::Brewing;
    state.pinters[0].brew_index = 5U;
    EXPECT_TRUE(pinter_controller::reset_selected(state));
    EXPECT_TRUE(state.pinters[0].state == PinterState::Idle);
}

HOST_TEST(reset_selected_is_a_no_op_for_an_already_idle_vessel_with_no_history)
{
    ConsoleState state = make_state();
    state.pinters[0].state = PinterState::Idle;
    EXPECT_FALSE(pinter_controller::reset_selected(state));
}

HOST_TEST(change_list_page_is_gated_to_the_pinter_select_brew_page)
{
    ConsoleState state = make_state();
    state.active_page = MenuPage::Pinter; // wrong page
    state.pinter_catalogue_page_index = 0U;
    EXPECT_FALSE(pinter_controller::change_list_page(state, 1));

    state.active_page = MenuPage::PinterSelectBrew;
    EXPECT_TRUE(pinter_controller::change_list_page(state, 1));
    EXPECT_EQ(state.pinter_catalogue_page_index, 1U);

    // Can't go past the last page.
    state.pinter_catalogue_page_index =
        static_cast<uint8_t>(pinter_controller::list_page_count(pinter_controller::brew_catalogue_count()) - 1U);
    EXPECT_FALSE(pinter_controller::change_list_page(state, 1));
}

HOST_TEST(select_list_item_prepares_a_pending_start_from_the_catalogue)
{
    ConsoleState state = make_state();
    state.active_page = MenuPage::PinterSelectBrew;
    state.pinters[0].state = PinterState::Idle;
    state.pinter_catalogue_page_index = 0U;

    EXPECT_TRUE(pinter_controller::select_list_item(state, 0U));
    EXPECT_TRUE(state.active_page == MenuPage::PinterStartTiming);
    EXPECT_EQ(state.pinter_pending_brew_index, 0U);
}

HOST_TEST(adjust_pending_days_stays_within_the_given_bounds)
{
    uint8_t value = 5U;
    EXPECT_TRUE(pinter_controller::adjust_pending_days(value, 1, 1U, 10U));
    EXPECT_EQ(value, 6U);
    value = 10U;
    EXPECT_FALSE(pinter_controller::adjust_pending_days(value, 1, 1U, 10U));
    EXPECT_EQ(value, 10U);
    value = 1U;
    EXPECT_FALSE(pinter_controller::adjust_pending_days(value, -1, 1U, 10U));
    EXPECT_EQ(value, 1U);
}

HOST_TEST(flush_pending_save_only_reports_true_once_after_a_mutation)
{
    ConsoleState state = make_state();
    // g_pinter_save_pending is a file-scope static in pinter_controller.cpp,
    // shared across every test in this binary -- drain any flag left pending
    // by an earlier test before asserting our own starting point.
    (void)pinter_controller::flush_pending_save(state);
    EXPECT_FALSE(pinter_controller::flush_pending_save(state)); // nothing pending yet
    state.pinters[0].state = PinterState::Brewing;
    EXPECT_TRUE(pinter_controller::reset_selected(state)); // marks a save pending
    EXPECT_TRUE(pinter_controller::flush_pending_save(state));
    EXPECT_FALSE(pinter_controller::flush_pending_save(state)); // already flushed
}
