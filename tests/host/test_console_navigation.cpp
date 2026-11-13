// Navigation tests that drive the real console_controller through key presses,
// the same path the panel and the /preview page use. Each test starts from
// console_controller::init(), so the softkey maps under test are the ones the
// firmware builds, not hand-written fixtures.

#include <array>
#include <cstddef>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config_manager.h"
#include "console_controller.h"
#include "console_model.h"
#include "input.h"
#include "test_framework.h"

namespace
{

constexpr std::array<ButtonId, static_cast<size_t>(SoftKeyId::Count)> kSoftkeyButtons = {
    ButtonId::LeftTop,  ButtonId::LeftUpper,  ButtonId::LeftMiddle,  ButtonId::LeftLower,
    ButtonId::LeftBottom, ButtonId::RightTop, ButtonId::RightUpper, ButtonId::RightMiddle,
    ButtonId::RightLower, ButtonId::RightBottom};

/// @brief Order in which Select Brew fills its eight list slots (L1-L4, R1-R4).
constexpr std::array<ButtonId, kPinterBrewListVisibleCount> kListSlotButtons = {
    ButtonId::LeftTop,  ButtonId::LeftUpper,  ButtonId::LeftMiddle,  ButtonId::LeftLower,
    ButtonId::RightTop, ButtonId::RightUpper, ButtonId::RightMiddle, ButtonId::RightLower};

void press(ButtonId id)
{
    (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

MenuPage page()
{
    return console_controller::state().active_page;
}

/// @brief Resets the controller and gives it `share_count` watched shares, so
/// the Shares list has entries and Share detail can be reached.
void start_fresh(uint8_t share_count = 1U)
{
    console_controller::init();
    ShareMarketStatus shares = {};
    shares.configured = true;
    shares.data_valid = true;
    shares.share_count = share_count;
    for (uint8_t i = 0U; i < share_count; ++i)
    {
        std::snprintf(shares.watched_shares[i].display_name.data(),
                      shares.watched_shares[i].display_name.size(), "Share %u",
                      static_cast<unsigned>(i));
        std::snprintf(shares.watched_shares[i].price_text.data(),
                      shares.watched_shares[i].price_text.size(), "100");
    }
    (void)console_controller::set_share_market_status(shares);
}

/// @brief One navigation node: the active page plus the Settings root page
/// index, since Settings 1/2 and 2/2 offer different keys.
using Node = std::pair<int, int>;

Node current_node()
{
    const ConsoleState& state = console_controller::state();
    const int settings_index = state.active_page == MenuPage::Settings
                                   ? static_cast<int>(state.settings_page_index)
                                   : 0;
    return {static_cast<int>(state.active_page), settings_index};
}

void replay(const std::vector<ButtonId>& path)
{
    start_fresh();
    for (const ButtonId id : path)
    {
        press(id);
    }
}

} // namespace

HOST_TEST(select_brew_offers_every_catalogue_entry_exactly_once)
{
    start_fresh();
    press(ButtonId::LeftLower); // Home L4: PINTER
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Pinter));
    press(ButtonId::RightTop); // Pinter R1: START for an idle vessel
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::PinterSelectBrew));

    std::array<int, kPinterBrewCatalogueCount> times_offered = {};
    for (uint8_t list_page = 0U; list_page < kPinterBrewListPageCount; ++list_page)
    {
        EXPECT_EQ(console_controller::state().pinter_catalogue_page_index, list_page);
        for (const SoftKeyAction& key : console_controller::state().softkeys)
        {
            EXPECT_FALSE(key.route == SoftKeyRoute::GoPinter);
        }

        for (const ButtonId slot : kListSlotButtons)
        {
            press(slot);
            if (page() != MenuPage::PinterStartTiming)
            {
                continue; // Past the end of the catalogue on the last page.
            }

            const uint8_t brew_index = console_controller::state().pinter_pending_brew_index;
            EXPECT_TRUE(brew_index < kPinterBrewCatalogueCount);
            if (brew_index < kPinterBrewCatalogueCount)
            {
                ++times_offered[brew_index];
            }
            press(ButtonId::BackStep);
            EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::PinterSelectBrew));
        }
        press(ButtonId::CursorRight);
    }

    for (size_t i = 0U; i < times_offered.size(); ++i)
    {
        EXPECT_EQ(times_offered[i], 1);
    }
}

