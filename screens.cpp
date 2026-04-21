#include "screens.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "console_model.h"
#include "framebuffer.h"
#include "panel_config.h"
#include "screen_banners.h"

#if __has_include("weather_display_config.h")
#include "weather_display_config.h"
#else
inline constexpr char HOME_ASSISTANT_WEATHER_SOURCE_LABEL[] = "";
#endif

namespace screens
{

namespace
{

/// @brief Returns the compact label used for the letter annunciator mode.
const char* letter_mode_text(LetterMode mode)
{
    return (mode == LetterMode::On) ? "ON" : "OFF";
}

/// @brief Returns the shortened alert label used on the constrained settings UI.
const char* alert_severity_text(AlertSeverity severity)
{
    switch (severity)
    {
    case AlertSeverity::None:
        return "NONE";
    case AlertSeverity::Message:
        return "MSG";
    case AlertSeverity::Warning:
        return "WARN";
    case AlertSeverity::Alert:
        return "ALERT";
    }

    return "?";
}

/// @brief Returns the terse test-state label shown on status-oriented screens.
const char* test_state_text(SystemTestState state)
{
    switch (state)
    {
    case SystemTestState::Idle:
        return "IDLE";
    case SystemTestState::Running:
        return "RUN";
    case SystemTestState::Passed:
        return "PASS";
    case SystemTestState::Failed:
        return "FAIL";
    }

    return "?";
}

/// @brief Returns the fixed-width brightness label used by menu pages.
const char* brightness_text(BrightnessLevel level)
{
    switch (level)
    {
    case BrightnessLevel::Off:
        return "OFF";
    case BrightnessLevel::Low:
        return "LOW";
    case BrightnessLevel::Medium:
        return "MED";
    case BrightnessLevel::High:
        return "HIGH";
    }

    return "?";
}

/// @brief Returns the abbreviated lamp-mode label used in compact layouts.
const char* lamp_mode_text(LampMode mode)
{
    switch (mode)
    {
    case LampMode::Off:
        return "OFF";
    case LampMode::On:
        return "ON";
    case LampMode::FlashSlow:
        return "F-SLOW";
    case LampMode::FlashFast:
        return "F-FAST";
    }

    return "?";
}

/// @brief Returns the Wi-Fi state label sized to fit the one-line status panel.
const char* wifi_state_text(WifiConnectionState state)
{
    switch (state)
    {
    case WifiConnectionState::Disabled:
        return "DISABLED";
    case WifiConnectionState::Unconfigured:
        return "UNCONFIG";
    case WifiConnectionState::Initializing:
        return "INIT";
    case WifiConnectionState::Scanning:
        return "SCAN";
    case WifiConnectionState::Connecting:
        return "CONNECT";
    case WifiConnectionState::WaitingForIp:
        return "DHCP";
    case WifiConnectionState::Connected:
        return "UP";
    case WifiConnectionState::AuthFailed:
        return "BADAUTH";
    case WifiConnectionState::NoNetwork:
        return "NO NET";
    case WifiConnectionState::ConnectFailed:
        return "FAIL";
    case WifiConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

/// @brief Returns the Home Assistant state label used on diagnostics screens.
const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state)
    {
    case HomeAssistantConnectionState::Disabled:
        return "DISABLED";
    case HomeAssistantConnectionState::Unconfigured:
        return "UNCONFIG";
    case HomeAssistantConnectionState::WaitingForWifi:
        return "WAIT WIFI";
    case HomeAssistantConnectionState::Resolving:
        return "RESOLVE";
    case HomeAssistantConnectionState::Connecting:
        return "CONNECT";
    case HomeAssistantConnectionState::Authorizing:
        return "AUTH";
    case HomeAssistantConnectionState::Connected:
        return "UP";
    case HomeAssistantConnectionState::Unauthorized:
        return "TOKEN";
    case HomeAssistantConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

/// @brief Returns the MQTT state label used on the condensed status page.
const char* mqtt_state_text(MqttConnectionState state)
{
    switch (state)
    {
    case MqttConnectionState::Disabled:
        return "DISABLED";
    case MqttConnectionState::Unconfigured:
        return "UNCONFIG";
    case MqttConnectionState::WaitingForWifi:
        return "WAIT WIFI";
    case MqttConnectionState::Resolving:
        return "RESOLVE";
    case MqttConnectionState::Connecting:
        return "CONNECT";
    case MqttConnectionState::Connected:
        return "UP";
    case MqttConnectionState::AuthFailed:
        return "AUTH";
    case MqttConnectionState::Error:
        return "ERROR";
    }

    return "?";
}

/// @brief Returns the page title that matches the active menu route.
const char* menu_page_title(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
        return "WEATHER";
    case MenuPage::Status:
        return "STATUS";
    case MenuPage::Settings:
        return "SETTINGS";
    case MenuPage::WeatherSources:
        return "SOURCES";
    case MenuPage::Alignment:
        return "ALIGN";
    case MenuPage::KeypadDebug:
        return "KEYPAD";
    }

    return "MENU";
}

/// @brief Measures text through one shared helper so layout math stays consistent.
int text_width(const char* text, fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1)
{
    if (text == nullptr || text[0] == '\0')
    {
        return 0;
    }

    return framebuffer::measure_text(text, font, spacing);
}

struct SoftkeyLayout
{
    int left_x;
    int width;
    int height;
    int top_y;
    int pitch;
    int text_inset;
    int line_gap;
};

constexpr SoftkeyLayout kSoftkeyLayout = {
    .left_x = 2,
    .width = 34,
    .height = 18,
    .top_y = 41,
    .pitch = 57,
    .text_inset = 4,
    .line_gap = 2,
};

/// @brief Returns the top edge of the indexed softkey label block.
constexpr int softkey_y_for_index(int index)
{
    return kSoftkeyLayout.top_y + (index * kSoftkeyLayout.pitch);
}

/// @brief Returns the vertical center line of the indexed softkey position.
constexpr int softkey_center_y_for_index(int index)
{
    return softkey_y_for_index(index) + (kSoftkeyLayout.height / 2);
}

/// @brief Chooses the softkey font for the active page.
/// @details Alignment uses a larger font so the calibration labels stay legible
/// from a distance while the rest of the menu system favors denser text.
fonts::FontFace softkey_label_font(MenuPage page)
{
    return (page == MenuPage::Alignment) ? fonts::FontFace::Font8x12 : fonts::FontFace::Font5x7;
}

/// @brief Returns the maximum drawable width for one softkey label line.
constexpr int softkey_label_max_width()
{
    return (kUiWidth / 2) - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset;
}

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

struct WrappedSoftkeyLabel
{
    char line_one[48];
    char line_two[48];
    int line_count;
};

/// @brief Copies one bounded label slice into a temporary line buffer.
/// @details Wrapping is done with fixed local buffers so the UI can stay
/// allocation-free and predictable on the Pico.
void copy_softkey_label_slice(char* dest, size_t dest_size, const char* src, size_t length)
{
    if (dest_size == 0)
    {
        return;
    }

    const size_t copy_length = (length < (dest_size - 1)) ? length : (dest_size - 1);
    std::memcpy(dest, src, copy_length);
    dest[copy_length] = '\0';
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
        if (text_width(candidate, font) > max_width)
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

/// @brief Wraps one softkey label into at most two renderable lines.
/// @details Softkeys are deliberately limited to two lines so long labels
/// degrade gracefully without taking over the rest of the screen layout.
WrappedSoftkeyLabel wrap_label_two_lines(const char* label, fonts::FontFace font, int max_width)
{
    WrappedSoftkeyLabel wrapped = {};
    wrapped.line_count = 0;

    if (label == nullptr || label[0] == '\0')
    {
        return wrapped;
    }

    const size_t first_fit_length = fit_wrapped_label_prefix(label, font, max_width);
    if (first_fit_length == 0)
    {
        return wrapped;
    }

    size_t first_length = find_wrapped_label_split(label, first_fit_length);
    while (first_length > 0 && label[first_length - 1] == ' ')
    {
        --first_length;
    }

    copy_softkey_label_slice(wrapped.line_one, sizeof(wrapped.line_one), label, first_length);
    wrapped.line_count = 1;

    size_t second_start = skip_wrapped_label_breaks(label, first_length);
    if (label[second_start] == '\0')
    {
        return wrapped;
    }

    const size_t second_fit_length =
        fit_wrapped_label_prefix(label + second_start, font, max_width);
    if (second_fit_length == 0)
    {
        return wrapped;
    }

    size_t second_length = second_fit_length;
    if (label[second_start + second_fit_length] != '\0' &&
        label[second_start + second_fit_length] != '\n')
    {
        const size_t second_split =
            find_wrapped_label_split(label + second_start, second_fit_length);
        if (second_split > 0)
        {
            second_length = second_split;
        }
    }

    while (second_length > 0 && label[second_start + second_length - 1] == ' ')
    {
        --second_length;
    }

    copy_softkey_label_slice(wrapped.line_two, sizeof(wrapped.line_two), label + second_start,
                             second_length);
    wrapped.line_count = 2;
    return wrapped;
}

/// @brief Applies the current softkey width policy to one label string.
/// @details This keeps callers from hard-coding layout limits in multiple places
/// when the bezel or font rules change.
WrappedSoftkeyLabel wrap_softkey_label(const char* label, fonts::FontFace font)
{
    return wrap_label_two_lines(label, font, softkey_label_max_width());
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

    const char hour_tens = text[0];
    const char hour_ones = text[1];
    const char minute_tens = text[3];
    const char minute_ones = text[4];

    if (hour_tens < '0' || hour_tens > '9' || hour_ones < '0' || hour_ones > '9' ||
        minute_tens < '0' || minute_tens > '9' || minute_ones < '0' || minute_ones > '9')
    {
        return false;
    }

    const int hours = ((hour_tens - '0') * 10) + (hour_ones - '0');
    const int minutes = ((minute_tens - '0') * 10) + (minute_ones - '0');
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59)
    {
        return false;
    }

    *out_minutes = (hours * 60) + minutes;
    return true;
}

struct ForecastDisplayWindow
{
    uint8_t first_index;
    uint8_t count;
};

/// @brief Chooses which forecast entries should be shown first on the home page.
/// @details Home Assistant forecast rows only expose time-of-day strings here,
/// so this helper reconstructs a forward-looking window anchored on the current
/// hour instead of blindly starting from element zero.
ForecastDisplayWindow active_forecast_window(const ConsoleState& console_state)
{
    ForecastDisplayWindow window = {0, console_state.home_assistant_status.weather_forecast_count};
    if (!console_state.time_status.synced || window.count == 0)
    {
        return window;
    }

    int current_minutes = 0;
    if (!parse_clock_text_minutes(console_state.time_status.time_text.data(), &current_minutes))
    {
        return window;
    }

    const int current_hour_floor = (current_minutes / 60) * 60;
    const int kDayMinutes = 24 * 60;
    const int kOffsetCandidates[] = {-kDayMinutes, 0, kDayMinutes};
    int previous_forecast_minutes = -100000;
    int day_offset = 0;
    bool have_previous_entry = false;

    // Forecast entries only carry time-of-day text, not a date, so this logic
    // reconstructs a monotonic window around "now" and skips stale earlier slots.
    while (window.first_index < console_state.home_assistant_status.weather_forecast_count)
    {
        int raw_forecast_minutes = 0;
        const WeatherForecastEntry& entry =
            console_state.home_assistant_status.weather_forecast[window.first_index];
        if (!parse_clock_text_minutes(entry.time_text.data(), &raw_forecast_minutes))
        {
            break;
        }

        if (!have_previous_entry)
        {
            int best_distance = 0x7fffffff;
            for (const int candidate_offset : kOffsetCandidates)
            {
                const int candidate_minutes = raw_forecast_minutes + candidate_offset;
                const int distance = candidate_minutes > current_hour_floor
                                         ? (candidate_minutes - current_hour_floor)
                                         : (current_hour_floor - candidate_minutes);
                if (distance < best_distance)
                {
                    best_distance = distance;
                    day_offset = candidate_offset;
                }
            }
        }

        int forecast_minutes = raw_forecast_minutes + day_offset;
        while (have_previous_entry && forecast_minutes < previous_forecast_minutes)
        {
            day_offset += kDayMinutes;
            forecast_minutes = raw_forecast_minutes + day_offset;
        }

        if (forecast_minutes >= current_hour_floor)
        {
            break;
        }

        previous_forecast_minutes = forecast_minutes;
        have_previous_entry = true;
        ++window.first_index;
    }

    window.count =
        (window.first_index < console_state.home_assistant_status.weather_forecast_count)
            ? static_cast<uint8_t>(console_state.home_assistant_status.weather_forecast_count -
                                   window.first_index)
            : 0;
    return window;
}

/// @brief Draws text centered around a given x-coordinate.
/// @details Centralizing the centering math keeps titles and status callouts
/// aligned consistently across the different page renderers.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1)
{
    framebuffer::draw_text(fb, center_x - (text_width(text, font, spacing) / 2), y, text, on, font,
                           spacing);
}

/// @brief Draws one mirrored softkey label block.
/// @details The label is vertically centered within the physical key slot so
/// wrapped text still reads like it belongs to one button location.
void draw_softkey_label(uint8_t* fb, int y, const SoftKeyAction& action, bool left_side,
                        fonts::FontFace font)
{
    if (action.label == nullptr || action.label[0] == '\0')
    {
        return;
    }

    const WrappedSoftkeyLabel wrapped = wrap_softkey_label(action.label, font);
    const int line_height = framebuffer::font_height(font);
    const int block_height =
        (wrapped.line_count * line_height) + ((wrapped.line_count - 1) * kSoftkeyLayout.line_gap);
    const int block_top_y = y + ((kSoftkeyLayout.height - block_height) / 2);

    auto draw_line = [&](const char* line_text, int line_index)
    {
        if (line_text == nullptr || line_text[0] == '\0')
        {
            return;
        }

        const int label_width = text_width(line_text, font);
        const int text_x =
            left_side
                ? (kSoftkeyLayout.left_x + kSoftkeyLayout.text_inset)
                : (kUiWidth - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset - label_width);
        const int text_y = block_top_y + (line_index * (line_height + kSoftkeyLayout.line_gap));
        framebuffer::draw_text(fb, text_x, text_y, line_text, true, font, 1);
    };

    draw_line(wrapped.line_one, 0);
    if (wrapped.line_count > 1)
    {
        draw_line(wrapped.line_two, 1);
    }
}

/// @brief Converts a softkey enum into the backing array index.
constexpr size_t softkey_map_index(SoftKeyId key)
{
    return static_cast<size_t>(key);
}

/// @brief Returns the best weather-source label available for the footer.
/// @details Runtime hints take priority so the UI can show the actual selected
/// source rather than only the build-time default.
const char* weather_source_label_text(const ConsoleState& console_state)
{
    if (console_state.home_assistant_status.weather_source_hint[0] != '\0')
    {
        return console_state.home_assistant_status.weather_source_hint.data();
    }

    return HOME_ASSISTANT_WEATHER_SOURCE_LABEL[0] ? HOME_ASSISTANT_WEATHER_SOURCE_LABEL : "Weather";
}

/// @brief Returns whether there is enough sun-time data to render that section.
bool weather_sun_times_available(const ConsoleState& console_state)
{
    return console_state.home_assistant_status.sunrise_text[0] != '\0' ||
           console_state.home_assistant_status.sunset_text[0] != '\0';
}

/// @brief Draws sunrise and sunset information for the weather home page.
/// @details The block collapses to one centered line when only one value is
/// available so the footer still looks intentional instead of half-empty.
void draw_weather_sun_times(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace sun_font = fonts::FontFace::Font5x7;
    char sunrise_label[24] = {};
    char sunset_label[24] = {};

    if (console_state.home_assistant_status.sunrise_text[0] != '\0')
    {
        std::snprintf(sunrise_label, sizeof(sunrise_label), "SUNRISE %s",
                      console_state.home_assistant_status.sunrise_text.data());
    }

    if (console_state.home_assistant_status.sunset_text[0] != '\0')
    {
        std::snprintf(sunset_label, sizeof(sunset_label), "SUNSET %s",
                      console_state.home_assistant_status.sunset_text.data());
    }

    if (sunrise_label[0] != '\0' && sunset_label[0] != '\0')
    {
        framebuffer::draw_hline(fb, 12, kUiWidth - 12, weather_sun_times_y() - 10, true);
        framebuffer::draw_text(fb, 12, weather_sun_times_y(), sunrise_label, true, sun_font, 1);
        framebuffer::draw_text(fb, kUiWidth - 12 - text_width(sunset_label, sun_font),
                               weather_sun_times_y(), sunset_label, true, sun_font, 1);
        return;
    }

    const char* label = sunrise_label[0] != '\0' ? sunrise_label : sunset_label;
    if (label[0] == '\0')
    {
        return;
    }

    framebuffer::draw_hline(fb, 12, kUiWidth - 12, weather_sun_times_y() - 10, true);
    draw_centered_text(fb, kUiWidth / 2, weather_sun_times_y(), label, true, sun_font, 1);
}

/// @brief Draws the visual guide for one physical softkey position.
/// @details The guide marks where the real panel button sits so text can remain
/// offset from the bezel while still reading as tied to that input.
void draw_softkey_guide(uint8_t* fb, int index, bool left_side)
{
    const int center_y = softkey_center_y_for_index(index);
    const int guide_x = left_side ? 1 : (kUiWidth - 2);
    const int fill_x = left_side ? 0 : (kUiWidth - 3);

    framebuffer::draw_vline(fb, guide_x, center_y - 6, center_y + 6, true);
    framebuffer::fill_rect(fb, fill_x, center_y - 1, 3, 3, true);
}

/// @brief Draws all left and right softkey guides around the content area.
void draw_softkey_guides(uint8_t* fb)
{
    for (int i = 0; i < 5; ++i)
    {
        draw_softkey_guide(fb, i, true);
        draw_softkey_guide(fb, i, false);
    }
}

/// @brief Draws every softkey label for the current menu page.
/// @details Labels are rendered separately from the page body so most pages can
/// share the same bezel framing while only the content region changes.
void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    const fonts::FontFace label_font = softkey_label_font(console_state.active_page);

    for (int i = 0; i < 5; ++i)
    {
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i], true, label_font);
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i + 5], false,
                           label_font);
    }
}

