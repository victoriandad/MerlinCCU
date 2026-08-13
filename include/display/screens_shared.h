#pragma once

#include <cstddef>

#include "console_model.h"
#include "environment_sensor_manager.h"
#include "fonts.h"

/// @brief Small cross-page-family primitives from `screens.cpp` that other
/// split-out page renderer files (e.g. `status_screens.cpp`) also need.
/// @details Not part of the public `screens.h` API -- only `screens.cpp` and
/// its sibling page-family translation units should include this.
namespace screens
{

/// @brief Returns the Home Assistant state label used on diagnostics screens.
const char* home_assistant_state_text(HomeAssistantConnectionState state);

/// @brief Returns a provider-neutral weather fetch state label.
const char* weather_fetch_state_text(HomeAssistantConnectionState state);

/// @brief One label/value pair rendered by `draw_compact_detail_rows`.
struct DetailRow
{
    const char* label;
    const char* value;
};

/// @brief Draws compact `Label: value` rows for data-dense pages.
void draw_compact_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y,
                              int row_pitch);

/// @brief Formats a fixed-point centi-Celsius temperature for the Status page.
void build_environment_temperature_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 humidity as percent relative humidity.
void build_environment_humidity_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 pressure as hPa, which is easier to scan than raw Pa.
void build_environment_pressure_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats a compact "Xs"/"Xm"/"Xh" elapsed-time label.
void build_elapsed_time_text(uint32_t elapsed_ms, char* out, size_t out_size);

/// @brief Formats one data-backed subsystem's freshness into a short,
/// consistent label ("LIVE", "STALE 12m", "NO DATA") -- issue #16's
/// stale-data display policy. See the definition in screens.cpp for the full
/// contract, including why deliberate placeholder/demo states should not use
/// this helper.
void build_data_freshness_text(bool currently_valid, uint32_t last_success_ms, uint32_t now_ms,
                               uint32_t stale_after_ms, char* out, size_t out_size);

/// @brief Measures text through one shared helper so layout math stays consistent.
int text_width(const char* text, fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1);

/// @brief Draws text centred around a given x-coordinate.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1);

/// @brief One softkey/footer label wrapped into up to three fixed-size lines.
struct WrappedSoftkeyLabel
{
    char line_one[48];
    char line_two[48];
    char line_three[48];
    int line_count;
};

/// @brief Wraps one label into at most `max_lines` renderable lines.
/// @details Softkeys are deliberately capped (two lines for most pages, three
/// where a page opts in) so long labels degrade gracefully without taking
/// over the rest of the screen layout. Each line greedily fits as much text
/// as the width allows, preferring to break on whitespace.
WrappedSoftkeyLabel wrap_label_lines(const char* label, fonts::FontFace font, int max_width,
                                     int max_lines);

/// @brief Draws the standard row-based presentation used by information pages.
/// @details The top banner carries the page title, so the body can stay clean
/// and consistent without extra boxes, subtitles, or divider lines.
void draw_info_page_rows(uint8_t* fb, const DetailRow* rows, size_t count);

/// @brief Draws shared left/right page-navigation arrows used by paged menus.
void draw_page_navigation_arrows(uint8_t* fb, bool show_left, bool show_right);

} // namespace screens