HOST_TEST(shares_list_fills_left_then_right_and_each_key_opens_its_own_share)
{
    const std::array<ButtonId, kMaxWatchedShares> expected_slots = {
        ButtonId::LeftTop,   ButtonId::LeftUpper, ButtonId::LeftMiddle,
        ButtonId::LeftLower, ButtonId::RightTop,  ButtonId::RightUpper};

    for (size_t i = 0U; i < expected_slots.size(); ++i)
    {
        start_fresh(static_cast<uint8_t>(kMaxWatchedShares));
        press(ButtonId::LeftUpper); // Home L2: SHARES
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Shares));
        press(expected_slots[i]);
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::ShareDetail));
        EXPECT_EQ(console_controller::state().selected_share_index, i);
    }

    start_fresh(static_cast<uint8_t>(kMaxWatchedShares));
    press(ButtonId::LeftUpper);
    const SoftKeyMap& keys = console_controller::state().softkeys;
    EXPECT_TRUE(keys[static_cast<size_t>(SoftKeyId::Left5)].label[0] == '\0');
    for (const SoftKeyId unused : {SoftKeyId::Right3, SoftKeyId::Right4})
    {
        EXPECT_TRUE(keys[static_cast<size_t>(unused)].label[0] == '\0');
    }
    EXPECT_EQ(static_cast<int>(keys[static_cast<size_t>(SoftKeyId::Right5)].route),
              static_cast<int>(SoftKeyRoute::GoHome));

    press(ButtonId::CursorLeft);
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Shares));
    press(ButtonId::CursorRight);
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Shares));
}

HOST_TEST(time_zone_page_is_a_fixed_option_list_with_the_current_zone_selected)
{
    // Every zone, chosen from the panel: Home R1 SETTINGS, cursor right to
    // Settings 2/2, L3 TIME ZONE, page with the cursor keys, press the slot.
    for (uint8_t target = 0U; target < kTimeZoneSelectionCount; ++target)
    {
        start_fresh();
        press(ButtonId::RightTop);
        press(ButtonId::CursorRight);
        press(ButtonId::LeftMiddle);
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::TimeZoneSettings));
        for (int i = 0; i < kTimeZonePageCount; ++i)
        {
            press(ButtonId::CursorLeft);
        }

        const uint8_t target_page = static_cast<uint8_t>(target / kTimeZoneOptionsPerPage);
        for (uint8_t i = 0U; i < target_page; ++i)
        {
            press(ButtonId::CursorRight);
        }
        EXPECT_EQ(console_controller::state().time_zone_page_index, target_page);

        std::array<const char*, static_cast<size_t>(SoftKeyId::Count)> labels_before = {};
        for (size_t k = 0U; k < labels_before.size(); ++k)
        {
            labels_before[k] = console_controller::state().softkeys[k].label;
        }

        press(kListSlotButtons[target % kTimeZoneOptionsPerPage]);
        EXPECT_EQ(static_cast<int>(config_manager::settings().time_zone), static_cast<int>(target));

        int selected = 0;
        for (size_t k = 0U; k < labels_before.size(); ++k)
        {
            const SoftKeyAction& key = console_controller::state().softkeys[k];
            // Labels do not move when the selection changes (NAV-11).
            EXPECT_TRUE(std::string(key.label) == std::string(labels_before[k]));
            selected += key.inverted ? 1 : 0;
        }
        EXPECT_EQ(selected, 1);
        EXPECT_TRUE(console_controller::state()
                        .softkeys[static_cast<size_t>(SoftKeyId::Right5)]
                        .route == SoftKeyRoute::GoHome);
    }

    // Re-entering opens on the page that holds the current zone.
    press(ButtonId::BackStep);
    press(ButtonId::LeftMiddle);
    EXPECT_EQ(console_controller::state().time_zone_page_index,
              static_cast<uint8_t>((kTimeZoneSelectionCount - 1U) / kTimeZoneOptionsPerPage));
}

HOST_TEST(keypad_debug_and_greyscale_return_to_status)
{
    const std::array<ButtonId, 2> status_keys = {ButtonId::RightLower,   // R4: KEYPAD
                                                 ButtonId::RightMiddle}; // R3: GREYSCALE
    const std::array<MenuPage, 2> destinations = {MenuPage::KeypadDebug, MenuPage::GreyscaleTest};

    for (size_t i = 0U; i < status_keys.size(); ++i)
    {
        start_fresh();
        press(ButtonId::LeftTop); // Home L1: STATUS
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Status));
        press(status_keys[i]);
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(destinations[i]));
        press(ButtonId::BackStep);
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Status));
    }
}

