#include "pinter_scheduling.h"

#include "test_framework.h"

namespace
{

PinterStatus make_pinter(PinterState state, uint8_t planned_cold_crash_days = 0U,
                         bool cold_crash_used = false)
{
    PinterStatus pinter = {};
    pinter.state = state;
    pinter.planned_cold_crash_days = planned_cold_crash_days;
    pinter.cold_crash_used = cold_crash_used;
    return pinter;
}

std::array<PinterStatus, kPinterCount> make_fleet(PinterState a, PinterState b, PinterState c,
                                                  PinterState d)
{
    return {make_pinter(a), make_pinter(b), make_pinter(c), make_pinter(d)};
}

} // namespace

HOST_TEST(brew_dock_count_only_counts_brewing)
{
    const auto fleet = make_fleet(PinterState::Brewing, PinterState::ColdCrash,
                                  PinterState::Conditioning, PinterState::Idle);
    EXPECT_EQ(pinter_scheduling::brew_dock_count(fleet), 1U);
}

HOST_TEST(fridge_count_only_counts_conditioning_and_ready)
{
    const auto fleet = make_fleet(PinterState::Conditioning, PinterState::Ready,
                                  PinterState::ColdCrash, PinterState::Brewing);
    EXPECT_EQ(pinter_scheduling::fridge_count(fleet), 2U);
    EXPECT_TRUE(pinter_scheduling::uses_fridge(PinterState::Conditioning));
    EXPECT_TRUE(pinter_scheduling::uses_fridge(PinterState::Ready));
    EXPECT_FALSE(pinter_scheduling::uses_fridge(PinterState::ColdCrash));
    EXPECT_FALSE(pinter_scheduling::uses_fridge(PinterState::Brewing));
}

HOST_TEST(has_pending_cold_crash_requires_brewing_days_and_not_yet_used)
{
    EXPECT_TRUE(
        pinter_scheduling::has_pending_cold_crash(make_pinter(PinterState::Brewing, 3U, false)));
    EXPECT_FALSE(
        pinter_scheduling::has_pending_cold_crash(make_pinter(PinterState::Brewing, 0U, false)));
    EXPECT_FALSE(
        pinter_scheduling::has_pending_cold_crash(make_pinter(PinterState::Brewing, 3U, true)));
    EXPECT_FALSE(
        pinter_scheduling::has_pending_cold_crash(make_pinter(PinterState::ColdCrash, 3U, false)));
}

HOST_TEST(can_start_requires_dock_space)
{
    EXPECT_TRUE(pinter_scheduling::can_start(0U));
    EXPECT_TRUE(pinter_scheduling::can_start(kPinterBrewDockCapacity - 1U));
    EXPECT_FALSE(pinter_scheduling::can_start(kPinterBrewDockCapacity)); // dock full
}

HOST_TEST(can_enter_fridge_pending_cold_crash_bypasses_fridge_capacity)
{
    // A vessel with a pending cold crash goes to ColdCrash next, which does not
    // use fridge space -- so it must be allowed regardless of fridge occupancy.
    const PinterStatus pinter = make_pinter(PinterState::Brewing, 3U, false);
    EXPECT_TRUE(pinter_scheduling::can_enter_fridge(pinter, kPinterFridgeCapacity));
}

HOST_TEST(can_enter_fridge_is_gated_by_capacity_once_no_cold_crash_pending)
{
    const PinterStatus brewing_done_crashing = make_pinter(PinterState::Brewing, 0U, false);
    EXPECT_TRUE(pinter_scheduling::can_enter_fridge(brewing_done_crashing, 0U));
    EXPECT_FALSE(
        pinter_scheduling::can_enter_fridge(brewing_done_crashing, kPinterFridgeCapacity));

    const PinterStatus cold_crashing = make_pinter(PinterState::ColdCrash, 3U, true);
    EXPECT_TRUE(pinter_scheduling::can_enter_fridge(cold_crashing, kPinterFridgeCapacity - 1U));
    EXPECT_FALSE(pinter_scheduling::can_enter_fridge(cold_crashing, kPinterFridgeCapacity));
}

