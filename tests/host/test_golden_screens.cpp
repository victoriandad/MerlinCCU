#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "alert_controller.h"
#include "alert_history.h"
#include "alert_screens.h"
#include "config_manager.h"
#include "console_controller.h"
#include "console_model.h"
#include "framebuffer.h"
#include "golden_test_support.h"
#include "input.h"
#include "local_conditions.h"
#include "panel_config.h"
#include "screens.h"
#include "screens_shared.h"
#include "test_framework.h"
#include "weather_extremes.h"
#include "weather_screens.h"

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
        .nominal_frame_rate_hz = 297U,
        .sample_sequence = 1U,
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
        share.history_points[i] = 1350.0F + (static_cast<float>(i) * 2.0F);
    }

    const auto fb = render_page(state, 60000U);
    EXPECT_TRUE(golden_test::check_golden("share_detail_page", fb.data()));
}

HOST_TEST(share_detail_graph_shows_a_fall_from_the_previous_close_in_percent)
{
    // #178: a GBX share that opened above yesterday's close and fell through
    // it. Bars rise and fall from the dotted base line, the axis is in
    // percent, and the price row reads in pounds.
    ConsoleState state = {};
    state.active_page = MenuPage::ShareDetail;
    state.share_count = 1;
    state.share_period = SharePeriod::Today;
    state.share_data_valid = true;
    state.share_data_last_success_ms = 30000U;
    ShareWatchEntry& share = state.watched_shares[0];
    std::strncpy(share.display_name.data(), "BAE SYSTEMS", share.display_name.size());
    std::strncpy(share.symbol.data(), "BA.L", share.symbol.size());
    std::strncpy(share.currency.data(), "GBX", share.currency.size());
    std::strncpy(share.price_text.data(), "1,346.5", share.price_text.size());
    std::strncpy(share.change_text.data(), "-1.71%", share.change_text.size());
    share.base_price = 1370.0F;
    share.base_valid = true;
    for (size_t i = 0; i < share.history_points.size(); ++i)
    {
        share.history_points[i] = 1378.0F - (static_cast<float>(i) * 1.4F);
    }

    const auto fb = render_page(state, 60000U);
    EXPECT_TRUE(golden_test::check_golden("share_detail_fall_from_close", fb.data()));
}

HOST_TEST(settings_page_renders_consistently)
{
    ConsoleState state = {};
    state.active_page = MenuPage::Settings;
    state.settings_page_index = 0;

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("settings_page", fb.data()));
}

HOST_TEST(home_footer_fits_the_longest_address_and_build_time)
{
    ConsoleState state = {};
    state.active_page = MenuPage::Home;
    state.wifi_status.state = WifiConnectionState::Connected;
    std::snprintf(state.wifi_status.ip_address.data(), state.wifi_status.ip_address.size(),
                  "255.255.255.255");
    // A 15-character name, the longest a unit can advertise, is as wide as
    // the longest IP address it stands in for.
    std::snprintf(state.wifi_status.host_name.data(), state.wifi_status.host_name.size(),
                  "merlin-landing2");
    state.firmware_status.valid = true;
    std::snprintf(state.firmware_status.built.data(), state.firmware_status.built.size(),
                  "28 Sep 23:59");

    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("home_footer_longest", fb.data()));
}

namespace
{

/// @brief Adds one other unit to the Units page's fleet (#210).
void add_peer(UnitFleet& fleet, const char* mac, const char* summary, const char* availability,
              uint32_t now_ms)
{
    unit_fleet::TopicRef ref = {};
    std::snprintf(ref.mac.data(), ref.mac.size(), "%s", mac);
    if (summary != nullptr)
    {
        ref.kind = unit_fleet::Kind::Summary;
        (void)unit_fleet::apply(fleet, ref, summary, now_ms, "28cdc11a896c");
    }
    ref.kind = unit_fleet::Kind::Availability;
    (void)unit_fleet::apply(fleet, ref, availability, now_ms, "28cdc11a896c");
}

/// @brief The Units page with MQTT set up, as this unit `merlinccu3`.
ConsoleState units_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::StatusUnits;
    state.mqtt_status.configured = true;
    std::snprintf(state.wifi_status.host_name.data(), state.wifi_status.host_name.size(),
                  "merlinccu3");
    std::snprintf(state.firmware_status.version.data(), state.firmware_status.version.size(),
                  "76c0214");
    return state;
}

} // namespace

HOST_TEST(units_page_shows_each_unit_its_firmware_and_its_own_alerts)
{
    ConsoleState state = units_state();
    UnitFleet& fleet = state.unit_fleet;
    (void)unit_fleet::set_listening(fleet, true);
    add_peer(fleet, "88a29e0b828a",
             "name=merlinccu2\nversion=76c0214\nboard=pico2_w\nalerts=2\ntop=HEAP LOW\n", "online",
             0U);
    // Offline for 45 minutes, accepted on another unit, with the longest name.
    add_peer(fleet, "d83add000001",
             "name=merlin-landing2\nversion=9271096+\nboard=pico2_w\nalerts=0\ntop=\n", "offline",
             0U);
    unit_fleet::TopicRef ack = {};
    std::snprintf(ack.mac.data(), ack.mac.size(), "d83add000001");
    ack.kind = unit_fleet::Kind::OfflineAck;
    (void)unit_fleet::apply(fleet, ack, "1", 0U, "28cdc11a896c");
    // An old unit that publishes no summary.
    add_peer(fleet, "d83add0a1b2c", nullptr, "online", 0U);

    const auto fb = render_page(state, 45U * 60U * 1000U);
    EXPECT_TRUE(golden_test::check_golden("status_units_page", fb.data()));
}

HOST_TEST(units_page_waits_for_mqtt_then_says_when_it_is_alone)
{
    ConsoleState state = units_state();
    EXPECT_TRUE(golden_test::check_golden("status_units_waiting", render_page(state).data()));

    (void)unit_fleet::set_listening(state.unit_fleet, true);
    EXPECT_TRUE(golden_test::check_golden("status_units_alone", render_page(state).data()));
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

} // namespace

