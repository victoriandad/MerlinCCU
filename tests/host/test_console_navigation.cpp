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
