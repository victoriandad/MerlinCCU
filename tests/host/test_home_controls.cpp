#include "home_controls.h"

#include <array>
#include <cstring>
#include <string>

#include "console_controller.h"
#include "golden_test_support.h"
#include "input.h"
#include "panel_config.h"
#include "screens.h"
#include "test_framework.h"

namespace
{

constexpr char kList[] = "{\"v\":1,\"list\":\"ab12cd34\","
                         "\"names\":[\"Hall light\",\"Evening\",\"Porch\"],"
                         "\"kinds\":[\"toggle\",\"run\",\"toggle\"],"
                         "\"states\":[\"off\",\"\",\"unavailable\"]}";

/// @brief Ten entries, so the list has a second page.
constexpr char kLongList[] =
    "{\"v\":1,\"list\":\"5f3a9c01\","
    "\"names\":[\"A\",\"B\",\"C\",\"D\",\"E\",\"F\",\"G\",\"H\",\"Study lamp\",\"Bedtime\"],"
    "\"kinds\":[\"toggle\",\"toggle\",\"toggle\",\"toggle\",\"toggle\",\"toggle\",\"toggle\","
    "\"toggle\",\"toggle\",\"run\"],"
    "\"states\":[\"on\",\"on\",\"on\",\"on\",\"on\",\"on\",\"on\",\"on\",\"off\",\"\"]}";

HomeControls linked_with(const char* json)
{
    HomeControls controls = {};
    (void)home_controls::set_link(controls, true);
    (void)home_controls::apply_list(json, controls);
    return controls;
}

void press_button(ButtonId id)
{
    (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

using Framebuffer = std::array<uint8_t, static_cast<size_t>(kUiFbSize)>;

Framebuffer render_current()
{
    Framebuffer fb = {};
    screens::draw_menu_screen(fb.data(), console_controller::state(), 0U);
    return fb;
}

/// @brief The active alert with this summary, if any.
const ActiveAlert* find_alert(const char* summary)
{
    const ConsoleState& state = console_controller::state();
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (std::strcmp(state.active_alerts[i].summary.data(), summary) == 0)
        {
            return &state.active_alerts[i];
        }
    }
    return nullptr;
}

/// @brief Opens Controls from Home with `json` as the list, as an operator would.
void open_controls(bool link_up, const char* json)
{
    console_controller::init();
    (void)console_controller::update_home_controls(link_up, 1000U);
    if (json != nullptr)
    {
        (void)console_controller::apply_home_controls(home_controls::Topic::List, json);
    }
    press_button(ButtonId::RightLower); // R4: CONTROLS
}

} // namespace

HOST_TEST(home_controls_topics_are_matched_under_the_units_prefix)
{
    EXPECT_TRUE(home_controls::topic_for("merlinccu/aabb/controls/list", "merlinccu/aabb") ==
                home_controls::Topic::List);
    EXPECT_TRUE(home_controls::topic_for("merlinccu/aabb/controls/result", "merlinccu/aabb") ==
                home_controls::Topic::Result);
    EXPECT_FALSE(home_controls::topic_for("merlinccu/aabb/controls/press", "merlinccu/aabb"));
    EXPECT_FALSE(home_controls::topic_for("merlinccu/ccdd/controls/list", "merlinccu/aabb"));
    EXPECT_FALSE(home_controls::topic_for("merlinccu/aabb/controls/list", ""));
}

HOST_TEST(home_controls_list_is_parsed_into_kinds_and_states)
{
    const HomeControls controls = linked_with(kList);
    EXPECT_TRUE(controls.received);
    EXPECT_EQ(controls.count, uint8_t{3U});
    EXPECT_TRUE(std::strcmp(controls.items[0].name.data(), "Hall light") == 0);
    EXPECT_TRUE(controls.items[0].kind == ControlKind::Toggle);
    EXPECT_TRUE(controls.items[0].state == ControlState::Off);
    EXPECT_TRUE(controls.items[1].kind == ControlKind::Run);
    EXPECT_TRUE(controls.items[2].state == ControlState::Unavailable);
    EXPECT_TRUE(std::strcmp(controls.list_id.data(), "ab12cd34") == 0);
}

HOST_TEST(home_controls_refuses_bad_lists_and_keeps_the_last_one)
{
    HomeControls controls = linked_with(kList);
    // Another schema version, no list id, arrays of different lengths.
    EXPECT_FALSE(home_controls::apply_list(
        "{\"v\":2,\"list\":\"x\",\"names\":[],\"kinds\":[],\"states\":[]}", controls));
    EXPECT_FALSE(
        home_controls::apply_list("{\"v\":1,\"names\":[],\"kinds\":[],\"states\":[]}", controls));
    EXPECT_FALSE(home_controls::apply_list(
        "{\"v\":1,\"list\":\"x\",\"names\":[\"A\"],\"kinds\":[],\"states\":[\"on\"]}", controls));
    EXPECT_EQ(controls.count, uint8_t{3U});

    // An empty list is a list.
    EXPECT_TRUE(home_controls::apply_list(
        "{\"v\":1,\"list\":\"e\",\"names\":[],\"kinds\":[],\"states\":[]}", controls));
    EXPECT_EQ(controls.count, uint8_t{0U});
    EXPECT_EQ(home_controls::page_count(controls), uint8_t{1U});
}

HOST_TEST(home_controls_long_names_are_cut_to_the_name_length)
{
    const HomeControls controls = linked_with(
        "{\"v\":1,\"list\":\"x\",\"names\":[\"A name much longer than twenty\"],"
        "\"kinds\":[\"toggle\"],\"states\":[\"on\"]}");
    EXPECT_EQ(controls.count, uint8_t{1U});
    EXPECT_EQ(std::strlen(controls.items[0].name.data()), home_controls::kNameLength);
}

HOST_TEST(home_controls_press_waits_for_ha_and_a_success_flips_a_toggle)
{
    HomeControls controls = linked_with(kList);
    EXPECT_TRUE(home_controls::press(controls, 0U, 100U));
    EXPECT_TRUE(controls.pending == uint8_t{0U});
    // One press at a time.
    EXPECT_FALSE(home_controls::press(controls, 1U, 100U));

    char payload[24] = {};
    EXPECT_TRUE(home_controls::take_press(controls, payload, sizeof(payload)));
    EXPECT_TRUE(std::strcmp(payload, "ab12cd34:0") == 0);
    EXPECT_FALSE(home_controls::take_press(controls, payload, sizeof(payload)));

    // An answer about another entry is not this press's.
    EXPECT_FALSE(home_controls::apply_result("{\"v\":1,\"index\":1,\"ok\":true}", controls, 200U));
    EXPECT_TRUE(home_controls::apply_result("{\"v\":1,\"index\":0,\"ok\":true}", controls, 200U));
    EXPECT_FALSE(controls.pending.has_value());
    EXPECT_TRUE(controls.items[0].state == ControlState::On);
    EXPECT_TRUE(controls.failure == ControlFailure::None);
}

HOST_TEST(home_controls_a_list_before_the_answer_is_not_flipped_twice)
{
    HomeControls controls = linked_with(kList);
    EXPECT_TRUE(home_controls::press(controls, 0U, 100U));
    // HA's state change reaches the CCU before its answer to the press.
    EXPECT_TRUE(home_controls::apply_list("{\"v\":1,\"list\":\"ab12cd34\","
                                          "\"names\":[\"Hall light\",\"Evening\",\"Porch\"],"
                                          "\"kinds\":[\"toggle\",\"run\",\"toggle\"],"
                                          "\"states\":[\"on\",\"\",\"unavailable\"]}",
                                          controls));
    EXPECT_TRUE(controls.pending == uint8_t{0U});
    EXPECT_TRUE(home_controls::apply_result("{\"v\":1,\"index\":0,\"ok\":true}", controls, 200U));
    EXPECT_TRUE(controls.items[0].state == ControlState::On);
}

HOST_TEST(home_controls_refusal_and_silence_are_failures)
{
    HomeControls controls = linked_with(kList);
    EXPECT_TRUE(home_controls::press(controls, 1U, 100U));
    EXPECT_TRUE(home_controls::apply_result(
        "{\"v\":1,\"index\":1,\"ok\":false,\"error\":\"Script failed\"}", controls, 300U));
    EXPECT_TRUE(controls.failure == ControlFailure::Refused);
    EXPECT_EQ(controls.failure_count, uint32_t{1U});
    char notice[48] = {};
    home_controls::failure_notice(controls, notice, sizeof(notice));
    EXPECT_TRUE(std::string(notice) == "EVENING: HA REFUSED");

    EXPECT_TRUE(home_controls::press(controls, 0U, 1000U));
    EXPECT_TRUE(controls.failure == ControlFailure::None);
    EXPECT_FALSE(home_controls::tick(controls, 1000U + home_controls::kReplyTimeoutMs - 1U));
    EXPECT_TRUE(home_controls::tick(controls, 1000U + home_controls::kReplyTimeoutMs));
    EXPECT_TRUE(controls.failure == ControlFailure::NoReply);
    EXPECT_EQ(controls.failure_count, uint32_t{2U});
    home_controls::failure_notice(controls, notice, sizeof(notice));
    EXPECT_TRUE(std::string(notice) == "HALL LIGHT: NO REPLY FROM HA");

    // The notice goes after a while.
    EXPECT_TRUE(home_controls::tick(controls, 1000U + home_controls::kReplyTimeoutMs +
                                                  home_controls::kFailureNoticeMs));
    EXPECT_TRUE(controls.failure == ControlFailure::None);
}

HOST_TEST(home_controls_cannot_press_without_a_link_or_an_available_entity)
{
    HomeControls controls = linked_with(kList);
    EXPECT_FALSE(home_controls::press(controls, 2U, 0U)); // unavailable
    EXPECT_FALSE(home_controls::press(controls, 3U, 0U)); // no such entry

    EXPECT_TRUE(home_controls::press(controls, 0U, 0U));
    // Losing the link ends the press without blaming HA.
    EXPECT_TRUE(home_controls::set_link(controls, false));
    EXPECT_FALSE(controls.pending.has_value());
    EXPECT_FALSE(controls.press_queued);
    EXPECT_EQ(controls.failure_count, uint32_t{0U});
    EXPECT_FALSE(home_controls::press(controls, 0U, 0U));
}

HOST_TEST(home_controls_a_new_list_ends_the_press_and_keeps_the_page_in_range)
{
    HomeControls controls = linked_with(kLongList);
    EXPECT_EQ(home_controls::page_count(controls), uint8_t{2U});
    controls.page_index = 1U;
    EXPECT_TRUE(home_controls::press(controls, 8U, 0U));

    // Same list, new states: the press goes on.
    EXPECT_TRUE(home_controls::apply_list(kLongList, controls));
    EXPECT_TRUE(controls.pending == uint8_t{8U});

    // A different list: index 8 may be another entity now.
    EXPECT_TRUE(home_controls::apply_list(kList, controls));
    EXPECT_FALSE(controls.pending.has_value());
    EXPECT_EQ(controls.page_index, uint8_t{0U});
}

HOST_TEST(a_failed_control_press_raises_control_failed_until_one_works)
{
    open_controls(true, kList);
    press_button(ButtonId::LeftTop); // Hall light
    (void)console_controller::apply_home_controls(home_controls::Topic::Result,
                                                  "{\"v\":1,\"index\":0,\"ok\":false}");
    const ActiveAlert* alert = find_alert("CONTROL FAILED");
    EXPECT_TRUE(alert != nullptr);
    EXPECT_TRUE(alert != nullptr && std::strstr(alert->detail.data(), "Hall light") != nullptr);

    // It outlasts the page's notice...
    (void)console_controller::update_home_controls(true, 1000U + home_controls::kFailureNoticeMs);
    EXPECT_TRUE(find_alert("CONTROL FAILED") != nullptr);

    // ...and clears when a press works.
    press_button(ButtonId::LeftTop);
    (void)console_controller::apply_home_controls(home_controls::Topic::Result,
                                                  "{\"v\":1,\"index\":0,\"ok\":true}");
    EXPECT_TRUE(find_alert("CONTROL FAILED") == nullptr);
    console_controller::init();
}

HOST_TEST(controls_page_states_match_their_golden_images)
{
    open_controls(false, nullptr);
    EXPECT_TRUE(console_controller::state().active_page == MenuPage::Controls);
    EXPECT_TRUE(golden_test::check_golden("controls_mqtt_not_set_up", render_current().data()));

    open_controls(true, nullptr);
    EXPECT_TRUE(golden_test::check_golden("controls_no_list", render_current().data()));

    // Second page, a press waiting for HA, then HA refusing one.
    open_controls(true, kLongList);
    press_button(ButtonId::CursorRight);
    EXPECT_EQ(console_controller::state().home_controls.page_index, uint8_t{1U});
    press_button(ButtonId::LeftTop); // Study lamp
    EXPECT_TRUE(console_controller::state().home_controls.pending == uint8_t{8U});
    EXPECT_TRUE(golden_test::check_golden("controls_page_2_pending", render_current().data()));

    (void)console_controller::apply_home_controls(home_controls::Topic::Result,
                                                  "{\"v\":1,\"index\":8,\"ok\":false}");
    EXPECT_TRUE(golden_test::check_golden("controls_page_2_refused", render_current().data()));

    // A full first page: the notice sits between the two-line row 4 keys
    // and HOME.
    open_controls(true, kLongList);
    press_button(ButtonId::LeftLower); // D
    (void)console_controller::update_home_controls(true, 1000U + home_controls::kReplyTimeoutMs);
    EXPECT_TRUE(console_controller::state().home_controls.failure == ControlFailure::NoReply);
    EXPECT_TRUE(golden_test::check_golden("controls_no_reply", render_current().data()));

    // Losing MQTT takes the keys away.
    MqttStatus mqtt = {};
    mqtt.configured = true;
    (void)console_controller::set_mqtt_status(mqtt);
    (void)console_controller::update_home_controls(false, 2000U);
    EXPECT_TRUE(golden_test::check_golden("controls_not_connected", render_current().data()));

    console_controller::init();
}