namespace
{

constexpr int kAlertTextRightMarginX = kUiWidth - 12;

ConsoleState make_alert_detail_state(const char* detail)
{
    ConsoleState state = {};
    state.active_page = MenuPage::AlertDetail;
    state.alert_count = 1U;
    state.alert_detail_index = 0U;
    ActiveAlert& alert = state.active_alerts[0];
    alert.severity = AlertSeverity::Warning;
    std::snprintf(alert.summary.data(), alert.summary.size(), "TEST");
    // Cut to what an alert keeps, in a buffer that outlives the state, as
    // alert_controller's shared detail buffers do.
    static std::array<std::array<char, kAlertDetailCapacity>, 4> texts = {};
    static size_t next_text = 0U;
    std::array<char, kAlertDetailCapacity>& text = texts[next_text++ % texts.size()];
    std::snprintf(text.data(), text.size(), "%s", detail);
    alert.detail.point_to(text.data());
    state.softkeys[static_cast<size_t>(SoftKeyId::Left5)] = {"IGNORE", SoftKeyRoute::AlertIgnore,
                                                             true};
    state.softkeys[static_cast<size_t>(SoftKeyId::Right5)] = {"ACCEPT", SoftKeyRoute::AlertAccept,
                                                              true};
    return state;
}

/// @brief True when the detail body stays inside the text column and above
/// the row 5 labels.
bool alert_detail_stays_in_bounds(const ConsoleState& state)
{
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const auto fb = render_page(body_only);
    return !any_pixel_lit(fb.data(), kAlertTextRightMarginX, kBodyTopY, kUiWidth - 1,
                          kFooterTopY - 1) &&
           !any_pixel_lit(fb.data(), 0, 264, kUiWidth - 1, kFooterTopY - 1);
}

// 318 characters: close to ActiveAlert::detail's capacity, with long words.
constexpr const char* kLongAlertDetail =
    "Tracked Home Assistant entity is missing or unavailable.\nCheck the entity id in "
    "settings and HA integration state.\nEnvironment sensor board is not fully detected.\n"
    "Verify SSID and password in NETWORK settings.\nAn_unbroken_identifier_longer_than_a_line "
    "must still be hard-broken.\nLast line of the longest alert.";

} // namespace

HOST_TEST(alert_detail_wraps_every_raised_alert_inside_the_text_column)
{
    ConsoleState state = {};
    state.wifi_status.state = WifiConnectionState::AuthFailed;
    state.time_status.synced = false;
    state.environment_sensor_status.enabled = true;
    state.environment_sensor_status.health =
        environment_sensor_manager::EnvironmentSensorHealth::Fault;
    state.share_data_configured = true;
    state.share_data_last_error = -1;
    for (int i = 0; i < 6; ++i)
    {
        alert_controller::reset();
        alert_controller::sync(state);
    }
    for (int i = 0; i < 6; ++i)
    {
        alert_controller::sync(state);
    }
    EXPECT_TRUE(state.alert_count >= 3U);

    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        ConsoleState detail = make_alert_detail_state(state.active_alerts[i].detail.data());
        EXPECT_TRUE(alert_detail_stays_in_bounds(detail));
    }
    EXPECT_TRUE(alert_detail_stays_in_bounds(make_alert_detail_state(kLongAlertDetail)));
}

HOST_TEST(alert_detail_scrolls_until_the_last_line_is_in_view)
{
    // Twenty short lines: more than the twelve visible, so the page scrolls.
    std::string many_lines;
    for (int line = 1; line <= 20; ++line)
    {
        many_lines += "Line " + std::to_string(line) + (line < 20 ? "\n" : "");
    }
    ConsoleState state = make_alert_detail_state(many_lines.c_str());
    const uint8_t max_scroll = alert_screens::detail_max_scroll(state.active_alerts[0]);
    EXPECT_EQ(max_scroll, 8U);

    state.alert_detail_scroll_line = max_scroll;
    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("alert_detail_long_scrolled_to_end", fb.data()));
    EXPECT_TRUE(alert_detail_stays_in_bounds(state));

    // Asking for more than the maximum renders the same last page.
    state.alert_detail_scroll_line = static_cast<uint8_t>(max_scroll + 5U);
    const auto clamped = render_page(state);
    EXPECT_TRUE(clamped == fb);
}

HOST_TEST(alert_detail_cursor_keys_stop_at_the_scroll_limit)
{
    console_controller::init();
    // Joined once since boot, so a drop raises NETWORK at once.
    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connected;
    (void)console_controller::set_wifi_status(wifi);
    wifi.state = WifiConnectionState::Disabled;
    (void)console_controller::set_wifi_status(wifi);
    for (const ButtonId id : {ButtonId::Alert, ButtonId::LeftTop})
    {
        (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
    }
    const ConsoleState& state = console_controller::state();
    EXPECT_TRUE(state.active_page == MenuPage::AlertDetail);
    const uint8_t max_scroll =
        alert_screens::detail_max_scroll(state.active_alerts[state.alert_detail_index]);

    for (int i = 0; i < 40; ++i)
    {
        (void)console_controller::handle_button_event(
            {ButtonId::CursorRight, ButtonEventType::Pressed});
    }
    EXPECT_EQ(state.alert_detail_scroll_line, max_scroll);
    for (int i = 0; i < 40; ++i)
    {
        (void)console_controller::handle_button_event(
            {ButtonId::CursorLeft, ButtonEventType::Pressed});
    }
    EXPECT_EQ(state.alert_detail_scroll_line, 0U);
}

HOST_TEST(a_recurring_alert_shows_its_count_on_the_key_and_in_the_detail)
{
    console_controller::init();
    // Joined once since boot, so a drop raises NETWORK at once.
    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connected;
    (void)console_controller::set_wifi_status(wifi);
    for (int i = 0; i < 3; ++i)
    {
        wifi.state = WifiConnectionState::Disabled;
        (void)console_controller::set_wifi_status(wifi);
        wifi.state = WifiConnectionState::Connected;
        (void)console_controller::set_wifi_status(wifi);
    }
    wifi.state = WifiConnectionState::Disabled;
    (void)console_controller::set_wifi_status(wifi);

    (void)console_controller::handle_button_event({ButtonId::Alert, ButtonEventType::Pressed});
    const ConsoleState& list = console_controller::state();
    EXPECT_TRUE(list.active_page == MenuPage::AlertList);
    const char* label = list.softkeys[static_cast<size_t>(SoftKeyId::Left1)].label;
    EXPECT_TRUE(std::strstr(label, " x4]") != nullptr);
    EXPECT_TRUE(golden_test::check_golden("alert_list_recurring", render_page(list).data()));

    (void)console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Pressed});
    const ConsoleState& detail = console_controller::state();
    EXPECT_TRUE(detail.active_page == MenuPage::AlertDetail);
    EXPECT_TRUE(alert_detail_stays_in_bounds(detail));
    EXPECT_TRUE(golden_test::check_golden("alert_detail_recurring", render_page(detail).data()));
}

