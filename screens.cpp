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

/// @brief Draws a static calibration screen for alignment and extent testing.
void draw_calibration_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    const int mid_x = UI_WIDTH / 2;
    const int mid_y = UI_HEIGHT / 2;
    const int q1_x = UI_WIDTH / 4;
    const int q3_x = (UI_WIDTH * 3) / 4;
    const int q1_y = UI_HEIGHT / 4;
    const int q3_y = (UI_HEIGHT * 3) / 4;

    // Full outer border of the logical UI.
    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);

    // Inner border to make clipping easier to see in photos.
    framebuffer::draw_rect(fb, 4, 4, UI_WIDTH - 8, UI_HEIGHT - 8, true);

    // Corner markers.
    framebuffer::fill_rect(fb, 0, 0, 8, 8, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 8, 0, 8, 8, true);
    framebuffer::fill_rect(fb, 0, UI_HEIGHT - 8, 8, 8, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 8, UI_HEIGHT - 8, 8, 8, true);

    // Center cross.
    framebuffer::draw_vline(fb, mid_x, 0, UI_HEIGHT - 1, true);
    framebuffer::draw_hline(fb, 0, UI_WIDTH - 1, mid_y, true);

    // Quarter lines.
    framebuffer::draw_vline(fb, q1_x, 16, UI_HEIGHT - 17, true);
    framebuffer::draw_vline(fb, q3_x, 16, UI_HEIGHT - 17, true);
    framebuffer::draw_hline(fb, 16, UI_WIDTH - 17, q1_y, true);
    framebuffer::draw_hline(fb, 16, UI_WIDTH - 17, q3_y, true);

    // Small edge ticks every 32 logical pixels.
    for (int x = 0; x < UI_WIDTH; x += 32) {
        framebuffer::draw_vline(fb, x, 0, 5, true);
        framebuffer::draw_vline(fb, x, UI_HEIGHT - 6, UI_HEIGHT - 1, true);
    }

    for (int y = 0; y < UI_HEIGHT; y += 32) {
        framebuffer::draw_hline(fb, 0, 5, y, true);
        framebuffer::draw_hline(fb, UI_WIDTH - 6, UI_WIDTH - 1, y, true);
    }

    // Central box.
    framebuffer::draw_rect(fb, mid_x - 30, mid_y - 20, 60, 40, true);

    // Top and bottom labels for orientation.
    framebuffer::draw_text(fb, 12, 12, "TOP", true, 1, 1);
    framebuffer::draw_text(fb, UI_WIDTH - 34, 12, "R", true, 2, 1);
    framebuffer::draw_text(fb, 12, UI_HEIGHT - 20, "BOTTOM", true, 1, 1);

    // A few filled blocks for checking edge visibility and stability.
    framebuffer::fill_rect(fb, 20, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 32, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, mid_x - 10, 24, 20, 12, true);
    framebuffer::fill_rect(fb, mid_x - 10, UI_HEIGHT - 36, 20, 12, true);
}

}  // namespace screens
