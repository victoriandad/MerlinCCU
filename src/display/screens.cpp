#include "screens.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "air_traffic_screens.h"
#include "alert_screens.h"
#include "calendar_screens.h"
#include "config_manager.h"
#include "console_model.h"
#include "framebuffer.h"
#include "local_conditions_screens.h"
#include "panel_config.h"
#include "pinter_screens.h"
#include "screen_banners.h"
#include "screens_shared.h"
#include "settings_screens.h"
#include "shares_screens.h"
#include "status_screens.h"
#include "text_case.h"
#include "text_styles.h"
#include "ui_layout.h"
#include "weather_screens.h"

namespace screens
{

namespace
{

/// @brief Returns the Home-page network footer text.
/// @details A connected CCU advertises the address a browser should use for
/// remote configuration; intermediate or failed states use plain operator-facing
/// text rather than technical DHCP or lwIP details.
const char* home_ip_status_text(const WifiStatus& status, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    buffer[0] = '\0';
    if (status.ip_address[0] != '\0')
    {
        std::snprintf(buffer, buffer_size, "http://%s", status.ip_address.data());
        return buffer;
    }

    switch (status.state)
    {
    case WifiConnectionState::Disabled:
        return "WIFI DISABLED";
    case WifiConnectionState::Unconfigured:
        return "WIFI NOT SET";
    case WifiConnectionState::Initializing:
    case WifiConnectionState::Scanning:
    case WifiConnectionState::Connecting:
    case WifiConnectionState::WaitingForIp:
    case WifiConnectionState::Connected:
        return "WAITING FOR IP";
    case WifiConnectionState::AuthFailed:
        return "WIFI AUTH FAILED";
    case WifiConnectionState::NoNetwork:
        return "NO WIFI NETWORK";
    case WifiConnectionState::ConnectFailed:
    case WifiConnectionState::Error:
        return "NO IP ADDRESS";
    }

    return "NO IP ADDRESS";
}

} // namespace

/// @brief Returns the Home Assistant state label used on diagnostics screens.
/// @details Declared in screens_shared.h so status_screens.cpp (and other
/// split-out page families) can call it without duplicating the switch.
const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state)
    {
    case HomeAssistantConnectionState::Disabled:
        return "Disabled";
    case HomeAssistantConnectionState::Unconfigured:
        return "Unconfig";
    case HomeAssistantConnectionState::WaitingForWifi:
        return "Wait wifi";
    case HomeAssistantConnectionState::Resolving:
        return "Resolve";
    case HomeAssistantConnectionState::Connecting:
        return "Connect";
    case HomeAssistantConnectionState::Authorizing:
        return "Auth";
    case HomeAssistantConnectionState::Connected:
        return "Up";
    case HomeAssistantConnectionState::Unauthorized:
        return "Token";
    case HomeAssistantConnectionState::Error:
        return "Error";
    }

    return "?";
}

/// @brief Returns a provider-neutral weather fetch state label.
/// @details Declared in screens_shared.h so status_screens.cpp (and other
/// split-out page families) can call it. Matches home_assistant_state_text()
/// except where a weather fetch reads differently: Open-Meteo has no HA
/// token, so the authorising step is just the fetch, and an auth failure
/// names the step rather than the token.
const char* weather_fetch_state_text(HomeAssistantConnectionState state)
{
    if (state == HomeAssistantConnectionState::Authorizing)
    {
        return "Fetch";
    }
    if (state == HomeAssistantConnectionState::Unauthorized)
    {
        return "Auth";
    }
    return home_assistant_state_text(state);
}

/// @brief Formats a compact "Xs"/"Xm"/"Xh" elapsed-time label.
void build_elapsed_time_text(uint32_t elapsed_ms, char* out, size_t out_size)
{
    const uint32_t elapsed_s = elapsed_ms / 1000U;
    if (elapsed_s < 60U)
    {
        std::snprintf(out, out_size, "%lus", static_cast<unsigned long>(elapsed_s));
        return;
    }
    const uint32_t elapsed_m = elapsed_s / 60U;
    if (elapsed_m < 60U)
    {
        std::snprintf(out, out_size, "%lum", static_cast<unsigned long>(elapsed_m));
        return;
    }
    std::snprintf(out, out_size, "%luh", static_cast<unsigned long>(elapsed_m / 60U));
}

/// @brief Formats one data-backed subsystem's freshness into a short, consistent
/// label -- issue #16's stale-data display policy.
/// @details `now_ms`/`last_success_ms` are boot-uptime milliseconds
/// (`to_ms_since_boot`), matching the existing environment-sensor idiom
/// (EnvironmentSensorStatus::bme_last_read_ms). `last_success_ms == 0` means
/// "never succeeded" -> "NO DATA". `currently_valid` means the most recent
/// fetch attempt succeeded (vs an error/timeout since the last good one).
/// `stale_after_ms` is the subsystem's own staleness threshold, typically a
/// small multiple of its own refresh interval.
///
/// Deliberately not used for deliberate placeholder/demo states (e.g. Shares
/// before issue #42's local feed lands) -- those aren't a freshness problem,
/// callers should show "DEMO" directly instead of calling this.
void build_data_freshness_text(bool currently_valid, uint32_t last_success_ms, uint32_t now_ms,
                               uint32_t stale_after_ms, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    if (last_success_ms == 0U)
    {
        std::snprintf(out, out_size, "NO DATA");
        return;
    }

    const uint32_t age_ms = now_ms - last_success_ms;
    if (currently_valid && age_ms < stale_after_ms)
    {
        std::snprintf(out, out_size, "LIVE");
        return;
    }

    char age_text[16] = {};
    build_elapsed_time_text(age_ms, age_text, sizeof(age_text));
    std::snprintf(out, out_size, "STALE %s", age_text);
}

