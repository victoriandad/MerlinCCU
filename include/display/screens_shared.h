#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "console_model.h"
#include "environment_sensor_manager.h"
#include "fonts.h"
#include "text_styles.h"
#include "ui_layout.h"

/// @brief Small cross-page-family primitives from `screens.cpp` that other
/// split-out page renderer files (e.g. `status_screens.cpp`) also need.
/// @details Not part of the public `screens.h` API -- only `screens.cpp` and
/// its sibling page-family translation units should include this.
namespace screens
{

using text_styles::TextStyle;

/// @brief Number of top-level Settings root pages, used both by the header's
/// "SETTINGS N/M" title (screens.cpp) and Settings' own page-navigation
/// arrows (settings_screens.cpp).
inline constexpr uint8_t kSettingsPageCount = 2U;

/// @brief Returns the Home Assistant state label used on diagnostics screens.
const char* home_assistant_state_text(HomeAssistantConnectionState state);

/// @brief Returns a provider-neutral weather fetch state label.
const char* weather_fetch_state_text(HomeAssistantConnectionState state);

/// @brief One label/value pair rendered by `draw_compact_detail_rows`.
struct DetailRow
{
    const char* label;
    const char* value;
};

/// @brief Draws compact `Label: value` rows for data-dense pages.
void draw_compact_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y,
                              int row_pitch);

/// @brief Formats a compact "Xs"/"Xm"/"Xh" elapsed-time label.
void build_elapsed_time_text(uint32_t elapsed_ms, char* out, size_t out_size);

/// @brief Formats one data-backed subsystem's freshness into a short,
/// consistent label ("LIVE", "STALE 12m", "NO DATA") -- issue #16's
/// stale-data display policy. See the definition in screens.cpp for the full
/// contract, including why deliberate placeholder/demo states should not use
/// this helper.
void build_data_freshness_text(bool currently_valid, uint32_t last_success_ms, uint32_t now_ms,
                               uint32_t stale_after_ms, char* out, size_t out_size);

/// @brief Width in pixels of `text` in `style`'s current font; 0 for null or
/// empty text.
int text_width(const char* text, TextStyle style);

/// @brief Draws `text` in `style` with its top-left corner at (`x`, `y`).
void draw_text(uint8_t* fb, int x, int y, const char* text, TextStyle style, bool on = true);

/// @brief Draws `text` in `style` centred on `center_x`.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, TextStyle style,
                        bool on = true);

/// @brief Draws `text` in `style` so it ends at `right_x` (exclusive).
void draw_right_aligned_text(uint8_t* fb, int right_x, int y, const char* text, TextStyle style,
                             bool on = true);

/// @brief Draws the shared empty-state block (STATE-2).
/// @details One uppercase headline at a fixed position, and an optional
/// mixed-case hint beneath it saying what the operator can do or what will
/// happen. Both are centred. `hint` may be null or empty; it wraps to the
/// content width over at most kMaxEmptyStateHintLines lines. The first line
/// ends above the row 3 softkey cells; later lines reach into them, so a
/// hint longer than one line is only for pages whose row 3 keys are blank
/// (SCR-5).
void draw_empty_state(uint8_t* fb, const char* headline, const char* hint);

/// @brief One softkey/footer label wrapped into up to three fixed-size lines.
struct WrappedSoftkeyLabel
{
    char line_one[48];
    char line_two[48];
    char line_three[48];
    int line_count;
};

/// @brief Most lines any softkey label may use (LBL-1).
inline constexpr int kMaxSoftkeyLabelLines = 5;

/// @brief One rendered line of a softkey label.
struct SoftkeyLabelLine
{
    char text[48];
    /// True when the line is part of a bracketed value; the brackets are
    /// drawn as graphics around every line of that value, not as glyphs.
    bool bracketed;
    /// True on the last line of a bracketed value.
    bool value_end;
};

/// @brief A softkey label laid out into lines that each fit the cell.
struct SoftkeyLabelLayout
{
    std::array<SoftkeyLabelLine, kMaxSoftkeyLabelLines> lines;
    int count;
};

/// @brief Lays out a softkey label into lines that each fit the cell in
/// `style`; a `[value]` part wraps inside one bracket pair.
SoftkeyLabelLayout layout_softkey_label(const char* label, TextStyle style, int max_lines);

