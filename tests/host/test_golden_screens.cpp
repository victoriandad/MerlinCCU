#include <algorithm>
#include <array>
#include <cstdio>
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

namespace
{

constexpr uint32_t kPinterTestDay = 20000U;

PinterStatus make_vessel(int number, PinterState state)
{
    PinterStatus vessel = {};
    std::snprintf(vessel.label.data(), vessel.label.size(), "Pinter %d", number);
    vessel.state = state;
    vessel.planned_brewing_days = 7U;
    vessel.planned_conditioning_days = 10U;
    vessel.brew_start_day = kPinterTestDay - 2U;
    vessel.conditioning_start_day = kPinterTestDay - 1U;
    return vessel;
}

/// @brief Opens the Pinter page with the given vessels, a synced clock (so
/// non-idle vessels get a third countdown line), and `slot` pressed to
/// select a vessel.
const ConsoleState& open_pinter_page(const std::array<PinterStatus, kPinterCount>& vessels,
                                     ButtonId slot)
{
    console_controller::init();
    console_controller::apply_persisted_pinters(vessels);
    TimeStatus time = {};
    time.synced = true;
    time.local_epoch_day = kPinterTestDay;
    (void)console_controller::set_time_status(time);
    for (const ButtonId id : {ButtonId::LeftLower, slot}) // Home L4: PINTER, then the vessel
    {
        (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
    }
    return console_controller::state();
}

// Body zone between the header and the footer (docs/ui_requirements.md
// section 1). Moves to the shared layout header under #113.
constexpr int kBodyTopY = 34;
constexpr int kFooterTopY = 288;

/// @brief Renders the page body alone and checks it draws nothing between the
/// header and the footer, so the block-reason line cannot touch a label.
bool pinter_body_leaves_softkey_rows_clear(const ConsoleState& state)
{
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const auto fb = render_page(body_only);
    return !any_pixel_lit(fb.data(), 0, kBodyTopY, kUiWidth - 1, kFooterTopY - 1);
}

} // namespace

HOST_TEST(pinter_page_dock_full_notice_sits_in_the_footer)
{
    const ConsoleState& state = open_pinter_page(
        {make_vessel(1, PinterState::Brewing), make_vessel(2, PinterState::Brewing),
         make_vessel(3, PinterState::Brewing), make_vessel(4, PinterState::Idle)},
        ButtonId::LeftLower); // L4: the idle vessel
    EXPECT_TRUE(state.pinter_block_reason[0] != '\0');
    EXPECT_TRUE(pinter_body_leaves_softkey_rows_clear(state));

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("pinter_page_dock_full", fb.data()));
}

HOST_TEST(pinter_page_fridge_full_notice_sits_in_the_footer)
{
    const ConsoleState& state = open_pinter_page(
        {make_vessel(1, PinterState::Conditioning), make_vessel(2, PinterState::Conditioning),
         make_vessel(3, PinterState::Brewing), make_vessel(4, PinterState::Brewing)},
        ButtonId::LeftMiddle); // L3: a brewing vessel with the fridge full
    EXPECT_TRUE(state.pinter_block_reason[0] != '\0');
    EXPECT_TRUE(pinter_body_leaves_softkey_rows_clear(state));

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("pinter_page_fridge_full", fb.data()));
}

