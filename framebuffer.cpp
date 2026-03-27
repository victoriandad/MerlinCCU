#include "framebuffer.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "font_5x7.h"
#include "panel_config.h"

namespace framebuffer {

namespace {

uint8_t fb_a[UI_FB_SIZE];
uint8_t fb_b[UI_FB_SIZE];

uint8_t* fb_front = fb_a;
uint8_t* fb_back = fb_b;

inline int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

}  // namespace

uint8_t* front()
{
    return fb_front;
}

uint8_t* back()
{
    return fb_back;
}

void swap()
{
    uint8_t* tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;
}

void clear(uint8_t* fb, bool on)
{
    std::memset(fb, on ? 0xFF : 0x00, UI_FB_SIZE);
}

void set_pixel(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return;
    }

    uint8_t& byte = fb[y * UI_STRIDE + (x >> 3)];
    const uint8_t mask = 0x80u >> (x & 7);

    if (on) {
        byte |= mask;
    } else {
        byte &= static_cast<uint8_t>(~mask);
    }
}

bool get_pixel(const uint8_t* fb, int x, int y)
{
    if (x < 0 || x >= UI_WIDTH || y < 0 || y >= UI_HEIGHT) {
        return false;
    }

    const uint8_t byte = fb[y * UI_STRIDE + (x >> 3)];
    const uint8_t mask = 0x80u >> (x & 7);
    return (byte & mask) != 0;
}

void draw_hline(uint8_t* fb, int x0, int x1, int y, bool on)
{
    if (y < 0 || y >= UI_HEIGHT) return;
    if (x0 > x1) std::swap(x0, x1);
    x0 = std::max(0, x0);
    x1 = std::min(UI_WIDTH - 1, x1);

    for (int x = x0; x <= x1; ++x) {
        set_pixel(fb, x, y, on);
    }
}

void draw_vline(uint8_t* fb, int x, int y0, int y1, bool on)
{
    if (x < 0 || x >= UI_WIDTH) return;
    if (y0 > y1) std::swap(y0, y1);
    y0 = std::max(0, y0);
    y1 = std::min(UI_HEIGHT - 1, y1);

    for (int y = y0; y <= y1; ++y) {
        set_pixel(fb, x, y, on);
    }
}

void draw_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;

    draw_hline(fb, x, x + w - 1, y, on);
    draw_hline(fb, x, x + w - 1, y + h - 1, on);
    draw_vline(fb, x, y, y + h - 1, on);
    draw_vline(fb, x + w - 1, y, y + h - 1, on);
}

void fill_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0) return;

    for (int yy = y; yy < y + h; ++yy) {
        draw_hline(fb, x, x + w - 1, yy, on);
    }
}

void draw_diag(uint8_t* fb, bool on)
{
    const int limit = std::min(UI_WIDTH, UI_HEIGHT);
    for (int i = 0; i < limit; ++i) {
        set_pixel(fb, i, i, on);
    }
}

void draw_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool on)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true) {
        set_pixel(fb, x0, y0, on);
        if (x0 == x1 && y0 == y1) break;

        const int e2 = 2 * err;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void draw_char(uint8_t* fb, int x, int y, char c, bool on, int scale)
{
    if (scale < 1) scale = 1;

    const Glyph5x7* g = font_lookup(c);
    for (int col = 0; col < 5; ++col) {
        const uint8_t bits = g->col[col];

        for (int row = 0; row < 7; ++row) {
            if (((bits >> row) & 0x01u) == 0) {
                continue;
            }

            for (int sx = 0; sx < scale; ++sx) {
                for (int sy = 0; sy < scale; ++sy) {
                    set_pixel(fb, x + col * scale + sx, y + row * scale + sy, on);
                }
            }
        }
    }
}

void draw_text(uint8_t* fb, int x, int y, const char* s, bool on, int scale, int spacing)
{
    int cursor_x = x;
    while (*s) {
        draw_char(fb, cursor_x, y, *s, on, scale);
        cursor_x += (5 * scale) + spacing;
        ++s;
    }
}

}  // namespace framebuffer