/// @brief Wraps one label into at most `max_lines` renderable lines.
/// @details Softkeys are deliberately capped (two lines for most pages, three
/// where a page opts in) so long labels degrade gracefully without taking
/// over the rest of the screen layout. Each line greedily fits as much text
/// as the width allows, preferring to break on whitespace.
WrappedSoftkeyLabel wrap_label_lines(const char* label, TextStyle style, int max_width,
                                     int max_lines);

/// @brief One wrapped line of a longer text: a view into the source string.
struct TextSpan
{
    const char* text;
    size_t length;
};

/// @brief Wraps free text into lines no wider than `max_width` pixels.
/// @details The shared Text block (docs/ui_patterns.md). Breaks on spaces,
/// treats `\n` as a forced break (an empty paragraph gives an empty line),
/// and hard-breaks a single word wider than the line. Spans point into
/// `text`, so it must outlive them. Returns the number of spans written,
/// at most `max_spans`; lines beyond that are dropped.
size_t wrap_text(const char* text, TextStyle style, int max_width, TextSpan* out, size_t max_spans);

/// @brief Draws the standard row-based presentation used by information pages.
/// @details The top banner carries the page title, so the body can stay clean
/// and consistent without extra boxes, subtitles, or divider lines.
void draw_info_page_rows(uint8_t* fb, const DetailRow* rows, size_t count);

/// @brief Draws shared left/right page-navigation arrows used by paged menus.
void draw_page_navigation_arrows(uint8_t* fb, bool show_left, bool show_right);

/// @brief Bordered graph plot rect shared by every page-family history graph
/// (Shares, Local Conditions), with a symmetric one-pixel inset on all sides.
struct GraphPlotArea
{
    int x;
    int y;
    int width;
    int height;
    int inset;
};

/// @brief Labels of the shared history graph frame (BODY-4).
struct HistoryGraphLabels
{
    /// Left axis, top to bottom: the values at the top, middle and bottom
    /// of the plot.
    const char* maximum;
    const char* middle;
    const char* minimum;
    /// Under the plot: where the history starts (left) and ends (right).
    const char* start;
    const char* end;
};

/// @brief Draws the shared history graph frame and returns its plot area.
/// @details Every history graph uses it, so they share one plot position
/// and labelling scheme: the plot spans ui_layout::kGraphLeftX to the same
/// margin on the right and starts at the content-top line, with the axis
/// values right-aligned to its left and a tick at the middle value, and the
/// start and end of the period beneath it. Only the height differs: a page
/// with rows beneath its graph uses a shorter plot (BODY-4).
GraphPlotArea draw_history_graph_frame(uint8_t* fb, int plot_height,
                                       const HistoryGraphLabels& labels);

/// @brief Top of the line under a history graph frame of `plot_height`,
/// below its start and end labels, where a page may add a caption.
int history_graph_caption_y(int plot_height);

/// @brief Draws a graph plot area's border rect.
void draw_graph_plot_border(uint8_t* fb, const GraphPlotArea& area);

/// @brief Maps a 0-based point index across `point_count` evenly spaced points
/// to an x pixel coordinate within the inset plot area.
int graph_plot_x(const GraphPlotArea& area, int index, int point_count);

/// @brief Maps a value's position between `min_value`/`max_value` to a y pixel
/// coordinate within the inset plot area, zero at the bottom.
/// @details `value` is clamped to the given range first, so callers whose data
/// can exceed the display scale (e.g. local-condition history against its
/// fixed axis range) do not need to clamp separately.
int graph_plot_y(const GraphPlotArea& area, int64_t value, int64_t min_value, int64_t max_value);

/// @brief Draws one oversized square bracket pair around a softkey value.
/// @details The brackets are rendered as simple line primitives so they stay
/// readable even when the underlying bitmap font has tiny punctuation glyphs.
/// Used both by the core softkey label renderer (screens.cpp) and by
/// Settings' screen-saver timeout scratchpad.
void draw_softkey_selection_brackets(uint8_t* fb, int left_x, int top_y, int total_height,
                                     int total_width, TextStyle style, bool on);

} // namespace screens
