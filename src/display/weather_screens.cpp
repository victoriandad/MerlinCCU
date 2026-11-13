#include "weather_screens.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "framebuffer.h"
#include "home_assistant_manager.h"
#include "panel_config.h"
#include "screens_shared.h"

#if __has_include("weather_display_config.h")
#include "weather_display_config.h"
#else
inline constexpr char HOME_ASSISTANT_WEATHER_SOURCE_LABEL[] = "";
#endif

namespace weather_screens
{

namespace
{

/// @brief Returns the shared left inset for the weather-source footer.
constexpr int weather_source_footer_left_x()
{
    return 12;
}

/// @brief Returns the footer baseline used for weather-source attribution text.
constexpr int weather_source_footer_bottom_y()
{
    return kUiHeight - 18;
}

/// @brief Returns the y-position reserved for sunrise and sunset information.
constexpr int weather_sun_times_y()
{
    return 244;
}

/// @brief Returns the maximum width available to the weather-source footer text.
constexpr int weather_source_footer_max_width()
{
    return kUiWidth - (weather_source_footer_left_x() * 2);
}

/// @brief Parses a `HH:MM` string into minutes after midnight.
/// @details A tiny local parser keeps the firmware independent of heavier
/// locale/time helpers that are unnecessary on the Pico.
bool parse_clock_text_minutes(const char* text, int* out_minutes)
{
    if (text == nullptr || out_minutes == nullptr || std::strlen(text) < 5 || text[2] != ':')
    {
        return false;
    }

    const char kHourTens = text[0];
    const char kHourOnes = text[1];
    const char kMinuteTens = text[3];
    const char kMinuteOnes = text[4];

    if (kHourTens < '0' || kHourTens > '9' || kHourOnes < '0' || kHourOnes > '9' ||
        kMinuteTens < '0' || kMinuteTens > '9' || kMinuteOnes < '0' || kMinuteOnes > '9')
    {
        return false;
    }

    const int kHours = ((kHourTens - '0') * 10) + (kHourOnes - '0');
    const int kMinutes = ((kMinuteTens - '0') * 10) + (kMinuteOnes - '0');
    if (kHours < 0 || kHours > 23 || kMinutes < 0 || kMinutes > 59)
    {
        return false;
    }

    *out_minutes = (kHours * 60) + kMinutes;
    return true;
}

struct ForecastRenderEntry
{
    uint8_t forecast_index;
    int absolute_minutes;
};

struct ForecastRenderSlice
{
    std::array<ForecastRenderEntry, kWeatherForecastEntryCount> entries;
    uint8_t count;
    bool current_time_valid;
    int current_hour_floor;
};

// One table frame for every forecast period (BODY-3): header line, divider,
// first row and row pitch are shared; only the columns differ.
constexpr size_t kForecastVisibleRows = 10U;
constexpr int kForecastHeaderY = 36;
constexpr int kForecastDividerY = 46;
constexpr int kForecastFirstRowY = 54;
constexpr int kForecastRowPitchY = 18;
constexpr int kForecastTableLeftX = 12;
constexpr int kForecastTableRightX = kUiWidth - 12;
constexpr fonts::FontFace kForecastHeaderFont = fonts::FontFace::Font5x7;
constexpr fonts::FontFace kForecastBodyFont = fonts::FontFace::Font5x7;

/// @brief One forecast table column: where it starts and its header.
struct ForecastColumn
{
    int x;
    const char* header;
};

/// @brief Columns of a forecast table, left to right: when, temperature,
/// wind, and conditions.
using ForecastColumns = std::array<ForecastColumn, 4>;

// The wind unit is written the same way in both tables (TXT-6).
constexpr ForecastColumns kHourlyColumns = {{
    {kForecastTableLeftX, "Time"},
    {60, "Temp"},
    {102, "Wind mph"},
    {160, "Conditions"},
}};

// The day column is wider than the time column so "Tomorrow" fits.
constexpr ForecastColumns kDailyColumns = {{
    {kForecastTableLeftX, "Day"},
    {66, "Temp"},
    {102, "Wind mph"},
    {160, "Conditions"},
}};

/// @brief Returns the top of forecast table row `index`.
constexpr int forecast_row_y(size_t index)
{
    return kForecastFirstRowY + (static_cast<int>(index) * kForecastRowPitchY);
}

/// @brief Reconstructs monotonic forecast minutes and drops stale pre-now rows.
/// @details Weather rows expose only local `HH:MM` text, so this helper rebuilds
/// a chronological timeline, then returns only the forward-looking slice that
/// matches the active local hour when clock sync data is available.
ForecastRenderSlice build_forecast_render_slice(const ConsoleState& console_state)
{
    ForecastRenderSlice slice = {};
    slice.count = 0;
    slice.current_time_valid = false;
    slice.current_hour_floor = 0;

    constexpr int kDayMinutes = 24 * 60;
    std::array<int, kWeatherForecastEntryCount> raw_minutes = {};
    std::array<int, kWeatherForecastEntryCount> absolute_minutes = {};
    const uint8_t kForecastCount =
        std::min(console_state.home_assistant_status.weather_forecast_count,
                 static_cast<uint8_t>(kWeatherForecastEntryCount));
    uint8_t parsed_count = 0;
    while (parsed_count < kForecastCount)
    {
        int parsed_minutes = 0;
        if (!parse_clock_text_minutes(
                console_state.home_assistant_status.weather_forecast[parsed_count].time_text.data(),
                &parsed_minutes))
        {
            break;
        }

        raw_minutes[parsed_count] = parsed_minutes;
        ++parsed_count;
    }

    if (parsed_count == 0)
    {
        return slice;
    }

    int current_minutes = 0;
    if (console_state.time_status.synced &&
        parse_clock_text_minutes(console_state.time_status.time_text.data(), &current_minutes))
    {
        slice.current_time_valid = true;
        slice.current_hour_floor = (current_minutes / 60) * 60;
    }

    if (slice.current_time_valid)
    {
        constexpr std::array<int, 3> kOffsetCandidates = {-kDayMinutes, 0, kDayMinutes};
        int best_offset = 0;
        int best_distance = 0x7fffffff;
        for (const int candidate_offset : kOffsetCandidates)
        {
            const int candidate_minutes = raw_minutes[0] + candidate_offset;
            const int candidate_distance = candidate_minutes > slice.current_hour_floor
                                               ? (candidate_minutes - slice.current_hour_floor)
                                               : (slice.current_hour_floor - candidate_minutes);
            if (candidate_distance < best_distance)
            {
                best_distance = candidate_distance;
                best_offset = candidate_offset;
            }
        }

        absolute_minutes[0] = raw_minutes[0] + best_offset;
    }
    else
    {
        absolute_minutes[0] = raw_minutes[0];
    }

    int day_offset = absolute_minutes[0] - raw_minutes[0];
    for (uint8_t i = 1; i < parsed_count; ++i)
    {
        int candidate_minutes = raw_minutes[i] + day_offset;
        while (candidate_minutes < absolute_minutes[i - 1])
        {
            day_offset += kDayMinutes;
            candidate_minutes = raw_minutes[i] + day_offset;
        }

        absolute_minutes[i] = candidate_minutes;
    }

    uint8_t first_future_index = 0;
    if (slice.current_time_valid)
    {
        while (first_future_index < parsed_count &&
               absolute_minutes[first_future_index] < slice.current_hour_floor)
        {
            ++first_future_index;
        }
    }

    for (uint8_t i = first_future_index; i < parsed_count; ++i)
    {
        slice.entries[slice.count] = {i, absolute_minutes[i]};
        ++slice.count;
    }

    return slice;
}

/// @brief Chooses representative hourly rows for the Next 24 Hours weather layout.
/// @details The provider cache holds up to 24 rows, but the EL320 page can only
/// show a readable subset. Sampling preserves the beginning and end of the
/// 24-hour window rather than merely showing the next few consecutive hours.
uint8_t build_next_twenty_four_hours_forecast_sample(
    const ForecastRenderSlice& forecast_slice,
    std::array<uint8_t, kForecastVisibleRows>& out_forecast_indices)
{
    out_forecast_indices.fill(0);
    if (forecast_slice.count == 0)
    {
        return 0;
    }

    constexpr int kDayMinutes = 24 * 60;
    const int period_end_minutes =
        (forecast_slice.current_time_valid ? forecast_slice.current_hour_floor
                                           : forecast_slice.entries[0].absolute_minutes) +
        kDayMinutes;
    std::array<uint8_t, kWeatherForecastEntryCount> next_twenty_four_hour_entries = {};
    uint8_t next_twenty_four_hour_entry_count = 0;
    for (uint8_t i = 0; i < forecast_slice.count; ++i)
    {
        if (forecast_slice.entries[i].absolute_minutes >= period_end_minutes)
        {
            break;
        }

        next_twenty_four_hour_entries[next_twenty_four_hour_entry_count] =
            forecast_slice.entries[i].forecast_index;
        ++next_twenty_four_hour_entry_count;
    }

    if (next_twenty_four_hour_entry_count == 0)
    {
        next_twenty_four_hour_entries[0] = forecast_slice.entries[0].forecast_index;
        next_twenty_four_hour_entry_count = 1;
    }

    const uint8_t max_rows = static_cast<uint8_t>(out_forecast_indices.size());
    if (next_twenty_four_hour_entry_count <= max_rows)
    {
        for (uint8_t i = 0; i < next_twenty_four_hour_entry_count; ++i)
        {
            out_forecast_indices[i] = next_twenty_four_hour_entries[i];
        }
        return next_twenty_four_hour_entry_count;
    }

    for (uint8_t row = 0; row < max_rows; ++row)
    {
        const uint8_t sample_index = static_cast<uint8_t>(
            (row * (next_twenty_four_hour_entry_count - 1U)) / (max_rows - 1U));
        out_forecast_indices[row] = next_twenty_four_hour_entries[sample_index];
    }

    return max_rows;
}

/// @brief Parses one `YYYY-MM-DD` date string used by daily forecast rows.
bool parse_forecast_iso_date(const char* date_text, int* out_month, int* out_day)
{
    if (date_text == nullptr || out_month == nullptr || out_day == nullptr || date_text[0] == '\0')
    {
        return false;
    }

    if (!(std::isdigit(static_cast<unsigned char>(date_text[0])) &&
          std::isdigit(static_cast<unsigned char>(date_text[1])) &&
          std::isdigit(static_cast<unsigned char>(date_text[2])) &&
          std::isdigit(static_cast<unsigned char>(date_text[3])) && date_text[4] == '-' &&
          std::isdigit(static_cast<unsigned char>(date_text[5])) &&
          std::isdigit(static_cast<unsigned char>(date_text[6])) && date_text[7] == '-' &&
          std::isdigit(static_cast<unsigned char>(date_text[8])) &&
          std::isdigit(static_cast<unsigned char>(date_text[9]))))
    {
        return false;
    }

    *out_month = ((date_text[5] - '0') * 10) + (date_text[6] - '0');
    *out_day = ((date_text[8] - '0') * 10) + (date_text[9] - '0');
    return *out_month >= 1 && *out_month <= 12 && *out_day >= 1 && *out_day <= 31;
}

/// @brief Formats one day label for the Next 7 Days rows.
const char* next_seven_days_label_text(uint8_t row_index, const char* date_text, char* buffer,
                                       size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    if (row_index == 0)
    {
        std::snprintf(buffer, buffer_size, "Today");
    }
    else if (row_index == 1)
    {
        std::snprintf(buffer, buffer_size, "Tomorrow");
    }
    else
    {
        int month = 0;
        int day = 0;
        if (parse_forecast_iso_date(date_text, &month, &day))
        {
            std::snprintf(buffer, buffer_size, "%02d/%02d", day, month);
        }
        else
        {
            std::snprintf(buffer, buffer_size, "+%ud", static_cast<unsigned>(row_index));
        }
    }

    return buffer;
}

/// @brief Returns the best weather-source label available for the footer.
/// @details Runtime hints take priority so the UI can show the actual selected
/// source rather than only the build-time default.
const char* weather_source_label_text(const ConsoleState& console_state)
{
    if (console_state.weather_source == WeatherSource::OpenMeteo)
    {
        return "Open-Meteo";
    }

    if (console_state.home_assistant_status.weather_source_hint[0] != '\0')
    {
        return console_state.home_assistant_status.weather_source_hint.data();
    }

    return HOME_ASSISTANT_WEATHER_SOURCE_LABEL[0] ? HOME_ASSISTANT_WEATHER_SOURCE_LABEL
                                                  : "Home Assistant";
}

/// @brief Returns whether there is enough sun-time data to render that section.
bool weather_sun_times_available(const ConsoleState& console_state)
{
    return console_state.home_assistant_status.sunrise_text[0] != '\0' ||
           console_state.home_assistant_status.sunset_text[0] != '\0';
}

/// @brief Draws sunrise and sunset information for the dedicated weather page.
/// @details The block collapses to one centred line when only one value is
/// available so the footer still looks intentional instead of half-empty.
void draw_weather_sun_times(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace kSunFont = fonts::FontFace::Font5x7;
    char sunrise_label[24] = {};
    char sunset_label[24] = {};

    if (console_state.home_assistant_status.sunrise_text[0] != '\0')
    {
        std::snprintf(sunrise_label, sizeof(sunrise_label), "Sunrise %s",
                      console_state.home_assistant_status.sunrise_text.data());
    }

    if (console_state.home_assistant_status.sunset_text[0] != '\0')
    {
        std::snprintf(sunset_label, sizeof(sunset_label), "Sunset %s",
                      console_state.home_assistant_status.sunset_text.data());
    }

    if (sunrise_label[0] != '\0' && sunset_label[0] != '\0')
    {
        framebuffer::draw_hline(fb, 12, kUiWidth - 12, weather_sun_times_y() - 10, true);
        framebuffer::draw_text(fb, 12, weather_sun_times_y(), sunrise_label, true, kSunFont, 1);
        framebuffer::draw_text(fb, kUiWidth - 12 - screens::text_width(sunset_label, kSunFont),
                               weather_sun_times_y(), sunset_label, true, kSunFont, 1);
        return;
    }

    const char* label = sunrise_label[0] != '\0' ? sunrise_label : sunset_label;
    if (label[0] == '\0')
    {
        return;
    }

    framebuffer::draw_hline(fb, 12, kUiWidth - 12, weather_sun_times_y() - 10, true);
    screens::draw_centered_text(fb, kUiWidth / 2, weather_sun_times_y(), label, true, kSunFont, 1);
}

/// @brief Draws the weather-source attribution footer.
/// @details Provenance stays visible even on summary views so it is obvious
/// which backend is driving the weather data on the screen.
void draw_weather_source_footer(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    constexpr fonts::FontFace kFooterFont = fonts::FontFace::Font5x7;
    constexpr int kFooterLineGap = 2;

    // The fetcher owns the stale threshold and what counts as a failed fetch
    // (STATE-4). Both sources fill the same status struct, so this is right
    // for Open-Meteo as well as Home Assistant.
    char freshness_text[16] = {};
    screens::build_data_freshness_text(
        home_assistant_manager::weather_fetch_ok(console_state.home_assistant_status.state),
        console_state.home_assistant_status.weather_last_success_ms, now_ms,
        home_assistant_manager::kWeatherStaleAfterMs, freshness_text, sizeof(freshness_text));

    char footer_label[80] = {};
    std::snprintf(footer_label, sizeof(footer_label), "%s - %s",
                  weather_source_label_text(console_state), freshness_text);

    const screens::WrappedSoftkeyLabel kWrapped =
        screens::wrap_label_lines(footer_label, kFooterFont, weather_source_footer_max_width(), 2);
    const int kLineHeight = framebuffer::font_height(kFooterFont);
    const int kFirstLineY = weather_source_footer_bottom_y() -
                            ((kWrapped.line_count - 1) * (kLineHeight + kFooterLineGap));

    if (kWrapped.line_one[0] != '\0')
    {
        framebuffer::draw_text(fb, weather_source_footer_left_x(), kFirstLineY, kWrapped.line_one,
                               true, kFooterFont, 1);
    }

    if (kWrapped.line_count > 1 && kWrapped.line_two[0] != '\0')
    {
        framebuffer::draw_text(fb, weather_source_footer_left_x(),
                               kFirstLineY + kLineHeight + kFooterLineGap, kWrapped.line_two, true,
                               kFooterFont, 1);
    }
}

/// @brief Converts a weather phrase to simple title case for display.
/// @details Home Assistant conditions can arrive in machine-friendly casing, so
/// this keeps user-facing weather text readable without affecting status labels.
void format_weather_phrase(const char* source, char* dest, size_t dest_size)
{
    if (dest == nullptr || dest_size == 0)
    {
        return;
    }

    if (source == nullptr || source[0] == '\0')
    {
        dest[0] = '\0';
        return;
    }

    bool new_word = true;
    size_t out_index = 0;
    for (size_t i = 0; source[i] != '\0' && out_index + 1 < dest_size; ++i)
    {
        char c = source[i];
        if (c == '_' || c == '-')
        {
            c = ' ';
        }

        if (c >= 'A' && c <= 'Z')
        {
            c = new_word ? c : static_cast<char>(c - 'A' + 'a');
        }
        else if (c >= 'a' && c <= 'z')
        {
            c = new_word ? static_cast<char>(c - 'a' + 'A') : c;
        }

        dest[out_index++] = c;
        new_word = (c == ' ');
    }

    dest[out_index] = '\0';
}

/// @brief Draws a forecast table's column headers and divider in the shared frame.
void draw_forecast_table_header(uint8_t* fb, const ForecastColumns& columns)
{
    for (const ForecastColumn& column : columns)
    {
        framebuffer::draw_text(fb, column.x, kForecastHeaderY, column.header, true,
                               kForecastHeaderFont, 1);
    }
    framebuffer::draw_hline(fb, kForecastTableLeftX, kForecastTableRightX, kForecastDividerY,
                            true);
}

/// @brief Draws one forecast table row: when, temperature, wind, conditions.
void draw_forecast_table_row(uint8_t* fb, const ForecastColumns& columns, size_t index,
                             const char* when, const char* temperature, const char* wind,
                             const char* condition)
{
    char formatted_condition[24] = {};
    format_weather_phrase(condition, formatted_condition, sizeof(formatted_condition));
    const std::array<const char*, 4> cells = {
        when, temperature, wind, formatted_condition[0] != '\0' ? formatted_condition : condition};
    for (size_t i = 0U; i < cells.size(); ++i)
    {
        framebuffer::draw_text(fb, columns[i].x, forecast_row_y(index), cells[i], true,
                               kForecastBodyFont, 1);
    }
}

/// @brief Draws one hourly forecast entry as a table row.
void draw_hourly_forecast_row(uint8_t* fb, const WeatherForecastEntry& entry, size_t index)
{
    draw_forecast_table_row(fb, kHourlyColumns, index, entry.time_text.data(),
                            entry.temperature_text.data(), entry.wind_text.data(),
                            entry.condition_text.data());
}

/// @brief Draws the dense forecast table used by the Hourly range mode.
bool draw_hourly_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                                 const ForecastRenderSlice& forecast_slice)
{
    if (forecast_slice.count == 0)
    {
        return false;
    }

    draw_forecast_table_header(fb, kHourlyColumns);

    const uint8_t row_count =
        std::min(forecast_slice.count, static_cast<uint8_t>(kForecastVisibleRows));
    for (uint8_t i = 0; i < row_count; ++i)
    {
        draw_hourly_forecast_row(fb,
                                 console_state.home_assistant_status
                                     .weather_forecast[forecast_slice.entries[i].forecast_index],
                                 i);
    }

    return true;
}

/// @brief Draws the Next 24 Hours range as a sampled hourly table.
bool draw_next_twenty_four_hours_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                                                 const ForecastRenderSlice& forecast_slice)
{
    std::array<uint8_t, kForecastVisibleRows> forecast_sample_indices = {};
    const uint8_t row_count = build_next_twenty_four_hours_forecast_sample(
        forecast_slice, forecast_sample_indices);
    if (row_count == 0)
    {
        return false;
    }

    draw_forecast_table_header(fb, kHourlyColumns);

    for (uint8_t i = 0; i < row_count; ++i)
    {
        draw_hourly_forecast_row(
            fb, console_state.home_assistant_status.weather_forecast[forecast_sample_indices[i]], i);
    }

    return true;
}