namespace
{

/// @brief Returns the page title that matches the active menu route.
const char* menu_page_title(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
        return "HOME";
    case MenuPage::Calendar:
        return "CALENDAR";
    case MenuPage::CalendarDetail:
        return "EVENT";
    case MenuPage::Weather:
        return "WEATHER";
    case MenuPage::Status:
        return "STATUS";
    case MenuPage::StatusOverview:
        return "OVERVIEW";
    case MenuPage::StatusConnectivity:
        return "CONNECT";
    case MenuPage::StatusResources:
        return "RESOURCES";
    case MenuPage::StatusSensors:
        return "SENSORS";
    case MenuPage::StatusIntegrations:
        return "INTEGRATIONS";
    case MenuPage::LocalConditions:
        return "LOCAL CONDITIONS";
    case MenuPage::AirTraffic:
        return "ADS-B TRAFFIC";
    case MenuPage::LocalConditionGraph:
        return "LOCAL GRAPH";
    case MenuPage::Settings:
        return "SETTINGS";
    case MenuPage::DeviceSettings:
        return "DEVICE IDENTITY";
    case MenuPage::WifiSettings:
        return "NETWORK";
    case MenuPage::HomeAssistantSettings:
        return "HOME ASSISTANT";
    case MenuPage::MqttSettings:
        return "MQTT DISCOVERY";
    case MenuPage::AirTrafficSettings:
        return "ADS-B TRAFFIC";
    case MenuPage::ScreenSaverSettings:
        return "SCREEN SAVER";
    case MenuPage::ScreenSaverOptions:
        return "SAVER";
    case MenuPage::WeatherSources:
        return "WEATHER SOURCE";
    case MenuPage::TimeZoneSettings:
        return "TIME ZONE";
    case MenuPage::KeypadDebug:
        return "KEYPAD DEBUG";
    case MenuPage::GreyscaleTest:
        return "GREYSCALE";
    case MenuPage::AlertList:
        return "ALERTS";
    case MenuPage::AlertDetail:
        return "ALERT";
    case MenuPage::Pinter:
        return "PINTER";
    case MenuPage::PinterSelectBrew:
        return "SELECT BREW";
    case MenuPage::PinterStartTiming:
        return "BREW TIMING";
    case MenuPage::Shares:
        return "SHARES";
    case MenuPage::ShareDetail:
        return "SHARE";
    }

    return "MENU";
}

/// @brief Builds the header title for pages that carry a visible page index.
const char* menu_page_title(const ConsoleState& console_state, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    if (console_state.active_page == MenuPage::Settings)
    {
        std::snprintf(buffer, buffer_size, "SETTINGS %u/%u",
                      static_cast<unsigned>(console_state.settings_page_index + 1U),
                      static_cast<unsigned>(kSettingsPageCount));
        return buffer;
    }
    if (console_state.active_page == MenuPage::Status)
    {
        std::snprintf(buffer, buffer_size, "STATUS");
        return buffer;
    }
    if (console_state.active_page == MenuPage::StatusResources)
    {
        std::snprintf(buffer, buffer_size, "RESOURCES");
        return buffer;
    }
    if (console_state.active_page == MenuPage::AlertList)
    {
        const uint8_t page_count = alert_list_page_count(console_state.alert_count);
        std::snprintf(buffer, buffer_size, "ALERTS %u/%u",
                      static_cast<unsigned>(console_state.alert_list_page_index + 1U),
                      static_cast<unsigned>(page_count));
        return buffer;
    }
    if (console_state.active_page == MenuPage::TimeZoneSettings && kTimeZonePageCount > 1U)
    {
        std::snprintf(buffer, buffer_size, "TIME ZONE %u/%u",
                      static_cast<unsigned>(console_state.time_zone_page_index + 1U),
                      static_cast<unsigned>(kTimeZonePageCount));
        return buffer;
    }
    if (console_state.active_page == MenuPage::PinterSelectBrew && kPinterBrewListPageCount > 1U)
    {
        std::snprintf(buffer, buffer_size, "SELECT BREW %u/%u",
                      static_cast<unsigned>(console_state.pinter_catalogue_page_index + 1U),
                      static_cast<unsigned>(kPinterBrewListPageCount));
        return buffer;
    }
    if (console_state.active_page == MenuPage::AirTraffic &&
        console_state.air_traffic_view_mode == AirTrafficViewMode::Tabular)
    {
        const uint8_t page_count =
            air_traffic_list_page_count(console_state.air_traffic_status.aircraft_count);
        if (page_count > 1U)
        {
            std::snprintf(buffer, buffer_size, "ADS-B TRAFFIC %u/%u",
                          static_cast<unsigned>(console_state.air_traffic_page_index + 1U),
                          static_cast<unsigned>(page_count));
            return buffer;
        }
    }

    return menu_page_title(console_state.active_page);
}

/// @brief Width of `text` in one font face; 0 for null or empty text.
/// @details The face-level helper behind text_width(); softkey layout and
/// wrapping work in the face a style resolves to.
int measure(const char* text, fonts::FontFace font)
{
    if (text == nullptr || text[0] == '\0')
    {
        return 0;
    }

    return framebuffer::measure_text(text, font, 1);
}

} // namespace

int text_width(const char* text, TextStyle style)
{
    return measure(text, text_styles::font(style));
}

void draw_text(uint8_t* fb, int x, int y, const char* text, TextStyle style, bool on)
{
    framebuffer::draw_text(fb, x, y, text, on, text_styles::font(style), 1);
}

void draw_right_aligned_text(uint8_t* fb, int right_x, int y, const char* text, TextStyle style,
                             bool on)
{
    draw_text(fb, right_x - text_width(text, style), y, text, style, on);
}

/// @brief Draws a graph plot area's border rect.
/// @details Declared in screens_shared.h -- shared by the Shares history
/// graph (shares_screens.cpp) and the Local Conditions history graph (still
/// in this file).
void draw_graph_plot_border(uint8_t* fb, const GraphPlotArea& area)
{
    framebuffer::draw_rect(fb, area.x, area.y, area.width, area.height, true);
}

namespace
{

/// @brief Gap between the plot's left edge and its axis labels.
constexpr int kGraphAxisLabelGapX = 6;

/// @brief Length of the tick marking the middle axis value.
constexpr int kGraphAxisTickLength = 3;

/// @brief Gap between the plot's bottom edge and the start/end labels.
constexpr int kGraphTimelineGapY = 5;

/// @brief Top of the start/end labels under a plot of `plot_height`.
int history_graph_timeline_y(int plot_height)
{
    return ui_layout::kContentTopY + plot_height + kGraphTimelineGapY;
}

} // namespace

