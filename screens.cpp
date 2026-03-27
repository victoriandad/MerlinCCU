#include "screens.h"

#include "framebuffer.h"
#include "panel_config.h"

namespace screens {

/// @brief Draws a simple geometry and fill-pattern test screen.
void draw_demo_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    framebuffer::draw_rect(fb, 10, 10, UI_WIDTH - 20, UI_HEIGHT - 20, true);

    framebuffer::fill_rect(fb, 20, 20, 60, 40, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 80, 30, 40, 70, true);

    framebuffer::draw_diag(fb, true);

    for (int i = 0; i < 10; ++i) {
        framebuffer::fill_rect(fb, 5, 20 + i * 28, 6, 12, true);
        framebuffer::fill_rect(fb, UI_WIDTH - 11, 20 + i * 28, 6, 12, true);
    }

    framebuffer::fill_rect(fb, 0, UI_HEIGHT - 16, UI_WIDTH, 16, true);
    framebuffer::fill_rect(fb, 8, UI_HEIGHT - 12, 100, 8, false);
}

/// @brief Draws a static mock-up of a future Merlin CCU home/status page.
void draw_dummy_menu_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    framebuffer::draw_rect(fb, 6, 6, UI_WIDTH - 12, UI_HEIGHT - 12, true);

    framebuffer::fill_rect(fb, 8, 8, UI_WIDTH - 16, 18, true);
    framebuffer::draw_text(fb, 14, 14, "MERLIN CCU", false, 1, 1);

    const char* left_labels[5] = {"LIGHTS", "HEAT", "GARAGE", "MEDIA", "ALARM"};
    const char* right_labels[5] = {"STATUS", "CAMERAS", "ENERGY", "SCENES", "SETUP"};

    for (int i = 0; i < 5; ++i) {
        const int y = 42 + i * 42;

        framebuffer::fill_rect(fb, 8, y + 4, 8, 10, true);
        framebuffer::draw_rect(fb, 22, y, 82, 18, true);
        framebuffer::draw_text(fb, 28, y + 5, left_labels[i], true, 1, 1);

        framebuffer::fill_rect(fb, UI_WIDTH - 16, y + 4, 8, 10, true);
        framebuffer::draw_rect(fb, UI_WIDTH - 104, y, 82, 18, true);
        framebuffer::draw_text(fb, UI_WIDTH - 98, y + 5, right_labels[i], true, 1, 1);
    }

    framebuffer::draw_rect(fb, 72, 54, 108, 148, true);
    framebuffer::draw_text(fb, 96, 66, "HOME", true, 2, 2);
    framebuffer::draw_text(fb, 84, 100, "TEMP  19", true, 1, 1);
    framebuffer::draw_text(fb, 84, 116, "DOOR  SHUT", true, 1, 1);
    framebuffer::draw_text(fb, 84, 132, "ALARM OFF", true, 1, 1);
    framebuffer::draw_text(fb, 84, 148, "WIFI  GOOD", true, 1, 1);

    framebuffer::fill_rect(fb, 8, UI_HEIGHT - 20, UI_WIDTH - 16, 10, true);
    framebuffer::draw_text(fb, 14, UI_HEIGHT - 18, "IDLE 00:42", false, 1, 1);
    framebuffer::draw_text(fb, UI_WIDTH - 54, UI_HEIGHT - 18, "HA OK", false, 1, 1);
}

}  // namespace screens