/// @brief Draws a Next 7 Days summary using provider-native daily forecast rows.
bool draw_next_seven_days_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                                          const ForecastRenderSlice& forecast_slice)
{
    (void)forecast_slice;

    const uint8_t row_count =
        std::min(console_state.home_assistant_status.weather_daily_forecast_count,
                 static_cast<uint8_t>(kWeatherDailyForecastEntryCount));
    if (row_count == 0)
    {
        return false;
    }

    draw_forecast_table_header(fb, kDailyColumns);

    char day_label[16] = {};
    for (uint8_t i = 0; i < row_count; ++i)
    {
        const WeatherDailyForecastEntry& entry =
            console_state.home_assistant_status.weather_daily_forecast[i];
        draw_forecast_table_row(
            fb, kDailyColumns, i,
            next_seven_days_label_text(i, entry.date_text.data(), day_label, sizeof(day_label)),
            entry.temperature_text.data(), entry.wind_text.data(), entry.condition_text.data());
    }

    return true;
}

/// @brief Draws weather forecast content for the active Hourly/Next 24 Hours/Next 7 Days range.
bool draw_weather_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                                  const ForecastRenderSlice& forecast_slice)
{
    switch (console_state.weather_period)
    {
    case WeatherPeriod::Hourly:
        return draw_hourly_forecast_period(fb, console_state, forecast_slice);
    case WeatherPeriod::NextTwentyFourHours:
        return draw_next_twenty_four_hours_forecast_period(fb, console_state, forecast_slice);
    case WeatherPeriod::NextSevenDays:
        return draw_next_seven_days_forecast_period(fb, console_state, forecast_slice);
    }

    return false;
}

} // namespace

