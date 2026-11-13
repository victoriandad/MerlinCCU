#include "air_traffic_screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "air_traffic_manager.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "ui_layout.h"

namespace air_traffic_screens
{

namespace
{

using screens::TextStyle;

constexpr int kCallsignX = ui_layout::kMarginX;
constexpr int kDistanceX = 90;
constexpr int kAltitudeX = 148;
constexpr int kBearingX = 208;

/// @brief Gap between the last table row and the provenance line beneath it.
constexpr int kProvenanceGapY = 6;

/// @brief Where the provenance line goes in the table view. The footer
/// belongs to the paging arrows there (SCR-4), so the line sits in the body
/// directly under the last row.
int table_provenance_y()
{
    const ui_layout::TableFrame kTable = ui_layout::table_frame();
    return kTable.first_row_y + (static_cast<int>(kAirTrafficRowsPerPage) * kTable.row_pitch) +
           kProvenanceGapY;
}

/// @brief Draws the nearby-aircraft table header row.
void draw_table_header(uint8_t* fb)
{
    const ui_layout::TableFrame kTable = ui_layout::table_frame();
    screens::draw_text(fb, kCallsignX, kTable.header_y, "Callsign", TextStyle::TableHeader);
    screens::draw_text(fb, kDistanceX, kTable.header_y, "Dist", TextStyle::TableHeader);
    screens::draw_text(fb, kAltitudeX, kTable.header_y, "Alt", TextStyle::TableHeader);
    screens::draw_text(fb, kBearingX, kTable.header_y, "Brg", TextStyle::TableHeader);
    framebuffer::draw_hline(fb, kTable.left_x, kTable.right_x, kTable.divider_y, true);
}

/// @brief Draws one aircraft row.
void draw_table_row(uint8_t* fb, const AirTrafficEntry& entry, int row_y)
{
    screens::draw_text(fb, kCallsignX, row_y, entry.callsign.data(), TextStyle::TableCell);
    screens::draw_text(fb, kDistanceX, row_y, entry.distance_text.data(), TextStyle::TableCell);
    screens::draw_text(fb, kAltitudeX, row_y, entry.altitude_text.data(), TextStyle::TableCell);
    screens::draw_text(fb, kBearingX, row_y, entry.bearing_text.data(), TextStyle::TableCell);
}

/// @brief Draws the small provenance/coverage disclaimer, plus a data-freshness
/// label, centred at `y` -- issue #16's stale-data display policy.
void draw_provenance_line(uint8_t* fb, const AirTrafficStatus& status, uint32_t now_ms, int y)
{
    char freshness_text[16] = {};
    screens::build_data_freshness_text(status.data_valid, status.last_success_ms, now_ms,
                                       air_traffic_manager::kAirTrafficStaleAfterMs, freshness_text,
                                       sizeof(freshness_text));

    char footer_text[32] = {};
    std::snprintf(footer_text, sizeof(footer_text), "ADS-B TRAFFIC ONLY - %s", freshness_text);
    screens::draw_centered_text(fb, ui_layout::kCentreX, y, footer_text, TextStyle::Footer);
}

/// @brief Draws the paginated Callsign/Dist/Alt/Brg table view.
void draw_tabular_view(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    const AirTrafficStatus& status = console_state.air_traffic_status;
    const uint8_t page_count = air_traffic_list_page_count(status.aircraft_count);
    const uint8_t page_index = console_state.air_traffic_page_index;
    const uint8_t start = static_cast<uint8_t>(page_index * kAirTrafficRowsPerPage);
    const uint8_t end = static_cast<uint8_t>(
        std::min<uint16_t>(static_cast<uint16_t>(start) + kAirTrafficRowsPerPage,
                          status.aircraft_count));

    const ui_layout::TableFrame kTable = ui_layout::table_frame();
    draw_table_header(fb);
    for (uint8_t i = start; i < end; ++i)
    {
        draw_table_row(fb, status.aircraft[i],
                       kTable.first_row_y + (static_cast<int>(i - start) * kTable.row_pitch));
    }
    screens::draw_page_navigation_arrows(fb, page_index > 0U,
                                        (page_index + 1U) < page_count);
    draw_provenance_line(fb, status, now_ms, table_provenance_y());
}

/// @brief Integer midpoint-circle rings, matching the radar screensaver's algorithm.
void draw_ring(uint8_t* fb, int cx, int cy, int radius)
{
    int x = radius;
    int y = 0;
    int err = 0;

    while (x >= y)
    {
        framebuffer::set_pixel(fb, cx + x, cy + y, true);
        framebuffer::set_pixel(fb, cx + y, cy + x, true);
        framebuffer::set_pixel(fb, cx - y, cy + x, true);
        framebuffer::set_pixel(fb, cx - x, cy + y, true);
        framebuffer::set_pixel(fb, cx - x, cy - y, true);
        framebuffer::set_pixel(fb, cx - y, cy - x, true);
        framebuffer::set_pixel(fb, cx + y, cy - x, true);
        framebuffer::set_pixel(fb, cx + x, cy - y, true);

        ++y;
        if (err <= 0)
        {
            err += (2 * y) + 1;
        }
        if (err > 0)
        {
            --x;
            err -= (2 * x) + 1;
        }
    }
}

constexpr int kPlotCenterX = ui_layout::kCentreX;
// Plot spans y 36-280: below the header, above the footer line.
constexpr int kPlotTopY = ui_layout::kBodyTopY + 2;
constexpr int kPlotBottomY = 280;
constexpr int kPlotCenterY = (kPlotTopY + kPlotBottomY) / 2;
constexpr int kPlotRadius = 96;

/// @brief Converts a distance/bearing pair to a screen point on the PPI plot.
/// @details Bearing is compass degrees (0 = North = up), not screen angle, so
/// this rotates the usual sin/cos assignment to put North at the top.
void polar_to_screen(int16_t distance_deci_nm, int16_t bearing_deg, uint16_t range_nm, int* out_x,
                    int* out_y)
{
    const double distance_nm = static_cast<double>(distance_deci_nm) / 10.0;
    const double clamped_nm = std::min(distance_nm, static_cast<double>(range_nm));
    const double radius_px = (clamped_nm / static_cast<double>(range_nm)) * kPlotRadius;
    const double radians = static_cast<double>(bearing_deg) * (3.14159265358979 / 180.0);

    *out_x = kPlotCenterX + static_cast<int>(std::lround(radius_px * std::sin(radians)));
    *out_y = kPlotCenterY - static_cast<int>(std::lround(radius_px * std::cos(radians)));
}

/// @brief Draws one aircraft's snail trail (oldest, smallest first) then its
/// current position as a bold marker with a compact callsign tag.
void draw_plot_blip(uint8_t* fb, const AirTrafficEntry& entry, uint16_t range_nm)
{
    if (entry.trail_count > 1U)
    {
        for (uint8_t i = 0U; i + 1U < entry.trail_count; ++i)
        {
            const AirTrafficTrailPoint& point = entry.trail[i];
            int x = 0;
            int y = 0;
            polar_to_screen(point.distance_deci_nm, point.bearing_deg, range_nm, &x, &y);

            const uint8_t age_from_newest = static_cast<uint8_t>(entry.trail_count - 1U - i);
            if (age_from_newest <= 2U)
            {
                framebuffer::fill_rect(fb, x - 1, y - 1, 2, 2, true);
            }
            else
            {
                framebuffer::set_pixel(fb, x, y, true);
            }
        }
    }

    int x = 0;
    int y = 0;
    polar_to_screen(entry.distance_deci_nm, entry.bearing_deg, range_nm, &x, &y);
    framebuffer::fill_rect(fb, x - 1, y - 1, 3, 3, true);
    screens::draw_text(fb, x + 4, y - 6, entry.callsign.data(), TextStyle::Annotation);
}

/// @brief Draws the ATC-style PPI plot: range rings, compass tick, home
/// marker, and every tracked aircraft with its snail trail.
void draw_plot_view(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    const AirTrafficStatus& status = console_state.air_traffic_status;
    const uint16_t range_nm = (status.configured_radius_nm != 0U) ? status.configured_radius_nm : 30U;

    draw_ring(fb, kPlotCenterX, kPlotCenterY, kPlotRadius);
    draw_ring(fb, kPlotCenterX, kPlotCenterY, kPlotRadius / 3);
    draw_ring(fb, kPlotCenterX, kPlotCenterY, (kPlotRadius * 2) / 3);
    framebuffer::draw_vline(fb, kPlotCenterX, kPlotCenterY - kPlotRadius - 4,
                            kPlotCenterY - kPlotRadius, true);
    framebuffer::fill_rect(fb, kPlotCenterX - 1, kPlotCenterY - 1, 3, 3, true);

    char range_text[8] = {};
    std::snprintf(range_text, sizeof(range_text), "%unm", static_cast<unsigned>(range_nm));
    screens::draw_text(fb, kPlotCenterX + 4, kPlotCenterY - kPlotRadius, range_text,
                       TextStyle::Annotation);

    for (uint8_t i = 0U; i < status.aircraft_count; ++i)
    {
        draw_plot_blip(fb, status.aircraft[i], range_nm);
    }

    draw_provenance_line(fb, status, now_ms, ui_layout::kFooterLineY);
}

} // namespace

void draw_air_traffic_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    const AirTrafficStatus& status = console_state.air_traffic_status;

