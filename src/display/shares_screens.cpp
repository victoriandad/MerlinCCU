#include "shares_screens.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "settings_controller.h"
#include "share_price_manager.h"
#include "text_case.h"
#include "ui_layout.h"

namespace shares_screens
{

namespace
{

using screens::TextStyle;

/// @brief Positions on the Shares pages that are not yet on the shared
/// layout: the empty-state headline sits 12 pixels above
/// ui_layout::kEmptyStateHeadlineY, the no-history headline elsewhere again,
/// and the detail rows use a 14-pixel pitch. To be aligned.
constexpr int kEmptyHeadlineY = 112;
constexpr int kNoHistoryHeadlineY = 106;
constexpr int kDetailRowsTopY = 164;
constexpr int kDetailRowPitch = 14;

/// @brief Formats one graph-axis share value using compact thousands separators.
void format_share_graph_value(uint16_t value, char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0U)
    {
        return;
    }

    output[0] = '\0';
    if (value >= 1000U && value < 10000U)
    {
        const unsigned thousands = value / 1000U;
        const unsigned remainder = value % 1000U;
        std::snprintf(output, output_size, "%u,%03u", thousands, remainder);
        return;
    }

    std::snprintf(output, output_size, "%u", static_cast<unsigned>(value));
}

/// @brief Returns true when a share row has enough history to draw a useful graph.
bool share_history_has_values(const ShareWatchEntry& share)
{
    for (uint16_t value : share.history_points)
    {
        if (value > 0U)
        {
            return true;
        }
    }

    return false;
}

/// @brief Draws one full-width share history graph in the centre detail region.
void draw_share_history_graph(uint8_t* fb, const ShareWatchEntry& share, SharePeriod period)
{
    constexpr int kGraphX = ui_layout::kGraphLeftX;
    constexpr int kGraphY = ui_layout::kContentTopY;
    constexpr int kGraphWidth = ui_layout::kGraphWidth;
    constexpr int kGraphHeight = 94;
    constexpr int kGraphMinLabelGapY = 6;
    constexpr int kPeriodLabelInsetY = 2;
    const int point_count = static_cast<int>(share.history_points.size());
    if (point_count < 2 || !share_history_has_values(share))
    {
        screens::draw_centered_text(fb, ui_layout::kCentreX, kNoHistoryHeadlineY,
                                    "NO PRICE HISTORY", TextStyle::Headline);
        return;
    }

    uint16_t min_value = share.history_points[0];
    uint16_t max_value = share.history_points[0];
    for (uint16_t value : share.history_points)
    {
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    }

    const screens::GraphPlotArea plot_area{kGraphX, kGraphY, kGraphWidth, kGraphHeight,
                                           ui_layout::kGraphPlotInset};
    screens::draw_graph_plot_border(fb, plot_area);
    const int base_y = screens::graph_plot_y(plot_area, min_value, min_value, max_value);
    for (int i = 0; i < point_count; ++i)
    {
        const uint16_t value = share.history_points[static_cast<size_t>(i)];
        const int x = screens::graph_plot_x(plot_area, i, point_count);
        const int y = screens::graph_plot_y(plot_area, value, min_value, max_value);
        framebuffer::draw_vline(fb, x, y, base_y, true);
    }

    char period_label[16] = {};
    text_case::copy_uppercase(settings_controller::share_period_name(period), period_label,
                              sizeof(period_label));
    screens::draw_text(fb, kGraphX, kGraphY + kPeriodLabelInsetY, period_label,
                       TextStyle::Annotation);

    char min_value_text[16] = {};
    char max_value_text[16] = {};
    char min_label[24] = {};
    char max_label[24] = {};
    format_share_graph_value(min_value, min_value_text, sizeof(min_value_text));
    format_share_graph_value(max_value, max_value_text, sizeof(max_value_text));
    std::snprintf(min_label, sizeof(min_label), "MIN %s", min_value_text);
    std::snprintf(max_label, sizeof(max_label), "MAX %s", max_value_text);

    const int label_y = kGraphY + kGraphHeight + kGraphMinLabelGapY;
    screens::draw_text(fb, kGraphX, label_y, min_label, TextStyle::Annotation);
    screens::draw_right_aligned_text(fb, kGraphX + kGraphWidth, label_y, max_label,
                                     TextStyle::Annotation);
}

} // namespace

/// @brief Draws the share watchlist page.
/// @details Shares are selected from the surrounding softkeys. The centre of the
/// watchlist page intentionally stays clear so the selected share name and price
/// are not duplicated beside the L1 label.
void draw_shares_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.share_count == 0U)
    {
        screens::draw_centered_text(fb, ui_layout::kCentreX, kEmptyHeadlineY, "NO SHARES",
                                    TextStyle::Headline);
        screens::draw_centered_text(fb, ui_layout::kCentreX, ui_layout::kEmptyStateHintY,
                                    "ADD VIA WEB CONFIG", TextStyle::Hint);
        return;
    }

    (void)fb;
}

/// @brief Draws one watched share's detail page.
void draw_share_detail_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    if (console_state.selected_share_index >= console_state.share_count)
    {
        screens::draw_centered_text(fb, ui_layout::kCentreX, kEmptyHeadlineY, "NO SHARE",
                                    TextStyle::Headline);
        return;
    }

    const ShareWatchEntry& share = console_state.watched_shares[console_state.selected_share_index];
    draw_share_history_graph(fb, share, console_state.share_period);

    // last_success_ms stays 0 for as long as live fetching is disabled
    // (kEnableLiveShareFetch in share_price_manager.cpp) -- that's a
    // deliberate placeholder, not a freshness problem, so it gets its own
    // "DEMO" label rather than running through the shared freshness helper.
    // See issue #16's stale-data display policy.
    char data_text[16] = {};
    if (console_state.share_data_last_success_ms == 0U)
    {
        std::snprintf(data_text, sizeof(data_text), "DEMO");
    }
    else
    {
        screens::build_data_freshness_text(
            console_state.share_data_valid, console_state.share_data_last_success_ms, now_ms,
            share_price_manager::kShareStaleAfterMs, data_text, sizeof(data_text));
    }

    const screens::DetailRow rows[] = {
        {"NAME", share.display_name.data()},
        {"SYMBOL", share.symbol.data()},
        {"PERIOD", settings_controller::share_period_name(console_state.share_period)},
        {"DATA", data_text},
        {"PRICE", share.price_text.data()},
        {"CHANGE", share.change_text.data()},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), kDetailRowsTopY,
                                      kDetailRowPitch);
}

} // namespace shares_screens
