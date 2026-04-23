#include "screensaver_matrix.h"

#include <array>
#include <cstdlib>

#include "framebuffer.h"
#include "panel_config.h"
#include "pico/stdlib.h"

namespace screensaver_matrix
{

namespace
{

struct MatrixColumn
{
    int x;
    int head_y;
    int speed;
    int length;
    uint8_t glyph_seed;
};

constexpr int kColumnSpacing = 6;
constexpr int kGlyphStepY = 8;
constexpr int kColumnCount = kUiWidth / kColumnSpacing;
constexpr int kMinTrailLength = 5;
constexpr int kMaxTrailLength = 18;

std::array<MatrixColumn, kColumnCount> g_columns = {};

/// @brief Produces a compact pseudo-codepoint that stays within the 5x7 font table.
char random_matrix_glyph(uint8_t seed, int row)
{
    constexpr char kGlyphs[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    constexpr int kGlyphCount = static_cast<int>(sizeof(kGlyphs) - 1);
    return kGlyphs[(seed + (row * 7)) % kGlyphCount];
}

/// @brief Restarts one column above the visible screen with varied speed and trail length.
void reset_column(MatrixColumn& column, int x)
{
    column.x = x;
    column.head_y = -(std::rand() % kUiHeight);
    column.speed = 2 + (std::rand() % 4);
    column.length = kMinTrailLength + (std::rand() % (kMaxTrailLength - kMinTrailLength + 1));
    column.glyph_seed = static_cast<uint8_t>(std::rand() & 0xFF);
}

} // namespace

/// @brief Initializes the falling-glyph Matrix screensaver state.
void init()
{
    std::srand(static_cast<unsigned int>(to_ms_since_boot(get_absolute_time())));

    for (int index = 0; index < kColumnCount; ++index)
    {
        reset_column(g_columns[index], index * kColumnSpacing);
        g_columns[index].head_y = std::rand() % kUiHeight;
    }
}

/// @brief Advances the Matrix screensaver and renders one frame.
void step_and_render(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    for (MatrixColumn& column : g_columns)
    {
        column.head_y += column.speed;
        if (column.head_y - (column.length * kGlyphStepY) > kUiHeight)
        {
            reset_column(column, column.x);
        }

        for (int trail = 0; trail < column.length; ++trail)
        {
            const int y = column.head_y - (trail * kGlyphStepY);
            if (y < -7 || y >= kUiHeight)
            {
                continue;
            }

            // Monochrome trails are thinned as they age so the head remains visually dominant.
            if (trail > 2 && ((trail + column.glyph_seed) % 3) == 0)
            {
                continue;
            }

            const char glyph = random_matrix_glyph(column.glyph_seed, (column.head_y / kGlyphStepY) - trail);
            framebuffer::draw_char(fb, column.x, y, glyph, true, 1);
        }
    }
}

} // namespace screensaver_matrix