/// @brief Draws the common inner panel frame used by menu pages.
/// @details Reusing one bezel silhouette helps the menus read like part of the
/// physical console instead of a collection of unrelated test screens.
void draw_panel_frame(uint8_t* fb)
{
    framebuffer::draw_rect(fb, 40, 40, kUiWidth - 80, 206, true);
    framebuffer::draw_rect(fb, 46, 46, kUiWidth - 92, 194, true);
}

/// @brief Draws the weather-source attribution footer.
/// @details Provenance stays visible even on summary views so it is obvious
/// which backend is driving the weather data on the screen.
void draw_weather_source_footer(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace footer_font = fonts::FontFace::Font5x7;
    constexpr int footer_line_gap = 2;

    const WrappedSoftkeyLabel wrapped = wrap_label_two_lines(
        weather_source_label_text(console_state), footer_font, weather_source_footer_max_width());
    const int line_height = framebuffer::font_height(footer_font);
    const int first_line_y = weather_source_footer_bottom_y() -
                             ((wrapped.line_count - 1) * (line_height + footer_line_gap));

    if (wrapped.line_one[0] != '\0')
    {
        framebuffer::draw_text(fb, weather_source_footer_left_x(), first_line_y, wrapped.line_one,
                               true, footer_font, 1);
    }

    if (wrapped.line_count > 1 && wrapped.line_two[0] != '\0')
    {
        framebuffer::draw_text(fb, weather_source_footer_left_x(),
                               first_line_y + line_height + footer_line_gap, wrapped.line_two, true,
                               footer_font, 1);
    }
}