HOST_TEST(alert_history_pages_clear_all_asks_first)
{
    console_controller::init();
    alert_history::reset();
    for (uint8_t i = 0U; i < 10U; ++i)
    {
        ActiveAlert alert = {};
        alert.code = i;
        alert.severity = AlertSeverity::Message;
        alert.raised_minute = (20770U * 1440U) + (60U * i);
        std::snprintf(alert.summary.data(), alert.summary.size(), "PAST %u",
                      static_cast<unsigned>(i));
        alert_history::record(alert, alert_history::Ending::Cleared, alert.raised_minute + 5U);
    }
    const auto press = [](ButtonId id)
    { (void)console_controller::handle_button_event({id, ButtonEventType::Pressed}); };

    press(ButtonId::Alert);
    press(ButtonId::LeftBottom);
    const ConsoleState& history = console_controller::state();
    EXPECT_TRUE(history.active_page == MenuPage::AlertHistory);
    EXPECT_EQ(alert_history::page_count(), 2U);
    press(ButtonId::CursorRight);
    EXPECT_EQ(console_controller::state().alert_history_page_index, 1U);
    EXPECT_TRUE(golden_test::check_golden("alert_history_page_2", render_page(history).data()));

    // CLEAR ALL asks first; BACK STEP and CANCEL both keep the history.
    press(ButtonId::LeftBottom);
    EXPECT_TRUE(console_controller::state().alert_history_clear_confirming);
    EXPECT_TRUE(golden_test::check_golden("alert_history_clear_confirm",
                                          render_page(console_controller::state()).data()));
    press(ButtonId::BackStep);
    EXPECT_FALSE(console_controller::state().alert_history_clear_confirming);
    EXPECT_TRUE(console_controller::state().active_page == MenuPage::AlertHistory);
    press(ButtonId::LeftBottom);
    press(ButtonId::LeftBottom);
    EXPECT_EQ(alert_history::count(), 10U);

    press(ButtonId::LeftBottom);
    press(ButtonId::RightBottom);
    EXPECT_EQ(alert_history::count(), 0U);
    EXPECT_EQ(console_controller::state().alert_history_page_index, 0U);
    EXPECT_TRUE(golden_test::check_golden("alert_history_empty",
                                          render_page(console_controller::state()).data()));
    alert_history::reset();
}

HOST_TEST(the_longest_history_values_fit_the_key)
{
    // History keys use the 8x12 font with two label lines (#203).
    for (const char* value : {"[12 Nov 23:50]", "[12 Nov x99]", "[06:12-07:40]"})
    {
        EXPECT_TRUE(screens::text_width(value, text_styles::TextStyle::Softkey) <=
                    ui_layout::kSoftkeyLabelMaxWidth);
    }
}

HOST_TEST(the_largest_alert_count_fits_the_alert_key)
{
    // The alert list uses the 8x12 font with two label lines; the count must
    // stay inside the value's brackets on the second line.
    const screens::SoftkeyLabelLayout layout =
        screens::layout_softkey_label("NETWORK\n[--:-- x99]", text_styles::TextStyle::Softkey, 2);
    EXPECT_EQ(layout.count, 2);
    EXPECT_TRUE(layout.lines[1].bracketed);
    EXPECT_TRUE(std::strcmp(layout.lines[1].text, "--:-- x99") == 0);

    // The longest detail text plus the count line still fits the page.
    ConsoleState state = make_alert_detail_state(kLongAlertDetail);
    state.active_alerts[0].occurrence_count = kMaxAlertOccurrenceCount;
    EXPECT_TRUE(alert_detail_stays_in_bounds(state));
}

HOST_TEST(alert_detail_short_and_empty_states_render_consistently)
{
    const auto short_fb = render_page(make_alert_detail_state(
        "Wi-Fi link is down.\nCheck SSID/password or signal.\nWithout network, cloud features "
        "are unavailable."));
    EXPECT_TRUE(golden_test::check_golden("alert_detail_short", short_fb.data()));

    ConsoleState empty = make_alert_detail_state("");
    empty.alert_count = 0U;
    const auto empty_fb = render_page(empty);
    EXPECT_TRUE(golden_test::check_golden("alert_detail_empty", empty_fb.data()));
}

HOST_TEST(time_zone_pages_render_consistently)
{
    // Home R1 SETTINGS, cursor right to Settings 2/2, L3 TIME ZONE, choose
    // LONDON (page 1, slot 5 = R1), then view page 2.
    const ConsoleState& first = navigate({ButtonId::RightTop, ButtonId::CursorRight,
                                          ButtonId::LeftMiddle, ButtonId::CursorLeft,
                                          ButtonId::RightTop});
    EXPECT_TRUE(first.active_page == MenuPage::TimeZoneSettings);
    EXPECT_TRUE(first.time_zone == TimeZoneSelection::EuropeLondon);
    const auto first_fb = render_page(first);
    EXPECT_TRUE(golden_test::check_golden("time_zone_page_1", first_fb.data()));

    (void)console_controller::handle_button_event({ButtonId::CursorRight, ButtonEventType::Pressed});
    const auto second_fb = render_page(console_controller::state());
    EXPECT_TRUE(golden_test::check_golden("time_zone_page_2", second_fb.data()));
}

HOST_TEST(pinter_reset_states_render_consistently)
{
    const ConsoleState& normal = open_pinter_page(
        {make_vessel(1, PinterState::Brewing), make_vessel(2, PinterState::Idle),
         make_vessel(3, PinterState::Idle), make_vessel(4, PinterState::Idle)},
        ButtonId::LeftTop);
    const auto normal_fb = render_page(normal);
    EXPECT_TRUE(golden_test::check_golden("pinter_page_normal", normal_fb.data()));

    (void)console_controller::handle_button_event(
        {ButtonId::RightMiddle, ButtonEventType::Pressed}); // R3: RESET
    const ConsoleState& confirming = console_controller::state();
    EXPECT_TRUE(confirming.pinter_reset_confirming);
    EXPECT_FALSE(body_touches_softkeys(confirming, 60000U));
    const auto confirm_fb = render_page(confirming);
    EXPECT_TRUE(golden_test::check_golden("pinter_page_reset_confirm", confirm_fb.data()));
}

