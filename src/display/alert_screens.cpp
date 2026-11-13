#include "alert_screens.h"

#include <algorithm>
#include <array>
#include <cstdint>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace alert_screens
{

namespace
{

constexpr fonts::FontFace kDetailFont = fonts::FontFace::Font8x12;
constexpr int kDetailFontHeight = 12;
constexpr int kDetailTextX = 12;
constexpr int kDetailTextWidth = kUiWidth - (kDetailTextX * 2);
constexpr int kDetailStartY = 44;
constexpr int kDetailRowPitch = 18;
constexpr int kDetailVisibleLines = 12;
// Top of the row 5 softkey labels (IGNORE / ACCEPT). The last visible line
// must end above it (SCR-5). Moves to the shared layout header under #113.
constexpr int kRow5LabelTopY = 264;
static_assert(kDetailStartY + ((kDetailVisibleLines - 1) * kDetailRowPitch) + kDetailFontHeight <=
                  kRow5LabelTopY,
              "alert detail text would reach the row 5 softkey labels");

// ActiveAlert::detail holds 319 characters; even at one short word per line
// that is well under this many wrapped lines at the detail width.
constexpr size_t kMaxDetailLines = 48U;

// Same position as the alert list's empty state.
constexpr int kEmptyStateY = 112;

using DetailLines = std::array<screens::TextSpan, kMaxDetailLines>;

size_t wrap_detail(const ActiveAlert& alert, DetailLines& lines)
{
    return screens::wrap_text(alert.detail.data(), kDetailFont, kDetailTextWidth, lines.data(),
                              lines.size());
}

} // namespace

/// @brief Draws compact status lines for the alert-list page.
void draw_alert_list_page(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr uint8_t kAlertsPerPage = 9U;
    const uint8_t page_count = static_cast<uint8_t>(
        (console_state.alert_count == 0U)
            ? 1U
            : ((console_state.alert_count + (kAlertsPerPage - 1U)) / kAlertsPerPage));
    screens::draw_page_navigation_arrows(fb, console_state.alert_list_page_index > 0U,
                                         (console_state.alert_list_page_index + 1U) < page_count);
    if (console_state.alert_count == 0U)
    {
        screens::draw_centered_text(fb, kUiWidth / 2, kEmptyStateY, "NO ACTIVE ALERTS", true,
                                    fonts::FontFace::Font8x12, 1);
    }
}

uint8_t detail_max_scroll(const ActiveAlert& alert)
{
    DetailLines lines = {};
    const size_t count = wrap_detail(alert, lines);
    const size_t visible = static_cast<size_t>(kDetailVisibleLines);
    return static_cast<uint8_t>(count > visible ? count - visible : 0U);
}

/// @brief Draws the selected alert detail text, wrapped to the body width,
/// with line-based scrolling on the cursor keys.
void draw_alert_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        screens::draw_centered_text(fb, kUiWidth / 2, kEmptyStateY, "NO ALERT SELECTED", true,
                                    fonts::FontFace::Font8x12, 1);
        return;
    }

    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    DetailLines lines = {};
    const size_t count = wrap_detail(alert, lines);
    const size_t first = std::min<size_t>(console_state.alert_detail_scroll_line,
                                          detail_max_scroll(alert));

    for (size_t row = 0U; row < static_cast<size_t>(kDetailVisibleLines) && first + row < count;
         ++row)
    {
        const screens::TextSpan& span = lines[first + row];
        char line[96] = {};
        const size_t length = std::min(span.length, sizeof(line) - 1U);
        std::copy(span.text, span.text + length, line);
        framebuffer::draw_text(fb, kDetailTextX,
                               kDetailStartY + (static_cast<int>(row) * kDetailRowPitch), line,
                               true, kDetailFont, 1);
    }

    screens::draw_page_navigation_arrows(fb, first > 0U, first < detail_max_scroll(alert));
}

} // namespace alert_screens
