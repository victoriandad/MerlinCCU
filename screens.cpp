#include "screens.h"

#include <cstddef>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"

namespace screens {

namespace {

const char* letter_mode_text(LetterMode mode)
{
    return (mode == LetterMode::On) ? "ON" : "OFF";
}

const char* alert_severity_text(AlertSeverity severity)
{
    switch (severity) {
    case AlertSeverity::None:
        return "NONE";
    case AlertSeverity::Message:
        return "MSG";
    case AlertSeverity::Warning:
        return "WARN";
    case AlertSeverity::Alert:
        return "ALERT";
    }

    return "?";
}

const char* test_state_text(SystemTestState state)
{
    switch (state) {
    case SystemTestState::Idle:
        return "IDLE";
    case SystemTestState::Running:
        return "RUN";
    case SystemTestState::Passed:
        return "PASS";
    case SystemTestState::Failed:
        return "FAIL";
    }

    return "?";
}

const char* brightness_text(BrightnessLevel level)
{
    switch (level) {
    case BrightnessLevel::Off:
        return "OFF";
    case BrightnessLevel::Low:
        return "LOW";
    case BrightnessLevel::Medium:
        return "MED";
    case BrightnessLevel::High:
        return "HIGH";
    }

    return "?";
}

const char* lamp_mode_text(LampMode mode)
{
    switch (mode) {
    case LampMode::Off:
        return "OFF";
    case LampMode::On:
        return "ON";
    case LampMode::FlashSlow:
        return "F-SLOW";
    case LampMode::FlashFast:
        return "F-FAST";
    }

    return "?";
}

const char* wifi_state_text(WifiConnectionState state)
{
    switch (state) {
    case WifiConnectionState::Disabled:
        return "DISABLED";
    case WifiConnectionState::Unconfigured:
        return "UNCONFIG";
    case WifiConnectionState::Initializing:
        return "INIT";
    case WifiConnectionState::Connecting:
        return "CONNECT";
    case WifiConnectionState::WaitingForIp:
        return "DHCP";
    case WifiConnectionState::Connected:
        return "UP";
    case WifiConnectionState::AuthFailed:
        return "BADAUTH";
    case WifiConnectionState::NoNetwork:
        return "NO NET";
    case WifiConnectionState::ConnectFailed:
        return "FAIL";
    case WifiConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

void draw_softkey_column(uint8_t* fb,
                         int indicator_x,
                         int box_x,
                         int text_x,
                         const SoftKeyAction* actions)
{
    for (int i = 0; i < 5; ++i) {
        const int y = 42 + i * 42;
        const int box_w = 82;

        framebuffer::fill_rect(fb, indicator_x, y + 4, 8, 10, true);
        framebuffer::draw_rect(fb, box_x, y, box_w, 18, true);
        framebuffer::draw_text(fb, text_x, y + 5, actions[i].label, true, 1, 1);
        if (!actions[i].enabled) {
            framebuffer::draw_hline(fb, box_x + 4, box_x + box_w - 5, y + 9, true);
        }
    }
}

}  // namespace

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
void draw_dummy_menu_screen(uint8_t* fb, const ConsoleState& console_state)
{
    framebuffer::clear(fb, false);

    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    framebuffer::draw_rect(fb, 6, 6, UI_WIDTH - 12, UI_HEIGHT - 12, true);

    framebuffer::fill_rect(fb, 8, 8, UI_WIDTH - 16, 18, true);
    framebuffer::draw_text(fb, 14, 14, "MERLIN CCU", false, 1, 1);

    draw_softkey_column(fb, 8, 22, 28, console_state.softkeys.data());
    draw_softkey_column(fb,
                        UI_WIDTH - 16,
                        UI_WIDTH - 104,
                        UI_WIDTH - 98,
                        console_state.softkeys.data() + 5);

    framebuffer::draw_rect(fb, 72, 54, 108, 148, true);
    framebuffer::draw_text(fb, 84, 66, "FRONT PANEL", true, 1, 1);
    framebuffer::draw_text(fb, 84, 86, "LTRS", true, 1, 1);
    framebuffer::draw_text(fb, 130, 86, letter_mode_text(console_state.letter_mode), true, 1, 1);
    framebuffer::draw_text(fb, 84, 102, "ALRT", true, 1, 1);
    framebuffer::draw_text(fb, 130, 102, alert_severity_text(console_state.alert_severity), true, 1, 1);
    framebuffer::draw_text(fb, 84, 118, "TEST", true, 1, 1);
    framebuffer::draw_text(fb, 130, 118, test_state_text(console_state.test_state), true, 1, 1);
    framebuffer::draw_text(fb, 84, 134, "PANEL", true, 1, 1);
    framebuffer::draw_text(fb, 130, 134, brightness_text(console_state.panel_brightness), true, 1, 1);
    framebuffer::draw_text(fb, 84, 150, "KEYS", true, 1, 1);
    framebuffer::draw_text(fb, 130, 150, brightness_text(console_state.key_backlight_brightness), true, 1, 1);
    framebuffer::draw_text(fb, 84, 166, "WIFI", true, 1, 1);
    framebuffer::draw_text(fb, 124, 166, wifi_state_text(console_state.wifi_status.state), true, 1, 1);
    framebuffer::draw_text(fb, 84, 182, "SSID", true, 1, 1);
    framebuffer::draw_text(fb,
                           124,
                           182,
                           console_state.wifi_status.credentials_present ? console_state.wifi_status.ssid.data() : "-",
                           true,
                           1,
                           1);
    framebuffer::draw_text(fb, 84, 198, "IP", true, 1, 1);
    framebuffer::draw_text(fb,
                           124,
                           198,
                           console_state.wifi_status.ip_address[0] ? console_state.wifi_status.ip_address.data() : "-",
                           true,
                           1,
                           1);
    framebuffer::draw_text(fb, 84, 214, "A LAMP", true, 1, 1);
    framebuffer::draw_text(fb,
                           126,
                           214,
                           lamp_mode_text(console_state.lamps[static_cast<size_t>(LampId::AlertLamp)]),
                           true,
                           1,
                           1);
    framebuffer::draw_text(fb, 84, 230, "T LAMP", true, 1, 1);
    framebuffer::draw_text(fb,
                           126,
                           230,
                           lamp_mode_text(console_state.lamps[static_cast<size_t>(LampId::TestLamp)]),
                           true,
                           1,
                           1);

    framebuffer::fill_rect(fb, 8, UI_HEIGHT - 20, UI_WIDTH - 16, 10, true);
    framebuffer::draw_text(fb, 14, UI_HEIGHT - 18, "SOFTKEY DEV HARNESS", false, 1, 1);
    framebuffer::draw_text(fb, UI_WIDTH - 54, UI_HEIGHT - 18, "READY", false, 1, 1);
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
