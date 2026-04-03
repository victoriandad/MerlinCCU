#include <cstdio>

#include "pico/stdlib.h"
#include "hardware/pio.h"

#include "display.h"
#include "el320_raster.pio.h"
#include "framebuffer.h"
#include "input.h"
#include "panel_config.h"
#include "screens.h"
#include "screensaver_life.h"
#include "console_controller.h"
#include "time_manager.h"
#include "wifi_manager.h"

namespace {

/// @brief High-level content modes shown on the display.
/// @details The dummy menu is the current default because it exposes the
/// front-panel development harness while the keypad hardware is still pending.
enum class ScreenMode : uint8_t {
    Calibration = 0,
    DemoPattern,
    Menu,
    LifeScreensaver,
};

}  // namespace

/// @brief Firmware entry point.
/// @details The startup sequence is:
/// - prepare the first framebuffer contents
/// - convert that framebuffer into the first scanout raster
/// - prepare any configured keypad GPIOs
/// - start PIO and DMA scanout
/// - keep updating the active mode in the main loop
int main()
{
    stdio_init_all();
    sleep_ms(2000);

    std::printf("MerlinCCU start. clkdiv=%.2f row_offset=%d hblank=(%d,%d)\n",
                PANEL.clkdiv,
                PANEL.native_row_offset,
                PANEL.h_pre_blank,
                PANEL.h_post_blank);

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);
    const ScreenMode mode = ScreenMode::Menu;

    uint32_t life_frame_counter = 0;
    absolute_time_t next_life_stats = make_timeout_time_ms(1000);
    const float current_clkdiv = PANEL.clkdiv;

    console_controller::init();
    time_manager::init();
    wifi_manager::init();
    console_controller::set_wifi_status(wifi_manager::status());
    console_controller::set_time_status(time_manager::status());

    if (mode == ScreenMode::LifeScreensaver) {
        screensaver_life::init();
        LifeFrameStats stats{};
        screensaver_life::step_and_render(framebuffer::back(), stats);
    } else if (mode == ScreenMode::Calibration) {
        screens::draw_calibration_screen(framebuffer::back());
    } else if (mode == ScreenMode::Menu) {
        screens::draw_menu_screen(framebuffer::back(), console_controller::state());
    } else {
        screens::draw_demo_screen(framebuffer::back());
    }

    framebuffer::swap();
    display::present(framebuffer::front());

    input::init();
    display::init(pio, sm, offset, PIN_BASE);
    display::set_clkdiv(current_clkdiv);
    std::printf("Active clkdiv=%.2f\n", current_clkdiv);

    if (mode == ScreenMode::Calibration) {
        screens::draw_calibration_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    } else if (mode == ScreenMode::DemoPattern) {
        screens::draw_demo_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    } else if (mode == ScreenMode::Menu) {
        screens::draw_menu_screen(framebuffer::back(), console_controller::state());
        framebuffer::swap();
        display::present(framebuffer::front());
    }

    while (true) {
        const ButtonEvent event = input::poll_buttons();
        input::handle_button_event(event);
        bool console_changed = console_controller::handle_button_event(event);
        console_changed = wifi_manager::update() || console_changed;
        console_changed = time_manager::update() || console_changed;
        console_changed = console_controller::set_wifi_status(wifi_manager::status()) || console_changed;
        console_changed = console_controller::set_time_status(time_manager::status()) || console_changed;

        if (mode == ScreenMode::LifeScreensaver) {
            LifeFrameStats stats{};
            screensaver_life::step_and_render(framebuffer::back(), stats);

            const absolute_time_t present_start = get_absolute_time();
            framebuffer::swap();
            display::present(framebuffer::front());
            stats.present_us = absolute_time_diff_us(present_start, get_absolute_time());

            ++life_frame_counter;

            if (absolute_time_diff_us(get_absolute_time(), next_life_stats) <= 0) {
                std::printf("Life fps=%lu sim=%lldus draw=%lldus present=%lldus\n",
                            static_cast<unsigned long>(life_frame_counter),
                            stats.sim_us,
                            stats.draw_us,
                            stats.present_us);
                life_frame_counter = 0;
                next_life_stats = make_timeout_time_ms(1000);
            }

            sleep_ms(75);
        } else {
            if (console_changed && mode == ScreenMode::Menu) {
                screens::draw_menu_screen(framebuffer::back(), console_controller::state());
                framebuffer::swap();
                display::present(framebuffer::front());
            }
            sleep_ms(100);
        }
    }

    return 0;
}
