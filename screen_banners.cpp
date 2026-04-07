#include "screen_banners.h"

#include "framebuffer.h"
#include "panel_config.h"

namespace screen_banners {

namespace {

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

/// @brief Gap between the header network icon and the time text.
constexpr int kHeaderStatusGap = 2;

/// @brief Width reserved for the compact internet status icon.
constexpr int kHeaderStatusIconWidth = 16;

/// @brief Y position of the header status icon.
constexpr int kHeaderStatusIconY = 11;

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

    if (reachable) {
        return;
    }

    if (pending) {
        framebuffer::draw_line(fb, x + 12, y + 3, x + 15, y + 3, kIconOn);
        framebuffer::draw_line(fb, x + 13, y + 2, x + 13, y + 5, kIconOn);
        return;
    }

    framebuffer::draw_line(fb, x + 1, y + 10, x + 10, y + 1, kIconOn);
    framebuffer::draw_line(fb, x + 1, y + 11, x + 11, y + 1, kIconOn);
}

}  // namespace

void draw_header_banner(uint8_t* fb, const ConsoleState& console_state, const char* title)
{
    const char* time_text = console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--";
    const int title_width = framebuffer::measure_text(title, fonts::FontFace::FontTitle8x12, 1);
    const int time_width = framebuffer::measure_text(time_text, fonts::FontFace::Font8x12, 1);
    const int time_x = UI_WIDTH - kHeaderStatusRightInset - time_width;
    const int icon_x = time_x - kHeaderStatusGap - kHeaderStatusIconWidth;

    framebuffer::draw_hline(fb, 0, UI_WIDTH - 1, kHeaderBannerY + kBannerHeight - 1, true);
    framebuffer::draw_text(
        fb, (UI_WIDTH / 2) - (title_width / 2), kHeaderTextY, title, true, fonts::FontFace::FontTitle8x12, 1);
    draw_internet_icon(fb,
                       icon_x,
                       kHeaderStatusIconY,
                       console_state.wifi_status.internet_reachable,
                       console_state.wifi_status.internet_probe_pending);
    framebuffer::draw_text(fb, time_x, kHeaderTimeTextY, time_text, true, fonts::FontFace::Font8x12, 1);
}

void draw_standard_banners(uint8_t* fb, const ConsoleState& console_state, const char* title)
{
    draw_header_banner(fb, console_state, title);
}

}  // namespace screen_banners
