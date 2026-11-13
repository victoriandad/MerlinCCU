#include "settings_screens.h"

#include <cstdint>
#include <cstdio>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace settings_screens
{

namespace
{

/// @brief Formats the current screen-saver timeout for labels and scratchpad text.
void build_screen_saver_timeout_text(uint16_t minutes, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return;
    }

    const char* unit = (minutes == 1U) ? "min" : "mins";
    std::snprintf(buffer, buffer_size, "%u %s", static_cast<unsigned>(minutes), unit);
}

/// @brief Draws the bottom scratchpad used for screen-saver timeout entry.
/// @details The original CCU scratchpad was a low, wide editing region, so this
/// version keeps the same bottom-of-screen placement and bracketed treatment.
void draw_screen_saver_scratchpad(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kScratchpadWidth = 160;
    constexpr int kScratchpadHeight = 15;
    constexpr int kScratchpadLeftX = (kUiWidth - kScratchpadWidth) / 2;
    constexpr int kScratchpadTopY = kUiHeight - kScratchpadHeight - 3;
    constexpr int kTextInsetY = 4;
    constexpr int kRightPadX = 10;
    char timeout_text[16] = {};
    build_screen_saver_timeout_text(console_state.screen_saver_timeout_edit_minutes, timeout_text,
                                    sizeof(timeout_text));
    const int kTextWidth = screens::text_width(timeout_text, fonts::FontFace::Font5x7, 1);
    const int kTextX = kScratchpadLeftX + kScratchpadWidth - kRightPadX - kTextWidth;

    framebuffer::fill_rect(fb, kScratchpadLeftX + 1, kScratchpadTopY + 1, kScratchpadWidth - 2,
                           kScratchpadHeight - 2, false);
    screens::draw_softkey_selection_brackets(fb, kScratchpadLeftX, kScratchpadTopY,
                                             kScratchpadHeight, kScratchpadWidth,
                                             fonts::FontFace::Font5x7, true);
    framebuffer::draw_text(fb, kTextX, kScratchpadTopY + kTextInsetY, timeout_text, true,
                           fonts::FontFace::Font5x7, 1);
}

} // namespace

/// @brief Draws the top-level settings routing page.
/// @details The root page intentionally leaves the centre clear. Section state
/// belongs in the bracketed softkey labels; detailed values are shown only
/// after the operator opens a focused settings subpage.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    screens::draw_page_navigation_arrows(
        fb, console_state.settings_page_index > 0U,
        (console_state.settings_page_index + 1U) < screens::kSettingsPageCount);
}

/// @brief Draws only the paging arrows of the Time Zone option list.
/// @details The zones themselves are option keys around the bezel; the
/// cursor keys page through them (NAV-7, NAV-8).
void draw_time_zone_page(uint8_t* fb, const ConsoleState& console_state)
{
    screens::draw_page_navigation_arrows(
        fb, console_state.time_zone_page_index > 0U,
        (console_state.time_zone_page_index + 1U) < kTimeZonePageCount);
}

/// @brief Draws only active screen-saver editing UI.
/// @details When not editing, the selected saver and timeout live on softkeys.
void draw_screen_saver_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.screen_saver_timeout_editing)
    {
        draw_screen_saver_scratchpad(fb, console_state);
    }
}