/// @brief Draws the lone explicit softkey shown on the home page.
/// @details The home page reserves most of the screen for weather data, so only
/// the key that matters there is drawn instead of the full ten-key legend set.
void draw_home_page_softkey(uint8_t* fb, const ConsoleState& console_state)
{
    const SoftKeyAction& action = console_state.softkeys[softkey_map_index(SoftKeyId::Right5)];
    if (action.label == nullptr || action.label[0] == '\0')
    {
        return;
    }

    draw_softkey_label(fb, softkey_y_for_index(4), action, false,
                       softkey_label_font(console_state.active_page));
    draw_softkey_guide(fb, 4, false);
}

/// @brief Produces the most useful short weather-status fallback string.
/// @details HTTP status is surfaced first because it is usually more actionable
/// during integration than a generic connection-state label.
const char* weather_status_detail(const HomeAssistantStatus& status, char* buffer,
                                  size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    buffer[0] = '\0';

    if (status.last_http_status > 0)
    {
        std::snprintf(buffer, buffer_size, "HTTP %d", status.last_http_status);
        return buffer;
    }

    if (status.last_error != 0)
    {
        std::snprintf(buffer, buffer_size, "NET ERR %d", status.last_error);
        return buffer;
    }

    return home_assistant_state_text(status.state);
}

