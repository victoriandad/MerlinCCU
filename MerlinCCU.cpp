#include <cstdio>
#include <cstdlib>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "config_manager.h"
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
#include "screensaver_clock.h"
#include "screensaver_life.h"
#include "screensaver_matrix.h"
#include "screensaver_radar.h"
#include "screensaver_rain.h"
#include "screensaver_starfield.h"
#include "screensaver_worms.h"
#include "time_manager.h"
#include "web_config_server.h"
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

/// @brief Menu loop sleep used between input polls and conditional redraws.
/// @details This remains comfortably above the debounce interval while still
/// sampling short softkey presses much more reliably than the previous 100 ms.
inline constexpr uint32_t kMenuLoopSleepMs = 20U;
inline constexpr int64_t kMicrosecondsPerMinute = 60LL * 1000LL * 1000LL;

} // namespace

/// @brief Chooses the concrete screen saver to run for this activation.
/// @details `Random` is resolved once when the saver starts so a single timeout
/// session remains visually consistent until the user wakes the panel again.
ScreenSaverSelection resolve_runtime_screen_saver(ScreenSaverSelection selection)
{
    if (selection != ScreenSaverSelection::Random)
    {
        return selection;
    }

    switch (std::rand() % 7)
    {
    case 0:
        return ScreenSaverSelection::Life;
    case 1:
        return ScreenSaverSelection::Clock;
    case 2:
        return ScreenSaverSelection::Starfield;
    case 3:
        return ScreenSaverSelection::Matrix;
    case 4:
        return ScreenSaverSelection::Radar;
    case 5:
        return ScreenSaverSelection::Rain;
    default:
        return ScreenSaverSelection::Worms;
    }
}

/// @brief Initializes whichever screen saver is currently selected.
void init_selected_screensaver(ScreenSaverSelection selection, const uint8_t* seed_fb,
                               const TimeStatus& time_status)
{
    (void)time_status;

    switch (selection)
    {
    case ScreenSaverSelection::Life:
        screensaver_life::init(seed_fb);
        break;
    case ScreenSaverSelection::Clock:
        screensaver_clock::init();
        break;
    case ScreenSaverSelection::Starfield:
        screensaver_starfield::init();
        break;
    case ScreenSaverSelection::Matrix:
        screensaver_matrix::init();
        break;
    case ScreenSaverSelection::Radar:
        screensaver_radar::init();
        break;
    case ScreenSaverSelection::Rain:
        screensaver_rain::init();
        break;
    case ScreenSaverSelection::Worms:
        screensaver_worms::init();
        break;
    case ScreenSaverSelection::Random:
        break;
    }
}

/// @brief Renders one frame from the currently selected screen saver.
void render_selected_screensaver(ScreenSaverSelection selection, uint8_t* fb,
                                 const ConsoleState& console_state, LifeFrameStats& life_stats)
{
    switch (selection)
    {
    case ScreenSaverSelection::Life:
        screensaver_life::step_and_render(fb, life_stats);
        break;
    case ScreenSaverSelection::Clock:
        life_stats = {};
        screensaver_clock::step_and_render(fb, console_state.time_status);
        break;
    case ScreenSaverSelection::Starfield:
        life_stats = {};
        screensaver_starfield::step_and_render(fb);
        break;
    case ScreenSaverSelection::Matrix:
        life_stats = {};
        screensaver_matrix::step_and_render(fb);
        break;
    case ScreenSaverSelection::Radar:
        life_stats = {};
        screensaver_radar::step_and_render(fb);
        break;
    case ScreenSaverSelection::Rain:
        life_stats = {};
        screensaver_rain::step_and_render(fb);
        break;
    case ScreenSaverSelection::Worms:
        life_stats = {};
        screensaver_worms::step_and_render(fb);
        break;
    case ScreenSaverSelection::Random:
        break;
    }
}

