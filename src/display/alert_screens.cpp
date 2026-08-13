#include "alert_screens.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace alert_screens
{

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
        screens::draw_centered_text(fb, kUiWidth / 2, 112, "NO ACTIVE ALERTS", true,
                                    fonts::FontFace::Font8x12, 1);
    }
}

/// @brief Draws the selected alert detail text with line-based scrolling.
void draw_alert_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        framebuffer::draw_text(fb, 18, 44, "No alert selected", true, fonts::FontFace::Font8x12, 1);
        return;
    }

    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    constexpr int kTextX = 18;
    constexpr int kStartY = 44;
    constexpr int kPitch = 28;
    constexpr int kVisibleLines = 8;
    constexpr fonts::FontFace kFont = fonts::FontFace::Font8x12;
    uint8_t logical_line = 0U;
    const char* cursor = alert.detail.data();
    int drawn = 0;
    while (cursor != nullptr && cursor[0] != '\0' && drawn < kVisibleLines)
    {
        const char* eol = std::strchr(cursor, '\n');
        char line[80] = {};
        if (eol == nullptr)
        {
            std::snprintf(line, sizeof(line), "%s", cursor);
        }
        else
        {
            const size_t len = static_cast<size_t>(eol - cursor);
            std::snprintf(line, sizeof(line), "%.*s", static_cast<int>(len), cursor);
        }

        if (logical_line >= console_state.alert_detail_scroll_line)
        {
            framebuffer::draw_text(fb, kTextX, kStartY + (drawn * kPitch), line, true, kFont, 1);
            ++drawn;
        }

        ++logical_line;
        cursor = (eol == nullptr) ? nullptr : (eol + 1);
    }
}

} // namespace alert_screens
