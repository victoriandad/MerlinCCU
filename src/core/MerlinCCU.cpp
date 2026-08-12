#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <malloc.h>

#include "hardware/clocks.h"
#include "hardware/pio.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

#include "air_traffic_manager.h"
#include "config_manager.h"
#include "console_controller.h"
#include "debug_logging.h"
#include "display.h"
#include "el320_raster.pio.h"
#include "environment_sensor_manager.h"
#include "framebuffer.h"
#include "home_assistant_manager.h"
#include "input.h"
#include "mqtt_manager.h"
#include "panel_config.h"
#include "pinter_store.h"
#include "screens.h"
#include "screensaver_clock.h"
#include "screensaver_life.h"
#include "screensaver_matrix.h"
#include "screensaver_radar.h"
#include "screensaver_rain.h"
#include "screensaver_starfield.h"
#include "screensaver_worms.h"
#include "share_price_manager.h"
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
inline constexpr int64_t kLoopLoadWindowUs = 1000LL * 1000LL;

/// @brief Hardware watchdog window for the main loop.
/// @details Normal iterations complete in tens of milliseconds (20ms menu sleep
/// plus draw/update work), so this is two to three orders of magnitude above
/// legitimate variance. Every subsystem the loop calls into is polled/async
/// rather than blocking -- wifi_manager's connect call was the one exception
/// and has been reworked to be async for exactly this reason. 8000ms is also
/// the RP2040 hardware ceiling, kept here even though this target is RP2350
/// (whose ceiling is higher) so the value stays portable.
inline constexpr uint32_t kWatchdogTimeoutMs = 8000U;

/// @brief Accumulates foreground loop timing for a bounded load estimate.
/// @details This deliberately measures the MerlinCCU main loop rather than
/// claiming full CPU usage. Background Wi-Fi callbacks and DMA/PIO scanout are
/// outside this foreground ratio.
struct MainLoopLoadAccumulator
{
    absolute_time_t window_start;
    uint64_t active_us;
    uint64_t sleep_us;
};

/// @brief Returns the generated PIO program symbol for the EL320 raster driver.
/// @details Different Pico SDK/pioasm combinations generate either
/// `kEl320RasterProgram` (newer style) or `el320_raster_program` (legacy style).
/// This helper keeps firmware source stable across both environments.
const pio_program* el320_raster_program_ptr()
{
#if defined(EL320_RASTER_WRAP_TARGET)
    return &kEl320RasterProgram;
#else
    return &el320_raster_program;
#endif
}

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
namespace
{

// core0's entire stack is a fixed 2048-byte region (RP2350 default linker
// script, .stack_dummy section) -- separate from the .bss/.data RAM region,
// with no configured override. These bound it.
extern "C" uint32_t __StackBottom;
extern "C" uint32_t __StackTop;

constexpr uint32_t kStackFillPattern = 0xEEEEEEEEU;
// Bytes below the stack pointer at fill time are skipped as a safety
// margin, so early crt0/main-prologue usage is never touched.
constexpr uint32_t kStackFillSafetyMarginBytes = 64U;

/// @brief Fills currently-unused stack with a known pattern for a later
/// high-water-mark scan. Must run as early as possible in main(), before
/// meaningful call depth accumulates, and never touches memory at or above
/// the stack pointer at the time it runs.
void fill_stack_canary()
{
    uint32_t sp = 0;
    asm volatile("mov %0, sp" : "=r"(sp));
    auto* begin = &__StackBottom;
    auto* end = reinterpret_cast<uint32_t*>(sp - kStackFillSafetyMarginBytes);
    for (uint32_t* p = begin; p < end; ++p)
    {
        *p = kStackFillPattern;
    }
}

/// @brief Returns the fewest bytes of stack ever left untouched since boot.
/// @details Scans from the bottom of the stack region upward for the first
/// word that no longer holds the fill pattern -- the deepest point any call
/// chain has reached. A small result means a real, measured near-miss, not a
/// guess; see docs/development.md-style diagnostics elsewhere in this file.
uint32_t stack_high_water_free_bytes()
{
    const auto* begin = &__StackBottom;
    const auto* end = &__StackTop;
    const uint32_t* p = begin;
    while (p < end && *p == kStackFillPattern)
    {
        ++p;
    }
    return static_cast<uint32_t>(reinterpret_cast<const uint8_t*>(p) -
                                 reinterpret_cast<const uint8_t*>(begin));
}

} // namespace

