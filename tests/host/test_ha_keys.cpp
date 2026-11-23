#include "input.h"

#include "ha_key_list.h"

#include <cstring>

#include "config_manager.h"
#include "console_controller.h"
#include "test_framework.h"

namespace
{

/// @brief A few real legends; the rest have none.
const char* fake_name(ButtonId id)
{
    switch (id)
    {
    case ButtonId::LeftTop:
        return "L1";
    case ButtonId::TFunc:
        return "T FUNC";
    case ButtonId::AlphaA:
        return "A";
    case ButtonId::Slash:
        return "/";
    case ButtonId::Dot:
        return ".";
    default:
        return nullptr;
    }
}

} // namespace

HOST_TEST(ha_keys_list_round_trips_ignoring_case_and_spacing)
{
    const auto mask = ha_keys::parse_list(" t func ,a,, L1 ", fake_name);
    EXPECT_TRUE(mask.has_value());
    EXPECT_TRUE(ha_keys::contains(*mask, ButtonId::TFunc));
    EXPECT_TRUE(ha_keys::contains(*mask, ButtonId::AlphaA));
    EXPECT_TRUE(ha_keys::contains(*mask, ButtonId::LeftTop));
    EXPECT_FALSE(ha_keys::contains(*mask, ButtonId::Slash));

    char text[64] = {};
    ha_keys::format_list(*mask, fake_name, text, sizeof(text));
    EXPECT_TRUE(std::strcmp(text, "L1, T FUNC, A") == 0 ||
                ha_keys::parse_list(text, fake_name) == mask);

    EXPECT_TRUE(ha_keys::parse_list("", fake_name) == uint64_t{0});
}

HOST_TEST(ha_keys_list_names_the_first_unknown_key)
{
    std::string_view bad;
    EXPECT_FALSE(ha_keys::parse_list("A, F9, L1", fake_name, &bad).has_value());
    EXPECT_TRUE(bad == "F9");
}

HOST_TEST(ha_key_slugs_are_topic_safe_and_distinct)
{
    char a[24] = {};
    char b[24] = {};
    ha_keys::slug("T FUNC", a, sizeof(a));
    EXPECT_TRUE(std::strcmp(a, "t_func") == 0);
    ha_keys::slug("/", a, sizeof(a));
    ha_keys::slug(".", b, sizeof(b));
    EXPECT_TRUE(std::strcmp(a, "_2f") == 0);
    EXPECT_TRUE(std::strcmp(a, b) != 0);
}

HOST_TEST(ha_keys_are_queued_instead_of_working_the_panel)
{
    RuntimeConfig settings = config_manager::settings();
    settings.ha_key_mask = ha_keys::bit(ButtonId::LeftTop);
    (void)config_manager::save(settings);
    console_controller::init();
    while (console_controller::pop_ha_key_press().has_value())
    {
    }

    const MenuPage before = console_controller::state().active_page;
    EXPECT_FALSE(console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Pressed}));
    EXPECT_FALSE(console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Released}));
    EXPECT_TRUE(console_controller::state().active_page == before);

    const auto press = console_controller::pop_ha_key_press();
    EXPECT_TRUE(press.has_value() && *press == ButtonId::LeftTop);
    EXPECT_FALSE(console_controller::pop_ha_key_press().has_value());

    // A burst past the queue is dropped, not stored.
    for (int i = 0; i < 20; ++i)
    {
        (void)console_controller::handle_button_event({ButtonId::LeftTop, ButtonEventType::Pressed});
    }
    int queued = 0;
    while (console_controller::pop_ha_key_press().has_value())
    {
        ++queued;
    }
    EXPECT_EQ(queued, 8);

    settings.ha_key_mask = 0U;
    (void)config_manager::save(settings);
}
