#include "text_styles.h"

#include <array>

#include "framebuffer.h"

namespace text_styles
{

namespace
{

using fonts::FontFace;

/// @brief Default font per style, in TextStyle order (docs/ui_requirements.md
/// section 6).
constexpr std::array<FontFace, kTextStyleCount> kDefaultFonts = {
    FontFace::FontTitle8x12, // HeaderTitle
    FontFace::Font8x12,      // HeaderClock
    FontFace::Font5x7,       // HeaderBadge
    FontFace::Font8x12,      // Softkey
    FontFace::Font5x7,       // SoftkeyDense
    FontFace::FontTitle8x12, // InfoRowLabel
    FontFace::Font5x7,       // BodyValue
    FontFace::Font5x7,       // CompactRow
    FontFace::Font8x12,      // BodyText
    FontFace::Font5x7,       // TableHeader
    FontFace::Font5x7,       // TableCell
    FontFace::Font5x7,       // Annotation
    FontFace::Font8x12,      // EmphasisValue
    FontFace::Font8x12,      // Headline
    FontFace::Font5x7,       // Hint
    FontFace::Font5x7,       // Footer
    FontFace::Font5x7,       // Scratchpad
};

std::array<FontFace, kTextStyleCount> g_fonts = kDefaultFonts;

size_t style_index(TextStyle style)
{
    const size_t index = static_cast<size_t>(style);
    return index < kTextStyleCount ? index : 0U;
}

} // namespace

fonts::FontFace font(TextStyle style)
{
    return g_fonts[style_index(style)];
}

void set_font(TextStyle style, fonts::FontFace face)
{
    if (style != TextStyle::Count)
    {
        g_fonts[style_index(style)] = face;
    }
}

void reset_fonts()
{
    g_fonts = kDefaultFonts;
}

int line_height(TextStyle style)
{
    return framebuffer::font_height(font(style));
}

} // namespace text_styles