/// @brief Returns the debug label for the currently active screen saver.
const char* screen_saver_name(ScreenSaverSelection selection)
{
    switch (selection)
    {
    case ScreenSaverSelection::Life:
        return "Life";
    case ScreenSaverSelection::Clock:
        return "Clock";
    case ScreenSaverSelection::Starfield:
        return "Starfield";
    case ScreenSaverSelection::Matrix:
        return "Matrix";
    case ScreenSaverSelection::Radar:
        return "Radar";
    case ScreenSaverSelection::Rain:
        return "Rain";
    case ScreenSaverSelection::Worms:
        return "Worms";
    case ScreenSaverSelection::Random:
        return "Random";
    }

    return "ScreenSaver";
}

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
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, &kEl320RasterProgram);
    const ScreenMode startup_mode = ScreenMode::Menu;
    ScreenMode active_mode = startup_mode;

    uint32_t life_frame_counter = 0;
    absolute_time_t next_life_stats = make_timeout_time_ms(1000);
    absolute_time_t last_user_activity = get_absolute_time();
    ScreenSaverSelection active_screen_saver = ScreenSaverSelection::Life;
    const float current_clkdiv = kPanel.clkdiv;

    // Initialize the state-producing subsystems before the first frame is drawn
    // so the initial UI reflects real status rather than placeholder defaults.
    config_manager::init();
    console_controller::init();
    console_controller::apply_runtime_config(config_manager::settings());
    time_manager::init();
    wifi_manager::init();
    home_assistant_manager::init();
    mqtt_manager::init();
    web_config_server::init();
    console_controller::set_wifi_status(wifi_manager::status());
    console_controller::set_time_status(time_manager::status());
    console_controller::set_home_assistant_status(home_assistant_manager::status());
    console_controller::set_mqtt_status(mqtt_manager::status());

    // Render one complete back buffer before scanout starts so the panel never
    // shows an uninitialized frame during bring-up.
    if (active_mode == ScreenMode::LifeScreensaver)
    {
        active_screen_saver =
            resolve_runtime_screen_saver(console_controller::state().screen_saver_selection);
        init_selected_screensaver(active_screen_saver, framebuffer::front(),
                                  console_controller::state().time_status);
        LifeFrameStats stats{};
        render_selected_screensaver(active_screen_saver, framebuffer::back(),
                                    console_controller::state(), stats);
    }
    else if (active_mode == ScreenMode::Calibration)
    {
        screens::draw_calibration_screen(framebuffer::back());
    }
    else if (active_mode == ScreenMode::Menu)
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
    if (active_mode == ScreenMode::Calibration)
    {
        screens::draw_calibration_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    }
    else if (active_mode == ScreenMode::DemoPattern)
    {
        screens::draw_demo_screen(framebuffer::back());
        framebuffer::swap();
        display::present(framebuffer::front());
    }
    else if (active_mode == ScreenMode::Menu)
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
        const bool any_key_activity =
            (event.type == ButtonEventType::Pressed) ||
            (input::keypad_monitor_status().active_count > 0);
        bool console_changed = false;

        if (active_mode == ScreenMode::LifeScreensaver && any_key_activity)
        {
            active_mode = ScreenMode::Menu;
            last_user_activity = get_absolute_time();
            next_life_stats = make_timeout_time_ms(1000);
            console_changed = true;
        }
        else if (active_mode != ScreenMode::LifeScreensaver)
        {
            if (any_key_activity)
            {
                last_user_activity = get_absolute_time();
            }

            console_changed = console_controller::handle_button_event(event);
        }

        // Keep the integration stack advancing every loop so network-driven UI
        // state stays fresh even when the user is not pressing keys.
        console_changed = wifi_manager::update() || console_changed;
        console_changed = time_manager::update() || console_changed;
        console_changed = home_assistant_manager::update(wifi_manager::status()) || console_changed;
        console_changed =
            mqtt_manager::update(wifi_manager::status(), home_assistant_manager::status(),
                                 time_manager::status()) ||
            console_changed;
        console_changed = web_config_server::update(wifi_manager::status()) || console_changed;

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
        console_changed = console_controller::consume_redraw_request() || console_changed;

        const uint16_t screen_saver_timeout_minutes =
            console_controller::state().screen_saver_timeout_minutes;
        if (active_mode == ScreenMode::Menu && screen_saver_timeout_minutes > 0)
        {
            const int64_t idle_us = absolute_time_diff_us(last_user_activity, get_absolute_time());
            const int64_t timeout_us =
                static_cast<int64_t>(screen_saver_timeout_minutes) * kMicrosecondsPerMinute;
            if (idle_us >= timeout_us)
            {
                active_mode = ScreenMode::LifeScreensaver;
                active_screen_saver =
                    resolve_runtime_screen_saver(console_controller::state().screen_saver_selection);
                init_selected_screensaver(active_screen_saver, framebuffer::front(),
                                          console_controller::state().time_status);
                next_life_stats = make_timeout_time_ms(1000);
            }
        }

        if (active_mode == ScreenMode::LifeScreensaver)
        {
            // The screensaver owns the whole frame, so it renders every loop and
            // separately tracks simulation, draw, and present timing for tuning.
            LifeFrameStats stats{};
            render_selected_screensaver(active_screen_saver, framebuffer::back(),
                                        console_controller::state(), stats);

            const absolute_time_t present_start = get_absolute_time();
            framebuffer::swap();
            display::present(framebuffer::front());
            stats.present_us = absolute_time_diff_us(present_start, get_absolute_time());

            ++life_frame_counter;

            if (absolute_time_diff_us(get_absolute_time(), next_life_stats) <= 0)
            {
                PERIODIC_LOG("%s fps=%lu sim=%lldus draw=%lldus present=%lldus\n",
                             screen_saver_name(active_screen_saver),
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
            if (console_changed)
            {
                screens::draw_menu_screen(framebuffer::back(), console_controller::state());
                framebuffer::swap();
                display::present(framebuffer::front());
            }
            sleep_ms(kMenuLoopSleepMs);
        }
    }

    return 0;
}
