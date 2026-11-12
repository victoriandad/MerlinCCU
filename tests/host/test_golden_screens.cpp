#include <array>
#include <cstring>
#include <initializer_list>

#include "console_controller.h"
#include "console_model.h"
#include "framebuffer.h"
#include "input.h"
#include "golden_test_support.h"
#include "panel_config.h"
#include "screens.h"
#include "test_framework.h"

namespace
{

/// @brief Renders `console_state` through the real screens::draw_menu_screen
/// dispatch (the same entry point MerlinCCU.cpp calls every tick) into a
/// fresh framebuffer, using a fixed `now_ms` so freshness-text pages render
/// deterministically.
std::array<uint8_t, static_cast<size_t>(kUiFbSize)> render_page(const ConsoleState& console_state,
                                                                uint32_t now_ms = 60000U)
{
    std::array<uint8_t, static_cast<size_t>(kUiFbSize)> fb = {};
    screens::draw_menu_screen(fb.data(), console_state, now_ms);
    return fb;
}

} // namespace

HOST_TEST(calendar_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::Calendar;
    state.calendar_day_offset = 3;
    state.time_status.synced = true;
    state.time_status.weekday_index = 2; // Tuesday
    std::strncpy(state.time_status.time_text.data(), "12:34", state.time_status.time_text.size());

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("calendar_page", fb.data()));
}

HOST_TEST(calendar_detail_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::CalendarDetail;
    state.calendar_event_count = 1;
    state.selected_calendar_event_index = 0;
    CalendarEvent& event = state.calendar_events[0];
    std::strncpy(event.title.data(), "Team Sync", event.title.size());
    std::strncpy(event.start_time.data(), "09:00", event.start_time.size());
    std::strncpy(event.end_time.data(), "09:30", event.end_time.size());
    std::strncpy(event.location.data(), "Room 4", event.location.size());
    event.owner = CalendarOwner::Owner1;

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("calendar_detail_page", fb.data()));
}

HOST_TEST(status_resources_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::StatusResources;
    state.image_footprint_status = {
        .valid = true,
        .program_flash_bytes = 512000U,
        .flash_budget_bytes = 4177920U,
        .reserved_flash_bytes = 8192U,
        .static_ram_bytes = 511900U,
        .total_ram_bytes = 524288U,
    };
    state.heap_status = {.valid = true, .used_bytes = 4096U, .arena_bytes = 8192U};
    state.stack_status = {.valid = true, .free_bytes = 1200U};
    state.main_loop_load_status = {.valid = true, .load_percent = 12U, .sample_ms = 250U};
    state.display_timing_status = {
        .valid = true,
        .frame_rate_hz = 25U,
        .last_rebuild_us = 3400U,
        .present_skipped_count = 2U,
    };

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("status_resources_page", fb.data()));
}

HOST_TEST(share_detail_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::ShareDetail;
    state.share_count = 1;
    state.selected_share_index = 0;
    state.share_period = SharePeriod::Today;
    state.share_data_valid = true;
    state.share_data_last_success_ms = 30000U;
    ShareWatchEntry& share = state.watched_shares[0];
    std::strncpy(share.display_name.data(), "BAE SYSTEMS", share.display_name.size());
    std::strncpy(share.symbol.data(), "BA.L", share.symbol.size());
    std::strncpy(share.price_text.data(), "1,372.0", share.price_text.size());
    std::strncpy(share.change_text.data(), "+0.02%", share.change_text.size());
    for (size_t i = 0; i < share.history_points.size(); ++i)
    {
        share.history_points[i] = static_cast<uint16_t>(1350U + (i * 2U));
    }

    const auto fb = render_page(state, 60000U);
    EXPECT_TRUE(golden_test::check_golden("share_detail_page", fb.data()));
}

HOST_TEST(settings_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::Settings;
    state.settings_page_index = 0;

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("settings_page", fb.data()));
}

HOST_TEST(keypad_debug_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::KeypadDebug;
    state.keypad_debug_status.active_mask = 0x0021U;
    state.keypad_debug_status.active_count = 2U;
    state.keypad_debug_status.configured_count = 40U;
    std::strncpy(state.keypad_debug_status.pressed_key_name.data(), "A",
                state.keypad_debug_status.pressed_key_name.size());

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("keypad_debug_page", fb.data()));
}

namespace
{

/// @brief Reaches `page` through real key presses so the rendered softkey map
/// is the one the firmware builds, not an empty fixture.
const ConsoleState& navigate(std::initializer_list<ButtonId> presses)
{
    console_controller::init();
    for (const ButtonId id : presses)
    {
        (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
    }
    return console_controller::state();
}

/// @brief Returns true if any pixel in the given rectangle is lit.
bool any_pixel_lit(const uint8_t* fb, int left, int top, int right, int bottom)
{
    for (int y = top; y <= bottom; ++y)
    {
        for (int x = left; x <= right; ++x)
        {
            if (framebuffer::get_pixel(fb, x, y))
            {
                return true;
            }
        }
    }
    return false;
}

// Row 5 softkey label block: top 41 + 4 x 57 pitch = 269, 18 px tall
// (kSoftkeyLayout in screens.cpp). The right-hand cell starts just right of
// the centre line. Moves to the shared layout header under #113.
constexpr int kRow5CellTopY = 269;
constexpr int kRow5CellBottomY = 286;
constexpr int kRightCellLeftX = kUiWidth / 2;

} // namespace

HOST_TEST(greyscale_test_card_renders_consistently_with_softkeys)
{
    const ConsoleState& state = navigate({ButtonId::LeftTop,       // Home L1: STATUS
                                          ButtonId::RightMiddle}); // Status R3: GREYSCALE
    EXPECT_TRUE(state.active_page == MenuPage::GreyscaleTest);

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("greyscale_test_page", fb.data()));
}

HOST_TEST(greyscale_test_card_body_stays_out_of_the_r5_softkey_cell)
{
    ConsoleState state = navigate({ButtonId::LeftTop, ButtonId::RightMiddle});
    for (SoftKeyAction& key : state.softkeys)
    {
        key.label = "";
    }

    const auto fb = render_page(state);
    EXPECT_FALSE(any_pixel_lit(fb.data(), kRightCellLeftX, kRow5CellTopY, kUiWidth - 1,
                               kRow5CellBottomY));
}