int main()
{
    fill_stack_canary();

    constexpr uint32_t kBootConsoleDelayMs = 10000U;

    // Bring up stdio first so any early hardware or config failures are visible
    // on the debug console before the display path is running.
    stdio_init_all();
    sleep_ms(kBootConsoleDelayMs);

    std::printf("MerlinCCU start. clkdiv=%.2f row_offset=%d hblank=(%d,%d)\n", kPanel.clkdiv,
                kPanel.native_row_offset, kPanel.h_pre_blank, kPanel.h_post_blank);
    if (watchdog_enable_caused_reboot())
    {
        std::printf("Last reboot was caused by a watchdog timeout (main loop hang)\n");
    }
    // Prints the real configured system clock so panel VCLK/frame-rate
    // assumptions in docs/greyscale-investigation.md can be checked against
    // hardware instead of the SDK-documented default.
    std::printf("Sys clock: %lu Hz\n", static_cast<unsigned long>(clock_get_hz(clk_sys)));
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));

    PIO pio = pio0;
    const uint sm = 0;
    const uint offset = pio_add_program(pio, el320_raster_program_ptr());
    const ScreenMode startup_mode = ScreenMode::Menu;
    ScreenMode active_mode = startup_mode;

    uint32_t life_frame_counter = 0;
    absolute_time_t next_life_stats = make_timeout_time_ms(1000);
    constexpr uint32_t kHeartbeatIntervalMs = 30000U;
    absolute_time_t next_heartbeat = make_timeout_time_ms(kHeartbeatIntervalMs);
    absolute_time_t last_user_activity = get_absolute_time();
    ScreenSaverSelection active_screen_saver = ScreenSaverSelection::Life;
    const float current_clkdiv = kPanel.clkdiv;
    MainLoopLoadAccumulator loop_load = {
        .window_start = get_absolute_time(),
        .active_us = 0U,
        .sleep_us = 0U,
    };
    uint32_t last_sampled_frame_count = display::frame_count();

    // Initialize the state-producing subsystems before the first frame is drawn
    // so the initial UI reflects real status rather than placeholder defaults.
    config_manager::init();
    console_controller::init();
    console_controller::apply_runtime_config(config_manager::settings());

    std::array<PinterStatus, kPinterCount> persisted_pinters = {};
    if (pinter_store::load(&persisted_pinters))
    {
        console_controller::apply_persisted_pinters(persisted_pinters);
    }

    time_manager::init();
    wifi_manager::init();
    home_assistant_manager::init();
    mqtt_manager::init();
    share_price_manager::init();
    air_traffic_manager::init();
    environment_sensor_manager::init();
    web_config_server::init();
    console_controller::set_wifi_status(wifi_manager::status());
    console_controller::set_time_status(time_manager::status());
    console_controller::set_home_assistant_status(home_assistant_manager::status());
    console_controller::set_mqtt_status(mqtt_manager::status());
    console_controller::set_share_market_status(share_price_manager::status());
    console_controller::set_air_traffic_status(air_traffic_manager::status());
    console_controller::set_environment_sensor_status(environment_sensor_manager::status());

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

    // Arm the watchdog only once one-time boot init is done: everything before
    // this point (the 10s console-attach delay, radio bring-up, flash config
    // reads) is a single straight-line cost that does not repeat, whereas the
    // loop below is expected to cycle in tens of milliseconds forever.
    // pause_on_debug keeps a debugger session from tripping it.
    watchdog_enable(kWatchdogTimeoutMs, true);

    while (true)
    {
        watchdog_update();

        // Below, every potentially slow per-iteration call is individually
        // timed and logged if it runs long: any one of these blocking for
        // even a couple hundred ms delays everything after it in this same
        // iteration, including web_config_server's handling of an
        // already-pending web-preview request -- exactly the kind of
        // multi-second UI stall that is otherwise very hard to pin down
        // after the fact without a timestamp trail.
        constexpr int64_t kSlowUpdateThresholdUs = 100000; // 100ms
        const auto log_if_slow = [](const char* name, absolute_time_t start)
        {
            const int64_t elapsed_us = absolute_time_diff_us(start, get_absolute_time());
            if (elapsed_us >= kSlowUpdateThresholdUs)
            {
                std::printf("Slow update: %s took %lldus (possible UI stall cause)\n", name,
                            static_cast<long long>(elapsed_us));
            }
        };

        // Flush any deferred flash saves (settings, Pinter state) queued by
        // the previous iteration here, at the top of the loop and outside any
        // network call stack: the write disables all interrupts for its
        // duration, and a full sleep has already elapsed since
        // web_config_server::update() last ran, so there is no in-flight
        // request or response this could stall.
        absolute_time_t update_start = get_absolute_time();
        config_manager::flush_pending_save();
        log_if_slow("config_manager::flush_pending_save", update_start);

        update_start = get_absolute_time();
        console_controller::flush_pending_pinter_save();
        log_if_slow("console_controller::flush_pending_pinter_save", update_start);

        const absolute_time_t loop_start = get_absolute_time();
        // Poll hardware first, then let the controller translate those raw
        // events into menu/state changes before the integrations update.
        const ButtonEvent event = input::poll_buttons();
        input::handle_button_event(event);
        const bool web_user_activity = console_controller::consume_user_activity_request();
        const bool any_key_activity =
            (event.type == ButtonEventType::Pressed) || web_user_activity ||
            (input::keypad_monitor_status().active_count > 0);
        bool console_changed = false;

        if (active_mode == ScreenMode::LifeScreensaver && any_key_activity)
        {
            active_mode = ScreenMode::Menu;
            last_user_activity = get_absolute_time();
            next_life_stats = make_timeout_time_ms(1000);
            console_changed = true;
            if (event.type == ButtonEventType::Pressed && event.id == ButtonId::Alert)
            {
                // ALERT is an operator-facing hard key, so preserve its action
                // even when the panel was asleep. Generic keys still behave as
                // wake-only input to avoid accidental menu navigation.
                console_changed = console_controller::handle_button_event(event) || console_changed;
            }
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
        update_start = get_absolute_time();
        console_changed = wifi_manager::update() || console_changed;
        log_if_slow("wifi_manager::update", update_start);

        update_start = get_absolute_time();
        console_changed = time_manager::update() || console_changed;
        log_if_slow("time_manager::update", update_start);

        update_start = get_absolute_time();
        console_changed = home_assistant_manager::update(wifi_manager::status()) || console_changed;
        log_if_slow("home_assistant_manager::update", update_start);

        update_start = get_absolute_time();
        console_changed =
            mqtt_manager::update(wifi_manager::status(), home_assistant_manager::status(),
                                 time_manager::status()) ||
            console_changed;
        log_if_slow("mqtt_manager::update", update_start);

        const ConsoleState& console_state = console_controller::state();
        const bool on_shares_landing = console_state.active_page == MenuPage::Shares;
        const bool on_share_detail = console_state.active_page == MenuPage::ShareDetail;
        // Key handling should always remain snappy, so share-network work backs
        // off while keys are actively being pressed or held.
        const bool share_fetch_enabled =
            (on_shares_landing || on_share_detail) && !any_key_activity;
        const SharePeriod share_fetch_period =
            on_shares_landing ? SharePeriod::Today : console_state.share_period;
        // Same page-isolation rule as shares: only poll the ADS-B feed while
        // its own page is the one actually visible.
        const bool air_traffic_fetch_enabled =
            console_state.active_page == MenuPage::AirTraffic && !any_key_activity;
        // Local web control should stay responsive even when share data refresh
        // is active, so service the web server before optional market fetches.
        update_start = get_absolute_time();
        console_changed = web_config_server::update(wifi_manager::status()) || console_changed;
        log_if_slow("web_config_server::update", update_start);

        update_start = get_absolute_time();
        console_changed =
            share_price_manager::update(wifi_manager::status(), share_fetch_period,
                                        share_fetch_enabled) ||
            console_changed;
        log_if_slow("share_price_manager::update", update_start);

        update_start = get_absolute_time();
        console_changed =
            air_traffic_manager::update(wifi_manager::status(), air_traffic_fetch_enabled) ||
            console_changed;
        log_if_slow("air_traffic_manager::update", update_start);

        update_start = get_absolute_time();
        environment_sensor_manager::update();
        log_if_slow("environment_sensor_manager::update", update_start);

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
        console_changed =
            console_controller::set_share_market_status(share_price_manager::status()) ||
            console_changed;
        console_changed =
            console_controller::set_air_traffic_status(air_traffic_manager::status()) ||
            console_changed;
        console_changed = console_controller::set_environment_sensor_status(
                              environment_sensor_manager::status()) ||
                          console_changed;
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

            const absolute_time_t sleep_start = get_absolute_time();
            loop_load.active_us +=
                static_cast<uint64_t>(absolute_time_diff_us(loop_start, sleep_start));
            sleep_ms(75);
            loop_load.sleep_us +=
                static_cast<uint64_t>(absolute_time_diff_us(sleep_start, get_absolute_time()));
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
            const absolute_time_t sleep_start = get_absolute_time();
            loop_load.active_us +=
                static_cast<uint64_t>(absolute_time_diff_us(loop_start, sleep_start));
            sleep_ms(kMenuLoopSleepMs);
            loop_load.sleep_us +=
                static_cast<uint64_t>(absolute_time_diff_us(sleep_start, get_absolute_time()));
        }

        const absolute_time_t loop_end = get_absolute_time();
        const int64_t window_us = absolute_time_diff_us(loop_load.window_start, loop_end);
        if (window_us >= kLoopLoadWindowUs)
        {
            // Diagnostic-only, deliberately always-on (not PERIODIC_LOG,
            // which compiles out by default): reports the worst-case stack
            // headroom seen so far against the core0 stack. If this heads
            // toward zero, a hard lockup from stack overflow (as opposed to
            // a software dead-end) is the explanation, not a guess. Rate
            // limited well below the 1s load/heap sampling window below so
            // it reads as an occasional liveness heartbeat, not per-second
            // spam indistinguishable from other log lines.
            if (absolute_time_diff_us(get_absolute_time(), next_heartbeat) <= 0)
            {
                std::printf("HEARTBEAT: alive, stack free (worst case since boot) = %lu bytes\n",
                            static_cast<unsigned long>(stack_high_water_free_bytes()));
                next_heartbeat = make_timeout_time_ms(kHeartbeatIntervalMs);
            }

            const uint64_t total_us = loop_load.active_us + loop_load.sleep_us;
            const uint8_t load_percent =
                total_us == 0U ? 0U
                               : static_cast<uint8_t>(
                                     std::min<uint64_t>((loop_load.active_us * 100U) / total_us,
                                                        100U));
            const MainLoopLoadStatus status = {
                .valid = total_us > 0U,
                .load_percent = load_percent,
                .sample_ms = static_cast<uint16_t>(
                    std::min<int64_t>((window_us + 500LL) / 1000LL,
                                      static_cast<int64_t>(UINT16_MAX))),
            };
            const bool loop_load_changed = console_controller::set_main_loop_load_status(status);
            const struct mallinfo heap_info = mallinfo();
            const HeapStatus heap_status = {
                .valid = true,
                .used_bytes = static_cast<uint32_t>(heap_info.uordblks),
                .arena_bytes = static_cast<uint32_t>(heap_info.arena),
            };
            const bool heap_status_changed = console_controller::set_heap_status(heap_status);
            const StackStatus stack_status = {
                .valid = true,
                .free_bytes = stack_high_water_free_bytes(),
            };
            const bool stack_status_changed = console_controller::set_stack_status(stack_status);
            if ((loop_load_changed || heap_status_changed || stack_status_changed) &&
                console_controller::state().active_page == MenuPage::StatusResources &&
                active_mode != ScreenMode::LifeScreensaver)
            {
                console_controller::request_redraw();
            }

            // Measures actual panel scanout timing on hardware rather than
            // assuming it — see docs/greyscale-investigation.md's open
            // questions and docs/multicore-raster-regen-design.md.
            const uint32_t current_frame_count = display::frame_count();
            const uint32_t frame_delta = current_frame_count - last_sampled_frame_count;
            last_sampled_frame_count = current_frame_count;
            const DisplayTimingStatus timing_status = {
                .valid = window_us > 0,
                .frame_rate_hz = static_cast<uint16_t>(std::min<uint64_t>(
                    (static_cast<uint64_t>(frame_delta) * 1000000ULL + static_cast<uint64_t>(window_us) / 2ULL) /
                        static_cast<uint64_t>(window_us),
                    UINT16_MAX)),
                .last_rebuild_us = display::last_rebuild_us(),
            };
            const bool timing_changed = console_controller::set_display_timing_status(timing_status);
            if (timing_changed &&
                console_controller::state().active_page == MenuPage::StatusResources &&
                active_mode != ScreenMode::LifeScreensaver)
            {
                console_controller::request_redraw();
            }

            loop_load = {
                .window_start = loop_end,
                .active_us = 0U,
                .sleep_us = 0U,
            };
        }
    }

    return 0;
}
