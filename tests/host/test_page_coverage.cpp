// Whole-UI coverage: every menu page rendered with the softkey map the
// firmware really builds, checked against a golden image, for labels that
// lose text, and for body content inside a labelled softkey cell (#112).
//
// Pages are found by exploring from Home with the same key presses an
// operator would use, so a new page is covered as soon as a key leads to it,
// and a page no key reaches fails the completeness check.

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "config_manager.h"
#include "console_controller.h"
#include "console_model.h"
#include "environment_sensor_manager.h"
#include "framebuffer.h"
#include "golden_test_support.h"
#include "input.h"
#include "panel_config.h"
#include "screens.h"
#include "screens_shared.h"
#include "softkey_builders.h"
#include "test_framework.h"
#include "ui_layout.h"

namespace
{

using text_styles::TextStyle;

/// @brief Fixed boot-uptime for every render, so freshness text is stable.
constexpr uint32_t kCoverageNowMs = 900000U;

constexpr int kMenuPageCount = static_cast<int>(MenuPage::AirTraffic) + 1;

constexpr std::array<ButtonId, static_cast<size_t>(SoftKeyId::Count)> kSoftkeyButtons = {
    ButtonId::LeftTop,    ButtonId::LeftUpper,  ButtonId::LeftMiddle, ButtonId::LeftLower,
    ButtonId::LeftBottom, ButtonId::RightTop,   ButtonId::RightUpper, ButtonId::RightMiddle,
    ButtonId::RightLower, ButtonId::RightBottom};

/// @brief Golden-image name for each page, in MenuPage order.
constexpr std::array<const char*, kMenuPageCount> kPageNames = {
    "home",
    "calendar",
    "calendar_detail",
    "weather",
    "status",
    "status_overview",
    "status_connectivity",
    "status_resources",
    "status_sensors",
    "status_integrations",
    "local_conditions",
    "local_condition_graph",
    "settings",
    "device_settings",
    "wifi_settings",
    "home_assistant_settings",
    "mqtt_settings",
    "air_traffic_settings",
    "screen_saver_settings",
    "screen_saver_options",
    "weather_sources",
    "time_zone_settings",
    "keypad_debug",
    "greyscale_test",
    "alert_list",
    "alert_detail",
    "pinter",
    "pinter_select_brew",
    "pinter_start_timing",
    "shares",
    "share_detail",
    "air_traffic",
};

void press(ButtonId id)
{
    (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

/// @brief Resets the controller and feeds it the data a working CCU has:
/// Wi-Fi, two watched shares, a live sensor board, nearby aircraft, and one
/// active alert (a keypad line fault), so every page has something to
/// show and every data-gated key exists.
void start_with_data()
{
    // Exploring presses keys that change settings; every replay starts from
    // the configuration the first one saw, so each path leads to one state.
    static const RuntimeConfig kStartConfig = config_manager::settings();
    (void)config_manager::save(kStartConfig);
    console_controller::init();

    WifiStatus wifi = {};
    wifi.state = WifiConnectionState::Connected;
    std::snprintf(wifi.ip_address.data(), wifi.ip_address.size(), "192.168.1.40");
    (void)console_controller::set_wifi_status(wifi);

    ShareMarketStatus shares = {};
    shares.configured = true;
    shares.data_valid = true;
    shares.share_count = 2U;
    for (uint8_t i = 0U; i < shares.share_count; ++i)
    {
        ShareWatchEntry& share = shares.watched_shares[i];
        std::snprintf(share.display_name.data(), share.display_name.size(), "Share %u",
                      static_cast<unsigned>(i + 1U));
        std::snprintf(share.symbol.data(), share.symbol.size(), "SH%u",
                      static_cast<unsigned>(i + 1U));
        std::snprintf(share.price_text.data(), share.price_text.size(), "1,2%u0.0",
                      static_cast<unsigned>(i));
        std::snprintf(share.change_text.data(), share.change_text.size(), "+0.4%%");
        for (size_t p = 0U; p < share.history_points.size(); ++p)
        {
            share.history_points[p] = static_cast<uint16_t>(1200U + (p * 5U));
        }
    }
    (void)console_controller::set_share_market_status(shares);

    environment_sensor_manager::EnvironmentSensorStatus sensor = {};
    sensor.enabled = true;
    sensor.health = environment_sensor_manager::EnvironmentSensorHealth::BoardDetected;
    sensor.bme_reading_valid = true;
    sensor.bme_temperature_centi_celsius = 2150;
    sensor.bme_humidity_milli_percent = 45200U;
    sensor.bme_pressure_pa = 101320U;
    sensor.bme_last_read_ms = kCoverageNowMs - 5000U;
    sensor.air_quality_score_valid = true;
    sensor.air_quality_score = 97U;
    sensor.air_quality_last_read_ms = kCoverageNowMs - 5000U;
    sensor.bme_history_count = 48U;
    sensor.air_quality_history_count = 48U;
    for (uint16_t i = 0U; i < sensor.bme_history_count; ++i)
    {
        const int wave = static_cast<int>(i % 24U) - 12;
        sensor.bme_temperature_history_centi_celsius[i] = static_cast<int16_t>(2000 + (wave * 15));
        sensor.bme_humidity_history_centi_percent[i] = static_cast<uint16_t>(4500 + (wave * 40));
        sensor.bme_pressure_history_deci_hpa[i] = static_cast<uint16_t>(10130 + wave);
        sensor.air_quality_history_score[i] = static_cast<uint8_t>(90 + (wave / 3));
    }
    (void)console_controller::set_environment_sensor_status(sensor);

    AirTrafficStatus traffic = {};
    traffic.enabled = true;
    traffic.configured = true;
    traffic.data_valid = true;
    traffic.configured_radius_nm = 30U;
    traffic.aircraft_count = 3U;
    traffic.last_success_ms = kCoverageNowMs - 5000U;
    const std::array<const char*, 3> callsigns = {"BAW123", "EZY45", "RYR8KJ"};
    for (uint8_t i = 0U; i < traffic.aircraft_count; ++i)
    {
        AirTrafficEntry& entry = traffic.aircraft[i];
        std::snprintf(entry.callsign.data(), entry.callsign.size(), "%s", callsigns[i]);
        std::snprintf(entry.distance_text.data(), entry.distance_text.size(), "%u.0nm",
                      static_cast<unsigned>(4U + (i * 7U)));
        std::snprintf(entry.altitude_text.data(), entry.altitude_text.size(), "%u00ft",
                      static_cast<unsigned>(80U + (i * 90U)));
        std::snprintf(entry.bearing_text.data(), entry.bearing_text.size(), "%u",
                      static_cast<unsigned>(40U + (i * 110U)));
        entry.distance_deci_nm = static_cast<int16_t>(40 + (i * 70));
        entry.bearing_deg = static_cast<int16_t>(40 + (i * 110));
    }
    (void)console_controller::set_air_traffic_status(traffic);

    TimeStatus time = {};
    time.synced = true;
    std::snprintf(time.time_text.data(), time.time_text.size(), "12:34");
    std::snprintf(time.date_text.data(), time.date_text.size(), "2026-11-13");
    time.weekday_index = 5U;
    (void)console_controller::set_time_status(time);

    // Four keys held at once is a keypad line fault: after its debounce the
    // KEYPAD alert is active, so the Alert list and Alert detail have an
    // entry. Each report differs so each one is taken and debounced.
    for (uint32_t i = 0U; i < 6U; ++i)
    {
        KeypadMonitorStatus keypad = {};
        keypad.active_count = 4U;
        keypad.active_mask = 0x0FU << i;
        (void)console_controller::set_keypad_monitor_status(keypad);
    }
}

/// @brief A page plus the Settings root page index, since Settings 1/2 and
/// 2/2 offer different keys.
using Node = std::pair<int, int>;

Node current_node()
{
    const ConsoleState& state = console_controller::state();
    return {static_cast<int>(state.active_page), state.active_page == MenuPage::Settings
                                                     ? static_cast<int>(state.settings_page_index)
                                                     : 0};
}

/// @brief Every page state reachable from Home, with the presses that reach it.
std::map<Node, std::vector<ButtonId>> explore()
{
    std::map<Node, std::vector<ButtonId>> path_to;
    std::vector<Node> queue;
    start_with_data();
    path_to[current_node()] = {};
    queue.push_back(current_node());

    std::vector<ButtonId> candidates(kSoftkeyButtons.begin(), kSoftkeyButtons.end());
    candidates.push_back(ButtonId::CursorLeft);
    candidates.push_back(ButtonId::CursorRight);
    candidates.push_back(ButtonId::Alert);

    for (size_t next = 0U; next < queue.size(); ++next)
    {
        const Node from = queue[next];
        for (const ButtonId candidate : candidates)
        {
            start_with_data();
            for (const ButtonId id : path_to[from])
            {
                press(id);
            }
            press(candidate);
            const Node to = current_node();
            if (path_to.count(to) == 0U)
            {
                std::vector<ButtonId> path = path_to[from];
                path.push_back(candidate);
                path_to[to] = path;
                queue.push_back(to);
            }
        }
    }
    return path_to;
}

std::string node_name(const Node& node)
{
    const char* page = kPageNames[static_cast<size_t>(node.first)];
    std::string name = "page_" + (page != nullptr ? std::string(page) : std::to_string(node.first));
    if (node.second > 0)
    {
        name += "_" + std::to_string(node.second + 1);
    }
    return name;
}

using Framebuffer = std::array<uint8_t, static_cast<size_t>(kUiFbSize)>;

Framebuffer render(const ConsoleState& state)
{
    Framebuffer fb = {};
    screens::draw_menu_screen(fb.data(), state, kCoverageNowMs);
    return fb;
}

/// @brief Removes the characters a label's layout does not draw as text:
/// line breaks, value brackets (drawn as graphics) and spaces (where lines
/// wrap).
std::string drawn_characters(const std::string& text)
{
    std::string out;
    for (const char c : text)
    {
        if (c != '\n' && c != '[' && c != ']' && c != ' ')
        {
            out.push_back(c);
        }
    }
    return out;
}

/// @brief True when `label` lays out in `style` within `max_lines` lines
/// without dropping any of its text (LBL-1, LBL-6).
bool softkey_label_fits(const char* label, TextStyle style, int max_lines)
{
    const screens::SoftkeyLabelLayout layout =
        screens::layout_softkey_label(label, style, max_lines);
    std::string drawn;
    for (int i = 0; i < layout.count; ++i)
    {
        drawn += layout.lines[static_cast<size_t>(i)].text;
    }
    return drawn_characters(drawn) == drawn_characters(label);
}

/// @brief Names the labelled softkeys whose cell contains body pixels (SCR-5).
/// @details The cell of a labelled key is its half of the screen, from the
/// label block's top to its bottom, padded by the bracket overhang.
std::vector<std::string> body_overlaps(const ConsoleState& state)
{
    ConsoleState body_only = state;
    for (SoftKeyAction& key : body_only.softkeys)
    {
        key.label = "";
    }
    const Framebuffer body = render(body_only);

    constexpr int kCellPadY = 3;
    const TextStyle style = screens::softkey_label_style(state.active_page);
    const int line_height = text_styles::line_height(style);
    std::vector<std::string> overlaps;
    for (size_t key = 0U; key < state.softkeys.size(); ++key)
    {
        const char* label = state.softkeys[key].label;
        if (label == nullptr || label[0] == '\0')
        {
            continue;
        }
        const int row = static_cast<int>(key % static_cast<size_t>(ui_layout::kSoftkeyRowCount));
        const bool left = key < static_cast<size_t>(ui_layout::kSoftkeyRowCount);
        const int lines = screens::layout_softkey_label(
                              label, style, screens::softkey_label_max_lines(state.active_page))
                              .count;
        const int top = ui_layout::softkey_label_top_y(row, lines, line_height) - kCellPadY;
        const int bottom = top + (lines * line_height) +
                           ((lines - 1) * ui_layout::kSoftkeyLineGap) + (kCellPadY * 2);
        const int x0 = left ? 0 : ui_layout::kCentreX;
        const int x1 = left ? ui_layout::kCentreX - 1 : kUiWidth - 1;
        bool hit = false;
        for (int y = std::max(top, ui_layout::kBodyTopY);
             y <= std::min(bottom, ui_layout::kFooterTopY - 1) && !hit; ++y)
        {
            for (int x = x0; x <= x1 && !hit; ++x)
            {
                hit = framebuffer::get_pixel(body.data(), x, y);
            }
        }
        if (hit)
        {
            overlaps.push_back((left ? "L" : "R") + std::to_string(row + 1));
        }
    }
    return overlaps;
}

/// @brief Names the softkeys whose role disagrees with their label, route,
/// inverse state or position (SK-1, SK-5, SK-9, LBL-5, NAV-4).
std::vector<std::string> role_problems(const SoftKeyMap& softkeys)
{
    std::vector<std::string> out;
    for (size_t key = 0U; key < softkeys.size(); ++key)
    {
        const SoftKeyAction& action = softkeys[key];
        const bool left = key < static_cast<size_t>(ui_layout::kSoftkeyRowCount);
        const size_t row = key % static_cast<size_t>(ui_layout::kSoftkeyRowCount);
        const std::string where = (left ? "L" : "R") + std::to_string(row + 1U);
        const bool labelled = action.label != nullptr && action.label[0] != '\0';
        const bool inert = action.role == SoftKeyRole::Blank || action.role == SoftKeyRole::Status;
        const bool editing = action.role == SoftKeyRole::Select && action.inverted;

        if (labelled == (action.role == SoftKeyRole::Blank))
        {
            out.push_back(where + " label does not match a blank role");
        }
        if (inert && (action.enabled || action.route != SoftKeyRoute::None))
        {
            out.push_back(where + " status or blank key would act");
        }
        if (!inert && !editing && (!action.enabled || action.route == SoftKeyRoute::None))
        {
            out.push_back(where + " labelled key does not act");
        }
        if (action.inverted && action.role != SoftKeyRole::Option &&
            action.role != SoftKeyRole::Item && action.role != SoftKeyRole::Select)
        {
            out.push_back(where + " inverse on a key that is not a selection");
        }
        if (action.role == SoftKeyRole::View && key != static_cast<size_t>(SoftKeyId::Left5))
        {
            out.push_back(where + " view key away from L5");
        }
    }
    return out;
}

} // namespace

HOST_TEST(the_role_check_catches_an_inverse_toggle_and_an_inert_labelled_key)
{
    SoftKeyMap softkeys = {};
    softkeys.fill(softkey::blank());
    EXPECT_TRUE(role_problems(softkeys).empty());

    SoftKeyAction on =
        softkey::toggle(SoftKeyId::Left1, "MQTT", true, SoftKeyRoute::ToggleMqttEnabled);
    EXPECT_FALSE(on.inverted);
    on.inverted = true;
    softkeys[static_cast<size_t>(SoftKeyId::Left1)] = on;
    EXPECT_EQ(role_problems(softkeys).size(), 1U);

    softkeys.fill(softkey::blank());
    softkeys[static_cast<size_t>(SoftKeyId::Left2)] = {"HOST\n[x]", SoftKeyRoute::None, true, false,
                                                       SoftKeyRole::Navigate};
    EXPECT_EQ(role_problems(softkeys).size(), 1U);

    softkeys.fill(softkey::blank());
    softkeys[static_cast<size_t>(SoftKeyId::Right5)] =
        softkey::view(SoftKeyId::Right5, "VIEW", "Map", SoftKeyRoute::ToggleAirTrafficViewMode);
    EXPECT_EQ(role_problems(softkeys).size(), 1U);
}

HOST_TEST(the_fit_and_overlap_checks_catch_a_long_label_and_an_overlapping_body)
{
    EXPECT_TRUE(softkey_label_fits("HOME\n[Ready]", TextStyle::Softkey, 2));
    // The longest Home PINTER summary, with every vessel conditioning.
    EXPECT_TRUE(softkey_label_fits("PINTER\n[4 conditioning]",
                                   screens::softkey_label_style(MenuPage::Home),
                                   screens::softkey_label_max_lines(MenuPage::Home)));
    EXPECT_FALSE(softkey_label_fits("A VERY LONG CAPTION THAT WRAPS\n[and a long value too]",
                                    TextStyle::Softkey, 2));

    // Local Traffic without coordinates draws Info rows from x 50, y 44: a
    // label on L1 would sit on top of them.
    ConsoleState state = {};
    state.active_page = MenuPage::AirTraffic;
    EXPECT_TRUE(body_overlaps(state).empty());
    state.softkeys[static_cast<size_t>(SoftKeyId::Left1)] = {"TEST", SoftKeyRoute::None, true};
    EXPECT_FALSE(body_overlaps(state).empty());
}

/// @details Fit and overlap problems that already exist are listed here with
/// the issue that fixes them, rather than hidden by loosening the checks. The
/// test fails on any problem not listed, and on a listed one that has gone,
/// so the list stays accurate.
HOST_TEST(
    every_page_renders_with_its_real_softkeys_and_no_label_loses_text_meets_the_body_or_breaks_its_role)
{
    const std::set<std::string> kKnownProblems = {
        // Recipe names too long for two 5x7 lines lose their last word, such
        // as "Adnams Ghost Ship Remixed". Label audit: #115.
        "page_pinter_select_brew fit L1",
        "page_pinter_select_brew fit L4",
        "page_pinter_select_brew fit R2",
        "page_pinter_select_brew fit R3",
        "page_pinter_select_brew fit R4",
        // "Atlantic Standard Time" loses "Time" on the TIME ZONE key. #115.
        "page_settings_2 fit L3",
    };

    // Pages no key reaches on the host yet, with the reason. Each still has
    // a golden image of its own elsewhere.
    const std::set<int> kUnreachable = {
        // Needs calendar events, and nothing feeds them to the controller
        // until calendar ingestion lands (#8); see calendar_detail_page.
        static_cast<int>(MenuPage::CalendarDetail),
    };

    // Exploring presses every key, including ones that change settings, so
    // keep the configuration to restore afterwards.
    const RuntimeConfig saved_config = config_manager::settings();

    const std::map<Node, std::vector<ButtonId>> nodes = explore();
    std::set<std::string> problems;
    std::set<int> pages;
    for (const auto& [node, path] : nodes)
    {
        pages.insert(node.first);
        start_with_data();
        for (const ButtonId id : path)
        {
            press(id);
        }
        const ConsoleState& state = console_controller::state();
        const std::string name = node_name(node);
        EXPECT_TRUE(golden_test::check_golden(name.c_str(), render(state).data()));

        const TextStyle style = screens::softkey_label_style(state.active_page);
        const int max_lines = screens::softkey_label_max_lines(state.active_page);
        for (size_t key = 0U; key < state.softkeys.size(); ++key)
        {
            const char* label = state.softkeys[key].label;
            if (label != nullptr && label[0] != '\0' &&
                !softkey_label_fits(label, style, max_lines))
            {
                const bool left = key < static_cast<size_t>(ui_layout::kSoftkeyRowCount);
                const size_t row = key % static_cast<size_t>(ui_layout::kSoftkeyRowCount);
                problems.insert(name + " fit " + (left ? "L" : "R") + std::to_string(row + 1));
            }
        }
        for (const std::string& key : body_overlaps(state))
        {
            problems.insert(name + " overlap " + key);
        }
        for (const std::string& role : role_problems(state.softkeys))
        {
            problems.insert(name + " role " + role);
        }
    }

    for (const std::string& problem : problems)
    {
        if (kKnownProblems.count(problem) == 0U)
        {
            host_tests::record_failure(__FILE__, __LINE__, "new UI problem: " + problem);
        }
    }
    for (const std::string& known : kKnownProblems)
    {
        if (problems.count(known) == 0U)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       "fixed, remove from the known list: " + known);
        }
    }

    // Completeness: every page must be reachable, and so covered.
    for (int value = 0; value < kMenuPageCount; ++value)
    {
        if (kPageNames[static_cast<size_t>(value)] == nullptr)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       "add a golden name for MenuPage " + std::to_string(value));
        }
        if (pages.count(value) == 0U && kUnreachable.count(value) == 0U)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       "no fixture reaches MenuPage " + std::to_string(value));
        }
    }

    // Leave the controller, and the alert state it owns, as a fresh start
    // finds it, so later tests do not inherit this one's TIME alert.
    (void)config_manager::save(saved_config);
    console_controller::init();
}