GraphPlotArea draw_history_graph_frame(uint8_t* fb, int plot_height,
                                       const HistoryGraphLabels& labels)
{
    constexpr TextStyle kStyle = TextStyle::Annotation;
    const GraphPlotArea kArea{ui_layout::kGraphLeftX, ui_layout::kContentTopY,
                              ui_layout::kGraphWidth, plot_height, ui_layout::kGraphPlotInset};
    draw_graph_plot_border(fb, kArea);

    // Each axis value is centred on its line; the bottom one sits just
    // above the plot's bottom edge so it stays clear of the start label.
    const int kLineHeight = text_styles::line_height(kStyle);
    const int kLabelRightX = kArea.x - kGraphAxisLabelGapX;
    const int kMiddleY = kArea.y + (plot_height / 2);
    draw_right_aligned_text(fb, kLabelRightX, kArea.y - (kLineHeight / 2), labels.maximum, kStyle);
    draw_right_aligned_text(fb, kLabelRightX, kMiddleY - (kLineHeight / 2), labels.middle, kStyle);
    draw_right_aligned_text(fb, kLabelRightX, kArea.y + plot_height - 1 - kLineHeight,
                            labels.minimum, kStyle);
    framebuffer::draw_hline(fb, kArea.x - kGraphAxisTickLength, kArea.x - 1, kMiddleY, true);

    const int kTimelineY = history_graph_timeline_y(plot_height);
    draw_text(fb, kArea.x, kTimelineY, labels.start, kStyle);
    draw_right_aligned_text(fb, kArea.x + kArea.width, kTimelineY, labels.end, kStyle);
    return kArea;
}

int history_graph_caption_y(int plot_height)
{
    return history_graph_timeline_y(plot_height) + ui_layout::row_pitch(TextStyle::Annotation);
}

/// @brief Maps a 0-based point index across `point_count` evenly spaced points
/// to an x pixel coordinate within the inset plot area.
int graph_plot_x(const GraphPlotArea& area, int index, int point_count)
{
    const int plot_width = area.width - (area.inset * 2) - 1;
    return area.x + area.inset + (plot_width * index) / (point_count - 1);
}

/// @brief Maps a value's position between `min_value`/`max_value` to a y pixel
/// coordinate within the inset plot area, zero at the bottom.
/// @details `value` is clamped to the given range first, so callers whose data
/// can exceed the display scale (e.g. local-condition history against its
/// fixed axis range) do not need to clamp separately.
int graph_plot_y(const GraphPlotArea& area, int64_t value, int64_t min_value, int64_t max_value)
{
    const int64_t range = (max_value > min_value) ? (max_value - min_value) : 1;
    const int plot_height = area.height - (area.inset * 2) - 1;
    const int64_t clamped = std::clamp(value, min_value, max_value);
    const int normalised = static_cast<int>(((clamped - min_value) * plot_height) / range);
    return area.y + area.height - 1 - area.inset - normalised;
}

namespace
{

/// @brief Chooses the softkey text style for the active page.
/// @details Selection-heavy pages use the dense style so two-line labels fit
/// comfortably, while diagnostic/detail pages use the standard one.
TextStyle softkey_label_style(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
    case MenuPage::Settings:
    case MenuPage::Calendar:
    case MenuPage::CalendarDetail:
    case MenuPage::LocalConditions:
    case MenuPage::LocalConditionGraph:
    case MenuPage::DeviceSettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::AirTrafficSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
    case MenuPage::ScreenSaverOptions:
    case MenuPage::Pinter:
    case MenuPage::PinterSelectBrew:
    case MenuPage::PinterStartTiming:
    case MenuPage::Shares:
    case MenuPage::ShareDetail:
        return TextStyle::SoftkeyDense;
    case MenuPage::Weather:
    case MenuPage::AirTraffic:
    case MenuPage::Status:
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
    case MenuPage::AlertList:
    case MenuPage::AlertDetail:
        return TextStyle::Softkey;
    case MenuPage::ScreenSaverSettings:
        return TextStyle::SoftkeyDense;
    }

    return TextStyle::SoftkeyDense;
}

/// @brief Longest single newline-separated part of a label, in characters.
constexpr size_t kSoftkeyLabelPartCapacity = 96U;

/// @brief Returns how many lines one softkey label may wrap onto for a page.
/// @details Most pages cap this at two lines so long labels degrade
/// gracefully. The Pinter page allows five, so each vessel key can carry its
/// name, a recipe value wrapped over up to three lines, and the stage with its
/// countdown (LBL-1). A five-line block still ends above the next key's label
/// at the 57-pixel key pitch. Its vessel keys are on rows 1-4 only; row 5
/// keeps one or two lines.
constexpr int softkey_label_max_lines(MenuPage page)
{
    return page == MenuPage::Pinter ? kMaxSoftkeyLabelLines : 2;
}

/// @brief Copies one bounded label slice into a temporary line buffer.
/// @details Wrapping is done with fixed local buffers so the UI can stay
/// allocation-free and predictable on the Pico.
void copy_softkey_label_slice(char* dest, size_t dest_size, const char* src, size_t length)
{
    if (dest_size == 0)
    {
        return;
    }

    const size_t kCopyLength = (length < (dest_size - 1)) ? length : (dest_size - 1);
    std::memcpy(dest, src, kCopyLength);
    dest[kCopyLength] = '\0';
}

/// @brief Returns the longest prefix that still fits within the softkey label width.
/// @details This is measured incrementally because font metrics vary per glyph
/// and the code needs a visual fit, not a character-count fit.
size_t fit_wrapped_label_prefix(const char* text, fonts::FontFace font, int max_width)
{
    char candidate[48] = {};
    size_t length = 0;

    while (text[length] != '\0' && text[length] != '\n')
    {
        copy_softkey_label_slice(candidate, sizeof(candidate), text, length + 1);
        if (measure(candidate, font) > max_width)
        {
            break;
        }
        ++length;
    }

    return length;
}

/// @brief Chooses a human-friendly wrap point for a softkey label.
/// @details The split prefers whitespace so the label reads like a panel legend
/// instead of being broken at an arbitrary character boundary.
size_t find_wrapped_label_split(const char* text, size_t fit_length)
{
    if (fit_length == 0)
    {
        return 0;
    }

    if (text[fit_length] == '\n' || text[fit_length] == '\0')
    {
        return fit_length;
    }

    for (size_t i = fit_length; i > 0; --i)
    {
        if (text[i - 1] == ' ')
        {
            return i - 1;
        }
    }

    return fit_length;
}

/// @brief Skips spaces and explicit line breaks after the first wrapped line.
size_t skip_wrapped_label_breaks(const char* text, size_t start)
{
    while (text[start] == ' ' || text[start] == '\n')
    {
        ++start;
    }

    return start;
}

} // namespace