namespace
{

/// @brief True when the selected period has forecast rows to show.
bool weather_period_has_data(const ConsoleState& console_state,
                             const ForecastRenderSlice& forecast_slice)
{
    switch (console_state.weather_period)
    {
    case WeatherPeriod::Hourly:
        return forecast_slice.count > 0U;
    case WeatherPeriod::NextTwentyFourHours:
    {
        std::array<uint8_t, kForecastVisibleRows> sample = {};
        return build_next_twenty_four_hours_forecast_sample(forecast_slice, sample) > 0U;
    }
    case WeatherPeriod::NextSevenDays:
        return console_state.home_assistant_status.weather_daily_forecast_count > 0U;
    }
    return false;
}

/// @brief Says where the selected source is set up, or null when it is set up.
/// @details Both sources are set up on the web config page's Weather Source
/// card (STATE-5).
const char* weather_setup_hint(const ConsoleState& console_state)
{
    const HomeAssistantStatus& status = console_state.home_assistant_status;
    const bool kSourceOff = status.state == HomeAssistantConnectionState::Unconfigured ||
                            status.state == HomeAssistantConnectionState::Disabled;
    if (console_state.weather_source == WeatherSource::HomeAssistant)
    {
        if (kSourceOff)
        {
            return "Set up Home Assistant in web config";
        }
        return status.weather_entity_id[0] == '\0' ? "Set the weather entity in web config"
                                                    : nullptr;
    }
    if (kSourceOff)
    {
        // The status carries the saved coordinate text, empty when none is saved.
        return status.weather_entity_id[0] == '\0' ? "Set coordinates in web config"
                                                    : "Check coordinates in web config";
    }
    return nullptr;
}

} // namespace