HOST_TEST(can_enter_fridge_is_false_for_states_that_never_move_to_the_fridge)
{
    EXPECT_FALSE(pinter_scheduling::can_enter_fridge(make_pinter(PinterState::Idle), 0U));
    EXPECT_FALSE(pinter_scheduling::can_enter_fridge(make_pinter(PinterState::Conditioning), 0U));
    EXPECT_FALSE(pinter_scheduling::can_enter_fridge(make_pinter(PinterState::Ready), 0U));
    EXPECT_FALSE(pinter_scheduling::can_enter_fridge(make_pinter(PinterState::Consumed), 0U));
}

HOST_TEST(primary_action_enabled_blocks_start_with_no_dock_space)
{
    const PinterStatus idle = make_pinter(PinterState::Idle);
    EXPECT_FALSE(pinter_scheduling::primary_action_enabled(idle, kPinterBrewDockCapacity, 0U));
    EXPECT_TRUE(pinter_scheduling::primary_action_enabled(idle, 0U, 0U));
}

HOST_TEST(primary_action_enabled_blocks_fridge_entry_when_full_but_not_cold_crash_entry)
{
    const PinterStatus pending_crash = make_pinter(PinterState::Brewing, 3U, false);
    EXPECT_TRUE(
        pinter_scheduling::primary_action_enabled(pending_crash, 0U, kPinterFridgeCapacity));

    const PinterStatus done_crashing = make_pinter(PinterState::Brewing, 0U, false);
    EXPECT_FALSE(
        pinter_scheduling::primary_action_enabled(done_crashing, 0U, kPinterFridgeCapacity));
    EXPECT_TRUE(pinter_scheduling::primary_action_enabled(done_crashing, 0U, 0U));
}

HOST_TEST(primary_action_enabled_is_always_true_past_the_fridge_stage)
{
    EXPECT_TRUE(pinter_scheduling::primary_action_enabled(make_pinter(PinterState::Conditioning),
                                                          0U, kPinterFridgeCapacity));
    EXPECT_TRUE(pinter_scheduling::primary_action_enabled(make_pinter(PinterState::Ready), 0U,
                                                          kPinterFridgeCapacity));
    EXPECT_TRUE(pinter_scheduling::primary_action_enabled(make_pinter(PinterState::Consumed), 0U,
                                                          kPinterFridgeCapacity));
}

HOST_TEST(summarize_groups_cold_crash_with_brewing_and_excludes_idle_and_consumed)
{
    const auto fleet = make_fleet(PinterState::Brewing, PinterState::ColdCrash,
                                  PinterState::Conditioning, PinterState::Ready);
    const pinter_scheduling::SummaryCounts counts = pinter_scheduling::summarize(fleet);
    EXPECT_EQ(counts.brewing, 2U);
    EXPECT_EQ(counts.conditioning, 1U);
    EXPECT_EQ(counts.ready, 1U);
}

HOST_TEST(advance_non_idle_does_nothing_for_an_idle_vessel)
{
    PinterStatus pinter = make_pinter(PinterState::Idle);
    EXPECT_FALSE(pinter_scheduling::advance_non_idle(pinter, 100U, 0U));
    EXPECT_TRUE(pinter.state == PinterState::Idle);
}

HOST_TEST(advance_non_idle_brewing_with_pending_cold_crash_moves_to_cold_crash)
{
    PinterStatus pinter = make_pinter(PinterState::Brewing, 3U, false);
    EXPECT_TRUE(pinter_scheduling::advance_non_idle(pinter, 100U, kPinterFridgeCapacity));
    EXPECT_TRUE(pinter.state == PinterState::ColdCrash);
    EXPECT_EQ(pinter.cold_crash_start_day, 100U);
    EXPECT_TRUE(pinter.cold_crash_used);
}

