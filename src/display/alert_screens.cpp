#include "alert_screens.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>

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

// ActiveAlert::detail holds 319 characters, plus the occurrence line; even
// at one short word per line that is well under this many wrapped lines at
// the detail width.
constexpr size_t kMaxDetailLines = 48U;

// The stored detail text plus a blank line and the occurrence line.
constexpr size_t kDetailTextCapacity = 400U;
using DetailText = std::array<char, kDetailTextCapacity>;

/// @brief Top of the alert pages' empty-state headline.
/// @details Twelve pixels above ui_layout::kEmptyStateHeadlineY; to be
/// aligned with the shared empty state.
constexpr int kEmptyStateY = 112;

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

} // namespace

/// @brief Draws compact status lines for the alert-list page.
void draw_alert_list_page(uint8_t* fb, const ConsoleState& console_state)
{
    const uint8_t page_count = alert_list_page_count(console_state.alert_count);
    screens::draw_page_navigation_arrows(fb, console_state.alert_list_page_index > 0U,
                                         (console_state.alert_list_page_index + 1U) < page_count);
    if (console_state.alert_count == 0U)
    {
        screens::draw_centered_text(fb, ui_layout::kCentreX, kEmptyStateY, "NO ACTIVE ALERTS",
                                    TextStyle::Headline);
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
        screens::draw_centered_text(fb, ui_layout::kCentreX, kEmptyStateY, "NO ALERT SELECTED",
                                    TextStyle::Headline);
        return;
    }

    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    DetailText text = {};
    DetailLines lines = {};
    const size_t count = wrap_detail(alert, text, lines);
    // Uses this wrap rather than detail_max_scroll(), which would wrap again
    // with a second set of buffers on the 4 KB core-0 stack.
    const uint8_t max_scroll = max_scroll_for(count);
    const size_t first =
        std::min<size_t>(console_state.alert_detail_scroll_line, max_scroll);

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

    screens::draw_page_navigation_arrows(fb, first > 0U, first < max_scroll);
}

} // namespace alert_screens
