#include "mqtt_commands.h"

#include <cstring>
#include <string>

#include "alert_controller.h"
#include "console_controller.h"
#include "test_framework.h"

namespace
{

using mqtt_commands::Target;

constexpr std::string_view kPrefix = "merlinccu/28cdc11a896c";

bool has_alert(const ConsoleState& state, AlertCode code)
{
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (state.active_alerts[i].code == static_cast<uint8_t>(code))
        {
            return true;
        }
    }
    return false;
}

} // namespace

HOST_TEST(mqtt_command_topics_map_to_targets)
{
    EXPECT_TRUE(mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/page/set", kPrefix) ==
                Target::Page);
    EXPECT_TRUE(mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/alert_lamp/set", kPrefix) ==
                Target::AlertLamp);
    EXPECT_TRUE(mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/message/set", kPrefix) ==
                Target::Message);
    // Wrong unit, wrong suffix, unknown or nested slug.
    EXPECT_FALSE(
        mqtt_commands::target_for_topic("merlinccu/aaaaaaaaaaaa/page/set", kPrefix).has_value());
    EXPECT_FALSE(
        mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/page/state", kPrefix).has_value());
    EXPECT_FALSE(
        mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/oven/set", kPrefix).has_value());
    EXPECT_FALSE(
        mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/x/page/set", kPrefix).has_value());
    EXPECT_FALSE(
        mqtt_commands::target_for_topic("merlinccu/28cdc11a896c/set", kPrefix).has_value());
}

HOST_TEST(mqtt_select_commands_accept_only_their_options)
{
    auto lamp = mqtt_commands::parse(Target::AlertLamp, "flash_fast");
    EXPECT_TRUE(lamp.has_value());
    EXPECT_TRUE(static_cast<LampOverride>(lamp->value) == LampOverride::FlashFast);
    EXPECT_TRUE(mqtt_commands::parse(Target::TestLamp, " auto\n").has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::AlertLamp, "blink").has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::AlertLamp, "").has_value());

    auto brightness = mqtt_commands::parse(Target::PanelBrightness, "medium");
    EXPECT_TRUE(brightness.has_value());
    EXPECT_TRUE(static_cast<BrightnessLevel>(brightness->value) == BrightnessLevel::Medium);

    auto page = mqtt_commands::parse(Target::Page, "weather");
    EXPECT_TRUE(page.has_value());
    EXPECT_TRUE(mqtt_commands::page_for(page->value) == MenuPage::Weather);
    EXPECT_FALSE(mqtt_commands::parse(Target::Page, "settings").has_value());

    EXPECT_TRUE(mqtt_commands::parse(Target::Display, "screen_saver").has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::Display, "off").has_value());
}

HOST_TEST(mqtt_option_names_round_trip)
{
    for (uint8_t i = 0U; i < 5U; ++i)
    {
        const char* name = mqtt_commands::option_name(Target::AlertLamp, i);
        EXPECT_TRUE(name != nullptr);
        EXPECT_TRUE(mqtt_commands::parse(Target::AlertLamp, name)->value == i);
    }
    EXPECT_TRUE(mqtt_commands::option_name(Target::AlertLamp, 5U) == nullptr);
    EXPECT_TRUE(mqtt_commands::page_option(MenuPage::Shares).has_value());
    EXPECT_FALSE(mqtt_commands::page_option(MenuPage::WifiSettings).has_value());
}

