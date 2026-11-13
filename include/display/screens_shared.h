#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "console_model.h"
#include "environment_sensor_manager.h"
#include "fonts.h"

/// @brief Small cross-page-family primitives from `screens.cpp` that other
/// split-out page renderer files (e.g. `status_screens.cpp`) also need.
/// @details Not part of the public `screens.h` API -- only `screens.cpp` and
/// its sibling page-family translation units should include this.
namespace screens
{

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

/// @brief Formats a fixed-point centi-Celsius temperature for the Status page.
void build_environment_temperature_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 humidity as percent relative humidity.
void build_environment_humidity_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 pressure as hPa, which is easier to scan than raw Pa.
void build_environment_pressure_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats a compact "Xs"/"Xm"/"Xh" elapsed-time label.
void build_elapsed_time_text(uint32_t elapsed_ms, char* out, size_t out_size);

/// @brief Formats one data-backed subsystem's freshness into a short,
/// consistent label ("LIVE", "STALE 12m", "NO DATA") -- issue #16's
/// stale-data display policy. See the definition in screens.cpp for the full
/// contract, including why deliberate placeholder/demo states should not use
/// this helper.
void build_data_freshness_text(bool currently_valid, uint32_t last_success_ms, uint32_t now_ms,
                               uint32_t stale_after_ms, char* out, size_t out_size);

/// @brief Measures text through one shared helper so layout math stays consistent.
int text_width(const char* text, fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1);

/// @brief Draws text centred around a given x-coordinate.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1);

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

/// @brief Lays out a softkey label into lines that each fit the cell at
/// `font`; a `[value]` part wraps inside one bracket pair.
SoftkeyLabelLayout layout_softkey_label(const char* label, fonts::FontFace font, int max_lines);

/// @brief Wraps one label into at most `max_lines` renderable lines.
/// @details Softkeys are deliberately capped (two lines for most pages, three
/// where a page opts in) so long labels degrade gracefully without taking
/// over the rest of the screen layout. Each line greedily fits as much text
/// as the width allows, preferring to break on whitespace.
WrappedSoftkeyLabel wrap_label_lines(const char* label, fonts::FontFace font, int max_width,
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
size_t wrap_text(const char* text, fonts::FontFace font, int max_width, TextSpan* out,
                 size_t max_spans);

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
                                     int total_width, fonts::FontFace font, bool on);

} // namespace screens
