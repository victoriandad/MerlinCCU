#include "framebuffer.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>

#include "bitmap_font.h"
#include "font_5x7.h"
#include "panel_config.h"

namespace framebuffer
{

namespace
{

// Double buffering is used here so the UI can draw a whole frame off-screen and
// only publish it once complete. That keeps the renderer simple and avoids
// partially updated menus becoming visible during slower redraws.
std::array<uint8_t, kUiFbSize> fb_a = {};
std::array<uint8_t, kUiFbSize> fb_b = {};

uint8_t* fb_front = fb_a.data();
uint8_t* fb_back = fb_b.data();

/// @brief Clamps a framebuffer coordinate into an inclusive range.
/// @details Local clipping code stays in integer space because these helpers run in the
/// hot path for primitive drawing and should not depend on heavier abstractions.
inline int clamp_int(int v, int lo, int hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

} // namespace

/// @brief Returns the framebuffer currently being presented to the display path.
uint8_t* front()
{
    return fb_front;
}

/// @brief Returns the off-screen framebuffer used for the next render pass.
uint8_t* back()
{
    return fb_back;
}

/// @brief Swaps the front and back framebuffers.
void swap()
{
    uint8_t* tmp = fb_front;
    fb_front = fb_back;
    fb_back = tmp;
}

/// @brief Fills the entire framebuffer with either set or cleared pixels.
void clear(uint8_t* fb, bool on)
{
    std::memset(fb, on ? 0xFF : 0x00, kUiFbSize);
}

/// @brief Sets or clears one logical UI pixel when it lies inside the bounds.
void set_pixel(uint8_t* fb, int x, int y, bool on)
{
    if (x < 0 || x >= kUiWidth || y < 0 || y >= kUiHeight)
    {
        return;
    }

    // Pixels are packed MSB-first so the in-memory layout matches the raster
    // generation code and avoids another bit-order transform during scanout.
    uint8_t& byte = fb[y * kUiStride + (x >> 3)];
    const uint8_t mask = 0x80U >> (x & 7);

    if (on)
    {
        byte |= mask;
    }
    else
    {
        byte &= static_cast<uint8_t>(~mask);
    }
}

/// @brief Returns the state of one logical UI pixel.
bool get_pixel(const uint8_t* fb, int x, int y)
{
    if (x < 0 || x >= kUiWidth || y < 0 || y >= kUiHeight)
    {
        return false;
    }

    const uint8_t byte = fb[y * kUiStride + (x >> 3)];
    const uint8_t mask = 0x80U >> (x & 7);
    return (byte & mask) != 0;
}

/// @brief Draws a horizontal line clipped to the framebuffer bounds.
void draw_hline(uint8_t* fb, int x0, int x1, int y, bool on)
{
    if (y < 0 || y >= kUiHeight)
    {
        return;
    }
    if (x0 > x1)
    {
        std::swap(x0, x1);
    }
    x0 = std::max(0, x0);
    x1 = std::min(kUiWidth - 1, x1);

    for (int x = x0; x <= x1; ++x)
    {
        set_pixel(fb, x, y, on);
    }
}

/// @brief Draws a vertical line clipped to the framebuffer bounds.
void draw_vline(uint8_t* fb, int x, int y0, int y1, bool on)
{
    if (x < 0 || x >= kUiWidth)
    {
        return;
    }
    if (y0 > y1)
    {
        std::swap(y0, y1);
    }
    y0 = std::max(0, y0);
    y1 = std::min(kUiHeight - 1, y1);

    for (int y = y0; y <= y1; ++y)
    {
        set_pixel(fb, x, y, on);
    }
}

/// @brief Draws an unfilled rectangle outline.
void draw_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    draw_hline(fb, x, x + w - 1, y, on);
    draw_hline(fb, x, x + w - 1, y + h - 1, on);
    draw_vline(fb, x, y, y + h - 1, on);
    draw_vline(fb, x + w - 1, y, y + h - 1, on);
}

/// @brief Draws a filled rectangle.
void fill_rect(uint8_t* fb, int x, int y, int w, int h, bool on)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }

    for (int yy = y; yy < y + h; ++yy)
    {
        draw_hline(fb, x, x + w - 1, yy, on);
    }
}

/// @brief Draws the main top-left to bottom-right diagonal.
void draw_diag(uint8_t* fb, bool on)
{
    const int limit = std::min(kUiWidth, kUiHeight);
    for (int i = 0; i < limit; ++i)
    {
        set_pixel(fb, i, i, on);
    }
}