HOST_TEST(advance_non_idle_brewing_without_cold_crash_moves_to_conditioning_when_fridge_has_room)
{
    PinterStatus pinter = make_pinter(PinterState::Brewing, 0U, false);
    EXPECT_TRUE(pinter_scheduling::advance_non_idle(pinter, 101U, 0U));
    EXPECT_TRUE(pinter.state == PinterState::Conditioning);
    EXPECT_EQ(pinter.conditioning_start_day, 101U);
}

HOST_TEST(advance_non_idle_is_blocked_when_the_fridge_is_full)
{
    PinterStatus pinter = make_pinter(PinterState::Brewing, 0U, false);
    EXPECT_FALSE(
        pinter_scheduling::advance_non_idle(pinter, 101U, kPinterFridgeCapacity));
    EXPECT_TRUE(pinter.state == PinterState::Brewing); // unchanged, not silently corrupted

    PinterStatus crashing = make_pinter(PinterState::ColdCrash, 3U, true);
    EXPECT_FALSE(
        pinter_scheduling::advance_non_idle(crashing, 101U, kPinterFridgeCapacity));
    EXPECT_TRUE(crashing.state == PinterState::ColdCrash);
}

HOST_TEST(advance_non_idle_walks_conditioning_through_ready_to_consumed_to_idle)
{
    PinterStatus pinter = make_pinter(PinterState::Conditioning);
    EXPECT_TRUE(pinter_scheduling::advance_non_idle(pinter, 200U, 0U));
    EXPECT_TRUE(pinter.state == PinterState::Ready);
    EXPECT_EQ(pinter.ready_day, 200U);

    EXPECT_TRUE(pinter_scheduling::advance_non_idle(pinter, 201U, 0U));
    EXPECT_TRUE(pinter.state == PinterState::Consumed);

    EXPECT_TRUE(pinter_scheduling::advance_non_idle(pinter, 202U, 0U));
    EXPECT_TRUE(pinter.state == PinterState::Idle);
    EXPECT_EQ(pinter.brew_start_day, 0U);
    EXPECT_EQ(pinter.planned_cold_crash_days, 0U);
    EXPECT_FALSE(pinter.cold_crash_used);
}

HOST_TEST(reset_is_a_no_op_for_an_already_idle_vessel_with_no_history)
{
    PinterStatus pinter = make_pinter(PinterState::Idle, 0U, false);
    EXPECT_FALSE(pinter_scheduling::reset(pinter, 7U));
}

HOST_TEST(reset_clears_an_idle_vessel_that_still_has_cold_crash_history)
{
    PinterStatus pinter = make_pinter(PinterState::Idle, 3U, true);
    EXPECT_TRUE(pinter_scheduling::reset(pinter, 7U));
    EXPECT_FALSE(pinter.cold_crash_used);
}

HOST_TEST(current_stage_target_day_is_zero_for_states_with_no_timed_target)
{
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(make_pinter(PinterState::Idle)), 0U);
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(make_pinter(PinterState::Ready)), 0U);
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(make_pinter(PinterState::Consumed)), 0U);
}

HOST_TEST(current_stage_target_day_sums_stage_start_and_planned_days)
{
    PinterStatus brewing = make_pinter(PinterState::Brewing);
    brewing.brew_start_day = 100U;
    brewing.planned_brewing_days = 7U;
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(brewing), 107U);

    PinterStatus crashing = make_pinter(PinterState::ColdCrash, 3U, true);
    crashing.cold_crash_start_day = 200U;
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(crashing), 203U);

    PinterStatus conditioning = make_pinter(PinterState::Conditioning);
    conditioning.conditioning_start_day = 300U;
    conditioning.planned_conditioning_days = 14U;
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(conditioning), 314U);
}