HOST_TEST(mqtt_message_takes_plain_text_or_json)
{
    auto plain = mqtt_commands::parse(Target::Message, "Washing is done");
    EXPECT_TRUE(plain.has_value());
    EXPECT_TRUE(std::strcmp(plain->message.text.data(), "Washing is done") == 0);
    EXPECT_TRUE(plain->message.severity == AlertSeverity::Message);

    auto json = mqtt_commands::parse(
        Target::Message,
        R"({"title":"Doorbell","message":"Someone is at the door","severity":"alert"})");
    EXPECT_TRUE(json.has_value());
    EXPECT_TRUE(std::strcmp(json->message.title.data(), "Doorbell") == 0);
    EXPECT_TRUE(std::strcmp(json->message.text.data(), "Someone is at the door") == 0);
    EXPECT_TRUE(json->message.severity == AlertSeverity::Alert);

    // Empty text clears the message.
    auto clear = mqtt_commands::parse(Target::Message, "");
    EXPECT_TRUE(clear.has_value());
    EXPECT_TRUE(clear->message.text[0] == '\0');

    EXPECT_FALSE(mqtt_commands::parse(Target::Message, R"({"title":"No text"})").has_value());
    EXPECT_FALSE(
        mqtt_commands::parse(Target::Message, R"({"message":"x","severity":"panic"})").has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::Message, std::string(301U, 'a')).has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::Message,
                                      R"({"title":"An overly long message title","message":"x"})")
                     .has_value());
    EXPECT_FALSE(mqtt_commands::parse(Target::Message, std::string(600U, 'a')).has_value());
}

HOST_TEST(mqtt_commands_drive_lamps_pages_and_messages)
{
    console_controller::init();
    alert_controller::reset();

    auto lamp = mqtt_commands::parse(Target::TestLamp, "on");
    EXPECT_TRUE(console_controller::apply_mqtt_command(*lamp));
    EXPECT_TRUE(console_controller::state().lamps[static_cast<size_t>(LampId::TestLamp)] ==
                LampMode::On);
    auto lamp_auto = mqtt_commands::parse(Target::TestLamp, "auto");
    EXPECT_TRUE(console_controller::apply_mqtt_command(*lamp_auto));
    EXPECT_TRUE(console_controller::state().lamps[static_cast<size_t>(LampId::TestLamp)] ==
                LampMode::Off);

    auto page = mqtt_commands::parse(Target::Page, "shares");
    EXPECT_TRUE(console_controller::apply_mqtt_command(*page));
    EXPECT_TRUE(console_controller::state().active_page == MenuPage::Shares);

    EXPECT_TRUE(std::strcmp(console_controller::ha_message_state(), "none") == 0);
    auto message = mqtt_commands::parse(Target::Message,
                                        R"({"title":"Washing","message":"Machine finished"})");
    EXPECT_TRUE(console_controller::apply_mqtt_command(*message));
    EXPECT_TRUE(has_alert(console_controller::state(), AlertCode::HomeAssistantMessage));
    EXPECT_TRUE(std::strcmp(console_controller::ha_message_state(), "active") == 0);

    // Accepting it on the panel is reported back as acknowledged.
    ConsoleState& state = const_cast<ConsoleState&>(console_controller::state());
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (state.active_alerts[i].code == static_cast<uint8_t>(AlertCode::HomeAssistantMessage))
        {
            EXPECT_TRUE(std::strcmp(state.active_alerts[i].summary.data(), "WASHING") == 0);
            alert_controller::accept_alert(state, i);
            break;
        }
    }
    EXPECT_TRUE(std::strcmp(console_controller::ha_message_state(), "acknowledged") == 0);

    // A new message shows again.
    EXPECT_TRUE(console_controller::apply_mqtt_command(*message));
    EXPECT_TRUE(has_alert(console_controller::state(), AlertCode::HomeAssistantMessage));
    EXPECT_TRUE(std::strcmp(console_controller::ha_message_state(), "active") == 0);

    auto clear = mqtt_commands::parse(Target::Message, "");
    EXPECT_TRUE(console_controller::apply_mqtt_command(*clear));
    EXPECT_FALSE(has_alert(console_controller::state(), AlertCode::HomeAssistantMessage));
    EXPECT_TRUE(std::strcmp(console_controller::ha_message_state(), "none") == 0);

    auto home = mqtt_commands::parse(Target::Page, "home");
    (void)console_controller::apply_mqtt_command(*home);
}
