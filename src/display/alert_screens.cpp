#include "alert_screens.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

#include "alert_history.h"
#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "ui_layout.h"

namespace alert_screens
{

namespace
{

using screens::TextStyle;

constexpr TextStyle kDetailStyle = TextStyle::BodyText;

/// @brief Lines of detail text that fit between the content-top line and
/// the row 5 labels (IGNORE / ACCEPT), which the last line must end above
/// (SCR-5). Measured from the current fonts: 12 lines of 8x12 text.
int detail_visible_lines()
{
    const int kLineHeight = text_styles::line_height(kDetailStyle);
    const int kPitch = ui_layout::row_pitch(kDetailStyle);
    const int kLimitY = ui_layout::softkey_two_line_label_top_y(4, TextStyle::Softkey);
    return std::max(1, ((kLimitY - ui_layout::kContentTopY - kLineHeight) / kPitch) + 1);
}

// An alert's detail holds 319 characters (kAlertDetailCapacity), plus the occurrence line or, for
// a past alert, its summary and times; even at one short word per line that
// is well under this many wrapped lines at the detail width.
constexpr size_t kMaxDetailLines = 56U;

// The stored detail text plus a blank line and the occurrence line, or a
// past alert's header lines (alert_history::format_detail).
constexpr size_t kDetailTextCapacity = 520U;
using DetailText = std::array<char, kDetailTextCapacity>;

using DetailLines = std::array<screens::TextSpan, kMaxDetailLines>;

/// @brief Wraps the alert's detail text, followed by how often it has
/// occurred once that is more than once. `text` backs the returned spans.
size_t wrap_detail(const ActiveAlert& alert, DetailText& text, DetailLines& lines)
{
    if (alert.occurrence_count > 1U)
    {
        std::snprintf(text.data(), text.size(),
                      "%s\n\nOccurred %u times since it was last accepted or the CCU "
                      "restarted.",
                      alert.detail.data(), static_cast<unsigned>(alert.occurrence_count));
    }
    else
    {
        std::snprintf(text.data(), text.size(), "%s", alert.detail.data());
    }
    return screens::wrap_text(text.data(), kDetailStyle, ui_layout::kContentWidth, lines.data(),
                              lines.size());
}

/// @brief Highest first-visible line for `line_count` wrapped lines.
uint8_t max_scroll_for(size_t line_count)
{
    const size_t visible = static_cast<size_t>(detail_visible_lines());
    return static_cast<uint8_t>(line_count > visible ? line_count - visible : 0U);
}

/// @brief Draws wrapped lines from `scroll_line`, as many as fit above row 5.
void draw_wrapped(uint8_t* fb, const DetailLines& lines, size_t count, uint8_t scroll_line)
{
    const size_t first = std::min<size_t>(scroll_line, max_scroll_for(count));
    const size_t kVisible = static_cast<size_t>(detail_visible_lines());
    const int kPitch = ui_layout::row_pitch(kDetailStyle);
    for (size_t row = 0U; row < kVisible && first + row < count; ++row)
    {
        const screens::TextSpan& span = lines[first + row];
        char line[96] = {};
        const size_t length = std::min(span.length, sizeof(line) - 1U);
        std::copy(span.text, span.text + length, line);
        screens::draw_text(fb, ui_layout::kMarginX,
                           ui_layout::kContentTopY + (static_cast<int>(row) * kPitch), line,
                           kDetailStyle);
    }
}

/// @brief Wraps a past alert's detail page text. `text` backs the spans.
size_t wrap_history_detail(const alert_history::Entry& entry, uint32_t today_minute,
                           DetailText& text, DetailLines& lines)
{
    alert_history::format_detail(entry, today_minute, text.data(), text.size());
    return screens::wrap_text(text.data(), kDetailStyle, ui_layout::kContentWidth, lines.data(),
                              lines.size());
}

} // namespace

/// @brief Draws compact status lines for the alert-list page.
void draw_alert_list_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_count == 0U)
    {
        screens::draw_empty_state(fb, "NO ACTIVE ALERTS", nullptr);
    }
}

uint8_t detail_max_scroll(const ActiveAlert& alert)
{
    DetailText text = {};
    DetailLines lines = {};
    return max_scroll_for(wrap_detail(alert, text, lines));
}

/// @brief Draws the selected alert detail text, wrapped to the body width,
/// with line-based scrolling on the cursor keys.
void draw_alert_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        screens::draw_empty_state(fb, "NO ALERT SELECTED", nullptr);
        return;
    }

    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    DetailText text = {};
    DetailLines lines = {};
    // Uses this wrap rather than detail_max_scroll(), which would wrap again
    // with a second set of buffers on the core-0 stack.
    const size_t count = wrap_detail(alert, text, lines);
    draw_wrapped(fb, lines, count, console_state.alert_detail_scroll_line);
}

void draw_alert_history_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_history_clear_confirming)
    {
        // Every key but CANCEL and CLEAR ALL is blank now, so the body says
        // plainly what will go (#203).
        char detail[96] = {};
        const unsigned count = alert_history::count();
        std::snprintf(detail, sizeof(detail),
                      "Clears %u past alert%s. Active alerts stay. This cannot be undone.", count,
                      count == 1U ? "" : "s");
        screens::draw_empty_state(fb, "CLEAR ALERT HISTORY?", detail);
        return;
    }
    if (alert_history::count() == 0U)
    {
        screens::draw_empty_state(fb, "NO PAST ALERTS", nullptr);
    }
}

uint8_t history_detail_max_scroll(const ConsoleState& console_state)
{
    const alert_history::Entry* entry =
        alert_history::find(console_state.alert_history_detail_sequence);
    if (entry == nullptr)
    {
        return 0U;
    }
    DetailText text = {};
    DetailLines lines = {};
    return max_scroll_for(wrap_history_detail(
        *entry, alert_history::local_minute(console_state.time_status), text, lines));
}

void draw_alert_history_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    const alert_history::Entry* entry =
        alert_history::find(console_state.alert_history_detail_sequence);
    if (entry == nullptr)
    {
        screens::draw_empty_state(fb, "NO ALERT SELECTED", nullptr);
        return;
    }
    DetailText text = {};
    DetailLines lines = {};
    const size_t count = wrap_history_detail(
        *entry, alert_history::local_minute(console_state.time_status), text, lines);
    draw_wrapped(fb, lines, count, console_state.alert_history_scroll_line);
}

} // namespace alert_screens
