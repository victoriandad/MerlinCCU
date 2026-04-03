#include "screen_banners.h"

#include "framebuffer.h"
#include "panel_config.h"

namespace screen_banners {

namespace {

/// @brief Shared banner height in UI pixels.
constexpr int kBannerHeight = 18;

/// @brief Shared horizontal inset for the page banners.
constexpr int kBannerInset = 8;

/// @brief Y position of the header banner.
constexpr int kHeaderBannerY = 8;

/// @brief Y position of the footer banner.
constexpr int kFooterBannerY = UI_HEIGHT - 26;

/// @brief X position used for the header title text.
constexpr int kHeaderTextX = 14;

/// @brief Y position used for the header title text.
constexpr int kHeaderTextY = 14;

/// @brief X position used for the footer left-hand text.
constexpr int kFooterTextX = 14;

/// @brief Y position used for the footer left-hand text.
constexpr int kFooterTextY = UI_HEIGHT - 20;

/// @brief Draws the small Wi-Fi symbol used inside the footer status icon.
void draw_wifi_symbol(uint8_t* fb, int x, int y, bool on)
{
    framebuffer::draw_line(fb, x + 1, y + 1, x + 9, y + 1, on);
    framebuffer::draw_line(fb, x + 0, y + 2, x + 10, y + 2, on);

    framebuffer::draw_line(fb, x + 2, y + 4, x + 8, y + 4, on);
    framebuffer::draw_line(fb, x + 3, y + 5, x + 7, y + 5, on);

    framebuffer::draw_line(fb, x + 4, y + 7, x + 6, y + 7, on);
    framebuffer::fill_rect(fb, x + 4, y + 9, 3, 2, on);
}

/// @brief Draws the footer internet status icon.
/// @param reachable True when the network probe has confirmed internet access.
/// @param pending True while the probe result is still pending.
void draw_internet_icon(uint8_t* fb, int x, int y, bool reachable, bool pending)
{
    constexpr bool kIconOn = false;
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

void draw_header_banner(uint8_t* fb, const char* title)
{
    framebuffer::fill_rect(fb, kBannerInset, kHeaderBannerY, UI_WIDTH - (kBannerInset * 2), kBannerHeight, true);
    framebuffer::draw_text(fb, kHeaderTextX, kHeaderTextY, title, false, 1, 1);
}

void draw_footer_banner(uint8_t* fb, const ConsoleState& console_state)
{
    const char* footer_left = console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--";

    framebuffer::fill_rect(fb, kBannerInset, kFooterBannerY, UI_WIDTH - (kBannerInset * 2), kBannerHeight, true);
    framebuffer::draw_text(fb, kFooterTextX, kFooterTextY, footer_left, false, 1, 1);
    draw_internet_icon(fb,
                       UI_WIDTH - 28,
                       UI_HEIGHT - 25,
                       console_state.wifi_status.internet_reachable,
                       console_state.wifi_status.internet_probe_pending);
}

void draw_standard_banners(uint8_t* fb, const ConsoleState& console_state, const char* title)
{
    draw_header_banner(fb, title);
    draw_footer_banner(fb, console_state);
}

}  // namespace screen_banners