/// @brief Wraps one softkey label into at most `max_lines` renderable lines.
/// @details Softkeys are deliberately capped (two lines for most pages, three
/// where a page opts in) so long labels degrade gracefully without taking
/// over the rest of the screen layout. Each line greedily fits as much text
/// as the width allows, preferring to break on whitespace. Declared in
/// screens_shared.h -- split-out page families (weather footer wrapping, etc.)
/// need it too.
WrappedSoftkeyLabel wrap_label_lines(const char* label, TextStyle style, int max_width,
                                     int max_lines)
{
    const fonts::FontFace font = text_styles::font(style);
    WrappedSoftkeyLabel wrapped = {};
    wrapped.line_count = 0;

    if (label == nullptr || label[0] == '\0' || max_lines <= 0)
    {
        return wrapped;
    }

    char* const line_buffers[3] = {wrapped.line_one, wrapped.line_two, wrapped.line_three};
    const int kEffectiveMaxLines = (max_lines > 3) ? 3 : max_lines;
    const char* cursor = label;

    for (int line_index = 0; line_index < kEffectiveMaxLines; ++line_index)
    {
        if (cursor[0] == '\0')
        {
            break;
        }

        const size_t kFitLength = fit_wrapped_label_prefix(cursor, font, max_width);
        if (kFitLength == 0)
        {
            break;
        }

        size_t length = find_wrapped_label_split(cursor, kFitLength);
        while (length > 0 && cursor[length - 1] == ' ')
        {
            --length;
        }

        copy_softkey_label_slice(line_buffers[line_index], 48, cursor, length);
        wrapped.line_count = line_index + 1;
        cursor += skip_wrapped_label_breaks(cursor, length);
    }

    return wrapped;
}

namespace
{

/// @brief Wraps free text into lines no wider than `max_width` pixels in `font`.
/// @details Widths are accumulated glyph by glyph with the same one-pixel
/// spacing `framebuffer::measure_text()` uses, so a span measured here
/// renders at exactly this width.
size_t wrap_text_in_font(const char* text, fonts::FontFace font, int max_width, TextSpan* out,
                         size_t max_spans)
{
    if (text == nullptr || out == nullptr || max_spans == 0U)
    {
        return 0U;
    }

    auto glyph_width = [font](char c)
    {
        const char glyph[2] = {c, '\0'};
        return measure(glyph, font);
    };

    size_t count = 0U;
    const char* cursor = text;
    while (*cursor != '\0' && count < max_spans)
    {
        const char* paragraph_end = std::strchr(cursor, '\n');
        if (paragraph_end == nullptr)
        {
            paragraph_end = cursor + std::strlen(cursor);
        }

        if (cursor == paragraph_end)
        {
            out[count++] = {cursor, 0U};
        }

        while (cursor < paragraph_end && count < max_spans)
        {
            while (cursor < paragraph_end && *cursor == ' ')
            {
                ++cursor;
            }
            if (cursor == paragraph_end)
            {
                break;
            }

            const size_t remaining = static_cast<size_t>(paragraph_end - cursor);
            size_t fit = 0U;
            int width = 0;
            while (fit < remaining)
            {
                const int next_width = width + (fit > 0U ? 1 : 0) + glyph_width(cursor[fit]);
                if (next_width > max_width)
                {
                    break;
                }
                width = next_width;
                ++fit;
            }

            size_t take = fit;
            if (fit < remaining)
            {
                size_t space = fit;
                while (space > 0U && cursor[space] != ' ')
                {
                    --space;
                }
                take = (space > 0U) ? space : std::max<size_t>(fit, 1U);
            }

            size_t length = take;
            while (length > 0U && cursor[length - 1U] == ' ')
            {
                --length;
            }
            out[count++] = {cursor, length};
            cursor += take;
        }

        cursor = (*paragraph_end == '\n') ? paragraph_end + 1 : paragraph_end;
    }

    return count;
}

} // namespace

/// @brief Wraps free text in `style`. Declared in screens_shared.h.
size_t wrap_text(const char* text, TextStyle style, int max_width, TextSpan* out, size_t max_spans)
{
    return wrap_text_in_font(text, text_styles::font(style), max_width, out, max_spans);
}

namespace
{

/// @brief Lowest y a detail block may grow to when its values wrap: the top
/// of the row 5 softkey slot, so wrapped lines never reach row 5 or the footer.
constexpr int kDetailRowsWrapLimitY = ui_layout::softkey_slot_top_y(4);

/// @brief Most lines one detail value may wrap onto.
constexpr size_t kMaxDetailValueLines = 8U;

/// @brief Longest single wrapped line of a detail value, in characters.
constexpr size_t kDetailValueLineCapacity = 96U;

/// @brief Ends a value that was cut because the block ran out of room.
constexpr const char* kDetailValueCutMarker = "...";

/// @brief Value shown for a missing or empty field (LBL-10).
const char* detail_value_text(const char* value)
{
    return (value != nullptr && value[0] != '\0') ? value : "-";
}

/// @brief Returns how many lines `value` needs at `max_width`, at least one.
int detail_value_line_count(const char* value, TextStyle style, int max_width)
{
    std::array<TextSpan, kMaxDetailValueLines> spans = {};
    const size_t kCount = wrap_text(value, style, max_width, spans.data(), spans.size());
    return std::max(1, static_cast<int>(kCount));
}

/// @brief How many extra lines a detail block may add by wrapping.
/// @details The block may grow down to `kDetailRowsWrapLimitY`. A block whose
/// unwrapped rows already reach past that line keeps its layout and gets no
/// extra lines, so pages that never overflowed render exactly as before.
int detail_wrap_budget(int natural_bottom_y, int line_pitch)
{
    const int kLimitY = std::max(kDetailRowsWrapLimitY, natural_bottom_y);
    return (kLimitY - natural_bottom_y) / line_pitch;
}

/// @brief Draws one detail value from `x` to the value margin, wrapped over at
/// most `max_lines` lines; text that still does not fit is cut at the last
/// character that fits and ends with the cut marker. Returns the lines drawn.
/// @details The single long-value policy for both detail-row blocks
/// (docs/ui_patterns.md). The stored value is never shortened.
int draw_detail_value(uint8_t* fb, int x, int y, const char* value, TextStyle style, int line_pitch,
                      int max_lines)
{
    const int kMaxWidth = ui_layout::kRightEdgeX - x;
    const size_t kLineLimit =
        static_cast<size_t>(std::clamp(max_lines, 1, static_cast<int>(kMaxDetailValueLines)));
    // One spare span shows whether the value runs past the line limit.
    std::array<TextSpan, kMaxDetailValueLines + 1U> spans = {};
    const size_t kCount = wrap_text(value, style, kMaxWidth, spans.data(), kLineLimit + 1U);
    const size_t kShown = std::min(kCount, kLineLimit);
    const bool kCut = kCount > kLineLimit;

    for (size_t i = 0U; i < kShown; ++i)
    {
        char line[kDetailValueLineCapacity] = {};
        copy_softkey_label_slice(line, sizeof(line), spans[i].text, spans[i].length);
        if (kCut && (i + 1U == kShown))
        {
            size_t length = std::strlen(line);
            char marked[kDetailValueLineCapacity + 4U] = {};
            while (true)
            {
                while (length > 0U && line[length - 1U] == ' ')
                {
                    --length;
                }
                std::snprintf(marked, sizeof(marked), "%.*s%s", static_cast<int>(length), line,
                              kDetailValueCutMarker);
                if (length == 0U || text_width(marked, style) <= kMaxWidth)
                {
                    break;
                }
                --length;
            }
            copy_softkey_label_slice(line, sizeof(line), marked, std::strlen(marked));
        }
        draw_text(fb, x, y + (static_cast<int>(i) * line_pitch), line, style);
    }
    return std::max(1, static_cast<int>(kShown));
}

} // namespace

