#pragma once

#include <cstdint>

namespace fonts {

/// @brief Fixed bitmap font faces currently available to the UI renderer.
enum class FontFace : uint8_t {
    Font5x7 = 0,
    FontTitle8x12,
    Font8x12,
    Font8x14,
};

/// @brief One packed 5x7 glyph entry stored column-wise.
struct Glyph5x7 {
    char c;
    uint8_t col[5];
};

/// @brief Looks up a 5x7 glyph entry for one ASCII character.
const Glyph5x7* lookup_5x7(char c);

/// @brief Looks up the row-major bitmap for one character in the selected font.
/// @return Pointer to immutable bitmap rows, or `nullptr` when the glyph does
/// not exist in that font face.
const uint8_t* lookup_bitmap_rows(FontFace font, char c);

/// @brief Returns the pixel width for one glyph in the selected font face.
int font_width(FontFace font);

/// @brief Returns the pixel height for one glyph in the selected font face.
int font_height(FontFace font);

}  // namespace fonts