/// @brief Draws the default home page with either forecast rows or summary text.
/// @details The layout prefers live hourly data when available, then degrades to
/// a centered summary so the page still feels complete during startup or errors.
void draw_home_page(uint8_t* fb, const ConsoleState& console_state)
{
    char status_detail[24] = {};
    const bool weather_configured =
        console_state.home_assistant_status.weather_entity_id[0] != '\0';
    const ForecastDisplayWindow forecast_window = active_forecast_window(console_state);
    const char* weather_condition = "WEATHER OFF";
    const char* weather_temperature = "";
    const char* weather_footer = "";

    if (weather_configured)
    {
        weather_condition =
            console_state.home_assistant_status.weather_condition[0]
                ? console_state.home_assistant_status.weather_condition.data()
                : (console_state.home_assistant_status.state ==
                           HomeAssistantConnectionState::Connected
                       ? "NO DATA"
                       : weather_status_detail(console_state.home_assistant_status, status_detail,
                                               sizeof(status_detail)));
        weather_temperature = console_state.home_assistant_status.weather_temperature.data();

        if (console_state.home_assistant_status.state == HomeAssistantConnectionState::Connected)
        {
            weather_footer = "NO HOURLY FORECAST";
        }
        else if (console_state.home_assistant_status.state ==
                     HomeAssistantConnectionState::Resolving ||
                 console_state.home_assistant_status.state ==
                     HomeAssistantConnectionState::Connecting ||
                 console_state.home_assistant_status.state ==
                     HomeAssistantConnectionState::Authorizing ||
                 console_state.home_assistant_status.state ==
                     HomeAssistantConnectionState::WaitingForWifi)
        {
            weather_footer = "WAITING FOR WEATHER";
        }
        else
        {
            weather_footer = weather_status_detail(console_state.home_assistant_status,
                                                   status_detail, sizeof(status_detail));
        }
    }

    if (forecast_window.count > 0)
    {
        framebuffer::draw_text(fb, 12, 36, "TIME", true, 1, 1);
        framebuffer::draw_text(fb, 60, 36, "TEMP", true, 1, 1);
        framebuffer::draw_text(fb, 102, 36, "WIND mph", true, 1, 1);
        framebuffer::draw_text(fb, 160, 36, "CONDITIONS", true, 1, 1);
        framebuffer::draw_hline(fb, 12, kUiWidth - 12, 46, true);

        for (uint8_t i = 0; i < forecast_window.count; ++i)
        {
            const WeatherForecastEntry& entry =
                console_state.home_assistant_status
                    .weather_forecast[forecast_window.first_index + i];
            const int row_y = 54 + (static_cast<int>(i) * 18);
            framebuffer::draw_text(fb, 12, row_y, entry.time_text.data(), true,
                                   fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 60, row_y, entry.temperature_text.data(), true,
                                   fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 102, row_y, entry.wind_text.data(), true,
                                   fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 160, row_y + 3, entry.condition_text.data(), true, 1, 1);
        }
        if (weather_configured && weather_sun_times_available(console_state))
        {
            draw_weather_sun_times(fb, console_state);
        }
        if (weather_configured)
        {
            draw_weather_source_footer(fb, console_state);
        }
        return;
    }

    draw_centered_text(fb, kUiWidth / 2, 92, weather_condition, true, fonts::FontFace::Font8x12, 1);
    if (weather_temperature[0] != '\0')
    {
        draw_centered_text(fb, kUiWidth / 2, 120, weather_temperature, true,
                           fonts::FontFace::Font8x14, 1);
    }

    if (weather_configured && weather_footer[0] != '\0')
    {
        draw_centered_text(fb, kUiWidth / 2, 162, weather_footer, true, fonts::FontFace::Font5x7,
                           1);
    }

    if (weather_configured)
    {
        draw_weather_source_footer(fb, console_state);
    }
}

