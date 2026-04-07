#include "screens.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screen_banners.h"

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
    case WifiConnectionState::Scanning:
        return "SCAN";
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

const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state) {
    case HomeAssistantConnectionState::Disabled:
        return "DISABLED";
    case HomeAssistantConnectionState::Unconfigured:
        return "UNCONFIG";
    case HomeAssistantConnectionState::WaitingForWifi:
        return "WAIT WIFI";
    case HomeAssistantConnectionState::Resolving:
        return "RESOLVE";
    case HomeAssistantConnectionState::Connecting:
        return "CONNECT";
    case HomeAssistantConnectionState::Authorizing:
        return "AUTH";
    case HomeAssistantConnectionState::Connected:
        return "UP";
    case HomeAssistantConnectionState::Unauthorized:
        return "TOKEN";
    case HomeAssistantConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

const char* mqtt_state_text(MqttConnectionState state)
{
    switch (state) {
    case MqttConnectionState::Disabled:
        return "DISABLED";
    case MqttConnectionState::Unconfigured:
        return "UNCONFIG";
    case MqttConnectionState::WaitingForWifi:
        return "WAIT WIFI";
    case MqttConnectionState::Resolving:
        return "RESOLVE";
    case MqttConnectionState::Connecting:
        return "CONNECT";
    case MqttConnectionState::Connected:
        return "UP";
    case MqttConnectionState::AuthFailed:
        return "AUTH";
    case MqttConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

const char* menu_page_title(MenuPage page)
{
    switch (page) {
    case MenuPage::Home:
        return "MENU";
    case MenuPage::Status:
        return "STATUS";
    case MenuPage::Settings:
        return "SETTINGS";
    }

    return "MENU";
}

int text_width(const char* text,
               fonts::FontFace font = fonts::FontFace::Font5x7,
               int spacing = 1)
{
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    return framebuffer::measure_text(text, font, spacing);
}

void draw_centered_text(uint8_t* fb,
                        int center_x,
                        int y,
                        const char* text,
                        bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7,
                        int spacing = 1)
{
    framebuffer::draw_text(fb, center_x - (text_width(text, font, spacing) / 2), y, text, on, font, spacing);
}

void draw_softkey_label(uint8_t* fb, int x, int y, int w, const SoftKeyAction& action, bool left_side)
{
    framebuffer::draw_rect(fb, x, y, w, 18, true);

    if (action.enabled) {
        framebuffer::fill_rect(fb, x + 1, y + 1, w - 2, 16, true);
    }

    if (action.label == nullptr || action.label[0] == '\0') {
        return;
    }

    const int label_width = text_width(action.label);
    const int text_x = left_side ? (x + 4) : (x + w - 4 - label_width);
    framebuffer::draw_text(fb, text_x, y + 5, action.label, !action.enabled, 1, 1);
}

void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kLabelWidth = 34;
    constexpr int kTopY = 50;
    constexpr int kPitch = 40;

    for (int i = 0; i < 5; ++i) {
        draw_softkey_label(fb, 2, kTopY + (i * kPitch), kLabelWidth, console_state.softkeys[i], true);
        draw_softkey_label(fb,
                           UI_WIDTH - kLabelWidth - 2,
                           kTopY + (i * kPitch),
                           kLabelWidth,
                           console_state.softkeys[i + 5],
                           false);
    }
}

void draw_panel_frame(uint8_t* fb)
{
    framebuffer::draw_rect(fb, 40, 40, UI_WIDTH - 80, 206, true);
    framebuffer::draw_rect(fb, 46, 46, UI_WIDTH - 92, 194, true);
}

void draw_home_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "MENU", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 58, 88, "L1 STATUS PAGE", true, 1, 1);
    framebuffer::draw_text(fb, 58, 104, "L2 SETTINGS PAGE", true, 1, 1);

    framebuffer::draw_text(fb, 58, 126, "Font samples upper/lower", true, 1, 1);
    framebuffer::draw_text(fb, 58, 142, "Status", true, fonts::FontFace::FontTitle8x12, 1);
    framebuffer::draw_text(fb, 58, 156, "Settings", true, fonts::FontFace::FontTitle8x12, 1);
    framebuffer::draw_text(fb, 58, 170, "Alert test", true, fonts::FontFace::FontTitle8x12, 1);
    framebuffer::draw_text(fb, 58, 184, "panel wifi abcxyz", true, fonts::FontFace::Font8x12, 1);

    framebuffer::draw_text(fb, 58, 206, "ALERT", true, 1, 1);
    framebuffer::draw_text(fb, 124, 206, alert_severity_text(console_state.alert_severity), true, 1, 1);

    framebuffer::draw_text(fb, 58, 222, "TEST", true, 1, 1);
    framebuffer::draw_text(fb, 124, 222, test_state_text(console_state.test_state), true, 1, 1);
}