HOST_TEST(brew_timing_page_with_a_planned_cold_crash)
{
    (void)open_pinter_page(
        {make_vessel(1, PinterState::Brewing), make_vessel(2, PinterState::Idle),
         make_vessel(3, PinterState::Idle), make_vessel(4, PinterState::Idle)},
        ButtonId::LeftUpper); // L2: an idle vessel
    for (const ButtonId id : {ButtonId::RightTop,     // R1: START
                              ButtonId::LeftTop,      // L1: first brew
                              ButtonId::RightUpper})  // R2: CRASH +
    {
        (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
    }
    const ConsoleState& state = console_controller::state();
    EXPECT_EQ(static_cast<int>(state.active_page), static_cast<int>(MenuPage::PinterStartTiming));
    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("brew_timing_page", fb.data()));
}

HOST_TEST(every_recipe_name_fits_the_brew_timing_footer)
{
    constexpr int kFooterMarginX = 4;
    for (uint8_t i = 0U; i < kPinterBrewCatalogueCount; ++i)
    {
        const char* name = console_controller::pinter_brew_timing(i).name;
        const int width = screens::text_width(name, text_styles::TextStyle::Footer);
        if (width > kUiWidth - (kFooterMarginX * 2))
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       std::string(name) + " is " + std::to_string(width) +
                                           " px wide");
        }
    }
}

HOST_TEST(screen_saver_pages_normal_editing_and_options)
{
    // Home R1 SETTINGS, cursor right to Settings 2/2, L4 SCREEN SAVER.
    const ConsoleState& normal =
        navigate({ButtonId::RightTop, ButtonId::CursorRight, ButtonId::LeftLower});
    EXPECT_EQ(static_cast<int>(normal.active_page), static_cast<int>(MenuPage::ScreenSaverSettings));
    EXPECT_TRUE(golden_test::check_golden("screen_saver_page", render_page(normal).data()));

    (void)console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Pressed});
    const ConsoleState& editing = console_controller::state();
    EXPECT_TRUE(editing.screen_saver_timeout_editing);
    EXPECT_FALSE(body_touches_softkeys(editing, 60000U));
    EXPECT_TRUE(golden_test::check_golden("screen_saver_timeout_editing", render_page(editing).data()));

    (void)console_controller::handle_button_event({ButtonId::LeftBottom, ButtonEventType::Pressed});
    (void)console_controller::handle_button_event({ButtonId::LeftUpper, ButtonEventType::Pressed});
    const ConsoleState& options = console_controller::state();
    EXPECT_EQ(static_cast<int>(options.active_page), static_cast<int>(MenuPage::ScreenSaverOptions));
    EXPECT_TRUE(golden_test::check_golden("screen_saver_options", render_page(options).data()));
}

HOST_TEST(pinter_vessel_labels_wrap_long_recipe_names_and_keep_the_countdown)
{
    // The recipe whose name is widest in the softkey font.
    uint8_t widest = 0U;
    int widest_width = 0;
    for (uint8_t i = 0U; i < kPinterBrewCatalogueCount; ++i)
    {
        const int width = screens::text_width(console_controller::pinter_brew_timing(i).name,
                                              text_styles::TextStyle::SoftkeyDense);
        if (width > widest_width)
        {
            widest_width = width;
            widest = i;
        }
    }
    const std::string recipe = console_controller::pinter_brew_timing(widest).name;

    std::array<PinterStatus, kPinterCount> vessels = {
        make_vessel(1, PinterState::Brewing), make_vessel(2, PinterState::ColdCrash),
        make_vessel(3, PinterState::Conditioning), make_vessel(4, PinterState::Ready)};
    for (PinterStatus& vessel : vessels)
    {
        vessel.brew_index = widest;
        vessel.planned_cold_crash_days = 2U;
        vessel.cold_crash_start_day = kPinterTestDay - 1U;
    }
    const ConsoleState& state = open_pinter_page(vessels, ButtonId::LeftTop);

    constexpr std::array<SoftKeyId, kPinterCount> vessel_keys = {
        SoftKeyId::Left1, SoftKeyId::Left2, SoftKeyId::Left3, SoftKeyId::Left4};
    for (const SoftKeyId id : vessel_keys)
    {
        const std::string label = state.softkeys[static_cast<size_t>(id)].label;
        const screens::SoftkeyLabelLayout layout = screens::layout_softkey_label(
            label.c_str(), text_styles::TextStyle::SoftkeyDense, screens::kMaxSoftkeyLabelLines);

        // Parts of the label as built: caption, [recipe], [stage and countdown].
        std::vector<std::string> parts;
        size_t start = 0U;
        for (size_t nl = label.find('\n'); nl != std::string::npos; nl = label.find('\n', start))
        {
            parts.push_back(label.substr(start, nl - start));
            start = nl + 1U;
        }
        parts.push_back(label.substr(start));
        EXPECT_TRUE(parts.size() >= 2U);

        std::string value;
        std::string last_line;
        int value_lines = 0;
        for (int i = 0; i < layout.count; ++i)
        {
            const screens::SoftkeyLabelLine& line = layout.lines[static_cast<size_t>(i)];
            // Brackets are drawn as graphics, never left as split glyphs.
            EXPECT_TRUE(std::string(line.text).find_first_of("[]") == std::string::npos);
            EXPECT_EQ(line.bracketed, i > 0);
            if (i > 0 && value_lines >= 0)
            {
                value += (value.empty() ? "" : " ") + std::string(line.text);
                ++value_lines;
                if (line.value_end)
                {
                    value_lines = -1; // The state-and-recipe value is complete.
                }
            }
            last_line = line.text;
        }
        // The whole recipe name is shown, wrapped rather than cut, and the
        // stage with its countdown is always the whole last line.
        EXPECT_EQ(parts.size(), 3U);
        EXPECT_TRUE(value == recipe);
        EXPECT_TRUE(parts.size() == 3U && "[" + last_line + "]" == parts[2]);
    }

    // Vessels with a stage target show the countdown.
    EXPECT_TRUE(std::string(state.softkeys[0].label).find("ready") != std::string::npos);
    const auto fb = render_page(state);
    EXPECT_TRUE(golden_test::check_golden("pinter_page_long_recipe", fb.data()));
}