/// @details Explores every page reachable from Home by softkey and cursor
/// presses. For each page it then checks that BACK STEP lands on a page that
/// has a key leading to it (NAV-1), and that every page is reachable at all.
HOST_TEST(every_page_is_reachable_and_back_step_returns_to_a_page_that_leads_there)
{
    std::map<Node, std::vector<ButtonId>> path_to;
    std::map<int, std::set<int>> predecessors;
    std::vector<Node> queue;

    start_fresh();
    path_to[current_node()] = {};
    queue.push_back(current_node());

    std::vector<ButtonId> candidates(kSoftkeyButtons.begin(), kSoftkeyButtons.end());
    candidates.push_back(ButtonId::CursorLeft);
    candidates.push_back(ButtonId::CursorRight);

    for (size_t next = 0U; next < queue.size(); ++next)
    {
        const Node from = queue[next];
        for (const ButtonId candidate : candidates)
        {
            replay(path_to[from]);
            press(candidate);
            const Node to = current_node();
            if (to.first != from.first)
            {
                predecessors[to.first].insert(from.first);
            }
            if (path_to.count(to) == 0U)
            {
                std::vector<ButtonId> path = path_to[from];
                path.push_back(candidate);
                path_to[to] = path;
                queue.push_back(to);
            }
        }
    }

    // Pages that need data or a hard key to reach, and so are not expected
    // from softkey and cursor presses alone on a fresh controller.
    const std::set<int> not_softkey_reachable = {
        static_cast<int>(MenuPage::CalendarDetail), // Needs calendar events.
        static_cast<int>(MenuPage::AlertList),      // Opened by the ALERT hard key.
        static_cast<int>(MenuPage::AlertDetail),    // Needs an active alert.
        static_cast<int>(MenuPage::LocalConditionGraph), // Needs sensor readings.
    };

    std::set<int> reached;
    for (const auto& entry : path_to)
    {
        reached.insert(entry.first.first);
    }

    for (int value = 0; value <= static_cast<int>(MenuPage::AirTraffic); ++value)
    {
        if (not_softkey_reachable.count(value) != 0U)
        {
            continue;
        }
        if (reached.count(value) == 0U)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       "MenuPage " + std::to_string(value) +
                                           " is not reachable from Home");
        }
    }

    for (const auto& entry : path_to)
    {
        const int target = entry.first.first;
        if (target == static_cast<int>(MenuPage::Home))
        {
            continue;
        }

        replay(entry.second);
        press(ButtonId::BackStep);
        const int parent = static_cast<int>(page());
        if (predecessors[target].count(parent) == 0U)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       "BACK STEP from MenuPage " + std::to_string(target) +
                                           " goes to MenuPage " + std::to_string(parent) +
                                           ", which has no key leading there");
        }
    }
}

namespace
{

/// @brief Opens the Pinter page with vessel 1 brewing and vessels 2-4 idle,
/// and clears any save left pending by earlier tests.
void open_pinter_with_one_brewing_vessel()
{
    start_fresh();
    std::array<PinterStatus, kPinterCount> vessels = {};
    for (size_t i = 0U; i < vessels.size(); ++i)
    {
        std::snprintf(vessels[i].label.data(), vessels[i].label.size(), "Pinter %u",
                      static_cast<unsigned>(i + 1U));
    }
    vessels[0].state = PinterState::Brewing;
    vessels[0].brew_start_day = 19998U;
    vessels[0].planned_brewing_days = 7U;
    console_controller::apply_persisted_pinters(vessels);
    TimeStatus time = {};
    time.synced = true;
    time.local_epoch_day = 20000U;
    (void)console_controller::set_time_status(time);
    (void)console_controller::flush_pending_pinter_save();
    press(ButtonId::LeftLower); // Home L4: PINTER
}

PinterState vessel_one_state()
{
    return console_controller::state().pinters[0].state;
}

const SoftKeyAction& key(SoftKeyId id)
{
    return console_controller::state().softkeys[static_cast<size_t>(id)];
}

} // namespace

