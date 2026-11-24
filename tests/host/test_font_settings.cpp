// Fonts page (#123): choosing a font on the panel saves it, shows at once on
// every page, survives a restart through the saved config, and ALL DEFAULTS
// puts everything back.

#include <array>
#include <cstring>

#include "config_manager.h"
#include "console_controller.h"
#include "golden_test_support.h"
#include "input.h"
#include "panel_config.h"
#include "screens.h"
#include "settings_controller.h"
#include "test_framework.h"
#include "text_styles.h"

namespace
{

using text_styles::TextStyle;

void press(ButtonId id)
{
    (void)console_controller::handle_button_event({id, ButtonEventType::Pressed});
}

std::array<uint8_t, static_cast<size_t>(kUiFbSize)> render_current()
{
    std::array<uint8_t, static_cast<size_t>(kUiFbSize)> fb = {};
    screens::draw_menu_screen(fb.data(), console_controller::state(), 0U);
    return fb;
}

/// @brief Home > SETTINGS > page 2 > FONTS.
void open_fonts_page()
{
    press(ButtonId::RightTop);    // SETTINGS
    press(ButtonId::CursorRight); // Settings 2/2
    press(ButtonId::RightTop);    // FONTS
}

} // namespace

HOST_TEST(choosing_a_font_on_the_panel_saves_and_shows_it)
{
    const RuntimeConfig saved = config_manager::settings();
    console_controller::init();
    (void)console_controller::apply_runtime_config(config_manager::settings());

    open_fonts_page();
    EXPECT_TRUE(console_controller::state().active_page == MenuPage::FontSettings);
    press(ButtonId::LeftLower); // SOFTKEY [8x12]
    EXPECT_TRUE(console_controller::state().active_page == MenuPage::FontOptions);
    press(ButtonId::LeftLower); // 8X14
    EXPECT_TRUE(text_styles::font(TextStyle::Softkey) == fonts::FontFace::Font8x14);
    EXPECT_EQ(config_manager::settings().font_choices[static_cast<size_t>(TextStyle::Softkey)],
              uint8_t{static_cast<uint8_t>(fonts::FontFace::Font8x14) + 1U});

    // A restart applies the saved choice again.
    console_controller::init();
    EXPECT_TRUE(text_styles::font(TextStyle::Softkey) == fonts::FontFace::Font8x12);
    (void)console_controller::apply_runtime_config(config_manager::settings());
    EXPECT_TRUE(text_styles::font(TextStyle::Softkey) == fonts::FontFace::Font8x14);

    // Home drawn with the larger softkey font, and an 8x12 footer.
    console_controller::init();
    RuntimeConfig custom = config_manager::settings();
    custom.font_choices[static_cast<size_t>(TextStyle::Footer)] =
        static_cast<uint8_t>(fonts::FontFace::Font8x12) + 1U;
    (void)config_manager::save(custom);
    (void)console_controller::apply_runtime_config(config_manager::settings());
    EXPECT_TRUE(golden_test::check_golden("home_custom_fonts", render_current().data()));

    // ALL DEFAULTS, the last entry, on page 3.
    open_fonts_page();
    press(ButtonId::CursorRight);
    press(ButtonId::CursorRight);
    press(ButtonId::LeftUpper); // ALL DEFAULTS
    EXPECT_TRUE(text_styles::font(TextStyle::Softkey) == fonts::FontFace::Font8x12);
    EXPECT_TRUE(text_styles::font(TextStyle::Footer) == fonts::FontFace::Font5x7);
    for (const uint8_t choice : config_manager::settings().font_choices)
    {
        EXPECT_EQ(choice, 0U);
    }
    EXPECT_TRUE(std::strcmp(settings_controller::fonts_selection_text(), "Default") == 0);

    (void)config_manager::save(saved);
    console_controller::init();
}