/// @brief Draws a clipped line segment using Bresenham stepping.
void draw_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool on)
{
    int dx = std::abs(x1 - x0);
    int sx = x0 < x1 ? 1 : -1;
    int dy = -std::abs(y1 - y0);
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;

    while (true)
    {
        set_pixel(fb, x0, y0, on);
        if (x0 == x1 && y0 == y1)
        {
            break;
        }

        const int e2 = 2 * err;
        if (e2 >= dy)
        {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx)
        {
            err += dx;
            y0 += sy;
        }
    }
}

/// @brief Draws one scaled 5x7 glyph.
void draw_char(uint8_t* fb, int x, int y, char c, bool on, int scale)
{
    if (scale < 1)
    {
        scale = 1;
    }

    const fonts::Glyph5x7* g = fonts::lookup_5x7(c);
    const fonts::GlyphMetrics metrics = fonts::glyph_metrics(fonts::FontFace::Font5x7, c);

    // Glyph metrics are used instead of always drawing the full cell width so
    // small labels look more like a panel legend and less like monospaced debug text.
    for (int col = 0; col < metrics.draw_width; ++col)
    {
        const uint8_t bits = g->col[metrics.first_column + col];

        for (int row = 0; row < 7; ++row)
        {
        if (((bits >> row) & 0x01U) == 0)
            {
                continue;
            }

            for (int sx = 0; sx < scale; ++sx)
            {
                for (int sy = 0; sy < scale; ++sy)
                {
                    set_pixel(fb, x + col * scale + sx, y + row * scale + sy, on);
                }
            }
        }
    }
}

/// @brief Draws one glyph from the requested font face.
void draw_char(uint8_t* fb, int x, int y, char c, bool on, fonts::FontFace font)
{
    if (font == fonts::FontFace::Font5x7)
    {
        const fonts::Glyph5x7* glyph = fonts::lookup_5x7(c);
        const fonts::GlyphMetrics metrics = fonts::glyph_metrics(font, c);

        // The 5x7 font is stored column-wise, so it is handled directly here
        // instead of forcing the generic bitmap-font path to special-case it later.
        for (int col = 0; col < metrics.draw_width; ++col)
        {
            const uint8_t bits = glyph->col[metrics.first_column + col];
            for (int row = 0; row < 7; ++row)
            {
        if (((bits >> row) & 0x01U) == 0)
                {
                    continue;
                }
                set_pixel(fb, x + col, y + row, on);
            }
        }
        return;
    }

    const uint8_t* rows = fonts::lookup_bitmap_rows(font, c);
    const fonts::GlyphMetrics metrics = fonts::glyph_metrics(font, c);
    const int height = fonts::font_height(font);

    // Larger fonts stay row-major because that makes title-style glyph data
    // easier to author and inspect by eye in the static font tables.
    for (int row = 0; row < height; ++row)
    {
        const uint8_t bits = rows[row];
        for (int col = 0; col < metrics.draw_width; ++col)
        {
            const int source_col = metrics.first_column + col;
        if ((bits & (0x80U >> source_col)) == 0)
            {
                continue;
            }
            set_pixel(fb, x + col, y + row, on);
        }
    }
}

/// @brief Draws a scaled 5x7 text string.
void draw_text(uint8_t* fb, int x, int y, const char* s, bool on, int scale, int spacing)
{
    int cursor_x = x;
    while (*s)
    {
        const fonts::GlyphMetrics metrics = fonts::glyph_metrics(fonts::FontFace::Font5x7, *s);
        draw_char(fb, cursor_x, y, *s, on, scale);
        cursor_x += (metrics.advance * scale) + spacing;
        ++s;
    }
}

/// @brief Draws a text string using the requested font face.
void draw_text(uint8_t* fb, int x, int y, const char* s, bool on, fonts::FontFace font, int spacing)
{
    int cursor_x = x;

    while (*s)
    {
        const fonts::GlyphMetrics metrics = fonts::glyph_metrics(font, *s);
        draw_char(fb, cursor_x, y, *s, on, font);
        cursor_x += metrics.advance + spacing;
        ++s;
    }
}

/// @brief Measures the rendered width of a string in pixels.
int measure_text(const char* s, fonts::FontFace font, int spacing)
{
    if (s == nullptr || s[0] == '\0')
    {
        return 0;
    }

    int width = 0;

    while (*s)
    {
        width += fonts::glyph_metrics(font, *s).advance;
        if (s[1] != '\0')
        {
            width += spacing;
        }
        ++s;
    }

    return width;
}

/// @brief Returns the pixel height of the requested font face.
int font_height(fonts::FontFace font)
{
    return fonts::font_height(font);
}

} // namespace framebuffer