HOST_TEST(pinter_reset_needs_confirmation_and_cancel_or_back_step_keep_the_vessel)
{
    open_pinter_with_one_brewing_vessel();
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Pinter));

    press(ButtonId::RightMiddle); // R3: RESET
    EXPECT_TRUE(console_controller::state().pinter_reset_confirming);
    EXPECT_TRUE(vessel_one_state() == PinterState::Brewing);
    EXPECT_FALSE(console_controller::flush_pending_pinter_save());

    // Only CANCEL (L5) and RESET (R5) are labelled in the confirm state.
    for (size_t k = 0U; k < static_cast<size_t>(SoftKeyId::Count); ++k)
    {
        const bool row5 = k == static_cast<size_t>(SoftKeyId::Left5) ||
                          k == static_cast<size_t>(SoftKeyId::Right5);
        EXPECT_EQ(console_controller::state().softkeys[k].label[0] != '\0', row5);
    }
    EXPECT_TRUE(key(SoftKeyId::Left5).route == SoftKeyRoute::CancelPinterReset);
    EXPECT_TRUE(key(SoftKeyId::Right5).route == SoftKeyRoute::ConfirmPinterReset);

    press(ButtonId::LeftBottom); // L5: CANCEL
    EXPECT_FALSE(console_controller::state().pinter_reset_confirming);
    EXPECT_TRUE(vessel_one_state() == PinterState::Brewing);

    press(ButtonId::RightMiddle);
    press(ButtonId::BackStep);
    EXPECT_FALSE(console_controller::state().pinter_reset_confirming);
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Pinter));
    EXPECT_TRUE(vessel_one_state() == PinterState::Brewing);
    EXPECT_FALSE(console_controller::flush_pending_pinter_save());

    press(ButtonId::RightMiddle);
    press(ButtonId::RightBottom); // R5: RESET (confirm)
    EXPECT_FALSE(console_controller::state().pinter_reset_confirming);
    EXPECT_TRUE(vessel_one_state() == PinterState::Idle);
    EXPECT_TRUE(console_controller::flush_pending_pinter_save());
}

HOST_TEST(pinter_page_leaves_keys_blank_when_they_cannot_act)
{
    open_pinter_with_one_brewing_vessel();
    press(ButtonId::LeftUpper); // L2: an idle vessel
    for (const SoftKeyAction& action : console_controller::state().softkeys)
    {
        if (!action.enabled)
        {
            EXPECT_TRUE(action.label[0] == '\0' || std::string(action.label).find('[') !=
                                                       std::string::npos);
        }
    }
    EXPECT_TRUE(key(SoftKeyId::Right3).label[0] == '\0'); // RESET
    EXPECT_TRUE(key(SoftKeyId::Right2).label[0] == '\0'); // -1 DAY
    EXPECT_TRUE(key(SoftKeyId::Right4).label[0] == '\0'); // +1 DAY
}

namespace
{

/// @brief From the Pinter page with vessel 1 brewing, selects idle vessel 2
/// and opens Brew Timing for the first catalogue entry.
void open_brew_timing_for_vessel_two()
{
    open_pinter_with_one_brewing_vessel();
    press(ButtonId::LeftUpper); // L2: vessel 2 (idle)
    press(ButtonId::RightTop);  // R1: START
    press(ButtonId::LeftTop);   // L1: first brew in the catalogue
}

} // namespace

HOST_TEST(brew_timing_plans_a_cold_crash_and_starts_from_r5)
{
    open_brew_timing_for_vessel_two();
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::PinterStartTiming));

    // Row 5 is the decision: CANCEL declines, START confirms, nothing else does.
    EXPECT_TRUE(key(SoftKeyId::Left5).route == SoftKeyRoute::GoPinterSelectBrew);
    EXPECT_TRUE(key(SoftKeyId::Right5).route == SoftKeyRoute::ConfirmPinterStart);
    int confirm_keys = 0;
    for (const SoftKeyAction& action : console_controller::state().softkeys)
    {
        confirm_keys += action.route == SoftKeyRoute::ConfirmPinterStart ? 1 : 0;
        // Every labelled key acts (SK-5).
        EXPECT_TRUE(action.label[0] == '\0' || action.enabled);
    }
    EXPECT_EQ(confirm_keys, 1);

    // Cold crash starts at 0 days, so CRASH - is blank until it is raised.
    EXPECT_TRUE(key(SoftKeyId::Left2).label[0] == '\0');
    press(ButtonId::RightUpper); // R2: CRASH +
    press(ButtonId::RightUpper);
    EXPECT_EQ(console_controller::state().pinter_pending_cold_crash_days, 2U);
    EXPECT_TRUE(key(SoftKeyId::Left2).route == SoftKeyRoute::DecreasePinterColdCrashDays);

    press(ButtonId::RightBottom); // R5: START
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::Pinter));
    const PinterStatus& vessel = console_controller::state().pinters[1];
    EXPECT_TRUE(vessel.state == PinterState::Brewing);
    EXPECT_EQ(vessel.planned_cold_crash_days, 2U);
    EXPECT_TRUE(std::string(key(SoftKeyId::Right1).label) == "COLD\nCRASH");
}