/// @brief Draws the keypad-debug diagnostics page.
/// @details The goal here is still hardware bring-up, but the layout now uses
/// the same clean row styling as the status page instead of a boxed panel.
void draw_keypad_debug_page(uint8_t* fb, const ConsoleState& console_state)
{
    char mask_text[16] = {};
    std::snprintf(mask_text, sizeof(mask_text), "0x%04lX",
                  static_cast<unsigned long>(console_state.keypad_debug_status.active_mask));
    char lines_text[24] = {};
    std::snprintf(lines_text, sizeof(lines_text), "%u/%u",
                  static_cast<unsigned>(console_state.keypad_debug_status.active_count),
                  static_cast<unsigned>(console_state.keypad_debug_status.configured_count));
    char drive_text[16] = {};
    if (console_state.keypad_debug_status.probe_drive_panel_pin != 0)
    {
        std::snprintf(
            drive_text, sizeof(drive_text), "%u",
            static_cast<unsigned>(console_state.keypad_debug_status.probe_drive_panel_pin));
    }
    else
    {
        std::snprintf(drive_text, sizeof(drive_text), "-");
    }

    const screens::DetailRow rows[] = {
        {"KEY PRESSED", console_state.keypad_debug_status.pressed_key_name[0]
                            ? console_state.keypad_debug_status.pressed_key_name.data()
                            : "-"},
        {"ACTIVE PINS", console_state.keypad_debug_status.active_panel_pins[0]
                            ? console_state.keypad_debug_status.active_panel_pins.data()
                            : "-"},
        {"ACTIVE MASK", mask_text},
        {"ACTIVE LINES", lines_text},
        {"PROBE DRIVE", drive_text},
        {"PROBE SENSE", console_state.keypad_debug_status.probe_hit_panel_pins[0]
                            ? console_state.keypad_debug_status.probe_hit_panel_pins.data()
                            : "-"},
    };

    screens::draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
}

/// @brief Draws the 2x2 ordered-dither greyscale test card.
/// @details Five bands, one per brightness level (0-4 of every 2x2 pixel
/// block lit), exercising `framebuffer::fill_rect_dithered` end-to-end on
/// real hardware and the browser preview. See `docs/greyscale-investigation.md`
/// and issue #54. Labels sit above each band on the plain background rather
/// than overlaid on the dithered fill, since this is a 1-bit framebuffer and
/// there is no intermediate pixel colour to guarantee label contrast against
/// every level.
void draw_greyscale_test_card(uint8_t* fb, const ConsoleState& console_state)
{
    (void)console_state;

    constexpr int kMarginX = 8;
    constexpr int kBandWidth = kUiWidth - (kMarginX * 2);
    constexpr int kBandHeight = 30;
    constexpr int kLabelHeight = 9;
    constexpr int kLabelToBandGap = 2;
    constexpr int kEntryGap = 4;
    constexpr int kStartY = 44;
    constexpr int kLevelCount = 5;
    constexpr int kEntryHeight = kLabelHeight + kLabelToBandGap + kBandHeight;
    constexpr int kCardBottomY =
        kStartY + (kLevelCount * kEntryHeight) + ((kLevelCount - 1) * kEntryGap);
    // Top of the row 5 softkey cells (softkey top 41 + 4 rows x 57 pitch in
    // screens.cpp). The full-width bands must end above it so R5 HOME stays
    // readable (SCR-5). Moves to the shared layout header under #113.
    constexpr int kRow5SoftkeyTopY = 269;
    static_assert(kCardBottomY <= kRow5SoftkeyTopY,
                  "greyscale bands would overlap the row 5 softkey labels");
    constexpr fonts::FontFace kLabelFont = fonts::FontFace::Font5x7;
    constexpr const char* kCaption = "2X2 ORDERED DITHER";

    for (int level = 0; level < kLevelCount; ++level)
    {
        const int kEntryY = kStartY + (level * (kEntryHeight + kEntryGap));
        char label[16] = {};
        std::snprintf(label, sizeof(label), "L%d  %d/4", level, level);
        framebuffer::draw_text(fb, kMarginX, kEntryY, label, true, kLabelFont);
        framebuffer::fill_rect_dithered(fb, kMarginX, kEntryY + kLabelHeight + kLabelToBandGap,
                                        kBandWidth, kBandHeight, level);
    }

    // The caption shares the first label row, right-aligned, so nothing is
    // drawn in the row 5 or footer zones.
    framebuffer::draw_text(fb, kMarginX + kBandWidth - screens::text_width(kCaption, kLabelFont),
                           kStartY, kCaption, true, kLabelFont);
}

} // namespace settings_screens