namespace
{

/// @brief Fills a text field to capacity with the widest glyph, in words
/// (`spaced`) or as one unbroken run like a host name.
template <size_t N> void fill_wide(std::array<char, N>& field, bool spaced)
{
    constexpr size_t kWordLength = 7U;
    for (size_t i = 0U; i + 1U < N; ++i)
    {
        field[i] = (spaced && (i % (kWordLength + 1U) == kWordLength)) ? ' ' : 'W';
    }
    field[N - 1U] = '\0';
}

// Detail-row values stop 12 px short of the right edge (kDetailValueRightX
// in screens.cpp), and a wrapped block stops above the row 5 cell.
constexpr int kDetailValueRightX = kUiWidth - 12;

/// @brief True when a page draws nothing right of the value margin and
/// nothing from the top of row 5 to the footer.
bool detail_rows_stay_in_bounds(const uint8_t* fb)
{
    return !any_pixel_lit(fb, kDetailValueRightX, kBodyTopY, kUiWidth - 1, kFooterTopY - 1) &&
           !any_pixel_lit(fb, 0, kRow5CellTopY, kUiWidth - 1, kFooterTopY - 1);
}

ConsoleState make_long_calendar_event_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::CalendarDetail;
    state.calendar_event_count = 1;
    state.selected_calendar_event_index = 0;
    CalendarEvent& event = state.calendar_events[0];
    fill_wide(event.title, true);
    fill_wide(event.location, true);
    fill_wide(event.reminder, true);
    fill_wide(event.attendees, true);
    fill_wide(event.description, true);
    std::strncpy(event.start_time.data(), "09:00", event.start_time.size());
    std::strncpy(event.end_time.data(), "09:30", event.end_time.size());
    event.owner = CalendarOwner::Owner1;
    return state;
}

} // namespace

HOST_TEST(detail_rows_wrap_or_cut_long_values_inside_the_body)
{
    const ConsoleState calendar = make_long_calendar_event_state();
    const auto calendar_fb = render_page(calendar);
    EXPECT_TRUE(detail_rows_stay_in_bounds(calendar_fb.data()));
    EXPECT_TRUE(golden_test::check_golden("calendar_detail_long_text", calendar_fb.data()));

    // Status Integrations reads HA HOST from the saved configuration.
    const RuntimeConfig saved_config = config_manager::settings();
    RuntimeConfig long_host_config = saved_config;
    fill_wide(long_host_config.home_assistant_host, false);
    (void)config_manager::save(long_host_config);

    ConsoleState integrations = {};
    integrations.active_page = MenuPage::StatusIntegrations;
    fill_wide(integrations.home_assistant_status.weather_host, false);
    const auto integrations_fb = render_page(integrations);
    (void)config_manager::save(saved_config);
    EXPECT_TRUE(detail_rows_stay_in_bounds(integrations_fb.data()));
    EXPECT_TRUE(golden_test::check_golden("status_integrations_long_host", integrations_fb.data()));

    ConsoleState connectivity = {};
    connectivity.active_page = MenuPage::StatusConnectivity;
    connectivity.wifi_status.credentials_present = true;
    fill_wide(connectivity.wifi_status.ssid, false);
    EXPECT_TRUE(detail_rows_stay_in_bounds(render_page(connectivity).data()));

    ConsoleState share = {};
    share.active_page = MenuPage::ShareDetail;
    share.share_count = 1;
    share.selected_share_index = 0;
    fill_wide(share.watched_shares[0].display_name, true);
    fill_wide(share.watched_shares[0].symbol, false);
    EXPECT_TRUE(detail_rows_stay_in_bounds(render_page(share).data()));

    ConsoleState keypad = {};
    keypad.active_page = MenuPage::KeypadDebug;
    fill_wide(keypad.keypad_debug_status.active_panel_pins, true);
    fill_wide(keypad.keypad_debug_status.probe_hit_panel_pins, true);
    EXPECT_TRUE(detail_rows_stay_in_bounds(render_page(keypad).data()));
}

HOST_TEST(detail_rows_cut_values_with_a_marker_once_the_block_reaches_row_5)
{
    // Twelve full-length rows need far more lines than fit above row 5, so the
    // later values must be cut rather than wrap into row 5 or the footer.
    std::array<char, 64> long_value = {};
    fill_wide(long_value, true);
    std::array<screens::DetailRow, 12> rows = {};
    for (screens::DetailRow& row : rows)
    {
        row = {"FIELD", long_value.data()};
    }

    std::array<uint8_t, static_cast<size_t>(kUiFbSize)> compact = {};
    screens::draw_compact_detail_rows(compact.data(), rows.data(), rows.size(), kBodyTopY, 13);
    EXPECT_TRUE(detail_rows_stay_in_bounds(compact.data()));
    EXPECT_TRUE(golden_test::check_golden("detail_rows_cut", compact.data()));

    std::array<uint8_t, static_cast<size_t>(kUiFbSize)> info = {};
    screens::draw_info_page_rows(info.data(), rows.data(), 8U);
    EXPECT_TRUE(detail_rows_stay_in_bounds(info.data()));
}

