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
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "calendar_controller.h"
#include "config_manager.h"
#include "console_controller.h"
#include "console_controller_internal.h"
#include "console_model.h"
#include "cursor_navigation.h"
#include "environment_sensor_manager.h"
#include "framebuffer.h"
#include "golden_test_support.h"
#include "input.h"
#include "page_table.h"
#include "panel_config.h"
#include "screens.h"
#include "screens_shared.h"
#include "softkey_builders.h"
#include "test_framework.h"
#include "ui_layout.h"

namespace
{

using text_styles::TextStyle;

/// @brief A Controls list as HA's integration publishes it (#152).
constexpr char kControlsList[] =
    "{\"v\":1,\"list\":\"5f3a9c01\","
    "\"names\":[\"Hall light\",\"Landing\",\"Kitchen spots\",\"Porch\",\"Evening\","
    "\"Goodnight\",\"Fan\",\"Christmas tree lights\",\"Garage door\",\"Movie time\"],"
    "\"kinds\":[\"toggle\",\"toggle\",\"toggle\",\"toggle\",\"run\",\"run\",\"toggle\","
    "\"toggle\",\"toggle\",\"run\"],"
    "\"states\":[\"on\",\"off\",\"on\",\"unavailable\",\"\",\"\",\"off\",\"on\",\"off\",\"\"]}";

/// @brief Fixed boot-uptime for every render, so freshness text is stable.
constexpr uint32_t kCoverageNowMs = 900000U;

constexpr int kPageCount = static_cast<int>(kMenuPageCount);

constexpr std::array<ButtonId, static_cast<size_t>(SoftKeyId::Count)> kSoftkeyButtons = {
    ButtonId::LeftTop,    ButtonId::LeftUpper,  ButtonId::LeftMiddle, ButtonId::LeftLower,
    ButtonId::LeftBottom, ButtonId::RightTop,   ButtonId::RightUpper, ButtonId::RightMiddle,
    ButtonId::RightLower, ButtonId::RightBottom};

/// @brief Golden-image name for each page, in MenuPage order.
constexpr std::array<const char*, kPageCount> kPageNames = {
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
    "controls",
    "font_settings",
    "font_options",
};

void press(ButtonId id)
{
    (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

/// @brief Resets the controller and feeds it the data a working CCU has:
/// Wi-Fi, two watched shares, a live sensor board, and one
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

    // A partitioned unit running from slot A, as after an OTA update (#154).
    FirmwareStatus firmware = {};
    firmware.valid = true;
    firmware.partitioned = true;
    firmware.slot = 0;
    std::snprintf(firmware.version.data(), firmware.version.size(), "2bf47da");
    (void)console_controller::set_firmware_status(firmware);

    // Up for 3 days 5 hours after a power cut (#155).
    (void)console_controller::set_boot_status(
        {.reset_reason = ResetReason::PowerOn, .uptime_minutes = (3U * 24U + 5U) * 60U + 12U});

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

    TimeStatus time = {};
    time.synced = true;
    std::snprintf(time.time_text.data(), time.time_text.size(), "12:34");
    std::snprintf(time.date_text.data(), time.date_text.size(), "2026-11-13");
    time.weekday_index = 5U;
    (void)console_controller::set_time_status(time);

    // Ten Home Assistant controls (#152): two pages, every kind and state,
    // and a name too long for its key.
    (void)console_controller::update_home_controls(true, kCoverageNowMs);
    (void)console_controller::apply_home_controls(home_controls::Topic::List, kControlsList);

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

using screens::softkey_label_fits;

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

/// @brief Names the softkeys a body or diagnostic page uses off row 5
/// (PAGE-1, PAGE-4): those page types keep their keys to L5 and R5.
std::vector<std::string> page_type_problems(const ConsoleState& state)
{
    std::vector<std::string> out;
    if (page_table::describe(state.active_page).type == page_table::PageType::Key)
    {
        return out;
    }
    for (size_t key = 0U; key < state.softkeys.size(); ++key)
    {
        const size_t row = key % static_cast<size_t>(ui_layout::kSoftkeyRowCount);
        if (row != 4U && state.softkeys[key].role != SoftKeyRole::Blank)
        {
            const bool left = key < static_cast<size_t>(ui_layout::kSoftkeyRowCount);
            out.push_back(std::string("type ") + (left ? "L" : "R") + std::to_string(row + 1U) +
                          " used on a body page");
        }
    }
    return out;
}

} // namespace

HOST_TEST(every_page_table_entry_has_a_title_and_a_parent_chain_ending_at_home)
{
    for (size_t value = 0U; value < kMenuPageCount; ++value)
    {
        const MenuPage page = static_cast<MenuPage>(value);
        const page_table::PageDescriptor& entry = page_table::describe(page);
        EXPECT_TRUE(entry.page == page);
        EXPECT_TRUE(entry.title != nullptr && entry.title[0] != '\0');
        EXPECT_TRUE(entry.label_max_lines >= 1U);

        ConsoleState state = {};
        state.active_page = page;
        state.alert_parent_page = MenuPage::Weather;
        size_t steps = 0U;
        while (state.active_page != MenuPage::Home && steps <= kMenuPageCount)
        {
            state.active_page = page_table::parent_page(state);
            ++steps;
        }
        EXPECT_TRUE(state.active_page == MenuPage::Home);
    }
}

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
        softkey::view(SoftKeyId::Right5, "PERIOD", "Today", SoftKeyRoute::CycleWeatherPeriod);
    EXPECT_EQ(role_problems(softkeys).size(), 1U);
}

HOST_TEST(a_caption_and_value_label_shortens_a_long_value_to_fit)
{
    // LBL-6 (#115): the label builder, not the renderer, makes a long value fit.
    const char* label = console_controller::console_controller_internal::
        build_selection_softkey_label(SoftKeyId::Left1, "LABEL", "Merlin CCU (Pimoroni) bench unit");
    EXPECT_TRUE(softkey_label_fits(label, TextStyle::Softkey, 2));
    EXPECT_TRUE(std::strstr(label, "..]") != nullptr);
    EXPECT_TRUE(std::strncmp(label, "LABEL\n[Merlin CCU", 17) == 0);

    // A value that fits is left alone.
    label = console_controller::console_controller_internal::build_selection_softkey_label(
        SoftKeyId::Left1, "LABEL", "Ground floor");
    EXPECT_TRUE(std::strcmp(label, "LABEL\n[Ground floor]") == 0);
}

HOST_TEST(the_fit_and_overlap_checks_catch_a_long_label_and_an_overlapping_body)
{
    EXPECT_TRUE(softkey_label_fits("HOME\n[Ready]", TextStyle::Softkey, 2));
    // The longest Home PINTER summary, with every vessel conditioning.
    EXPECT_TRUE(softkey_label_fits("PINTER\n[4 conditioning]",
                                   screens::softkey_label_style(MenuPage::Home),
                                   screens::softkey_label_max_lines(MenuPage::Home)));
    // The widest location the Weather Source page can show (#87, #115).
    EXPECT_TRUE(softkey_label_fits("LOCATION\n[S33 51.6' W151 12.6']",
                                   screens::softkey_label_style(MenuPage::WeatherSources),
                                   screens::softkey_label_max_lines(MenuPage::WeatherSources)));
    EXPECT_FALSE(softkey_label_fits("A VERY LONG CAPTION THAT WRAPS\n[and a long value too]",
                                    TextStyle::Softkey, 2));

    // Resources draws its PROGRAM FLASH bar from the top-left of the body: a
    // label on L1 would sit on top of it.
    ConsoleState state = {};
    state.active_page = MenuPage::StatusResources;
    EXPECT_TRUE(body_overlaps(state).empty());
    state.softkeys[static_cast<size_t>(SoftKeyId::Left1)] = {"TEST", SoftKeyRoute::None, true};
    EXPECT_FALSE(body_overlaps(state).empty());
}

namespace
{

/// @brief Replays `path` from the data fixture.
void replay(const std::vector<ButtonId>& path)
{
    start_with_data();
    for (const ButtonId step : path)
    {
        press(step);
    }
}

/// @brief Replays `path`, then presses `id`; returns whether the press
/// changed anything.
bool press_after(const std::vector<ButtonId>& path, ButtonId id)
{
    replay(path);
    return console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

/// @brief Names the navigation rules the page reached by `path` breaks:
/// R5 is HOME unless the page is Home or asking for a decision (NAV-2,
/// NAV-3), a cursor arrow shows exactly when its key would act (NAV-8), and
/// no softkey other than HOME leads where BACK STEP does (NAV-5).
std::vector<std::string> navigation_problems(const std::vector<ButtonId>& path)
{
    std::vector<std::string> out;
    replay(path);
    const ConsoleState state = console_controller::state();

    const SoftKeyAction& r5 = state.softkeys[static_cast<size_t>(SoftKeyId::Right5)];
    // Calendar's R5 is its filter RESET while a filter is set, by decision
    // in #116, until live calendar data decides how the page should work.
    const bool calendar_reset =
        state.active_page == MenuPage::Calendar && !calendar_controller::filters_are_default(state);
    if (state.active_page != MenuPage::Home && !page_is_asking_for_decision(state) &&
        !calendar_reset && !opens_page(r5, MenuPage::Home))
    {
        out.push_back("nav R5 is not HOME");
    }

    const std::array<std::pair<ButtonId, int>, 2> kCursorKeys = {
        {{ButtonId::CursorLeft, -1}, {ButtonId::CursorRight, 1}}};
    for (const auto& [button, direction] : kCursorKeys)
    {
        const bool arrow = cursor_navigation::can_move(state, direction);
        const bool acted = press_after(path, button);
        if (arrow != acted)
        {
            out.push_back(std::string("nav ") + (direction < 0 ? "left" : "right") +
                          (arrow ? " arrow but the key does nothing" : " key acts with no arrow"));
        }
    }

    if (page_is_asking_for_decision(state))
    {
        return out;
    }
    (void)press_after(path, ButtonId::BackStep);
    const MenuPage back_page = console_controller::state().active_page;
    if (back_page == state.active_page)
    {
        return out;
    }
    for (size_t key = 0U; key < state.softkeys.size(); ++key)
    {
        const SoftKeyAction& action = state.softkeys[key];
        if (!action.enabled || opens_page(action, MenuPage::Home))
        {
            continue;
        }
        if (press_after(path, kSoftkeyButtons[key]) &&
            console_controller::state().active_page == back_page)
        {
            const bool left = key < static_cast<size_t>(ui_layout::kSoftkeyRowCount);
            const size_t row = key % static_cast<size_t>(ui_layout::kSoftkeyRowCount);
            out.push_back(std::string("nav ") + (left ? "L" : "R") + std::to_string(row + 1U) +
                          " duplicates BACK STEP");
        }
    }
    return out;
}

} // namespace

/// @details TXT-3: a page's line allowance must suit its font. A label block
/// of that many lines must end a line gap above the next row's block, so a
/// larger font, or more lines, that would run two keys together fails here.
HOST_TEST(every_page_label_block_fits_between_softkey_rows)
{
    for (int value = 0; value < static_cast<int>(kMenuPageCount); ++value)
    {
        const page_table::PageDescriptor& page = page_table::describe(static_cast<MenuPage>(value));
        const int lines = page.label_max_lines;
        const int block = (lines * text_styles::line_height(page.label_style)) +
                          ((lines - 1) * ui_layout::kSoftkeyLineGap);
        if (block > ui_layout::kSoftkeyRowPitch - ui_layout::kSoftkeyLineGap)
        {
            host_tests::record_failure(__FILE__, __LINE__,
                                       std::string(page.title) + ": " + std::to_string(lines) +
                                           " lines need " + std::to_string(block) + " px");
        }
    }
}

/// @details The exploration above runs on short test settings. This sets the
/// values a real unit has (taken from the bench unit, plus the longest a
/// field may hold where that is realistic) and checks every settings label
/// still fits (#115).
HOST_TEST(settings_labels_fit_with_real_world_values)
{
    const RuntimeConfig saved_config = config_manager::settings();
    RuntimeConfig config = saved_config;
    auto set = [](auto& field, const char* text)
    { std::snprintf(field.data(), field.size(), "%s", text); };
    set(config.device_name, "merlin-ground");
    set(config.device_label, "Merlin CCU (Pimoroni)");
    set(config.location, "Ground Floor");
    set(config.room, "Dining Room");
    set(config.wifi_ssid, "PLUSNET-6RF6MF");
    config.home_assistant_enabled = true;
    set(config.home_assistant_host, "homeassistant.local");
    set(config.home_assistant_entity_id, "switch.sean_s_desk_top_room");
    set(config.home_assistant_self_entity_id, "sensor.merlin_ground_status");
    set(config.weather_entity_id, "weather.forecast_home");
    set(config.sun_entity_id, "sun.sun");
    config.mqtt_enabled = true;
    set(config.mqtt_host, "homeassistant.local");
    set(config.mqtt_username, "merlinccu-ground");
    set(config.mqtt_discovery_prefix, "homeassistant");
    set(config.mqtt_base_topic, "merlinccu");
    set(config.weather_coordinates, "-33.86,151.21");
    (void)config_manager::save(config);

    const MenuPage kPages[] = {
        MenuPage::Settings,     MenuPage::DeviceSettings,      MenuPage::WifiSettings,
        MenuPage::HomeAssistantSettings, MenuPage::MqttSettings, MenuPage::WeatherSources,
        MenuPage::ScreenSaverSettings,   MenuPage::TimeZoneSettings,
    };
    for (const WeatherSource source : {WeatherSource::HomeAssistant, WeatherSource::OpenMeteo})
    {
        for (const MenuPage page : kPages)
        {
            const page_table::PageDescriptor& descriptor = page_table::describe(page);
            for (uint8_t settings_page = 0U; settings_page < kSettingsPageCount; ++settings_page)
            {
                ConsoleState state = console_controller::state();
                state.active_page = page;
                state.weather_source = source;
                state.settings_page_index = settings_page;
                SoftKeyMap softkeys = {};
                descriptor.build_softkeys(state, softkeys);
                for (size_t key = 0U; key < softkeys.size(); ++key)
                {
                    const char* label = softkeys[key].label;
                    if (label != nullptr && label[0] != '\0' &&
                        !softkey_label_fits(label, descriptor.label_style,
                                            descriptor.label_max_lines))
                    {
                        host_tests::record_failure(__FILE__, __LINE__,
                                                   std::string(descriptor.title) +
                                                       " loses text: " + label);
                    }
                }
            }
        }
    }
    (void)config_manager::save(saved_config);
}

/// @details Fit and overlap problems that already exist are listed here with
/// the issue that fixes them, rather than hidden by loosening the checks. The
/// test fails on any problem not listed, and on a listed one that has gone,
/// so the list stays accurate.
HOST_TEST(
    every_page_renders_with_its_real_softkeys_and_no_label_loses_text_meets_the_body_or_breaks_its_role)
{
    // None at present: the #115 label audit cleared the last ones.
    const std::set<std::string> kKnownProblems = {};

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
        for (const std::string& type : page_type_problems(state))
        {
            problems.insert(name + " " + type);
        }
        for (const std::string& navigation : navigation_problems(path))
        {
            problems.insert(name + " " + navigation);
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
    for (int value = 0; value < kPageCount; ++value)
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
