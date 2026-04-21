#include <cstdio>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "console_controller.h"
#include "debug_logging.h"
#include "display.h"
#include "el320_raster.pio.h"
#include "framebuffer.h"
#include "home_assistant_manager.h"
#include "input.h"
#include "mqtt_manager.h"
#include "panel_config.h"
#include "screens.h"
#include "screensaver_life.h"
#include "time_manager.h"
#include "wifi_manager.h"

namespace
{

/// @brief High-level content modes shown on the display.
/// @details The dummy menu is the current default because it exposes the
/// front-panel development harness while the keypad hardware is still pending.
enum class ScreenMode : uint8_t
{
    Calibration = 0,
    DemoPattern,
    Menu,
    LifeScreensaver,
};

} // namespace

/// @brief Firmware entry point.
/// @details The startup sequence is:
/// - prepare the first framebuffer contents
/// - convert that framebuffer into the first scanout raster
/// - prepare any configured keypad GPIOs
/// - start PIO and DMA scanout
/// - keep updating the active mode in the main loop
int main()
{
    // Bring up stdio first so any early hardware or config failures are visible
    // on the debug console before the display path is running.
    stdio_init_all();
    sleep_ms(2000);

    std::printf("MerlinCCU start. clkdiv=%.2f row_offset=%d hblank=(%d,%d)\n", kPanel.clkdiv,
                kPanel.native_row_offset, kPanel.h_pre_blank, kPanel.h_post_blank);

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &el320_raster_program);
    const ScreenMode mode = ScreenMode::Menu;

    uint32_t life_frame_counter = 0;
    absolute_time_t next_life_stats = make_timeout_time_ms(1000);
    const float current_clkdiv = kPanel.clkdiv;

    // Initialize the state-producing subsystems before the first frame is drawn
    // so the initial UI reflects real status rather than placeholder defaults.
    console_controller::init();
    time_manager::init();
    wifi_manager::init();
    home_assistant_manager::init();
    mqtt_manager::init();
    console_controller::set_wifi_status(wifi_manager::status());
    console_controller::set_time_status(time_manager::status());
    console_controller::set_home_assistant_status(home_assistant_manager::status());
    console_controller::set_mqtt_status(mqtt_manager::status());

    // Render one complete back buffer before scanout starts so the panel never
    // shows an uninitialized frame during bring-up.
    if (mode == ScreenMode::LifeScreensaver)
    {
        screensaver_life::init();
        LifeFrameStats stats{};
        screensaver_life::step_and_render(framebuffer::back(), stats);
    }
    else if (mode == ScreenMode::Calibration)
    {
        screens::draw_calibration_screen(framebuffer::back());
    }
    else if (mode == ScreenMode::Menu)
    {
        screens::draw_menu_screen(framebuffer::back(), console_controller::state());
    }
    else
    {
        screens::draw_demo_screen(framebuffer::back());
    }

    framebuffer::swap();
    display::present(framebuffer::front());

    // Only after the first frame is ready do we enable input scanning and the
    // continuous PIO/DMA display path.
    input::init();
    display::init(pio, sm, offset, kPinBase);
    display::set_clkdiv(current_clkdiv);
    std::printf("Active clkdiv=%.2f\n", current_clkdiv);

    // Re-render now that scanout is live so the inactive back buffer contains
    // the same content as the visible front buffer from the start.
    if (mode == ScreenMode::Calibration)
    {
        screens::draw_calibration_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    }
    else if (mode == ScreenMode::DemoPattern)
    {
        screens::draw_demo_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    }
    else if (mode == ScreenMode::Menu)
    {
        screens::draw_menu_screen(framebuffer::back(), console_controller::state());
        framebuffer::swap();
        display::present(framebuffer::front());
    }

    while (true)
    {
        // Poll hardware first, then let the controller translate those raw
        // events into menu/state changes before the integrations update.
        const ButtonEvent event = input::poll_buttons();
        input::handle_button_event(event);
        bool console_changed = console_controller::handle_button_event(event);

        // Keep the integration stack advancing every loop so network-driven UI
        // state stays fresh even when the user is not pressing keys.
        console_changed = wifi_manager::update() || console_changed;
        console_changed = time_manager::update() || console_changed;
        console_changed = home_assistant_manager::update(wifi_manager::status()) || console_changed;
        console_changed =
            mqtt_manager::update(wifi_manager::status(), home_assistant_manager::status(),
                                 time_manager::status()) ||
            console_changed;

        // Mirror subsystem status back into the console model only after the
        // managers have had a chance to update this iteration.
        console_changed =
            console_controller::set_keypad_monitor_status(input::keypad_monitor_status()) ||
            console_changed;
        console_changed =
            console_controller::set_wifi_status(wifi_manager::status()) || console_changed;
        console_changed =
            console_controller::set_time_status(time_manager::status()) || console_changed;
        console_changed =
            console_controller::set_home_assistant_status(home_assistant_manager::status()) ||
            console_changed;
        console_changed =
            console_controller::set_mqtt_status(mqtt_manager::status()) || console_changed;

        if (mode == ScreenMode::LifeScreensaver)
        {
            // The screensaver owns the whole frame, so it renders every loop and
            // separately tracks simulation, draw, and present timing for tuning.
            LifeFrameStats stats{};
            screensaver_life::step_and_render(framebuffer::back(), stats);

            const absolute_time_t present_start = get_absolute_time();
            framebuffer::swap();
            display::present(framebuffer::front());
            stats.present_us = absolute_time_diff_us(present_start, get_absolute_time());

            ++life_frame_counter;

            if (absolute_time_diff_us(get_absolute_time(), next_life_stats) <= 0)
            {
                PERIODIC_LOG("Life fps=%lu sim=%lldus draw=%lldus present=%lldus\n",
                             static_cast<unsigned long>(life_frame_counter), stats.sim_us,
                             stats.draw_us, stats.present_us);
                life_frame_counter = 0;
                next_life_stats = make_timeout_time_ms(1000);
            }

            sleep_ms(75);
        }
        else
        {
            // The menu path only redraws on observable state changes to avoid
            // unnecessary raster rebuilds while the screen is otherwise static.
            if (console_changed && mode == ScreenMode::Menu)
            {
                screens::draw_menu_screen(framebuffer::back(), console_controller::state());
                framebuffer::swap();
                display::present(framebuffer::front());
            }
            sleep_ms(100);
        }
    }

    return 0;
}