HOST_TEST(brew_timing_cancel_returns_to_select_brew_without_starting)
{
    open_brew_timing_for_vessel_two();
    press(ButtonId::LeftBottom); // L5: CANCEL
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::PinterSelectBrew));
    EXPECT_TRUE(console_controller::state().pinters[1].state == PinterState::Idle);

    // The cold crash limit blanks CRASH + once reached.
    press(ButtonId::LeftTop);
    for (int i = 0; i < 5; ++i)
    {
        press(ButtonId::RightUpper);
    }
    EXPECT_EQ(console_controller::state().pinter_pending_cold_crash_days, 3U);
    EXPECT_TRUE(key(SoftKeyId::Right2).label[0] == '\0');
}

namespace
{

/// @brief Home R1 SETTINGS, cursor right to Settings 2/2, L4 SCREEN SAVER.
void open_screen_saver_settings()
{
    start_fresh();
    press(ButtonId::RightTop);
    press(ButtonId::CursorRight);
    press(ButtonId::LeftLower);
}

} // namespace

HOST_TEST(screen_saver_timeout_entry_offers_only_cancel_and_enter)
{
    open_screen_saver_settings();
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::ScreenSaverSettings));
    EXPECT_TRUE(key(SoftKeyId::Right5).route == SoftKeyRoute::GoHome);
    EXPECT_TRUE(key(SoftKeyId::Left2).route == SoftKeyRoute::GoScreenSaverOptions);
    const uint16_t saved = config_manager::settings().screen_saver_timeout_minutes;

    press(ButtonId::LeftTop); // L1: TIMEOUT
    EXPECT_TRUE(console_controller::state().screen_saver_timeout_editing);
    for (size_t k = 0U; k < static_cast<size_t>(SoftKeyId::Count); ++k)
    {
        const bool labelled = k == static_cast<size_t>(SoftKeyId::Left1) ||
                              k == static_cast<size_t>(SoftKeyId::Left5) ||
                              k == static_cast<size_t>(SoftKeyId::Right5);
        EXPECT_EQ(console_controller::state().softkeys[k].label[0] != '\0', labelled);
    }
    EXPECT_TRUE(key(SoftKeyId::Left1).inverted);
    EXPECT_TRUE(std::string(key(SoftKeyId::Left5).label) == "CANCEL");
    EXPECT_TRUE(std::string(key(SoftKeyId::Right5).label) == "ENTER");

    // Type a different value, then decline it.
    for (int i = 0; i < 4 && console_controller::state().letter_mode != LetterMode::Numbers; ++i)
    {
        press(ButtonId::Ltrs);
    }
    press(ButtonId::AlphaJ); // 1
    press(ButtonId::AlphaJ); // 1
    EXPECT_EQ(console_controller::state().screen_saver_timeout_edit_minutes, 11U);
    press(ButtonId::LeftBottom); // L5: CANCEL
    EXPECT_FALSE(console_controller::state().screen_saver_timeout_editing);
    EXPECT_EQ(config_manager::settings().screen_saver_timeout_minutes, saved);
    EXPECT_EQ(console_controller::state().screen_saver_timeout_minutes, saved);

    // ENTER keeps the typed value.
    press(ButtonId::LeftTop);
    press(ButtonId::AlphaJ);
    press(ButtonId::AlphaJ);
    press(ButtonId::RightBottom); // R5: ENTER
    EXPECT_FALSE(console_controller::state().screen_saver_timeout_editing);
    EXPECT_EQ(config_manager::settings().screen_saver_timeout_minutes, 11U);
}

HOST_TEST(screen_saver_options_page_selects_exactly_one_saver_in_place)
{
    open_screen_saver_settings();
    press(ButtonId::LeftUpper); // L2: SAVER
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::ScreenSaverOptions));
    EXPECT_TRUE(key(SoftKeyId::Left5).label[0] == '\0');
    EXPECT_TRUE(key(SoftKeyId::Right5).route == SoftKeyRoute::GoHome);

    for (const ButtonId slot : kListSlotButtons)
    {
        press(slot);
        EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::ScreenSaverOptions));
        int selected = 0;
        for (const SoftKeyAction& action : console_controller::state().softkeys)
        {
            selected += action.inverted ? 1 : 0;
        }
        EXPECT_EQ(selected, 1);
        EXPECT_TRUE(console_controller::state()
                        .softkeys[static_cast<size_t>(slot)]
                        .inverted);
    }

    press(ButtonId::BackStep);
    EXPECT_EQ(static_cast<int>(page()), static_cast<int>(MenuPage::ScreenSaverSettings));
}