/// @brief Draws the weather-source management page.
/// @details This page currently acts as an information and placeholder surface
/// so the menu structure can stabilize before source switching is fully wired.
void draw_weather_sources_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, kUiWidth / 2, 56, "SOURCES", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 84, "CURRENT SOURCE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 100, weather_source_label_text(console_state), true, 1, 1);
    framebuffer::draw_text(fb, 54, 116,
                           console_state.home_assistant_status.weather_entity_id[0]
                               ? console_state.home_assistant_status.weather_entity_id.data()
                               : "-",
                           true, 1, 1);

    framebuffer::draw_hline(fb, 54, kUiWidth - 54, 136, true);
    framebuffer::draw_text(fb, 54, 152, "SOURCE SLOT 2", true, 1, 1);
    framebuffer::draw_text(fb, 144, 152, "RESERVED", true, 1, 1);
    framebuffer::draw_text(fb, 54, 168, "SOURCE SLOT 3", true, 1, 1);
    framebuffer::draw_text(fb, 144, 168, "RESERVED", true, 1, 1);

    framebuffer::draw_text(fb, 54, 204, "Selection UI placeholder", true, 1, 1);
    framebuffer::draw_text(fb, 54, 220, "source switching not yet wired", true, 1, 1);
}

/// @brief Draws the consolidated status diagnostics page.
/// @details The page is intentionally dense so the most important network and
/// integration state can be inspected from one screen during bring-up.
void draw_status_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, kUiWidth / 2, 56, "STATUS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 84, "TIME", true, 1, 1);
    framebuffer::draw_text(
        fb, 116, 84,
        console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--",
        true, 1, 1);

    framebuffer::draw_text(fb, 54, 100, "WIFI", true, 1, 1);
    framebuffer::draw_text(fb, 116, 100, wifi_state_text(console_state.wifi_status.state), true, 1,
                           1);

    framebuffer::draw_text(fb, 54, 116, "SSID", true, 1, 1);
    framebuffer::draw_text(
        fb, 116, 116,
        console_state.wifi_status.credentials_present ? console_state.wifi_status.ssid.data() : "-",
        true, 1, 1);

    framebuffer::draw_text(fb, 54, 132, "IP", true, 1, 1);
    framebuffer::draw_text(
        fb, 116, 132,
        console_state.wifi_status.ip_address[0] ? console_state.wifi_status.ip_address.data() : "-",
        true, 1, 1);

    framebuffer::draw_text(fb, 54, 148, "HA", true, 1, 1);
    framebuffer::draw_text(fb, 116, 148,
                           home_assistant_state_text(console_state.home_assistant_status.state),
                           true, 1, 1);

    framebuffer::draw_text(fb, 54, 164, "HOST", true, 1, 1);
    framebuffer::draw_text(fb, 116, 164,
                           console_state.home_assistant_status.host[0]
                               ? console_state.home_assistant_status.host.data()
                               : "-",
                           true, 1, 1);

    framebuffer::draw_text(fb, 54, 180, "HTTP", true, 1, 1);
    if (console_state.home_assistant_status.last_http_status > 0)
    {
        char http_text[8] = {};
        std::snprintf(http_text, sizeof(http_text), "%d",
                      console_state.home_assistant_status.last_http_status);
        framebuffer::draw_text(fb, 116, 180, http_text, true, 1, 1);
    }
    else
    {
        framebuffer::draw_text(fb, 116, 180, "-", true, 1, 1);
    }

    framebuffer::draw_text(fb, 54, 196, "ENT", true, 1, 1);
    framebuffer::draw_text(fb, 116, 196,
                           console_state.home_assistant_status.tracked_entity_state[0]
                               ? console_state.home_assistant_status.tracked_entity_state.data()
                               : "-",
                           true, 1, 1);

    framebuffer::draw_text(fb, 54, 212, "MQTT", true, 1, 1);
    framebuffer::draw_text(fb, 116, 212, mqtt_state_text(console_state.mqtt_status.state), true, 1,
                           1);

    framebuffer::draw_text(fb, 54, 228, "DISC", true, 1, 1);
    framebuffer::draw_text(
        fb, 116, 228, console_state.mqtt_status.discovery_published ? "READY" : "-", true, 1, 1);
}

