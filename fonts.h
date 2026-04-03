#pragma once

#include <cstdint>

namespace fonts {

enum class FontFace : uint8_t {
    Font5x7 = 0,
    FontTitle8x12,
    Font8x12,
    Font8x14,
};

struct Glyph5x7 {
    char c;
    uint8_t col[5];
};

const Glyph5x7* lookup_5x7(char c);
const uint8_t* lookup_bitmap_rows(FontFace font, char c);
int font_width(FontFace font);
int font_height(FontFace font);

}  // namespace fonts