namespace
{

constexpr uint32_t kWeatherTestNowMs = 600000U;

template <size_t N> void set_text(std::array<char, N>& field, const char* text)
{
    std::snprintf(field.data(), field.size(), "%s", text);
}

/// @brief Opens the Weather page through real key presses (Home R2), with
/// `period_presses` presses of PERIOD (L5), and returns a copy to fill in.
ConsoleState open_weather_page(int period_presses)
{
    console_controller::init();
    (void)console_controller::handle_button_event({ButtonId::RightUpper, ButtonEventType::Pressed});
    for (int i = 0; i < period_presses; ++i)
    {
        (void)console_controller::handle_button_event(
            {ButtonId::LeftBottom, ButtonEventType::Pressed});
    }
    ConsoleState state = console_controller::state();
    state.weather_source = WeatherSource::HomeAssistant;
    state.time_status.synced = false;
    HomeAssistantStatus& status = state.home_assistant_status;
    status = {};
    status.configured = true;
    status.state = HomeAssistantConnectionState::Connected;
    status.weather_state = HomeAssistantConnectionState::Connected;
    set_text(status.weather_entity_id, "weather.home");
    set_text(status.weather_source_hint, "Met Office");
    return state;
}

/// @brief Fills a day of hourly rows and a week of daily rows, fetched just now.
void fill_weather_data(ConsoleState& state)
{
    constexpr std::array<const char*, 4> kConditions = {"partlycloudy", "rainy", "sunny",
                                                        "cloudy"};
    HomeAssistantStatus& status = state.home_assistant_status;
    set_text(status.weather_condition, "partlycloudy");
    set_text(status.weather_temperature, "12.5 C");
    set_text(status.sunrise_text, "07:12");
    set_text(status.sunset_text, "16:31");
    status.weather_forecast_count = static_cast<uint8_t>(kWeatherForecastEntryCount);
    for (size_t i = 0U; i < kWeatherForecastEntryCount; ++i)
    {
        WeatherForecastEntry& entry = status.weather_forecast[i];
        std::snprintf(entry.time_text.data(), entry.time_text.size(), "%02u:00",
                      static_cast<unsigned>((8U + i) % 24U));
        std::snprintf(entry.temperature_text.data(), entry.temperature_text.size(), "%u.0 C",
                      static_cast<unsigned>(8U + (i % 7U)));
        std::snprintf(entry.wind_text.data(), entry.wind_text.size(), "%u SW",
                      static_cast<unsigned>(5U + (i % 9U)));
        set_text(entry.condition_text, kConditions[i % kConditions.size()]);
    }
    status.weather_daily_forecast_count = static_cast<uint8_t>(kWeatherDailyForecastEntryCount);
    for (size_t i = 0U; i < kWeatherDailyForecastEntryCount; ++i)
    {
        WeatherDailyForecastEntry& entry = status.weather_daily_forecast[i];
        std::snprintf(entry.date_text.data(), entry.date_text.size(), "2026-11-%02u",
                      static_cast<unsigned>(13U + i));
        std::snprintf(entry.temperature_text.data(), entry.temperature_text.size(), "%u-%uC",
                      static_cast<unsigned>(3U + i), static_cast<unsigned>(11U + i));
        std::snprintf(entry.wind_text.data(), entry.wind_text.size(), "%u NW",
                      static_cast<unsigned>(10U + i));
        set_text(entry.condition_text, kConditions[i % kConditions.size()]);
    }
    status.weather_last_success_ms = kWeatherTestNowMs - 60000U;
}

/// @brief True when the Weather body stays clear of the labelled row 5 cells
/// and of the right margin. The table and sun-time dividers end on x 240, the
/// last pixel inside the 12-pixel margin, so the check starts one pixel right.
bool weather_body_stays_out_of_row_5(const ConsoleState& state)
{
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const auto fb = render_page(body_only, kWeatherTestNowMs);
    // Up to the footer line itself: a wrapped footer rising into the PERIOD
    // label is the overlap #186 found. The one-pixel pad of a warning's
    // highlight (#202) belongs to the footer, so the last row is left out.
    constexpr int kFooterHighlightPadY = 1;
    return !any_pixel_lit(fb.data(), 0, kRow5CellTopY, kUiWidth - 1,
                          ui_layout::kFooterLineY - 1 - kFooterHighlightPadY) &&
           !any_pixel_lit(fb.data(), kUiWidth - 11, kBodyTopY, kUiWidth - 1, kFooterTopY - 1);
}

/// @brief Checks a Weather state's page decision, bounds, and golden image.
void check_weather_state(const ConsoleState& state, weather_screens::WeatherPageState expected,
                         const char* golden)
{
    EXPECT_EQ(static_cast<int>(weather_screens::weather_page_message(state).state),
              static_cast<int>(expected));
    EXPECT_TRUE(weather_body_stays_out_of_row_5(state));
    EXPECT_TRUE(golden_test::check_golden(golden, render_page(state, kWeatherTestNowMs).data()));
}

} // namespace

HOST_TEST(weather_page_forecast_periods_share_one_table_frame)
{
    static constexpr std::array<WeatherPeriod, 3> kPeriods = {
        WeatherPeriod::Hourly, WeatherPeriod::NextTwentyFourHours, WeatherPeriod::NextSevenDays};
    static constexpr std::array<const char*, 3> kGoldens = {
        "weather_hourly", "weather_next_24_hours", "weather_next_7_days"};
    for (size_t presses = 0U; presses < kPeriods.size(); ++presses)
    {
        ConsoleState state = open_weather_page(static_cast<int>(presses));
        fill_weather_data(state);
        EXPECT_EQ(static_cast<int>(state.weather_period), static_cast<int>(kPeriods[presses]));
        check_weather_state(state, weather_screens::WeatherPageState::Forecast, kGoldens[presses]);
    }
}

HOST_TEST(weather_page_shows_stale_data_in_the_footer)
{
    ConsoleState state = open_weather_page(0);
    fill_weather_data(state);
    // Older than the fetcher's stale threshold, and the latest fetch failed.
    state.home_assistant_status.weather_last_success_ms = 1000U;
    state.home_assistant_status.weather_state = HomeAssistantConnectionState::Error;
    check_weather_state(state, weather_screens::WeatherPageState::Forecast, "weather_stale");
}

HOST_TEST(weather_page_inverts_exceptional_cells_and_leads_the_footer_with_a_warning)
{
    // #202: every hazard at once. A frosty hour, a hot hour, a gale with a
    // downpour on one row, heavy snow, and dense fog; on the week view a
    // hot, wet day and a cold, snowy, windy one. An Amber warning leads the
    // footer.
    static constexpr std::array<WeatherPeriod, 3> kPeriods = {
        WeatherPeriod::Hourly, WeatherPeriod::NextTwentyFourHours, WeatherPeriod::NextSevenDays};
    static constexpr std::array<const char*, 3> kGoldens = {"weather_hourly_extremes",
                                                            "weather_next_24_hours_extremes",
                                                            "weather_next_7_days_extremes"};
    for (size_t presses = 0U; presses < kPeriods.size(); ++presses)
    {
        ConsoleState state = open_weather_page(static_cast<int>(presses));
        fill_weather_data(state);
        auto& hourly = state.home_assistant_status.weather_forecast;
        set_text(hourly[0].temperature_text, "-2.0 C");
        hourly[0].hazards = weather_extremes::kCold;
        set_text(hourly[1].temperature_text, "29.0 C");
        hourly[1].hazards = weather_extremes::kHeat;
        set_text(hourly[2].wind_text, "44 SW");
        set_text(hourly[2].condition_text, "pouring");
        hourly[2].hazards = weather_extremes::kWind | weather_extremes::kRain;
        set_text(hourly[3].condition_text, "snowy");
        hourly[3].hazards = weather_extremes::kSnow;
        set_text(hourly[4].condition_text, "fog");
        hourly[4].hazards = weather_extremes::kFog;
        // An hour the Next 24 Hours sample would skip: it takes the row.
        set_text(hourly[8].wind_text, "52 W");
        hourly[8].hazards = weather_extremes::kWind;
        auto& daily = state.home_assistant_status.weather_daily_forecast;
        set_text(daily[1].temperature_text, "18-29C");
        daily[1].hazards = weather_extremes::kHeat | weather_extremes::kRain;
        set_text(daily[3].temperature_text, "-4-1C");
        set_text(daily[3].condition_text, "snowy");
        daily[3].hazards =
            weather_extremes::kCold | weather_extremes::kSnow | weather_extremes::kWind;

        state.weather_warnings.level = WarningLevel::Amber;
        set_text(state.weather_warnings.summary, "AMBER WIND");
        check_weather_state(state, weather_screens::WeatherPageState::Forecast, kGoldens[presses]);
    }
}