/// @brief Draws the standard row-based presentation used by information pages.
/// @details The top banner carries the page title, so the body can stay clean
/// and consistent without extra boxes, subtitles, or divider lines. Labels
/// stay uppercase on the left while values use mixed case on the right; a
/// long value wraps under itself and later rows move down (draw_detail_value).
/// Declared in screens_shared.h -- used by split-out page families too.
void draw_info_page_rows(uint8_t* fb, const DetailRow* rows, size_t count)
{
    constexpr int kLabelX = 50;
    constexpr int kValueX = 156;
    constexpr int kValueOffsetY = 1;
    constexpr TextStyle kLabelStyle = TextStyle::InfoRowLabel;
    constexpr TextStyle kValueStyle = TextStyle::BodyValue;
    // Wrapped value lines sit closer together than rows, so they read as one value.
    constexpr int kWrappedLineGapY = 3;
    const int kStartY = ui_layout::kContentTopY;
    const int kRowPitch = ui_layout::row_pitch(kLabelStyle);
    const int kValueLinePitch = text_styles::line_height(kValueStyle) + kWrappedLineGapY;
    constexpr size_t kLabelBufferSize = 32U;

    if (count == 0U)
    {
        return;
    }

    const int kNaturalBottomY = kStartY + ((static_cast<int>(count) - 1) * kRowPitch) +
                                text_styles::line_height(kLabelStyle);
    int spare_lines = detail_wrap_budget(kNaturalBottomY, kValueLinePitch);
    int y = kStartY;
    for (size_t i = 0U; i < count; ++i)
    {
        char label_text[kLabelBufferSize] = {};
        text_case::copy_uppercase(rows[i].label, label_text, sizeof(label_text));
        draw_text(fb, kLabelX, y, label_text, kLabelStyle);

        const char* value = detail_value_text(rows[i].value);
        const int kNeeded =
            detail_value_line_count(value, kValueStyle, ui_layout::kRightEdgeX - kValueX);
        const int kAllowed = 1 + std::min(kNeeded - 1, spare_lines);
        spare_lines -= kAllowed - 1;
        const int kDrawn = draw_detail_value(fb, kValueX, y + kValueOffsetY, value, kValueStyle,
                                             kValueLinePitch, kAllowed);
        y += kRowPitch + ((kDrawn - 1) * kValueLinePitch);
    }
}

/// @brief Draws shared left/right page-navigation arrows used by paged menus.
/// @details One shared helper keeps all paged screens visually consistent and
/// allows global position tweaks from a single location. Declared in
/// screens_shared.h -- used by split-out page families too (Air Traffic).
void draw_page_navigation_arrows(uint8_t* fb, bool show_left, bool show_right)
{
    constexpr int kArrowY = ui_layout::kFooterLineY;
    constexpr int kArrowHalfWidth = 5;
    constexpr int kArrowHalfHeight = 6;
    constexpr int kArrowOffsetX = 26;
    constexpr int kLeftArrowX = ui_layout::kCentreX - kArrowOffsetX;
    constexpr int kRightArrowX = ui_layout::kCentreX + kArrowOffsetX;

    if (show_left)
    {
        framebuffer::draw_line(fb, kLeftArrowX + kArrowHalfWidth, kArrowY - kArrowHalfHeight,
                               kLeftArrowX - kArrowHalfWidth, kArrowY, true);
        framebuffer::draw_line(fb, kLeftArrowX - kArrowHalfWidth, kArrowY,
                               kLeftArrowX + kArrowHalfWidth, kArrowY + kArrowHalfHeight, true);
    }

    if (show_right)
    {
        framebuffer::draw_line(fb, kRightArrowX - kArrowHalfWidth, kArrowY - kArrowHalfHeight,
                               kRightArrowX + kArrowHalfWidth, kArrowY, true);
        framebuffer::draw_line(fb, kRightArrowX + kArrowHalfWidth, kArrowY,
                               kRightArrowX - kArrowHalfWidth, kArrowY + kArrowHalfHeight, true);
    }
}

namespace
{

/// @brief Returns how far a bracket's serifs reach in from its upright: about
/// a quarter of the line height (2 for 5x7 text, 3 for 8x12 and 8x14), so
/// the brackets scale with whatever font the value is drawn in (TXT-3).
int softkey_bracket_depth(fonts::FontFace font)
{
    return (framebuffer::font_height(font) + 1) / 4;
}

} // namespace

/// @brief Draws one oversized square bracket pair around a softkey value.
/// @details The brackets are rendered as simple line primitives so they stay
/// readable even when the underlying bitmap font has tiny punctuation glyphs.
/// Declared in screens_shared.h -- used both by the core softkey label
/// renderer below and by Settings' screen-saver timeout scratchpad
/// (settings_screens.cpp).
void draw_softkey_selection_brackets(uint8_t* fb, int left_x, int top_y, int total_height,
                                     int total_width, TextStyle style, bool on)
{
    const int kDepth = softkey_bracket_depth(text_styles::font(style));
    const int kRightX = left_x + total_width - 1;
    const int kBottomY = top_y + total_height - 1;

    framebuffer::draw_vline(fb, left_x, top_y, kBottomY, on);
    framebuffer::draw_hline(fb, left_x, left_x + kDepth, top_y, on);
    framebuffer::draw_hline(fb, left_x, left_x + kDepth, kBottomY, on);

    framebuffer::draw_vline(fb, kRightX, top_y, kBottomY, on);
    framebuffer::draw_hline(fb, kRightX - kDepth, kRightX, top_y, on);
    framebuffer::draw_hline(fb, kRightX - kDepth, kRightX, kBottomY, on);
}