HOST_TEST(reset_clears_an_in_progress_vessel_back_to_idle_with_the_given_default_brew)
{
    PinterStatus pinter = make_pinter(PinterState::Conditioning, 3U, true);
    pinter.brew_start_day = 50U;
    pinter.conditioning_start_day = 55U;
    pinter.planned_brewing_days = 14U;

    EXPECT_TRUE(pinter_scheduling::reset(pinter, 9U));

    EXPECT_TRUE(pinter.state == PinterState::Idle);
    EXPECT_EQ(pinter.brew_index, 9U);
    EXPECT_EQ(pinter.brew_start_day, 0U);
    EXPECT_EQ(pinter.conditioning_start_day, 0U);
    EXPECT_EQ(pinter.planned_brewing_days, 0U);
    EXPECT_FALSE(pinter.cold_crash_used);
}

HOST_TEST(current_stage_planned_days_is_zero_for_states_with_no_timed_stage)
{
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(make_pinter(PinterState::Idle)), 0U);
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(make_pinter(PinterState::Ready)), 0U);
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(make_pinter(PinterState::Consumed)),
             0U);
}

HOST_TEST(current_stage_planned_days_reads_the_field_matching_the_active_stage)
{
    PinterStatus brewing = make_pinter(PinterState::Brewing);
    brewing.planned_brewing_days = 7U;
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(brewing), 7U);

    PinterStatus crashing = make_pinter(PinterState::ColdCrash, 3U, true);
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(crashing), 3U);

    PinterStatus conditioning = make_pinter(PinterState::Conditioning);
    conditioning.planned_conditioning_days = 14U;
    EXPECT_EQ(pinter_scheduling::current_stage_planned_days(conditioning), 14U);
}

HOST_TEST(nudge_current_stage_days_is_a_no_op_for_states_with_no_timed_stage)
{
    PinterStatus idle = make_pinter(PinterState::Idle);
    EXPECT_FALSE(pinter_scheduling::nudge_current_stage_days(idle, 1));

    PinterStatus ready = make_pinter(PinterState::Ready);
    EXPECT_FALSE(pinter_scheduling::nudge_current_stage_days(ready, 1));

    PinterStatus consumed = make_pinter(PinterState::Consumed);
    EXPECT_FALSE(pinter_scheduling::nudge_current_stage_days(consumed, -1));
}

HOST_TEST(nudge_current_stage_days_adjusts_the_field_matching_the_active_stage)
{
    PinterStatus brewing = make_pinter(PinterState::Brewing);
    brewing.brew_start_day = 100U;
    brewing.planned_brewing_days = 7U;
    EXPECT_TRUE(pinter_scheduling::nudge_current_stage_days(brewing, 1));
    EXPECT_EQ(brewing.planned_brewing_days, 8U);
    EXPECT_EQ(pinter_scheduling::current_stage_target_day(brewing), 108U);

    PinterStatus crashing = make_pinter(PinterState::ColdCrash, 3U, true);
    EXPECT_TRUE(pinter_scheduling::nudge_current_stage_days(crashing, -1));
    EXPECT_EQ(crashing.planned_cold_crash_days, 2U);

    PinterStatus conditioning = make_pinter(PinterState::Conditioning);
    conditioning.planned_conditioning_days = 14U;
    EXPECT_TRUE(pinter_scheduling::nudge_current_stage_days(conditioning, -1));
    EXPECT_EQ(conditioning.planned_conditioning_days, 13U);
}

HOST_TEST(nudge_current_stage_days_will_not_take_the_planned_duration_below_one)
{
    PinterStatus brewing = make_pinter(PinterState::Brewing);
    brewing.planned_brewing_days = 1U;
    EXPECT_FALSE(pinter_scheduling::nudge_current_stage_days(brewing, -1));
    EXPECT_EQ(brewing.planned_brewing_days, 1U);
}

HOST_TEST(nudge_current_stage_days_will_not_overflow_the_uint8_storage_range)
{
    PinterStatus brewing = make_pinter(PinterState::Brewing);
    brewing.planned_brewing_days = 255U;
    EXPECT_FALSE(pinter_scheduling::nudge_current_stage_days(brewing, 1));
    EXPECT_EQ(brewing.planned_brewing_days, 255U);
}