    if (!status.configured)
    {
        const screens::DetailRow rows[] = {
            {"SOURCE", "ADS-B"},
            {"STATUS", status.enabled ? "Enabled" : "Disabled"},
            {"DETAIL", status.enabled ? "No coordinates set" : "Enable in Settings"},
        };
        screens::draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
        return;
    }

    if (!status.data_valid || status.aircraft_count == 0U)
    {
        char detail[24] = {};
        if (!status.data_valid && status.last_http_status > 0)
        {
            std::snprintf(detail, sizeof(detail), "HTTP %d", status.last_http_status);
        }
        else if (!status.data_valid && status.last_error != 0)
        {
            std::snprintf(detail, sizeof(detail), "ERR %d", status.last_error);
        }

        const char* message = status.data_valid ? "NO AIRCRAFT NEARBY" : "WAITING FOR DATA";
        screens::draw_empty_state(fb, message, detail);
        draw_provenance_line(fb, status, now_ms, ui_layout::kFooterLineY);
        return;
    }

    if (console_state.air_traffic_view_mode == AirTrafficViewMode::Plot)
    {
        draw_plot_view(fb, console_state, now_ms);
    }
    else
    {
        draw_tabular_view(fb, console_state, now_ms);
    }
}

} // namespace air_traffic_screens