namespace
{

/// @brief Horizontal gap between a value and its brackets, and how far the
/// brackets extend above and below the text.
constexpr int kSoftkeyBracketGapX = 2;
constexpr int kSoftkeyBracketPadY = 2;

/// @brief Width the brackets and their gaps add around a bracketed value.
int softkey_bracket_overhead(fonts::FontFace font)
{
    return (softkey_bracket_depth(font) * 2) + (kSoftkeyBracketGapX * 2);
}

} // namespace

/// @brief Lays out a label: each newline-separated part is wrapped to the
/// cell width, and a part written as `[value]` is wrapped inside one bracket
/// pair rather than split with its bracket glyphs on different lines (LBL-3).
/// @details Stops at `max_lines`, keeping one line for each later part; a
/// value cut short still gets its closing bracket. Widths are measured, since
/// the fonts are proportional.
/// Declared in screens_shared.h so tests can check how a label lays out.
SoftkeyLabelLayout layout_softkey_label(const char* label, TextStyle style, int max_lines)
{
    const fonts::FontFace font = text_styles::font(style);
    SoftkeyLabelLayout layout = {};
    const int kMaxLines = std::min(max_lines, kMaxSoftkeyLabelLines);
    const char* cursor = label;
    while (*cursor != '\0' && layout.count < kMaxLines)
    {
        const char* part_end = std::strchr(cursor, '\n');
        if (part_end == nullptr)
        {
            part_end = cursor + std::strlen(cursor);
        }
        const size_t kPartLength = static_cast<size_t>(part_end - cursor);
        const bool kBracketed =
            kPartLength >= 3U && cursor[0] == '[' && cursor[kPartLength - 1U] == ']';

        char part[kSoftkeyLabelPartCapacity] = {};
        copy_softkey_label_slice(part, sizeof(part), kBracketed ? cursor + 1 : cursor,
                                 kBracketed ? kPartLength - 2U : kPartLength);
        const int kWidth =
            ui_layout::kSoftkeyLabelMaxWidth - (kBracketed ? softkey_bracket_overhead(font) : 0);

        // Keep one line for each part still to come, so a long value wraps
        // into the space left over rather than pushing later parts off the key.
        int later_parts = 0;
        for (const char* scan = part_end; *scan != '\0'; ++scan)
        {
            if (*scan == '\n')
            {
                ++later_parts;
            }
        }
        const int kLinesForPart = std::max(1, kMaxLines - layout.count - later_parts);

        std::array<TextSpan, kMaxSoftkeyLabelLines> spans = {};
        const size_t kSpanCount =
            wrap_text_in_font(part, font, kWidth, spans.data(), static_cast<size_t>(kLinesForPart));
        for (size_t i = 0U; i < kSpanCount; ++i)
        {
            SoftkeyLabelLine& line = layout.lines[static_cast<size_t>(layout.count++)];
            copy_softkey_label_slice(line.text, sizeof(line.text), spans[i].text, spans[i].length);
            line.bracketed = kBracketed;
            line.value_end = kBracketed && (i + 1U == kSpanCount);
        }
        cursor = (*part_end == '\n') ? part_end + 1 : part_end;
    }
    return layout;
}

/// @brief Draws text centred around a given x-coordinate.
/// @details Centralizing the centering math keeps titles and status callouts
/// aligned consistently across the different page renderers. Declared in
/// screens_shared.h -- used by split-out page families too.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, TextStyle style,
                        bool on)
{
    draw_text(fb, center_x - (text_width(text, style) / 2), y, text, style, on);
}

/// @brief Draws the shared empty-state block. Declared in screens_shared.h.
void draw_empty_state(uint8_t* fb, const char* headline, const char* hint)
{
    if (headline != nullptr && headline[0] != '\0')
    {
        draw_centered_text(fb, ui_layout::kCentreX, ui_layout::kEmptyStateHeadlineY, headline,
                           TextStyle::Headline);
    }
    if (hint == nullptr || hint[0] == '\0')
    {
        return;
    }
    std::array<TextSpan, ui_layout::kMaxEmptyStateHintLines> spans = {};
    const size_t kCount =
        wrap_text(hint, TextStyle::Hint, ui_layout::kContentWidth, spans.data(), spans.size());
    const int kPitch = ui_layout::row_pitch(TextStyle::Hint);
    for (size_t i = 0U; i < kCount; ++i)
    {
        char line[kDetailValueLineCapacity] = {};
        copy_softkey_label_slice(line, sizeof(line), spans[i].text, spans[i].length);
        draw_centered_text(fb, ui_layout::kCentreX,
                           ui_layout::kEmptyStateHintY + (static_cast<int>(i) * kPitch), line,
                           TextStyle::Hint);
    }
}

namespace
{

/// @brief Draws one mirrored softkey label block.
/// @details The label is vertically centred within the physical key slot so
/// wrapped text still reads like it belongs to one button location. A
/// bracketed value is drawn as one run: a single bracket pair, and on an
/// inverted key a single highlight, around all of its lines.
void draw_softkey_label(uint8_t* fb, int row, const SoftKeyAction& action, bool left_side,
                        TextStyle style, int max_lines)
{
    const fonts::FontFace font = text_styles::font(style);
    if (action.label == nullptr || action.label[0] == '\0')
    {
        return;
    }

    const SoftkeyLabelLayout kLayout = layout_softkey_label(action.label, style, max_lines);
    if (kLayout.count == 0)
    {
        return;
    }

    const int kLineHeight = framebuffer::font_height(font);
    const int kLinePitch = kLineHeight + ui_layout::kSoftkeyLineGap;
    const int kBlockTopY = ui_layout::softkey_label_top_y(row, kLayout.count, kLineHeight);
    const bool kTextOn = !action.inverted;

    int first = 0;
    while (first < kLayout.count)
    {
        // A run is one plain line, or every line of one bracketed value.
        const bool kBracketed = kLayout.lines[static_cast<size_t>(first)].bracketed;
        int end = first + 1;
        if (kBracketed)
        {
            while (end < kLayout.count && !kLayout.lines[static_cast<size_t>(end - 1)].value_end)
            {
                ++end;
            }
        }

        int widest_text = 0;
        for (int i = first; i < end; ++i)
        {
            widest_text =
                std::max(widest_text, measure(kLayout.lines[static_cast<size_t>(i)].text, font));
        }
        const int kRunWidth = widest_text + (kBracketed ? softkey_bracket_overhead(font) : 0);
        const int kRunX = left_side ? ui_layout::kSoftkeyLeftLabelX
                                    : (ui_layout::kSoftkeyRightLabelEndX - kRunWidth);
        const int kRunLines = end - first;
        const int kRunTextTopY = kBlockTopY + (first * kLinePitch);
        const int kRunTextHeight =
            (kRunLines * kLineHeight) + ((kRunLines - 1) * ui_layout::kSoftkeyLineGap);
        const int kPadY = kBracketed ? kSoftkeyBracketPadY : 0;

        if (action.inverted)
        {
            constexpr int kHighlightPadX = 1;
            constexpr int kHighlightPadY = 1;
            framebuffer::fill_rect(fb, kRunX - kHighlightPadX,
                                   kRunTextTopY - kPadY - kHighlightPadY,
                                   kRunWidth + (kHighlightPadX * 2),
                                   kRunTextHeight + (kPadY * 2) + (kHighlightPadY * 2), true);
        }

        int text_x = kRunX;
        if (kBracketed)
        {
            draw_softkey_selection_brackets(fb, kRunX, kRunTextTopY - kPadY,
                                            kRunTextHeight + (kPadY * 2), kRunWidth, style,
                                            kTextOn);
            text_x += softkey_bracket_depth(font) + kSoftkeyBracketGapX;
        }
        for (int i = first; i < end; ++i)
        {
            framebuffer::draw_text(fb, text_x, kBlockTopY + (i * kLinePitch),
                                   kLayout.lines[static_cast<size_t>(i)].text, kTextOn, font, 1);
        }
        first = end;
    }
}

/// @brief Draws every softkey label for the current menu page.
/// @details Labels are rendered separately from the page body so most pages can
/// share the same bezel framing while only the content region changes.
void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    const TextStyle kLabelStyle = softkey_label_style(console_state.active_page);
    const int kMaxLines = softkey_label_max_lines(console_state.active_page);