HOST_TEST(weather_page_shortens_a_long_attribution_to_one_footer_line)
{
    // A provider credit too long for the footer is cut short, ending in "..",
    // rather than wrapping up into the PERIOD label (#186).
    ConsoleState state = open_weather_page(0);
    fill_weather_data(state);
    set_text(state.home_assistant_status.weather_source_hint,
             "Norwegian Meteorological Institute and NRK");
    check_weather_state(state, weather_screens::WeatherPageState::Forecast,
                        "weather_long_attribution");
}

HOST_TEST(weather_page_not_configured_says_where_to_set_it_up)
{
    using weather_screens::WeatherPageState;

    // HA weather arrives over MQTT (#153): only MQTT being off needs setting up.
    ConsoleState ha_off = open_weather_page(0);
    ha_off.home_assistant_status.weather_state = HomeAssistantConnectionState::Unconfigured;
    check_weather_state(ha_off, WeatherPageState::NotConfigured,
                        "weather_not_configured_home_assistant");

    ConsoleState meteo_no_coords = open_weather_page(0);
    meteo_no_coords.weather_source = WeatherSource::OpenMeteo;
    meteo_no_coords.home_assistant_status.weather_state = HomeAssistantConnectionState::Unconfigured;
    meteo_no_coords.home_assistant_status.weather_entity_id.fill('\0');
    check_weather_state(meteo_no_coords, WeatherPageState::NotConfigured,
                        "weather_not_configured_open_meteo");

    ConsoleState meteo_bad_coords = meteo_no_coords;
    set_text(meteo_bad_coords.home_assistant_status.weather_entity_id, "not,a,place");

    for (const ConsoleState* state : {&ha_off, &meteo_no_coords, &meteo_bad_coords})
    {
        const weather_screens::WeatherPageMessage message =
            weather_screens::weather_page_message(*state);
        EXPECT_EQ(static_cast<int>(message.state),
                  static_cast<int>(WeatherPageState::NotConfigured));
        EXPECT_TRUE(std::strstr(message.hint.data(), "web config") != nullptr);
        EXPECT_TRUE(screens::text_width(message.hint.data(), text_styles::TextStyle::Hint) <=
                    kUiWidth - 24);
    }
}

HOST_TEST(weather_page_waiting_failed_and_no_data_use_the_empty_state_block)
{
    using weather_screens::WeatherPageState;

    ConsoleState waiting = open_weather_page(0);
    waiting.home_assistant_status.weather_state = HomeAssistantConnectionState::Connecting;
    check_weather_state(waiting, WeatherPageState::Waiting, "weather_waiting");

    ConsoleState no_wifi = open_weather_page(0);
    no_wifi.home_assistant_status.weather_state = HomeAssistantConnectionState::WaitingForWifi;
    EXPECT_EQ(static_cast<int>(weather_screens::weather_page_message(no_wifi).state),
              static_cast<int>(WeatherPageState::Waiting));

    // Open-Meteo and Home Assistant failures read the same on this page.
    ConsoleState failed = open_weather_page(0);
    failed.weather_source = WeatherSource::OpenMeteo;
    failed.home_assistant_status.weather_state = HomeAssistantConnectionState::Error;
    failed.home_assistant_status.weather_http_status = 404;
    set_text(failed.home_assistant_status.weather_entity_id, "51.5,-0.1");
    check_weather_state(failed, WeatherPageState::Failed, "weather_failed");
    ConsoleState ha_failed = failed;
    ha_failed.weather_source = WeatherSource::HomeAssistant;
    EXPECT_TRUE(std::strcmp(weather_screens::weather_page_message(ha_failed).hint.data(),
                            weather_screens::weather_page_message(failed).hint.data()) == 0);

    ConsoleState current_only = open_weather_page(0);
    fill_weather_data(current_only);
    current_only.home_assistant_status.weather_forecast_count = 0U;
    EXPECT_EQ(static_cast<int>(weather_screens::weather_page_message(current_only).state),
              static_cast<int>(WeatherPageState::CurrentOnly));
    EXPECT_TRUE(weather_body_stays_out_of_row_5(current_only));

    ConsoleState no_data = current_only;
    no_data.home_assistant_status.weather_condition.fill('\0');
    EXPECT_EQ(static_cast<int>(weather_screens::weather_page_message(no_data).state),
              static_cast<int>(WeatherPageState::NoData));
}

namespace
{

using environment_sensor_manager::EnvironmentSensorHealth;
using environment_sensor_manager::EnvironmentSensorStatus;

constexpr uint32_t kSensorTestNowMs = 900000U;

constexpr std::array<SoftKeyId, 4> kReadingKeyIds = {SoftKeyId::Left1, SoftKeyId::Left2,
                                                     SoftKeyId::Left3, SoftKeyId::Left4};
constexpr std::array<LocalConditionMetric, 4> kReadingMetrics = {
    LocalConditionMetric::Temperature, LocalConditionMetric::Humidity,
    LocalConditionMetric::AirPressure, LocalConditionMetric::AirQuality};

/// @brief A board that is fitted and was just read, with `history_points`
/// five-minute averages behind it.
EnvironmentSensorStatus make_live_sensor(uint16_t history_points)
{
    EnvironmentSensorStatus sensor = {};
    sensor.enabled = true;
    sensor.health = EnvironmentSensorHealth::BoardDetected;
    sensor.bme_reading_valid = true;
    sensor.bme_temperature_centi_celsius = 2150;
    sensor.bme_humidity_milli_percent = 45200U;
    sensor.bme_pressure_pa = 101320U;
    sensor.bme_last_read_ms = kSensorTestNowMs - 5000U;
    sensor.air_quality_score_valid = true;
    sensor.air_quality_score = 97U;
    sensor.air_quality_last_read_ms = kSensorTestNowMs - 5000U;
    sensor.bme_history_count = history_points;
    sensor.air_quality_history_count = history_points;
    for (uint16_t i = 0U; i < history_points; ++i)
    {
        const int wave = static_cast<int>(i % 24U) - 12;
        sensor.bme_temperature_history_centi_celsius[i] = static_cast<int16_t>(2000 + (wave * 15));
        sensor.bme_humidity_history_centi_percent[i] = static_cast<uint16_t>(4500 + (wave * 40));
        sensor.bme_pressure_history_deci_hpa[i] = static_cast<uint16_t>(10130 + wave);
        sensor.air_quality_history_score[i] = static_cast<uint8_t>(90 + (wave / 3));
    }
    return sensor;
}

/// @brief A board that is enabled but has produced no reading, in `health`.
EnvironmentSensorStatus make_unread_sensor(EnvironmentSensorHealth health)
{
    EnvironmentSensorStatus sensor = {};
    sensor.enabled = true;
    sensor.health = health;
    return sensor;
}

/// @brief Opens Local Conditions (Home R3) with `sensor` already reported.
const ConsoleState& open_local_conditions(const EnvironmentSensorStatus& sensor)
{
    console_controller::init();
    (void)console_controller::set_environment_sensor_status(sensor);
    (void)console_controller::handle_button_event(
        {ButtonId::RightMiddle, ButtonEventType::Pressed});
    return console_controller::state();
}

/// @brief True when the page body stays out of every labelled softkey cell row
/// it could meet: rows 1-4 on the left when labelled, and row 5.
bool local_body_stays_clear(const ConsoleState& state)
{
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const auto fb = render_page(body_only, kSensorTestNowMs);
    return !any_pixel_lit(fb.data(), 0, kRow5CellTopY, kUiWidth - 1, kFooterTopY - 1) &&
           !any_pixel_lit(fb.data(), kUiWidth - 11, kBodyTopY, kUiWidth - 1, kFooterTopY - 1);
}

} // namespace