/// @brief Draws the settings legend page.
/// @details This currently serves as an operator cheat sheet for the physical
/// keys while the deeper configuration flows are still evolving.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, kUiWidth / 2, 56, "SETTINGS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 86, "RIGHT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 102, "ALERT  LTRS  TEST", true, 1, 1);
    framebuffer::draw_text(fb, 54, 118, "PANEL + PANEL -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 146, "LEFT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 162, "HOME  STATUS  RESET", true, 1, 1);
    framebuffer::draw_text(fb, 54, 178, "KEYS + KEYS -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 206, "LTRS", true, 1, 1);
    framebuffer::draw_text(fb, 116, 206, letter_mode_text(console_state.letter_mode), true, 1, 1);

    framebuffer::draw_text(fb, 54, 222, "ALERT", true, 1, 1);
    framebuffer::draw_text(fb, 116, 222, alert_severity_text(console_state.alert_severity), true, 1,
                           1);
}

/// @brief Draws the keypad-debug diagnostics page.
/// @details The goal here is hardware bring-up, so the screen prioritizes raw
/// scan information and recent events over polished end-user presentation.
void draw_keypad_debug_page(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int panel_left = 60;
    constexpr int panel_top = 40;
    constexpr int panel_width = kUiWidth - 80;
    constexpr int panel_height = 200;
    constexpr int label_x = 74;
    constexpr int value_x = 136;
    constexpr int divider_y = 144;

    framebuffer::draw_rect(fb, panel_left, panel_top, panel_width, panel_height, true);
    draw_centered_text(fb, panel_left + (panel_width / 2), 56, "KEYPAD", true,
                       fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, label_x, 84, "ACTIVE", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 84,
                           console_state.keypad_debug_status.active_panel_pins[0]
                               ? console_state.keypad_debug_status.active_panel_pins.data()
                               : "-",
                           true, 1, 1);

    framebuffer::draw_text(fb, label_x, 100, "MASK", true, 1, 1);
    char mask_text[16] = {};
    std::snprintf(mask_text, sizeof(mask_text), "0x%04lX",
                  static_cast<unsigned long>(console_state.keypad_debug_status.active_mask));
    framebuffer::draw_text(fb, value_x, 100, mask_text, true, 1, 1);

    framebuffer::draw_text(fb, label_x, 116, "LINES", true, 1, 1);
    char lines_text[24] = {};
    std::snprintf(lines_text, sizeof(lines_text), "%u/%u",
                  static_cast<unsigned>(console_state.keypad_debug_status.active_count),
                  static_cast<unsigned>(console_state.keypad_debug_status.configured_count));
    framebuffer::draw_text(fb, value_x, 116, lines_text, true, 1, 1);

    framebuffer::draw_text(fb, label_x, 132, "DRIVE", true, 1, 1);
    char drive_text[16] = {};
    if (console_state.keypad_debug_status.probe_drive_panel_pin != 0)
    {
        std::snprintf(
            drive_text, sizeof(drive_text), "%u",
            static_cast<unsigned>(console_state.keypad_debug_status.probe_drive_panel_pin));
    }
    else
    {
        std::snprintf(drive_text, sizeof(drive_text), "-");
    }
    framebuffer::draw_text(fb, value_x, 132, drive_text, true, 1, 1);

    framebuffer::draw_hline(fb, label_x, panel_left + panel_width - 14, divider_y, true);
    framebuffer::draw_text(fb, label_x, 156, "SENSE", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 156,
                           console_state.keypad_debug_status.probe_hit_panel_pins[0]
                               ? console_state.keypad_debug_status.probe_hit_panel_pins.data()
                               : "-",
                           true, 1, 1);
    framebuffer::draw_text(fb, label_x, 172, "LAST KEY", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 172,
                           console_state.keypad_debug_status.last_button_name[0]
                               ? console_state.keypad_debug_status.last_button_name.data()
                               : "-",
                           true, 1, 1);
    framebuffer::draw_text(fb, label_x, 188, "EVENT", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 188,
                           console_state.keypad_debug_status.last_event_type[0]
                               ? console_state.keypad_debug_status.last_event_type.data()
                               : "-",
                           true, 1, 1);
    framebuffer::draw_text(fb, label_x, 204, "COUNT", true, 1, 1);
    char count_text[16] = {};
    std::snprintf(count_text, sizeof(count_text), "%lu",
                  static_cast<unsigned long>(console_state.keypad_debug_status.event_count));
    framebuffer::draw_text(fb, value_x, 204, count_text, true, 1, 1);

    framebuffer::draw_text(fb, label_x, 212, "D05", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 212,
                           console_state.keypad_debug_status.drive_5_hits[0]
                               ? console_state.keypad_debug_status.drive_5_hits.data()
                               : "-",
                           true, 1, 1);
    framebuffer::draw_text(fb, label_x, 224, "D20", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 224,
                           console_state.keypad_debug_status.drive_20_hits[0]
                               ? console_state.keypad_debug_status.drive_20_hits.data()
                               : "-",
                           true, 1, 1);
    framebuffer::draw_text(fb, label_x, 236, "D22", true, 1, 1);
    framebuffer::draw_text(fb, value_x, 236,
                           console_state.keypad_debug_status.drive_22_hits[0]
                               ? console_state.keypad_debug_status.drive_22_hits.data()
                               : "-",
                           true, 1, 1);
}