    for (int row = 0; row < ui_layout::kSoftkeyRowCount; ++row)
    {
        const size_t kLeft = static_cast<size_t>(row);
        const size_t kRight = kLeft + static_cast<size_t>(ui_layout::kSoftkeyRowCount);
        draw_softkey_label(fb, row, console_state.softkeys[kLeft], true, kLabelStyle, kMaxLines);
        draw_softkey_label(fb, row, console_state.softkeys[kRight], false, kLabelStyle, kMaxLines);
    }
}

/// @brief Left edge of compact row labels.
constexpr int kCompactDetailTextX = ui_layout::kMarginX;

/// @brief Space between the widest `LABEL:` and the value column, so no
/// value ever touches its label's colon.
constexpr int kCompactLabelValueGapX = 6;
constexpr TextStyle kCompactDetailStyle = TextStyle::CompactRow;

/// @brief Returns the value-column x-coordinate for compact detail pages.
/// @details The compact font is proportional, so spaces cannot be used for
/// visual alignment. Measuring the widest rendered `Label:` gives a stable
/// column for all values.
int compact_detail_value_x(const DetailRow* rows, size_t count)
{
    constexpr size_t kLabelBufferSize = 32U;
    int widest_label_width = 0;
    for (size_t i = 0; i < count; ++i)
    {
        char label_text[kLabelBufferSize] = {};
        std::snprintf(label_text, sizeof(label_text),
                      "%s:", rows[i].label != nullptr ? rows[i].label : "");
        widest_label_width =
            std::max(widest_label_width, text_width(label_text, kCompactDetailStyle));
    }

    return kCompactDetailTextX + widest_label_width + kCompactLabelValueGapX;
}

} // namespace

/// @brief Draws compact `Label: value` rows for data-dense pages.
/// @details A long value wraps under itself at the row pitch and later rows
/// move down; once the block would reach row 5 it is cut with a marker
/// instead (draw_detail_value). Declared in screens_shared.h -- used by every
/// split-out page family that renders dense key/value diagnostics.
void draw_compact_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y,
                              int row_pitch)
{
    constexpr size_t kLabelBufferSize = 32U;
    if (count == 0U)
    {
        return;
    }

    const int kValueX = compact_detail_value_x(rows, count);
    const int kNaturalBottomY = start_y + ((static_cast<int>(count) - 1) * row_pitch) +
                                text_styles::line_height(kCompactDetailStyle);
    int spare_lines = detail_wrap_budget(kNaturalBottomY, row_pitch);
    int y = start_y;
    for (size_t i = 0U; i < count; ++i)
    {
        char label_text[kLabelBufferSize] = {};
        std::snprintf(label_text, sizeof(label_text),
                      "%s:", rows[i].label != nullptr ? rows[i].label : "");
        draw_text(fb, kCompactDetailTextX, y, label_text, kCompactDetailStyle);

        const char* value = detail_value_text(rows[i].value);
        const int kNeeded =
            detail_value_line_count(value, kCompactDetailStyle, ui_layout::kRightEdgeX - kValueX);
        const int kAllowed = 1 + std::min(kNeeded - 1, spare_lines);
        spare_lines -= kAllowed - 1;
        const int kDrawn =
            draw_detail_value(fb, kValueX, y, value, kCompactDetailStyle, row_pitch, kAllowed);
        y += row_pitch * kDrawn;
    }
}

namespace
{

/// @brief Draws the clean top-level home menu.
/// @details The page body is intentionally empty so the only visual affordances
/// are the surrounding labels for the next level of navigation.
void draw_home_page(uint8_t* fb, const ConsoleState& console_state)
{
    char ip_text[32] = {};
    draw_text(fb, ui_layout::kMarginX, ui_layout::kFooterLineY,
              home_ip_status_text(console_state.wifi_status, ip_text, sizeof(ip_text)),
              TextStyle::Footer);
}

} // namespace

/// @brief Draws a simple geometry and fill-pattern test screen.
/// @details This is meant for quick panel sanity checks, not realistic UI, so it
/// emphasizes contrast, clipping, and obvious motion-free shapes.
void draw_demo_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    framebuffer::draw_rect(fb, 0, 0, kUiWidth, kUiHeight, true);
    framebuffer::draw_rect(fb, 10, 10, kUiWidth - 20, kUiHeight - 20, true);

    framebuffer::fill_rect(fb, 20, 20, 60, 40, true);
    framebuffer::fill_rect(fb, kUiWidth - 80, 30, 40, 70, true);

    framebuffer::draw_diag(fb, true);

    for (int i = 0; i < 10; ++i)
    {
        framebuffer::fill_rect(fb, 5, 20 + i * 28, 6, 12, true);
        framebuffer::fill_rect(fb, kUiWidth - 11, 20 + i * 28, 6, 12, true);
    }

    framebuffer::fill_rect(fb, 0, kUiHeight - 16, kUiWidth, 16, true);
    framebuffer::fill_rect(fb, 8, kUiHeight - 12, 100, 8, false);
}