HOST_TEST(local_conditions_without_a_board_blank_every_reading_key)
{
    const ConsoleState& state =
        open_local_conditions(make_unread_sensor(EnvironmentSensorHealth::BoardMissing));
    EXPECT_TRUE(state.active_page == MenuPage::LocalConditions);
    for (const SoftKeyId id : kReadingKeyIds)
    {
        EXPECT_TRUE(state.softkeys[static_cast<size_t>(id)].label[0] == '\0');
    }
    EXPECT_TRUE(local_body_stays_clear(state));
    EXPECT_TRUE(golden_test::check_golden("local_conditions_no_board",
                                          render_page(state, kSensorTestNowMs).data()));

    // No key on the page opens a graph.
    for (const ButtonId button : {ButtonId::LeftTop, ButtonId::LeftUpper, ButtonId::LeftMiddle,
                                  ButtonId::LeftLower})
    {
        (void)open_local_conditions(make_unread_sensor(EnvironmentSensorHealth::BoardMissing));
        (void)console_controller::handle_button_event({button, ButtonEventType::Pressed});
        EXPECT_TRUE(console_controller::state().active_page == MenuPage::LocalConditions);
    }
}

HOST_TEST(local_conditions_states_render_consistently)
{
    const ConsoleState& waiting =
        open_local_conditions(make_unread_sensor(EnvironmentSensorHealth::BoardDetected));
    EXPECT_TRUE(waiting.softkeys[0].label[0] == '\0');
    EXPECT_TRUE(local_body_stays_clear(waiting));
    EXPECT_TRUE(golden_test::check_golden("local_conditions_waiting",
                                          render_page(waiting, kSensorTestNowMs).data()));

    const ConsoleState& live = open_local_conditions(make_live_sensor(48U));
    EXPECT_TRUE(golden_test::check_golden("local_conditions_live",
                                          render_page(live, kSensorTestNowMs).data()));

    EnvironmentSensorStatus old = make_live_sensor(48U);
    old.bme_last_read_ms = 1000U;
    old.air_quality_last_read_ms = 1000U;
    const ConsoleState& stale = open_local_conditions(old);
    EXPECT_TRUE(golden_test::check_golden("local_conditions_stale",
                                          render_page(stale, kSensorTestNowMs).data()));

    ConsoleState off = open_local_conditions(EnvironmentSensorStatus{});
    EXPECT_EQ(static_cast<int>(local_conditions::board_state(off.environment_sensor_status)),
              static_cast<int>(local_conditions::BoardState::SensorOff));
    EXPECT_TRUE(local_body_stays_clear(off));
}

HOST_TEST(local_condition_keys_and_graph_use_the_same_reading_text)
{
    const EnvironmentSensorStatus sensor = make_live_sensor(48U);
    const ConsoleState& state = open_local_conditions(sensor);
    for (size_t i = 0U; i < kReadingKeyIds.size(); ++i)
    {
        char value[20] = {};
        local_conditions::format_metric(kReadingMetrics[i], sensor, value, sizeof(value));
        const std::string label = state.softkeys[static_cast<size_t>(kReadingKeyIds[i])].label;
        EXPECT_TRUE(label.find(std::string("[") + value + "]") != std::string::npos);
    }
}

HOST_TEST(local_condition_graph_states_render_consistently)
{
    // L1 opens the temperature graph; the graph page keeps only R5 HOME.
    (void)open_local_conditions(make_live_sensor(48U));
    (void)console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Pressed});
    const ConsoleState live = console_controller::state();
    EXPECT_TRUE(live.active_page == MenuPage::LocalConditionGraph);
    EXPECT_TRUE(live.softkeys[static_cast<size_t>(SoftKeyId::Right4)].label[0] == '\0');
    EXPECT_TRUE(local_body_stays_clear(live));
    EXPECT_TRUE(golden_test::check_golden("local_graph_live",
                                          render_page(live, kSensorTestNowMs).data()));

    ConsoleState stale = live;
    stale.environment_sensor_status.bme_last_read_ms = 1000U;
    stale.environment_sensor_status.air_quality_last_read_ms = 1000U;
    EXPECT_TRUE(golden_test::check_golden("local_graph_stale",
                                          render_page(stale, kSensorTestNowMs).data()));

    ConsoleState no_history = live;
    no_history.environment_sensor_status.bme_history_count = 1U;
    EXPECT_TRUE(local_body_stays_clear(no_history));
    EXPECT_TRUE(golden_test::check_golden("local_graph_no_history",
                                          render_page(no_history, kSensorTestNowMs).data()));

    ConsoleState waiting = live;
    waiting.environment_sensor_status = make_unread_sensor(EnvironmentSensorHealth::BoardDetected);
    EXPECT_TRUE(golden_test::check_golden("local_graph_waiting",
                                          render_page(waiting, kSensorTestNowMs).data()));

    ConsoleState no_board = live;
    no_board.environment_sensor_status = make_unread_sensor(EnvironmentSensorHealth::BoardMissing);
    EXPECT_TRUE(local_body_stays_clear(no_board));
    EXPECT_TRUE(golden_test::check_golden("local_graph_no_board",
                                          render_page(no_board, kSensorTestNowMs).data()));
}
