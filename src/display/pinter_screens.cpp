#include "pinter_screens.h"

#include <array>
#include <cstdint>
#include <cstdio>

#include "console_controller.h"
#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "text_case.h"
#include "ui_layout.h"

namespace pinter_screens
{

namespace
{

using screens::TextStyle;

/// @brief Draws the reset question in the otherwise empty body.
/// @details Every softkey except CANCEL and RESET is blank in this state, so
/// the body can say plainly which vessel and brew will be cleared. Uses the
/// same headline position as the other centred empty-state headlines.
void draw_reset_confirmation(uint8_t* fb, const ConsoleState& console_state)
{
    // Twelve pixels above ui_layout::kEmptyStateHeadlineY, with a 14-pixel
    // detail pitch and 16-pixel margins; to be aligned with the shared
    // empty state and row pitch.
    constexpr int kHeadlineY = 112;
    constexpr int kDetailFirstY = ui_layout::kEmptyStateHintY;
    constexpr int kDetailPitch = 14;
    constexpr int kDetailMarginX = 16;
    constexpr size_t kMaxDetailLines = 4U;
    constexpr TextStyle kDetailStyle = TextStyle::Hint;

    const uint8_t index = console_state.selected_pinter_index < console_state.pinters.size()
                              ? console_state.selected_pinter_index
                              : 0U;
    const PinterStatus& vessel = console_state.pinters[index];

    char headline[40] = {};
    std::snprintf(headline, sizeof(headline), "RESET %s?", vessel.label.data());
    text_case::copy_uppercase(headline, headline, sizeof(headline));
    screens::draw_centered_text(fb, ui_layout::kCentreX, kHeadlineY, headline, TextStyle::Headline);

    char detail[96] = {};
    std::snprintf(detail, sizeof(detail), "Clears %s and its stage timings. This cannot be undone.",
                  console_controller::pinter_brew_timing(vessel.brew_index).name);
    std::array<screens::TextSpan, kMaxDetailLines> lines = {};
    const size_t count = screens::wrap_text(detail, kDetailStyle, kUiWidth - (kDetailMarginX * 2),
                                            lines.data(), lines.size());
    for (size_t i = 0U; i < count; ++i)
    {
        char line[96] = {};
        std::snprintf(line, sizeof(line), "%.*s", static_cast<int>(lines[i].length),
                      lines[i].text);
        screens::draw_centered_text(fb, ui_layout::kCentreX,
                                    kDetailFirstY + (static_cast<int>(i) * kDetailPitch), line,
                                    kDetailStyle);
    }
}

} // namespace

/// @brief Draws only non-data navigation affordances for Pinter pages.
void draw_pinter_page(uint8_t* fb, const ConsoleState& console_state)
{
    switch (console_state.active_page)
    {
    case MenuPage::PinterSelectBrew:
        screens::draw_page_navigation_arrows(fb, console_state.pinter_catalogue_page_index > 0U,
                                             (console_state.pinter_catalogue_page_index + 1U) <
                                                 kPinterBrewListPageCount);
        return;
    case MenuPage::Pinter:
    {
        if (console_state.pinter_reset_confirming)
        {
            draw_reset_confirmation(fb, console_state);
            return;
        }

        // The body is blank on this page -- status is carried by the softkey
        // labels -- except when the primary action is blocked by a capacity
        // limit, since a two-line softkey hint is easy to press right past.
        // The reason goes in the footer, which is otherwise unused here: the
        // body has no gap between softkey rows wide enough once the vessel
        // keys use three-line labels (SCR-4, SCR-5). Same baseline as the
        // Home page's footer line.
        if (console_state.pinter_block_reason[0] != '\0')
        {
            screens::draw_centered_text(fb, ui_layout::kCentreX, ui_layout::kFooterLineY,
                                        console_state.pinter_block_reason.data(),
                                        TextStyle::Footer);
        }
        return;
    }
    case MenuPage::PinterStartTiming:
        // The recipe being started goes in the footer: every softkey row is
        // in use, and the operator should see what START will commit to.
        // The longest catalogue name fits the width (checked by a host test).
        screens::draw_centered_text(
            fb, ui_layout::kCentreX, ui_layout::kFooterLineY,
            console_controller::pinter_brew_timing(console_state.pinter_pending_brew_index).name,
            TextStyle::Footer);
        return;
    default:
        break;
    }
}

} // namespace pinter_screens