/// @brief Draws the active menu page and contextual softkey labels.
/// @details The shared frame and banner pass runs first so every menu page keeps
/// the same shell while only the central content renderer changes.
void draw_menu_screen(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    framebuffer::clear(fb, false);

    char title[24] = {};
    screen_banners::draw_standard_banners(fb, console_state,
                                          menu_page_title(console_state, title, sizeof(title)));
    draw_softkeys(fb, console_state);

    switch (console_state.active_page)
    {
    case MenuPage::Home:
        draw_home_page(fb, console_state);
        break;
    case MenuPage::Calendar:
        calendar_screens::draw_calendar_page(fb, console_state);
        break;
    case MenuPage::CalendarDetail:
        calendar_screens::draw_calendar_detail_page(fb, console_state);
        break;
    case MenuPage::Weather:
        weather_screens::draw_weather_page(fb, console_state, now_ms);
        break;
    case MenuPage::AirTraffic:
        air_traffic_screens::draw_air_traffic_page(fb, console_state, now_ms);
        break;
    case MenuPage::Pinter:
    case MenuPage::PinterSelectBrew:
    case MenuPage::PinterStartTiming:
        pinter_screens::draw_pinter_page(fb, console_state);
        break;
    case MenuPage::Shares:
        shares_screens::draw_shares_page(fb, console_state);
        break;
    case MenuPage::ShareDetail:
        shares_screens::draw_share_detail_page(fb, console_state, now_ms);
        break;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        status_screens::draw_status_page(fb, console_state, now_ms);
        break;
    case MenuPage::LocalConditions:
        local_conditions_screens::draw_local_conditions_page(fb, console_state, now_ms);
        break;
    case MenuPage::LocalConditionGraph:
        local_conditions_screens::draw_local_condition_history_graph(fb, console_state, now_ms);
        break;
    case MenuPage::Settings:
        settings_screens::draw_settings_page(fb, console_state);
        break;
    case MenuPage::ScreenSaverSettings:
        settings_screens::draw_screen_saver_page(fb, console_state);
        break;
    case MenuPage::Status:
    case MenuPage::DeviceSettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::AirTrafficSettings:
    case MenuPage::WeatherSources:
    case MenuPage::ScreenSaverOptions:
        // Key pages with no body: every value they show is on a softkey label
        // (PAGE-2), so the body is deliberately left empty.
        break;
    case MenuPage::TimeZoneSettings:
        settings_screens::draw_time_zone_page(fb, console_state);
        break;
    case MenuPage::KeypadDebug:
        settings_screens::draw_keypad_debug_page(fb, console_state);
        break;
    case MenuPage::GreyscaleTest:
        settings_screens::draw_greyscale_test_card(fb, console_state);
        break;
    case MenuPage::AlertList:
        alert_screens::draw_alert_list_page(fb, console_state);
        break;
    case MenuPage::AlertDetail:
        alert_screens::draw_alert_detail_page(fb, console_state);
        break;
    }
}

/// @brief Draws a static calibration screen for alignment and extent testing.
/// @details The pattern is intentionally photographic and high-contrast so panel
/// rotation, clipping, and centering issues are easy to spot on the real hardware.
void draw_calibration_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    const int kMidX = kUiWidth / 2;
    const int kMidY = kUiHeight / 2;
    const int kQ1X = kUiWidth / 4;
    const int kQ3X = (kUiWidth * 3) / 4;
    const int kQ1Y = kUiHeight / 4;
    const int kQ3Y = (kUiHeight * 3) / 4;

    // Full outer border of the logical UI.
    framebuffer::draw_rect(fb, 0, 0, kUiWidth, kUiHeight, true);

    // Inner border to make clipping easier to see in photos.
    framebuffer::draw_rect(fb, 4, 4, kUiWidth - 8, kUiHeight - 8, true);

    // Corner markers.
    framebuffer::fill_rect(fb, 0, 0, 8, 8, true);
    framebuffer::fill_rect(fb, kUiWidth - 8, 0, 8, 8, true);
    framebuffer::fill_rect(fb, 0, kUiHeight - 8, 8, 8, true);
    framebuffer::fill_rect(fb, kUiWidth - 8, kUiHeight - 8, 8, 8, true);

    // Centre cross.
    framebuffer::draw_vline(fb, kMidX, 0, kUiHeight - 1, true);
    framebuffer::draw_hline(fb, 0, kUiWidth - 1, kMidY, true);

    // Quarter lines.
    framebuffer::draw_vline(fb, kQ1X, 16, kUiHeight - 17, true);
    framebuffer::draw_vline(fb, kQ3X, 16, kUiHeight - 17, true);
    framebuffer::draw_hline(fb, 16, kUiWidth - 17, kQ1Y, true);
    framebuffer::draw_hline(fb, 16, kUiWidth - 17, kQ3Y, true);

    // Small edge ticks every 32 logical pixels.
    for (int x = 0; x < kUiWidth; x += 32)
    {
        framebuffer::draw_vline(fb, x, 0, 5, true);
        framebuffer::draw_vline(fb, x, kUiHeight - 6, kUiHeight - 1, true);
    }

    for (int y = 0; y < kUiHeight; y += 32)
    {
        framebuffer::draw_hline(fb, 0, 5, y, true);
        framebuffer::draw_hline(fb, kUiWidth - 6, kUiWidth - 1, y, true);
    }

    // Central box.
    framebuffer::draw_rect(fb, kMidX - 30, kMidY - 20, 60, 40, true);

    // Top and bottom labels for orientation.
    framebuffer::draw_text(fb, 12, 12, "TOP", true, 1, 1);
    framebuffer::draw_text(fb, kUiWidth - 34, 12, "R", true, 2, 1);
    framebuffer::draw_text(fb, 12, kUiHeight - 20, "BOTTOM", true, 1, 1);

    // A few filled blocks for checking edge visibility and stability.
    framebuffer::fill_rect(fb, 20, kMidY - 10, 12, 20, true);
    framebuffer::fill_rect(fb, kUiWidth - 32, kMidY - 10, 12, 20, true);
    framebuffer::fill_rect(fb, kMidX - 10, 24, 20, 12, true);
    framebuffer::fill_rect(fb, kMidX - 10, kUiHeight - 36, 20, 12, true);
}

} // namespace screens
