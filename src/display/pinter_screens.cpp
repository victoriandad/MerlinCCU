#include "pinter_screens.h"

#include <cstdint>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace pinter_screens
{

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
        // The body is blank on this page -- status is carried by the softkey
        // labels -- except when the primary action is blocked by a capacity
        // limit, since a two-line softkey hint is easy to press right past.
        // The reason goes in the footer, which is otherwise unused here: the
        // body has no gap between softkey rows wide enough once the vessel
        // keys use three-line labels (SCR-4, SCR-5). Same baseline as the
        // Home page's footer line; moves to the shared layout header under #113.
        constexpr int kFooterTextY = kUiHeight - 18;
        if (console_state.pinter_block_reason[0] != '\0')
        {
            screens::draw_centered_text(fb, kUiWidth / 2, kFooterTextY,
                                        console_state.pinter_block_reason.data(), true,
                                        fonts::FontFace::Font5x7, 1);
        }
        return;
    }
    default:
        break;
    }
}

} // namespace pinter_screens