/// @brief Placeholder for the future alignment menu page.
/// @details The route already exists so menu navigation can stabilize before the
/// dedicated alignment workflow is implemented.
void draw_alignment_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
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
void draw_menu_screen(uint8_t* fb, const ConsoleState& console_state)
{
    framebuffer::clear(fb, false);

    screen_banners::draw_standard_banners(fb, console_state,
                                          menu_page_title(console_state.active_page));
    if (console_state.active_page != MenuPage::Home)
    {
        draw_softkeys(fb, console_state);
    }

    switch (console_state.active_page)
    {
    case MenuPage::Home:
        draw_home_page(fb, console_state);
        break;
    case MenuPage::Status:
        draw_status_page(fb, console_state);
        break;
    case MenuPage::Settings:
        draw_settings_page(fb, console_state);
        break;
    case MenuPage::WeatherSources:
        draw_weather_sources_page(fb, console_state);
        break;
    case MenuPage::Alignment:
        draw_alignment_page(fb, console_state);
        break;
    case MenuPage::KeypadDebug:
        draw_keypad_debug_page(fb, console_state);
        break;
    }

    if (console_state.active_page == MenuPage::Home)
    {
        draw_home_page_softkey(fb, console_state);
    }
    else
    {
        draw_softkey_guides(fb);
    }
}

