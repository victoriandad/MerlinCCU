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
        // Centre data is otherwise intentionally blank on this page -- status is
        // carried by the softkey labels -- except for this one case: when the
        // primary action is blocked by a capacity limit, that reason goes here
        // too, since a two-line softkey hint is easy to press right past.
        if (console_state.pinter_block_reason[0] != '\0')
        {
            screens::draw_centered_text(fb, kUiWidth / 2, 150,
                                        console_state.pinter_block_reason.data(), true,
                                        fonts::FontFace::Font5x7, 1);
        }
        return;
    default:
        break;
    }
}

} // namespace pinter_screens