void draw_status_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "STATUS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 84, "TIME", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           84,
                           console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 100, "WIFI", true, 1, 1);
    framebuffer::draw_text(fb, 116, 100, wifi_state_text(console_state.wifi_status.state), true, 1, 1);

    framebuffer::draw_text(fb, 54, 116, "SSID", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           116,
                           console_state.wifi_status.credentials_present ? console_state.wifi_status.ssid.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 132, "IP", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           132,
                           console_state.wifi_status.ip_address[0] ? console_state.wifi_status.ip_address.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 148, "HA", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           148,
                           home_assistant_state_text(console_state.home_assistant_status.state),
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 164, "HOST", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           164,
                           console_state.home_assistant_status.host[0] ? console_state.home_assistant_status.host.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 180, "HTTP", true, 1, 1);
    if (console_state.home_assistant_status.last_http_status > 0) {
        char http_text[8] = {};
        std::snprintf(http_text, sizeof(http_text), "%d", console_state.home_assistant_status.last_http_status);
        framebuffer::draw_text(fb, 116, 180, http_text, true, 1, 1);
    } else {
        framebuffer::draw_text(fb, 116, 180, "-", true, 1, 1);
    }

    framebuffer::draw_text(fb, 54, 196, "ENT", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           196,
                           console_state.home_assistant_status.tracked_entity_state[0]
                               ? console_state.home_assistant_status.tracked_entity_state.data()
                               : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 212, "MQTT", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           212,
                           mqtt_state_text(console_state.mqtt_status.state),
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 228, "DISC", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           228,
                           console_state.mqtt_status.discovery_published ? "READY" : "-",
                           true,
                           1,
                           1);
}

void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "SETTINGS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 86, "RIGHT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 102, "ALERT  LTRS  TEST", true, 1, 1);
    framebuffer::draw_text(fb, 54, 118, "PANEL + PANEL -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 146, "LEFT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 162, "HOME  STATUS  RESET", true, 1, 1);
    framebuffer::draw_text(fb, 54, 178, "KEYS + KEYS -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 206, "LTRS", true, 1, 1);
    framebuffer::draw_text(fb, 116, 206, letter_mode_text(console_state.letter_mode), true, 1, 1);

    framebuffer::draw_text(fb, 54, 222, "ALERT", true, 1, 1);
    framebuffer::draw_text(fb, 116, 222, alert_severity_text(console_state.alert_severity), true, 1, 1);
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

/// @brief Draws the active menu page and contextual softkey labels.
void draw_menu_screen(uint8_t* fb, const ConsoleState& console_state)
{
    framebuffer::clear(fb, false);

    screen_banners::draw_standard_banners(fb, console_state, menu_page_title(console_state.active_page));
    draw_softkeys(fb, console_state);

    switch (console_state.active_page) {
    case MenuPage::Home:
        draw_home_page(fb, console_state);
        break;
    case MenuPage::Status:
        draw_status_page(fb, console_state);
        break;
    case MenuPage::Settings:
        draw_settings_page(fb, console_state);
        break;
    }
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