/// @brief Draws a static calibration screen for alignment and extent testing.
/// @details The pattern is intentionally photographic and high-contrast so panel
/// rotation, clipping, and centering issues are easy to spot on the real hardware.
void draw_calibration_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    const int mid_x = kUiWidth / 2;
    const int mid_y = kUiHeight / 2;
    const int q1_x = kUiWidth / 4;
    const int q3_x = (kUiWidth * 3) / 4;
    const int q1_y = kUiHeight / 4;
    const int q3_y = (kUiHeight * 3) / 4;

    // Full outer border of the logical UI.
    framebuffer::draw_rect(fb, 0, 0, kUiWidth, kUiHeight, true);

    // Inner border to make clipping easier to see in photos.
    framebuffer::draw_rect(fb, 4, 4, kUiWidth - 8, kUiHeight - 8, true);

    // Corner markers.
    framebuffer::fill_rect(fb, 0, 0, 8, 8, true);
    framebuffer::fill_rect(fb, kUiWidth - 8, 0, 8, 8, true);
    framebuffer::fill_rect(fb, 0, kUiHeight - 8, 8, 8, true);
    framebuffer::fill_rect(fb, kUiWidth - 8, kUiHeight - 8, 8, 8, true);

    // Center cross.
    framebuffer::draw_vline(fb, mid_x, 0, kUiHeight - 1, true);
    framebuffer::draw_hline(fb, 0, kUiWidth - 1, mid_y, true);

    // Quarter lines.
    framebuffer::draw_vline(fb, q1_x, 16, kUiHeight - 17, true);
    framebuffer::draw_vline(fb, q3_x, 16, kUiHeight - 17, true);
    framebuffer::draw_hline(fb, 16, kUiWidth - 17, q1_y, true);
    framebuffer::draw_hline(fb, 16, kUiWidth - 17, q3_y, true);

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
    framebuffer::draw_rect(fb, mid_x - 30, mid_y - 20, 60, 40, true);

    // Top and bottom labels for orientation.
    framebuffer::draw_text(fb, 12, 12, "TOP", true, 1, 1);
    framebuffer::draw_text(fb, kUiWidth - 34, 12, "R", true, 2, 1);
    framebuffer::draw_text(fb, 12, kUiHeight - 20, "BOTTOM", true, 1, 1);

    // A few filled blocks for checking edge visibility and stability.
    framebuffer::fill_rect(fb, 20, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, kUiWidth - 32, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, mid_x - 10, 24, 20, 12, true);
    framebuffer::fill_rect(fb, mid_x - 10, kUiHeight - 36, 20, 12, true);
}

} // namespace screens
