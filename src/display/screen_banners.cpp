#include "screen_banners.h"

#include "config_manager.h"
#include "framebuffer.h"
#include "panel_config.h"

namespace screen_banners
{

namespace
{

/// @brief Shared banner height in UI pixels.
constexpr int kBannerHeight = 18;

/// @brief Y position of the header banner.
constexpr int kHeaderBannerY = 8;

/// @brief Y position used for the header title text.
constexpr int kHeaderTextY = 10;

/// @brief Y position used for the larger header time text.
constexpr int kHeaderTimeTextY = 10;

/// @brief Right inset used by the compact header status cluster.
constexpr int kHeaderStatusRightInset = 2;

/// @brief Left inset used by the compact LTRS input-mode indicator.
constexpr int kHeaderInputModeLeftInset = 2;

/// @brief Gap between the header network icon and the time text.
constexpr int kHeaderStatusGap = 2;

/// @brief Gap between the Home Assistant and internet icons.
constexpr int kHeaderStatusIconGap = 2;

/// @brief Width reserved for the compact internet status icon.
constexpr int kHeaderStatusIconWidth = 16;

/// @brief Width reserved for the compact Home Assistant status icon.
constexpr int kHeaderHomeAssistantIconWidth = 12;

/// @brief Width reserved for the compact LTRS input-mode indicator.
constexpr int kHeaderInputModeIconWidth = 23;

/// @brief Height reserved for the compact LTRS input-mode indicator.
constexpr int kHeaderInputModeIconHeight = 11;

/// @brief Y position of the header status icon.
constexpr int kHeaderStatusIconY = 11;

/// @brief Returns the banner glyph for the current LTRS interpretation mode.
const char* input_mode_icon_text(LetterMode mode)
{
    switch (mode)
    {
    case LetterMode::UpperCase:
        return "ABC";
    case LetterMode::LowerCase:
        return "abc";
    case LetterMode::Numbers:
        return "123";
    }

    return "ABC";
}

/// @brief Draws the compact boxed LTRS input-mode indicator.
void draw_input_mode_icon(uint8_t* fb, int x, int y, LetterMode mode)
{
    constexpr bool kIconOn = true;
    framebuffer::draw_rect(fb, x, y, kHeaderInputModeIconWidth, kHeaderInputModeIconHeight,
                           kIconOn);
    framebuffer::draw_text(fb, x + 3, y + 2, input_mode_icon_text(mode), kIconOn,
                           fonts::FontFace::Font5x7, 1);
}

/// @brief Draws the small Wi-Fi symbol used inside the compact header status icon.
void draw_wifi_symbol(uint8_t* fb, int x, int y, bool on)
{
    framebuffer::draw_line(fb, x + 1, y + 1, x + 9, y + 1, on);
    framebuffer::draw_line(fb, x + 0, y + 2, x + 10, y + 2, on);

    framebuffer::draw_line(fb, x + 2, y + 4, x + 8, y + 4, on);
    framebuffer::draw_line(fb, x + 3, y + 5, x + 7, y + 5, on);

    framebuffer::draw_line(fb, x + 4, y + 7, x + 6, y + 7, on);
    framebuffer::fill_rect(fb, x + 4, y + 9, 3, 2, on);
}

/// @brief Draws the compact header internet status icon.
/// @param reachable True when the network probe has confirmed internet access.
/// @param pending True while the probe result is still pending.
void draw_internet_icon(uint8_t* fb, int x, int y, bool reachable, bool pending)
{
    constexpr bool kIconOn = true;
    draw_wifi_symbol(fb, x, y, kIconOn);

    if (reachable)
    {
        return;
    }

    if (pending)
    {
        framebuffer::draw_line(fb, x + 12, y + 3, x + 15, y + 3, kIconOn);
        framebuffer::draw_line(fb, x + 13, y + 2, x + 13, y + 5, kIconOn);
        return;
    }

    framebuffer::draw_line(fb, x + 1, y + 10, x + 10, y + 1, kIconOn);
    framebuffer::draw_line(fb, x + 1, y + 11, x + 11, y + 1, kIconOn);
}

/// @brief Returns true while the Home Assistant client is actively trying to connect.
bool home_assistant_pending(const HomeAssistantStatus& status)
{
    switch (status.state)
    {
    case HomeAssistantConnectionState::WaitingForWifi:
    case HomeAssistantConnectionState::Resolving:
    case HomeAssistantConnectionState::Connecting:
    case HomeAssistantConnectionState::Authorizing:
        return true;
    default:
        return false;
    }
}

/// @brief Returns true when the Home Assistant link is considered up.
bool home_assistant_connected(const HomeAssistantStatus& status)
{
    return status.configured && status.state == HomeAssistantConnectionState::Connected;
}

/// @brief Maps MQTT connectivity into the header Home Assistant icon state.
HomeAssistantConnectionState home_assistant_state_from_mqtt(const MqttStatus& mqtt_status)
{
    switch (mqtt_status.state)
    {
    case MqttConnectionState::Connected:
        return HomeAssistantConnectionState::Connected;
    case MqttConnectionState::Resolving:
    case MqttConnectionState::Connecting:
    case MqttConnectionState::WaitingForWifi:
        return HomeAssistantConnectionState::Connecting;
    case MqttConnectionState::AuthFailed:
        return HomeAssistantConnectionState::Unauthorized;
    case MqttConnectionState::Error:
        return HomeAssistantConnectionState::Error;
    case MqttConnectionState::Disabled:
    case MqttConnectionState::Unconfigured:
        return HomeAssistantConnectionState::Unconfigured;
    }

    return HomeAssistantConnectionState::Unconfigured;
}

/// @brief Draws a small house glyph used for the Home Assistant header icon.
void draw_home_assistant_symbol(uint8_t* fb, int x, int y, bool on)
{
    framebuffer::draw_line(fb, x + 1, y + 4, x + 5, y + 0, on);
    framebuffer::draw_line(fb, x + 5, y + 0, x + 9, y + 4, on);
    framebuffer::draw_hline(fb, x + 2, x + 8, y + 4, on);
    framebuffer::draw_rect(fb, x + 2, y + 4, 7, 6, on);
}

/// @brief Draws the compact header Home Assistant connectivity icon.
void draw_home_assistant_icon(uint8_t* fb, int x, int y, const HomeAssistantStatus& status)
{
    if (!status.configured)
    {
        return;
    }

    constexpr bool kIconOn = true;
    draw_home_assistant_symbol(fb, x, y, kIconOn);

    if (home_assistant_connected(status))
    {
        framebuffer::fill_rect(fb, x + 5, y + 6, 2, 4, kIconOn);
        return;
    }

    if (home_assistant_pending(status))
    {
        framebuffer::draw_line(fb, x + 5, y + 5, x + 5, y + 9, kIconOn);
        framebuffer::draw_line(fb, x + 3, y + 7, x + 7, y + 7, kIconOn);
        return;
    }

    framebuffer::draw_line(fb, x + 1, y + 9, x + 9, y + 1, kIconOn);
    framebuffer::draw_line(fb, x + 1, y + 10, x + 10, y + 1, kIconOn);
}

} // namespace

/// @brief Draws the shared top-of-screen banner for menu pages.
void draw_header_banner(uint8_t* fb, const ConsoleState& console_state, const char* title)
{
    const RuntimeConfig& config = config_manager::settings();
    const bool ha_rest_enabled = config.home_assistant_enabled;
    const bool ha_rest_config_present =
        ha_rest_enabled && config.home_assistant_host[0] != '\0' &&
        config.home_assistant_token[0] != '\0';
    const bool ha_mqtt_enabled = config.mqtt_enabled;
    const bool ha_mqtt_present =
        ha_mqtt_enabled || console_state.mqtt_status.configured ||
        console_state.mqtt_status.discovery_published ||
        console_state.mqtt_status.state == MqttConnectionState::Connected;
    const bool ha_config_present = ha_rest_enabled || ha_mqtt_present;

    // The header packs title, connectivity, and time into one shared strip so
    // every page can spend the rest of the screen on its own content.
    const char* time_text =
        console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--";
    const int title_width = framebuffer::measure_text(title, fonts::FontFace::FontTitle8x12, 1);
    const int time_width = framebuffer::measure_text(time_text, fonts::FontFace::Font8x12, 1);
    const int home_assistant_icon_width = ha_config_present ? kHeaderHomeAssistantIconWidth : 0;
    const int time_x = kUiWidth - kHeaderStatusRightInset - time_width;
    const int icon_x = time_x - kHeaderStatusGap - kHeaderStatusIconWidth;
    const int home_assistant_icon_x =
        icon_x -
        ((home_assistant_icon_width > 0) ? (kHeaderStatusIconGap + home_assistant_icon_width) : 0);

    // Draw the shared frame first so the remaining elements can key off its
    // baseline and right-edge measurements.
    framebuffer::draw_hline(fb, 0, kUiWidth - 1, kHeaderBannerY + kBannerHeight - 1, true);
    draw_input_mode_icon(fb, kHeaderInputModeLeftInset, kHeaderStatusIconY,
                         console_state.letter_mode);
    framebuffer::draw_text(fb, (kUiWidth / 2) - (title_width / 2), kHeaderTextY, title, true,
                           fonts::FontFace::FontTitle8x12, 1);

    // Connectivity icons are clustered next to the clock so they read as one
    // compact status area rather than competing with the centred page title.
    if (home_assistant_icon_width > 0)
    {
        HomeAssistantStatus icon_status = console_state.home_assistant_status;
        icon_status.configured = true;
        if (ha_rest_config_present)
        {
            // REST mode provides the canonical Home Assistant transport state.
            icon_status.state = console_state.home_assistant_status.state;
        }
        else
        {
            // MQTT-only integration still counts as Home Assistant presence.
            icon_status.state = home_assistant_state_from_mqtt(console_state.mqtt_status);
            if (console_state.mqtt_status.discovery_published &&
                icon_status.state != HomeAssistantConnectionState::Connected)
            {
                icon_status.state = HomeAssistantConnectionState::Connected;
            }
        }
        draw_home_assistant_icon(fb, home_assistant_icon_x, kHeaderStatusIconY, icon_status);
    }
    draw_internet_icon(fb, icon_x, kHeaderStatusIconY, console_state.wifi_status.internet_reachable,
                       console_state.wifi_status.internet_probe_pending);
    framebuffer::draw_text(fb, time_x, kHeaderTimeTextY, time_text, true,
                           fonts::FontFace::Font8x12, 1);
}

/// @brief Draws the standard banner set used by the current UI pages.
void draw_standard_banners(uint8_t* fb, const ConsoleState& console_state, const char* title)
{
    // This wrapper exists so more banner variants can be added later without
    // changing every caller that currently wants the standard header.
    draw_header_banner(fb, console_state, title);
}

} // namespace screen_banners
