#include "shares_screens.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "settings_controller.h"
#include "share_price_manager.h"
#include "ui_layout.h"

namespace shares_screens
{

namespace
{

using screens::TextStyle;

/// @brief Top of the share detail rows, beneath the graph and its labels.
constexpr int kDetailRowsTopY = 164;

/// @brief Smallest height the graph's scale spans, in hundredths of a
/// percent: a 0.2% wobble then fills a fifth of the graph, not all of it,
/// so the size of a move reads at a glance (#178).
constexpr int64_t kMinScaleSpanBasisPoints = 100;

/// @brief Formats a change in hundredths of a percent as "+1.25%" or
/// "-12.5%", with fewer decimals as the number grows so it fits the axis.
void format_percent(int64_t basis_points, char* output, size_t output_size)
{
    const char* sign = basis_points > 0 ? "+" : (basis_points < 0 ? "-" : "");
    const int64_t magnitude = basis_points < 0 ? -basis_points : basis_points;
    if (magnitude >= 10000)
    {
        std::snprintf(output, output_size, "%s%ld%%", sign, static_cast<long>(magnitude / 100));
    }
    else if (magnitude >= 1000)
    {
        std::snprintf(output, output_size, "%s%ld.%ld%%", sign, static_cast<long>(magnitude / 100),
                      static_cast<long>((magnitude % 100) / 10));
    }
    else
    {
        std::snprintf(output, output_size, "%s%ld.%02ld%%", sign,
                      static_cast<long>(magnitude / 100), static_cast<long>(magnitude % 100));
    }
}

/// @brief Where the timeline starts for each period, so the X axis says how
/// far back the graph reaches (#178).
const char* period_start_label(SharePeriod period)
{
    switch (period)
    {
    case SharePeriod::Today:
        return "OPEN";
    case SharePeriod::Week:
        return "5 DAYS AGO";
    case SharePeriod::Month:
        return "1 MONTH AGO";
    case SharePeriod::Year:
        return "1 YEAR AGO";
    case SharePeriod::AllTime:
        return "FIRST TRADE";
    }
    return "";
}

/// @brief Returns true when a share row has enough history to draw a useful graph.
bool share_history_has_values(const ShareWatchEntry& share)
{
    for (const float value : share.history_points)
    {
        if (value > 0.0F)
        {
            return true;
        }
    }

    return false;
}

/// @brief Draws one full-width share history graph in the centre detail region.
/// @details The scale is the change from the period's base price (the
/// previous close for Today, else the first close), in percent, so a share's
/// direction and the size of its move read the same at any price (#178).
/// Bars rise or fall from a dotted base line; the scale always includes the
/// base and spans at least kMinScaleSpanBasisPoints.
void draw_share_history_graph(uint8_t* fb, const ShareWatchEntry& share, SharePeriod period)
{
    // Shorter than a full-page graph: the share's rows sit beneath it.
    constexpr int kGraphHeight = 94;
    constexpr int kBaseLineDotStep = 3;
    const int point_count = static_cast<int>(share.history_points.size());
    if (point_count < 2 || !share_history_has_values(share))
    {
        screens::draw_empty_state(fb, "NO PRICE HISTORY", nullptr);
        return;
    }

    const float base = share.base_valid ? share.base_price : share.history_points[0];
    if (base <= 0.0F)
    {
        screens::draw_empty_state(fb, "NO PRICE HISTORY", nullptr);
        return;
    }
    std::array<int64_t, 24> changes = {};
    int64_t low = 0;
    int64_t high = 0;
    for (size_t i = 0U; i < changes.size(); ++i)
    {
        const float ratio = (share.history_points[i] - base) / base;
        changes[i] = std::lround(static_cast<double>(ratio) * 10000.0);
        low = std::min(low, changes[i]);
        high = std::max(high, changes[i]);
    }
    if (high - low < kMinScaleSpanBasisPoints)
    {
        const int64_t centre = (high + low) / 2;
        low = std::min<int64_t>(centre - (kMinScaleSpanBasisPoints / 2), 0);
        high = std::max<int64_t>(low + kMinScaleSpanBasisPoints, 0);
    }

    char max_text[16] = {};
    char middle_text[16] = {};
    char min_text[16] = {};
    format_percent(high, max_text, sizeof(max_text));
    format_percent((high + low) / 2, middle_text, sizeof(middle_text));
    format_percent(low, min_text, sizeof(min_text));

    const screens::GraphPlotArea plot_area = screens::draw_history_graph_frame(
        fb, kGraphHeight, {max_text, middle_text, min_text, period_start_label(period), "NOW"});
    const int base_y = screens::graph_plot_y(plot_area, 0, low, high);
    for (int x = plot_area.x + plot_area.inset; x < plot_area.x + plot_area.width - plot_area.inset;
         x += kBaseLineDotStep)
    {
        framebuffer::set_pixel(fb, x, base_y, true);
    }
    for (int i = 0; i < point_count; ++i)
    {
        const int x = screens::graph_plot_x(plot_area, i, point_count);
        const int y = screens::graph_plot_y(plot_area, changes[static_cast<size_t>(i)], low, high);
        framebuffer::draw_vline(fb, x, std::min(y, base_y), std::max(y, base_y), true);
    }
}

/// @brief The price with its currency in the unit people quote it in:
/// pence (GBX) as pounds, e.g. "13.72 GBP"; others as sent, e.g.
/// "182.5 USD" (#178).
void format_share_price(const ShareWatchEntry& share, char* output, size_t output_size)
{
    const char* price = share.price_text.data();
    const char* currency = share.currency.data();
    if (std::strcmp(currency, "GBX") == 0)
    {
        char digits[16] = {};
        size_t length = 0U;
        for (const char* c = price; *c != '\0' && length + 1U < sizeof(digits); ++c)
        {
            if (*c != ',')
            {
                digits[length++] = *c;
            }
        }
        char* end = nullptr;
        const double pence = std::strtod(digits, &end);
        if (end != digits)
        {
            std::snprintf(output, output_size, "%.2f GBP", pence / 100.0);
            return;
        }
    }
    if (currency[0] != '\0' && price[0] != '\0')
    {
        std::snprintf(output, output_size, "%s %s", price, currency);
        return;
    }
    std::snprintf(output, output_size, "%s", price);
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
        screens::draw_empty_state(fb, "NO SHARES", "Add shares in web config");
        return;
    }

    (void)fb;
}

/// @brief Draws one watched share's detail page.
void draw_share_detail_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    if (console_state.selected_share_index >= console_state.share_count)
    {
        screens::draw_empty_state(fb, "NO SHARE", nullptr);
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

    char price_text[24] = {};
    format_share_price(share, price_text, sizeof(price_text));
    const screens::DetailRow rows[] = {
        {"NAME", share.display_name.data()},
        {"SYMBOL", share.symbol.data()},
        {"PERIOD", settings_controller::share_period_name(console_state.share_period)},
        {"DATA", data_text},
        {"PRICE", price_text},
        {"CHANGE", share.change_text.data()},
    };

    screens::draw_compact_detail_rows(fb, rows, sizeof(rows) / sizeof(rows[0]), kDetailRowsTopY,
                                      ui_layout::row_pitch(TextStyle::CompactRow));
}

} // namespace shares_screens
