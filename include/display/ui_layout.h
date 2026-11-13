#pragma once

#include <algorithm>

#include "panel_config.h"
#include "text_styles.h"

/// @brief The one place the screen's zones, softkey rows, margins, body lines,
/// table frame and footer line are defined (SCR-2, BODY-2, BODY-3).
/// @details Page renderers take positions from here rather than restating
/// them as local numbers. Positions that depend on a font are functions that
/// measure the current font of a text style (TXT-3), so they stay right when
/// a style's font is changed at run time. See docs/ui_requirements.md section 1
/// for the screen model these values implement.
namespace ui_layout
{

// ---------------------------------------------------------------------------
// Zones
// ---------------------------------------------------------------------------

/// @brief Last row of the header zone (shared chrome).
inline constexpr int kHeaderBottomY = 33;

/// @brief First row of the body zone.
inline constexpr int kBodyTopY = kHeaderBottomY + 1;

/// @brief First row of the footer zone.
inline constexpr int kFooterTopY = 288;

/// @brief Last row of the body zone.
inline constexpr int kBodyBottomY = kFooterTopY - 1;

// ---------------------------------------------------------------------------
// Horizontal margins
// ---------------------------------------------------------------------------

/// @brief Left margin of body text, tables, dividers and footer text.
inline constexpr int kMarginX = 12;

/// @brief Right edge matching kMarginX; nothing in the body passes it (SCR-6).
inline constexpr int kRightEdgeX = kUiWidth - kMarginX;

/// @brief Width between the two margins.
inline constexpr int kContentWidth = kRightEdgeX - kMarginX;

/// @brief Horizontal centre line of the screen.
inline constexpr int kCentreX = kUiWidth / 2;

// ---------------------------------------------------------------------------
// Softkeys
// ---------------------------------------------------------------------------

/// @brief Softkey rows on each side of the screen.
inline constexpr int kSoftkeyRowCount = 5;

/// @brief Distance from the screen edge (bezel) to a softkey slot.
inline constexpr int kSoftkeyBezelX = 2;

/// @brief Distance from a softkey slot's edge to its label text.
inline constexpr int kSoftkeyTextInsetX = 4;

/// @brief Top of the row 1 softkey slot.
inline constexpr int kSoftkeyFirstSlotTopY = 41;

/// @brief Vertical distance between softkey rows, matching the bezel buttons.
inline constexpr int kSoftkeyRowPitch = 57;

/// @brief Height of the slot a softkey label is centred on.
inline constexpr int kSoftkeySlotHeight = 18;

/// @brief Gap between the lines of one softkey label.
inline constexpr int kSoftkeyLineGap = 4;

/// @brief Lines a label block is centred on its key over; a longer block (a
/// wrapped value) extends downward so it never rises into the header.
inline constexpr int kSoftkeyCentredLines = 3;

/// @brief x of a left-side label's left edge.
inline constexpr int kSoftkeyLeftLabelX = kSoftkeyBezelX + kSoftkeyTextInsetX;

/// @brief x one past a right-side label's right edge.
inline constexpr int kSoftkeyRightLabelEndX = kUiWidth - kSoftkeyLeftLabelX;

/// @brief Widest a softkey label line may be: from the bezel inset to the
/// centre line, so a label never crosses it.
inline constexpr int kSoftkeyLabelMaxWidth = kCentreX - kSoftkeyLeftLabelX;

/// @brief Top of the slot for softkey row `row` (0 = row 1).
constexpr int softkey_slot_top_y(int row)
{
    return kSoftkeyFirstSlotTopY + (row * kSoftkeyRowPitch);
}

/// @brief Vertical centre of the slot for softkey row `row` (0 = row 1).
constexpr int softkey_slot_centre_y(int row)
{
    return softkey_slot_top_y(row) + (kSoftkeySlotHeight / 2);
}

/// @brief Top of a label block of `lines` lines, each `line_height` tall, on
/// softkey row `row`. The first kSoftkeyCentredLines lines are centred on
/// the slot; any more extend downward.
constexpr int softkey_label_top_y(int row, int lines, int line_height)
{
    const int kCentred = std::clamp(lines, 1, kSoftkeyCentredLines);
    const int kHeight = (kCentred * line_height) + ((kCentred - 1) * kSoftkeyLineGap);
    return softkey_slot_top_y(row) + ((kSoftkeySlotHeight - kHeight) / 2);
}

/// @brief Top of a two-line label on row `row` in `style`: the highest a
/// typical caption-and-value label reaches. Body text must end above this on
/// rows whose keys are labelled (SCR-5).
inline int softkey_two_line_label_top_y(int row, text_styles::TextStyle style)
{
    return softkey_label_top_y(row, 2, text_styles::line_height(style));
}

// ---------------------------------------------------------------------------
// Body lines and row pitch
// ---------------------------------------------------------------------------

/// @brief Shared content-top line: where body text starts below the header.
inline constexpr int kContentTopY = 44;

/// @brief Top of the Info rows block.
/// @details Two pixels above kContentTopY; to be aligned with it.
inline constexpr int kInfoRowsTopY = 42;

/// @brief Space between one row's text and the next row's text.
inline constexpr int kRowGapY = 6;

/// @brief Row pitch for one-line rows of `style`: its line height plus the
/// row gap. 13 pixels for 5x7 text, 18 for 8x12.
inline int row_pitch(text_styles::TextStyle style)
{
    return text_styles::line_height(style) + kRowGapY;
}

// ---------------------------------------------------------------------------
// Table frame (BODY-3)
// ---------------------------------------------------------------------------

/// @brief Where every table puts its header, divider and rows. A page defines
/// only its columns.
struct TableFrame
{
    int header_y;
    int divider_y;
    int first_row_y;
    int row_pitch;
    int left_x;
    int right_x;
};

/// @brief Top of a table's column header row.
inline constexpr int kTableHeaderY = 36;

/// @brief Space between the column headers and the divider beneath them.
inline constexpr int kTableHeaderToDividerY = 3;

/// @brief Space between the divider and the first row.
inline constexpr int kTableDividerToFirstRowY = 8;

/// @brief Pitch of table rows: looser than a detail row so columns of short
/// values stay easy to follow across the screen.
inline constexpr int kTableRowPitch = 18;

/// @brief The shared table frame, measured from the table header font.
inline TableFrame table_frame()
{
    const int kDividerY = kTableHeaderY +
                          text_styles::line_height(text_styles::TextStyle::TableHeader) +
                          kTableHeaderToDividerY;
    return {kTableHeaderY,  kDividerY, kDividerY + kTableDividerToFirstRowY,
            kTableRowPitch, kMarginX,  kRightEdgeX};
}

// ---------------------------------------------------------------------------
// Graph frame (BODY-4)
// ---------------------------------------------------------------------------

/// @brief Left edge of a history graph's plot. The space to its left holds
/// the axis labels.
inline constexpr int kGraphLeftX = 42;

/// @brief Width of a history graph's plot, centred on the screen.
inline constexpr int kGraphWidth = kUiWidth - (kGraphLeftX * 2);

/// @brief Gap between a plot's border and the plotted data.
inline constexpr int kGraphPlotInset = 1;

// ---------------------------------------------------------------------------
// Footer (SCR-4)
// ---------------------------------------------------------------------------

/// @brief Top of a one-line footer text, and the centre line of the paging
/// arrows.
inline constexpr int kFooterLineY = kUiHeight - 18;

// ---------------------------------------------------------------------------
// Empty state (STATE-2)
// ---------------------------------------------------------------------------

/// @brief Top of the empty-state headline. The block sits in the gap between
/// the row 2 and row 3 softkey cells (y 116-155), so it never meets a label.
inline constexpr int kEmptyStateHeadlineY = 124;

/// @brief Top of the empty-state hint line, beneath the headline.
inline constexpr int kEmptyStateHintY = 142;

} // namespace ui_layout