namespace
{

/// @brief True when any lit body pixel is within one pixel of a lit softkey
/// label pixel (SCR-5, SCR-6). The softkey layer is rendered by drawing the
/// same softkey map on a page with no body; the body layer by drawing the
/// page with every label cleared. The header is excluded.
bool body_touches_softkeys(const ConsoleState& state, uint32_t now_ms)
{
    ConsoleState labels_only = state;
    labels_only.active_page = MenuPage::Status; // A key page with no body.
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const auto labels = render_page(labels_only, now_ms);
    const auto body = render_page(body_only, now_ms);

    for (int y = kBodyTopY; y < kUiHeight; ++y)
    {
        for (int x = 0; x < kUiWidth; ++x)
        {
            if (!framebuffer::get_pixel(body.data(), x, y))
            {
                continue;
            }
            if (any_pixel_lit(labels.data(), x - 1, std::max(y - 1, kBodyTopY), x + 1, y + 1))
            {
                return true;
            }
        }
    }
    return false;
}

constexpr uint32_t kAirTrafficNowMs = 600000U;
constexpr uint32_t kAirTrafficLiveMs = kAirTrafficNowMs - 5000U;
constexpr uint32_t kAirTrafficStaleMs = 1000U;

AirTrafficStatus make_air_traffic(uint8_t aircraft_count, uint32_t last_success_ms)
{
    AirTrafficStatus status = {};
    status.enabled = true;
    status.configured = true;
    status.data_valid = aircraft_count > 0U;
    status.configured_radius_nm = 30U;
    status.aircraft_count = aircraft_count;
    status.last_success_ms = last_success_ms;
    for (uint8_t i = 0U; i < aircraft_count; ++i)
    {
        AirTrafficEntry& entry = status.aircraft[i];
        std::snprintf(entry.callsign.data(), entry.callsign.size(), "BAW%u",
                      static_cast<unsigned>(100U + i));
        std::snprintf(entry.distance_text.data(), entry.distance_text.size(), "%unm",
                      static_cast<unsigned>(2U + i));
        std::snprintf(entry.altitude_text.data(), entry.altitude_text.size(), "%u00ft",
                      static_cast<unsigned>(30U + i));
        std::snprintf(entry.bearing_text.data(), entry.bearing_text.size(), "%03u",
                      static_cast<unsigned>((i * 37U) % 360U));
        entry.distance_deci_nm = static_cast<int16_t>(20 + (i * 10));
        entry.bearing_deg = static_cast<int16_t>((i * 37) % 360);
    }
    return status;
}

const ConsoleState& open_air_traffic(const AirTrafficStatus& status, bool plot_view)
{
    console_controller::init();
    (void)console_controller::set_air_traffic_status(status);
    (void)console_controller::handle_button_event({ButtonId::LeftBottom, ButtonEventType::Pressed});
    if (plot_view)
    {
        (void)console_controller::handle_button_event(
            {ButtonId::LeftBottom, ButtonEventType::Pressed}); // L5: VIEW
    }
    return console_controller::state();
}

void check_air_traffic_page(const char* golden_name, const AirTrafficStatus& status,
                            bool plot_view)
{
    const ConsoleState& state = open_air_traffic(status, plot_view);
    EXPECT_TRUE(state.active_page == MenuPage::AirTraffic);
    EXPECT_FALSE(body_touches_softkeys(state, kAirTrafficNowMs));
    const auto fb = render_page(state, kAirTrafficNowMs);
    EXPECT_TRUE(golden_test::check_golden(golden_name, fb.data()));
}

} // namespace

HOST_TEST(air_traffic_pages_keep_footer_text_clear_of_softkeys)
{
    check_air_traffic_page("air_traffic_table_live", make_air_traffic(5U, kAirTrafficLiveMs),
                           false);
    check_air_traffic_page("air_traffic_table_stale", make_air_traffic(5U, kAirTrafficStaleMs),
                           false);
    check_air_traffic_page("air_traffic_table_paged",
                           make_air_traffic(static_cast<uint8_t>(kAirTrafficRowsPerPage + 3U),
                                            kAirTrafficLiveMs),
                           false);
    check_air_traffic_page("air_traffic_plot_stale", make_air_traffic(5U, kAirTrafficStaleMs),
                           true);
    check_air_traffic_page("air_traffic_no_aircraft", make_air_traffic(0U, kAirTrafficLiveMs),
                           false);
    AirTrafficStatus waiting = make_air_traffic(0U, 0U);
    waiting.last_http_status = 503;
    check_air_traffic_page("air_traffic_waiting", waiting, false);
}

HOST_TEST(air_traffic_paged_table_leaves_the_footer_to_the_arrows)
{
    ConsoleState state = open_air_traffic(
        make_air_traffic(static_cast<uint8_t>(kAirTrafficRowsPerPage + 3U), kAirTrafficLiveMs),
        false);
    for (SoftKeyAction& key : state.softkeys)
    {
        key.label = "";
    }
    const auto fb = render_page(state, kAirTrafficNowMs);
    // Arrows are drawn within 32 px of the centre line; nothing else may be
    // in the footer while they are shown (SCR-4).
    constexpr int kArrowBandHalfWidth = 32;
    EXPECT_FALSE(any_pixel_lit(fb.data(), 0, kFooterTopY, kUiWidth / 2 - kArrowBandHalfWidth - 1,
                               kUiHeight - 1));
    EXPECT_FALSE(any_pixel_lit(fb.data(), kUiWidth / 2 + kArrowBandHalfWidth + 1, kFooterTopY,
                               kUiWidth - 1, kUiHeight - 1));
}
