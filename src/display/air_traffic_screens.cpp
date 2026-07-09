#include "air_traffic_screens.h"

#include <cstdio>

#include "framebuffer.h"
#include "panel_config.h"
#include "screens_shared.h"

namespace air_traffic_screens
{

namespace
{

constexpr int kTableHeaderY = 36;
constexpr int kTableDividerY = 46;
constexpr int kTableFirstRowY = 54;
constexpr int kTableRowPitchY = 18;
constexpr int kCallsignX = 12;
constexpr int kDistanceX = 90;
constexpr int kAltitudeX = 148;
constexpr int kBearingX = 208;
constexpr int kFooterY = kUiHeight - 10;

/// @brief Draws the nearby-aircraft table header row.
void draw_table_header(uint8_t* fb)
{
    constexpr fonts::FontFace kFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, kCallsignX, kTableHeaderY, "Callsign", true, kFont, 1);
    framebuffer::draw_text(fb, kDistanceX, kTableHeaderY, "Dist", true, kFont, 1);
    framebuffer::draw_text(fb, kAltitudeX, kTableHeaderY, "Alt", true, kFont, 1);
    framebuffer::draw_text(fb, kBearingX, kTableHeaderY, "Brg", true, kFont, 1);
    framebuffer::draw_hline(fb, 12, kUiWidth - 12, kTableDividerY, true);
}

/// @brief Draws one aircraft row.
void draw_table_row(uint8_t* fb, const AirTrafficEntry& entry, int row_y)
{
    constexpr fonts::FontFace kFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, kCallsignX, row_y, entry.callsign.data(), true, kFont, 1);
    framebuffer::draw_text(fb, kDistanceX, row_y, entry.distance_text.data(), true, kFont, 1);
    framebuffer::draw_text(fb, kAltitudeX, row_y, entry.altitude_text.data(), true, kFont, 1);
    framebuffer::draw_text(fb, kBearingX, row_y, entry.bearing_text.data(), true, kFont, 1);
}

/// @brief Draws the small provenance/coverage disclaimer at the page foot.
void draw_footer_disclaimer(uint8_t* fb)
{
    screens::draw_centered_text(fb, kUiWidth / 2, kFooterY, "ADS-B TRAFFIC ONLY", true,
                                fonts::FontFace::Font5x7, 1);
}

} // namespace

void draw_air_traffic_page(uint8_t* fb, const ConsoleState& console_state)
{
    const AirTrafficStatus& status = console_state.air_traffic_status;

    if (!status.configured)
    {
        const screens::DetailRow rows[] = {
            {"SOURCE", "Local Traffic (ADS-B)"},
            {"STATUS", "Disabled"},
            {"DETAIL", "Enable in Settings"},
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
        screens::draw_centered_text(fb, kUiWidth / 2, 92, message, true, fonts::FontFace::Font8x12,
                                    1);
        if (detail[0] != '\0')
        {
            screens::draw_centered_text(fb, kUiWidth / 2, 120, detail, true,
                                        fonts::FontFace::Font5x7, 1);
        }
        draw_footer_disclaimer(fb);
        return;
    }

    draw_table_header(fb);
    for (uint8_t i = 0U; i < status.aircraft_count; ++i)
    {
        draw_table_row(fb, status.aircraft[i], kTableFirstRowY + (static_cast<int>(i) * kTableRowPitchY));
    }
    draw_footer_disclaimer(fb);
}

} // namespace air_traffic_screens
