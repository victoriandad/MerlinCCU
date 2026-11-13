#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Renders the live Weather page and its forecast periods (issue #45).
/// @details Split out of `screens.cpp` -- shared drawing primitives (detail
/// rows, centred text, label wrapping) stay there; this file owns only the
/// Weather-page-specific formatting, forecast slicing, and layout.
namespace weather_screens
{

/// @brief What the Weather page body shows (STATE-1).
enum class WeatherPageState : uint8_t
{
    /// The source is not set up; the hint says where to set it up (STATE-5).
    NotConfigured,
    /// The first fetch has not finished yet.
    Waiting,
    /// The latest fetch failed and there is nothing to show.
    Failed,
    /// The source answered but sent nothing for the selected period.
    NoData,
    /// The source sent current conditions but no forecast for the period.
    CurrentOnly,
    /// A forecast table for the period; freshness, including STALE, is in the footer.
    Forecast,
};

/// @brief The Weather page's state and, for every state but Forecast, the
/// empty-state headline and hint it shows.
struct WeatherPageMessage
{
    WeatherPageState state;
    const char* headline;
    std::array<char, 48> hint;
};

/// @brief Decides what the Weather page shows for `console_state`.
/// @details Shared by the renderer and the host tests, so the wording each
/// state uses can be checked without decoding pixels.
WeatherPageMessage weather_page_message(const ConsoleState& console_state);

/// @brief Draws the live weather page reached directly from Home.
/// @details `now_ms` is the caller's boot-uptime sample (`to_ms_since_boot`)
/// used for the source footer's data-freshness text -- passed in rather than
/// read here so this file has no Pico SDK dependency (issue #71).
void draw_weather_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms);

} // namespace weather_screens
