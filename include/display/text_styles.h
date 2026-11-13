#pragma once

#include <cstddef>
#include <cstdint>

#include "fonts.h"

/// @brief Named text styles: every piece of UI text is drawn in one of these,
/// and takes its font from the style rather than naming a font face (TXT-1).
/// @details The font for each style is held in one table that can be changed
/// at run time (TXT-2), so a Settings page (#123) can try sizes on the panel
/// without a rebuild. Geometry that depends on a style's font (line height,
/// line pitch, wrap widths) must be measured from it at draw time (TXT-3),
/// never stored as a constant tuned for one face.
namespace text_styles
{

/// @brief What a piece of text is for. Defaults are listed with each style.
enum class TextStyle : uint8_t
{
    /// Page title in the header. Title 8x12.
    HeaderTitle = 0,
    /// Clock in the header. 8x12.
    HeaderClock,
    /// Text inside the header's small boxed indicators. 5x7.
    HeaderBadge,
    /// Softkey caption and value on most pages. 8x12.
    Softkey,
    /// Softkey caption and value on pages that opt into the dense face. 5x7.
    SoftkeyDense,
    /// Row label in the Info rows block. Title 8x12.
    InfoRowLabel,
    /// Value in the Info rows block, and other body values. 5x7.
    BodyValue,
    /// Label and value in the Compact detail rows block. 5x7.
    CompactRow,
    /// Scrollable body text, such as alert detail. 8x12.
    BodyText,
    /// Table column header. 5x7.
    TableHeader,
    /// Table cell. 5x7.
    TableCell,
    /// Small labels on graphs, plots and diagrams. 5x7.
    Annotation,
    /// One large live value beside a graph. 8x12.
    EmphasisValue,
    /// Empty-state and confirmation headline. 8x12.
    Headline,
    /// Hint or explanation beneath a headline. 5x7.
    Hint,
    /// Footer line: data freshness, provenance, address, stepper. 5x7.
    Footer,
    /// Text being typed into a scratchpad. 5x7.
    Scratchpad,
    Count,
};

/// @brief Number of text styles.
inline constexpr size_t kTextStyleCount = static_cast<size_t>(TextStyle::Count);

/// @brief Returns the font currently assigned to `style`.
fonts::FontFace font(TextStyle style);

/// @brief Assigns `face` to `style` until changed again or reset.
void set_font(TextStyle style, fonts::FontFace face);

/// @brief Restores every style to its default font.
void reset_fonts();

/// @brief Height in pixels of one line of `style`'s current font.
int line_height(TextStyle style);

} // namespace text_styles