WeatherPageMessage weather_page_message(const ConsoleState& console_state)
{
    constexpr const char* kWaitingHeadline = "WAITING FOR WEATHER";
    constexpr const char* kFailedHeadline = "WEATHER UNAVAILABLE";

    WeatherPageMessage message = {WeatherPageState::Forecast, nullptr, {}};
    const HomeAssistantStatus& status = console_state.home_assistant_status;
    auto set_message = [&message](WeatherPageState state, const char* headline,
                                  const char* hint)
    {
        message.state = state;
        message.headline = headline;
        std::snprintf(message.hint.data(), message.hint.size(), "%s", hint);
    };

    if (const char* setup_hint = weather_setup_hint(console_state); setup_hint != nullptr)
    {
        set_message(WeatherPageState::NotConfigured, "WEATHER NOT SET UP", setup_hint);
        return message;
    }

    if (weather_period_has_data(console_state, build_forecast_render_slice(console_state)))
    {
        return message;
    }

    // Transport detail (HTTP status, error codes) stays on Status
    // Integrations; this page uses operator wording.
    switch (status.state)
    {
    case HomeAssistantConnectionState::Unauthorized:
        set_message(WeatherPageState::Failed, kFailedHeadline, "Check the HA token in web config");
        return message;
    case HomeAssistantConnectionState::Error:
        set_message(WeatherPageState::Failed, kFailedHeadline, "Details in Status > Integrations");
        return message;
    case HomeAssistantConnectionState::WaitingForWifi:
        set_message(WeatherPageState::Waiting, kWaitingHeadline, "Waiting for Wi-Fi");
        return message;
    case HomeAssistantConnectionState::Disabled:
    case HomeAssistantConnectionState::Unconfigured:
    case HomeAssistantConnectionState::Resolving:
    case HomeAssistantConnectionState::Connecting:
    case HomeAssistantConnectionState::Authorizing:
    case HomeAssistantConnectionState::Connected:
        break;
    }

    char hint[48] = {};
    if (status.weather_last_success_ms == 0U)
    {
        std::snprintf(hint, sizeof(hint), "Fetching from %s",
                      weather_source_label_text(console_state));
        set_message(WeatherPageState::Waiting, kWaitingHeadline, hint);
        return message;
    }

    if (status.weather_condition[0] != '\0')
    {
        char condition[32] = {};
        format_weather_phrase(status.weather_condition.data(), condition, sizeof(condition));
        if (status.weather_temperature[0] != '\0')
        {
            std::snprintf(hint, sizeof(hint), "Now: %s, %s", condition,
                          status.weather_temperature.data());
        }
        else
        {
            std::snprintf(hint, sizeof(hint), "Now: %s", condition);
        }
        set_message(WeatherPageState::CurrentOnly, "NO FORECAST", hint);
        return message;
    }

    std::snprintf(hint, sizeof(hint), "%s sent no forecast",
                  weather_source_label_text(console_state));
    set_message(WeatherPageState::NoData, "NO WEATHER DATA", hint);
    return message;
}

void draw_weather_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    const WeatherPageMessage kMessage = weather_page_message(console_state);
    if (kMessage.state == WeatherPageState::Forecast)
    {
        (void)draw_weather_forecast_period(fb, console_state,
                                           build_forecast_render_slice(console_state));
        if (weather_sun_times_available(console_state))
        {
            draw_weather_sun_times(fb, console_state);
        }
    }
    else
    {
        screens::draw_empty_state(fb, kMessage.headline, kMessage.hint.data());
    }

    // Provenance and freshness stay in the footer once the source is set up.
    if (kMessage.state != WeatherPageState::NotConfigured)
    {
        draw_weather_source_footer(fb, console_state, now_ms);
    }
}

} // namespace weather_screens
