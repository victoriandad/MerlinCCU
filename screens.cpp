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

/// @brief Returns the user-facing label for the currently selected weather source.
const char* weather_source_text(WeatherSource source)
{
    switch (source)
    {
    case WeatherSource::HomeAssistant:
        return "Home Assistant";
    case WeatherSource::MetOffice:
        return "Met Office";
    case WeatherSource::BbcWeather:
        return "BBC Weather";
    }

    return "?";
}

/// @brief Returns whether the selected weather source is still only a stub.
bool weather_source_is_stub(WeatherSource source)
{
    switch (source)
    {
    case WeatherSource::HomeAssistant:
        return false;
    case WeatherSource::MetOffice:
    case WeatherSource::BbcWeather:
        return true;
    }

    return true;
}

/// @brief Returns the user-facing label for the currently selected time zone.
const char* time_zone_text(TimeZoneSelection zone)
{
    switch (zone)
    {
    case TimeZoneSelection::AtlanticStandard:
        return "Atlantic Standard Time";
    case TimeZoneSelection::ArgentinaStandard:
        return "Argentina Time";
    case TimeZoneSelection::SouthGeorgia:
        return "South Georgia Time";
    case TimeZoneSelection::Azores:
        return "Azores Time";
    case TimeZoneSelection::EuropeLondon:
        return "Europe/London";
    case TimeZoneSelection::CentralEuropean:
        return "Central European Time";
    case TimeZoneSelection::EasternEuropean:
        return "Eastern European Time";
    case TimeZoneSelection::ArabiaStandard:
        return "Arabia Standard Time";
    case TimeZoneSelection::GulfStandard:
        return "Gulf Standard Time";
    }

    return "?";
}

/// @brief Returns the clock zone the firmware is actually applying today.
const char* applied_time_zone_text()
{
    return "Europe/London";
}

/// @brief Returns the page title that matches the active menu route.
const char* menu_page_title(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
        return "HOME";
    case MenuPage::Weather:
        return "WEATHER";
    case MenuPage::Status:
        return "HOME ASSISTANT";
    case MenuPage::Settings:
        return "SETTINGS";
    case MenuPage::WifiSettings:
        return "WIFI";
    case MenuPage::ScreenSaverSettings:
        return "SCREEN SAVER";
    case MenuPage::WeatherSources:
        return "WEATHER SOURCE";
    case MenuPage::TimeZoneSettings:
        return "TIME ZONE";
    case MenuPage::Alignment:
        return "ALIGN";
    case MenuPage::KeypadDebug:
        return "KEYPAD DEBUG";
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
    .line_gap = 4,
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
/// @details Selection-heavy pages use the denser face so two-line labels fit
/// comfortably, while diagnostic/detail pages can use the larger font.
fonts::FontFace softkey_label_font(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
    case MenuPage::Settings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
        return fonts::FontFace::Font5x7;
    case MenuPage::Weather:
    case MenuPage::Status:
    case MenuPage::WifiSettings:
    case MenuPage::Alignment:
    case MenuPage::KeypadDebug:
        return fonts::FontFace::Font8x12;
    case MenuPage::ScreenSaverSettings:
        return fonts::FontFace::Font5x7;
    }

    return fonts::FontFace::Font5x7;
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

struct DetailRow
{
    const char* label;
    const char* value;
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

    const size_t kFirstFitLength = fit_wrapped_label_prefix(label, font, max_width);
    if (kFirstFitLength == 0)
    {
        return wrapped;
    }

    size_t first_length = find_wrapped_label_split(label, kFirstFitLength);
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

    const size_t kSecondFitLength = fit_wrapped_label_prefix(label + second_start, font, max_width);
    if (kSecondFitLength == 0)
    {
        return wrapped;
    }

    size_t second_length = kSecondFitLength;
    if (label[second_start + kSecondFitLength] != '\0' &&
        label[second_start + kSecondFitLength] != '\n')
    {
        const size_t kSecondSplit =
            find_wrapped_label_split(label + second_start, kSecondFitLength);
        if (kSecondSplit > 0)
        {
            second_length = kSecondSplit;
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

/// @brief Draws one aligned detail row for information-oriented pages.
/// @details Labels stay uppercase and compact on the left while values remain
/// free to use mixed case on the right for readability.
void draw_detail_row(uint8_t* fb, int y, const DetailRow& row, bool draw_divider)
{
    constexpr int kLabelX = 50;
    constexpr int kValueX = 156;
    constexpr int kDividerLeftX = 46;
    constexpr int kDividerRightX = kUiWidth - 46;
    constexpr int kDividerOffsetY = 15;
    constexpr int kValueBaselineOffsetY = 1;

    const char* value = (row.value != nullptr && row.value[0] != '\0') ? row.value : "-";
    framebuffer::draw_text(fb, kLabelX, y, row.label, true, fonts::FontFace::FontTitle8x12, 1);
    framebuffer::draw_text(fb, kValueX, y + kValueBaselineOffsetY, value, true,
                           fonts::FontFace::Font5x7, 1);

    if (draw_divider)
    {
        framebuffer::draw_hline(fb, kDividerLeftX, kDividerRightX, y + kDividerOffsetY, true);
    }
}

/// @brief Draws a consistent stacked detail layout without heavy framing.
/// @details The same helper keeps diagnostics and status pages visually aligned
/// once their surrounding boxes have been removed.
void draw_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y = 46,
                      int row_pitch = 18, bool draw_dividers = true)
{
    for (size_t i = 0; i < count; ++i)
    {
        draw_detail_row(fb, start_y + (static_cast<int>(i) * row_pitch), rows[i],
                        draw_dividers && (i + 1 < count));
    }
}

/// @brief Draws the standard row-based presentation used by information pages.
/// @details The top banner carries the page title, so the body can stay clean
/// and consistent without extra boxes, subtitles, or divider lines.
void draw_info_page_rows(uint8_t* fb, const DetailRow* rows, size_t count)
{
    constexpr int kInfoPageStartY = 42;
    constexpr int kInfoPageRowPitch = 18;
    draw_detail_rows(fb, rows, count, kInfoPageStartY, kInfoPageRowPitch, false);
}

void draw_softkey_selection_brackets(uint8_t* fb, int left_x, int top_y, int total_height,
                                     int total_width, fonts::FontFace font, bool on);

/// @brief Formats the current screen-saver timeout for labels and scratchpad text.
void build_screen_saver_timeout_text(uint16_t minutes, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return;
    }

    const char* unit = (minutes == 1U) ? "min" : "mins";
    std::snprintf(buffer, buffer_size, "%u %s", static_cast<unsigned>(minutes), unit);
}

/// @brief Draws the bottom scratchpad used for screen-saver timeout entry.
/// @details The original CCU scratchpad was a low, wide editing region, so this
/// version keeps the same bottom-of-screen placement and bracketed treatment.
void draw_screen_saver_scratchpad(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kScratchpadWidth = 160;
    constexpr int kScratchpadHeight = 15;
    constexpr int kScratchpadLeftX = (kUiWidth - kScratchpadWidth) / 2;
    constexpr int kScratchpadTopY = kUiHeight - kScratchpadHeight - 3;
    constexpr int kTextInsetY = 4;
    constexpr int kRightPadX = 10;
    char timeout_text[16] = {};
    build_screen_saver_timeout_text(console_state.screen_saver_timeout_edit_minutes, timeout_text,
                                    sizeof(timeout_text));
    const int kTextWidth = text_width(timeout_text, fonts::FontFace::Font5x7, 1);
    const int kTextX = kScratchpadLeftX + kScratchpadWidth - kRightPadX - kTextWidth;

    framebuffer::fill_rect(fb, kScratchpadLeftX + 1, kScratchpadTopY + 1, kScratchpadWidth - 2,
                           kScratchpadHeight - 2, false);
    draw_softkey_selection_brackets(fb, kScratchpadLeftX, kScratchpadTopY, kScratchpadHeight,
                                    kScratchpadWidth, fonts::FontFace::Font5x7, true);
    framebuffer::draw_text(fb, kTextX, kScratchpadTopY + kTextInsetY,
                           timeout_text, true, fonts::FontFace::Font5x7, 1);
}

/// @brief Extracts the inner text from a bracketed softkey value line.
/// @details Selection labels are still authored as `[value]`, but the renderer
/// can choose to draw larger brackets around the value instead of tiny glyphs.
bool extract_bracketed_softkey_value(const char* line_text, char* out_value, size_t out_size)
{
    if (line_text == nullptr || out_value == nullptr || out_size == 0)
    {
        return false;
    }

    const size_t kLength = std::strlen(line_text);
    if (kLength < 3 || line_text[0] != '[' || line_text[kLength - 1] != ']')
    {
        return false;
    }

    copy_softkey_label_slice(out_value, out_size, line_text + 1, kLength - 2);
    return true;
}

/// @brief Returns the bracket depth used around one rendered softkey selection.
constexpr int softkey_bracket_depth(fonts::FontFace font)
{
    switch (font)
    {
    case fonts::FontFace::Font5x7:
        return 2;
    case fonts::FontFace::FontTitle8x12:
    case fonts::FontFace::Font8x12:
    case fonts::FontFace::Font8x14:
        return 3;
    }

    return 2;
}

/// @brief Draws one oversized square bracket pair around a softkey value.
/// @details The brackets are rendered as simple line primitives so they stay
/// readable even when the underlying bitmap font has tiny punctuation glyphs.
void draw_softkey_selection_brackets(uint8_t* fb, int left_x, int top_y, int total_height,
                                     int total_width, fonts::FontFace font, bool on)
{
    const int kDepth = softkey_bracket_depth(font);
    const int kRightX = left_x + total_width - 1;
    const int kBottomY = top_y + total_height - 1;

    framebuffer::draw_vline(fb, left_x, top_y, kBottomY, on);
    framebuffer::draw_hline(fb, left_x, left_x + kDepth, top_y, on);
    framebuffer::draw_hline(fb, left_x, left_x + kDepth, kBottomY, on);

    framebuffer::draw_vline(fb, kRightX, top_y, kBottomY, on);
    framebuffer::draw_hline(fb, kRightX - kDepth, kRightX, top_y, on);
    framebuffer::draw_hline(fb, kRightX - kDepth, kRightX, kBottomY, on);
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

struct ForecastDisplayWindow
{
    uint8_t first_index;
    uint8_t count;
};

/// @brief Chooses which forecast entries should be shown first on the weather page.
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

    const int kCurrentHourFloor = (current_minutes / 60) * 60;
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
            for (const int kCandidateOffset : kOffsetCandidates)
            {
                const int kCandidateMinutes = raw_forecast_minutes + kCandidateOffset;
                const int kDistance = kCandidateMinutes > kCurrentHourFloor
                                          ? (kCandidateMinutes - kCurrentHourFloor)
                                          : (kCurrentHourFloor - kCandidateMinutes);
                if (kDistance < best_distance)
                {
                    best_distance = kDistance;
                    day_offset = kCandidateOffset;
                }
            }
        }

        int forecast_minutes = raw_forecast_minutes + day_offset;
        while (have_previous_entry && forecast_minutes < previous_forecast_minutes)
        {
            day_offset += kDayMinutes;
            forecast_minutes = raw_forecast_minutes + day_offset;
        }

        if (forecast_minutes >= kCurrentHourFloor)
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

    const WrappedSoftkeyLabel kWrapped = wrap_softkey_label(action.label, font);
    const int kLineHeight = framebuffer::font_height(font);
    const int kBlockHeight =
        (kWrapped.line_count * kLineHeight) + ((kWrapped.line_count - 1) * kSoftkeyLayout.line_gap);
    const int kBlockTopY = y + ((kSoftkeyLayout.height - kBlockHeight) / 2);

    auto draw_line = [&](const char* line_text, int line_index)
    {
        if (line_text == nullptr || line_text[0] == '\0')
        {
            return;
        }

        char bracketed_value[48] = {};
        const bool kBracketedValue =
            extract_bracketed_softkey_value(line_text, bracketed_value, sizeof(bracketed_value));
        const int kBracketDepth = softkey_bracket_depth(font);
        constexpr int kBracketGap = 2;
        constexpr int kBracketPadY = 2;
        const int kInnerTextWidth =
            kBracketedValue ? text_width(bracketed_value, font) : text_width(line_text, font);
        const int kLabelWidth =
            kBracketedValue ? (kInnerTextWidth + (kBracketDepth * 2) + (kBracketGap * 2))
                            : kInnerTextWidth;
        const int kTextX =
            left_side
                ? (kSoftkeyLayout.left_x + kSoftkeyLayout.text_inset)
                : (kUiWidth - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset - kLabelWidth);
        const int kTextY = kBlockTopY + (line_index * (kLineHeight + kSoftkeyLayout.line_gap));
        const int kBracketTopY = kTextY - kBracketPadY;
        const int kBracketHeight = kLineHeight + (kBracketPadY * 2);
        const bool kTextOn = !action.inverted;
        if (action.inverted)
        {
            constexpr int kHighlightPadX = 1;
            constexpr int kHighlightPadY = 1;
            const int kHighlightTopY = (kBracketedValue ? kBracketTopY : kTextY) - kHighlightPadY;
            const int kHighlightHeight =
                (kBracketedValue ? kBracketHeight : kLineHeight) + (kHighlightPadY * 2);
            framebuffer::fill_rect(fb, kTextX - kHighlightPadX, kHighlightTopY,
                                   kLabelWidth + (kHighlightPadX * 2),
                                   kHighlightHeight, true);
        }

        if (!kBracketedValue)
        {
            framebuffer::draw_text(fb, kTextX, kTextY, line_text, kTextOn, font, 1);
            return;
        }

        const int kInnerTextX = kTextX + kBracketDepth + kBracketGap;
        draw_softkey_selection_brackets(fb, kTextX, kBracketTopY, kBracketHeight, kLabelWidth,
                                        font, kTextOn);
        framebuffer::draw_text(fb, kInnerTextX, kTextY, bracketed_value, kTextOn, font, 1);
    };

    draw_line(kWrapped.line_one, 0);
    if (kWrapped.line_count > 1)
    {
        draw_line(kWrapped.line_two, 1);
    }
}

/// @brief Returns the best weather-source label available for the footer.
/// @details Runtime hints take priority so the UI can show the actual selected
/// source rather than only the build-time default.
const char* weather_source_label_text(const ConsoleState& console_state)
{
    if (console_state.weather_source == WeatherSource::MetOffice)
    {
        return "Met Office";
    }

    if (console_state.weather_source == WeatherSource::BbcWeather)
    {
        return "BBC Weather";
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
/// @details The block collapses to one centered line when only one value is
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
        framebuffer::draw_text(fb, kUiWidth - 12 - text_width(sunset_label, kSunFont),
                               weather_sun_times_y(), sunset_label, true, kSunFont, 1);
        return;
    }

    const char* label = sunrise_label[0] != '\0' ? sunrise_label : sunset_label;
    if (label[0] == '\0')
    {
        return;
    }

    framebuffer::draw_hline(fb, 12, kUiWidth - 12, weather_sun_times_y() - 10, true);
    draw_centered_text(fb, kUiWidth / 2, weather_sun_times_y(), label, true, kSunFont, 1);
}

/// @brief Draws every softkey label for the current menu page.
/// @details Labels are rendered separately from the page body so most pages can
/// share the same bezel framing while only the content region changes.
void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    const fonts::FontFace kLabelFont = softkey_label_font(console_state.active_page);

    for (int i = 0; i < 5; ++i)
    {
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i], true, kLabelFont);
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i + 5], false,
                           kLabelFont);
    }
}

/// @brief Draws the weather-source attribution footer.
/// @details Provenance stays visible even on summary views so it is obvious
/// which backend is driving the weather data on the screen.
void draw_weather_source_footer(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace kFooterFont = fonts::FontFace::Font5x7;
    constexpr int kFooterLineGap = 2;

    const WrappedSoftkeyLabel kWrapped = wrap_label_two_lines(
        weather_source_label_text(console_state), kFooterFont, weather_source_footer_max_width());
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

/// @brief Leaves a top-level menu page intentionally blank in the center area.
/// @details Label-only shells rely on the surrounding softkeys rather than any
/// center content, which keeps those pages deliberately sparse.
void draw_blank_menu_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Produces a short user-facing weather-status fallback string.
/// @details The weather page is meant for end users rather than diagnostics, so
/// transport-layer details such as HTTP and socket codes are intentionally
/// collapsed into plain-language availability messages.
const char* weather_status_detail(const HomeAssistantStatus& status, char* buffer,
                                  size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    buffer[0] = '\0';

    if (status.last_http_status > 0 || status.last_error != 0)
    {
        std::snprintf(buffer, buffer_size, "NO DATA AVAILABLE");
        return buffer;
    }

    return home_assistant_state_text(status.state);
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

/// @brief Draws the clean top-level home menu.
/// @details The page body is intentionally empty so the only visual affordances
/// are the surrounding labels for the next level of navigation.
void draw_home_page(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kHomeIpX = 12;
    constexpr int kHomeIpY = kUiHeight - 18;
    constexpr fonts::FontFace kHomeIpFont = fonts::FontFace::Font5x7;

    draw_blank_menu_page(fb, console_state);

    char ip_text[32] = {};
    framebuffer::draw_text(fb, kHomeIpX, kHomeIpY,
                           home_ip_status_text(console_state.wifi_status, ip_text,
                                               sizeof(ip_text)),
                           true, kHomeIpFont, 1);
}

/// @brief Draws the live weather page reached directly from Home.
/// @details This keeps the richer forecast-first presentation that existed
/// before the menu cleanup, including the no-data summary path.
void draw_weather_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (weather_source_is_stub(console_state.weather_source))
    {
        const DetailRow rows[] = {
            {"SOURCE", weather_source_text(console_state.weather_source)},
            {"STATUS", "Stub only"},
            {"DETAIL", "Provider integration pending"},
        };
        draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
        return;
    }

    char status_detail[24] = {};
    char formatted_condition[32] = {};
    char formatted_forecast_condition[24] = {};
    const bool kWeatherConfigured =
        console_state.home_assistant_status.weather_entity_id[0] != '\0';
    const ForecastDisplayWindow kForecastWindow = active_forecast_window(console_state);
    const char* weather_condition = "WEATHER OFF";
    const char* weather_temperature = "";
    const char* weather_footer = "";

    if (kWeatherConfigured)
    {
        weather_condition =
            console_state.home_assistant_status.weather_condition[0]
                ? console_state.home_assistant_status.weather_condition.data()
                : (console_state.home_assistant_status.state ==
                           HomeAssistantConnectionState::Connected
                       ? "NO DATA AVAILABLE"
                       : weather_status_detail(console_state.home_assistant_status, status_detail,
                                               sizeof(status_detail)));
        weather_temperature = console_state.home_assistant_status.weather_temperature.data();

        if (console_state.home_assistant_status.state == HomeAssistantConnectionState::Connected)
        {
            weather_footer = "NO DATA AVAILABLE";
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

        if (console_state.home_assistant_status.weather_condition[0] != '\0')
        {
            format_weather_phrase(console_state.home_assistant_status.weather_condition.data(),
                                  formatted_condition, sizeof(formatted_condition));
            weather_condition = formatted_condition;
        }
    }

    if (kForecastWindow.count > 0)
    {
        constexpr fonts::FontFace kForecastHeaderFont = fonts::FontFace::Font5x7;
        constexpr fonts::FontFace kForecastBodyFont = fonts::FontFace::Font5x7;

        framebuffer::draw_text(fb, 12, 36, "Time", true, kForecastHeaderFont, 1);
        framebuffer::draw_text(fb, 60, 36, "Temp", true, kForecastHeaderFont, 1);
        framebuffer::draw_text(fb, 102, 36, "Wind mph", true, kForecastHeaderFont, 1);
        framebuffer::draw_text(fb, 160, 36, "Conditions", true, kForecastHeaderFont, 1);
        framebuffer::draw_hline(fb, 12, kUiWidth - 12, 46, true);

        for (uint8_t i = 0; i < kForecastWindow.count; ++i)
        {
            const WeatherForecastEntry& entry =
                console_state.home_assistant_status
                    .weather_forecast[kForecastWindow.first_index + i];
            const int kRowY = 54 + (static_cast<int>(i) * 18);
            format_weather_phrase(entry.condition_text.data(), formatted_forecast_condition,
                                  sizeof(formatted_forecast_condition));
            framebuffer::draw_text(fb, 12, kRowY, entry.time_text.data(), true, kForecastBodyFont,
                                   1);
            framebuffer::draw_text(fb, 60, kRowY, entry.temperature_text.data(), true,
                                   kForecastBodyFont, 1);
            framebuffer::draw_text(fb, 102, kRowY, entry.wind_text.data(), true, kForecastBodyFont,
                                   1);
            framebuffer::draw_text(
                fb, 160, kRowY,
                formatted_forecast_condition[0] != '\0' ? formatted_forecast_condition
                                                        : entry.condition_text.data(),
                true, kForecastBodyFont, 1);
        }
        if (kWeatherConfigured && weather_sun_times_available(console_state))
        {
            draw_weather_sun_times(fb, console_state);
        }
        if (kWeatherConfigured)
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

    if (kWeatherConfigured && weather_footer[0] != '\0')
    {
        draw_centered_text(fb, kUiWidth / 2, 162, weather_footer, true, fonts::FontFace::Font5x7,
                           1);
    }

    if (kWeatherConfigured)
    {
        draw_weather_source_footer(fb, console_state);
    }
}

/// @brief Draws the weather-source selection page under Settings.
/// @details The selection is expressed entirely through the softkey labels, so
/// the center area stays empty unless a future request needs more detail.
void draw_weather_sources_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_blank_menu_page(fb, console_state);
}

/// @brief Draws the Home Assistant status page.
/// @details The page keeps the useful integration data but avoids extra title
/// blocks and row dividers now that the top banner already carries the heading.
void draw_status_page(uint8_t* fb, const ConsoleState& console_state)
{
    char http_text[12] = {};
    if (console_state.home_assistant_status.last_http_status > 0)
    {
        std::snprintf(http_text, sizeof(http_text), "%d",
                      console_state.home_assistant_status.last_http_status);
    }
    else
    {
        std::snprintf(http_text, sizeof(http_text), "-");
    }

    const DetailRow rows[] = {
        {"TIME", console_state.time_status.synced ? console_state.time_status.time_text.data()
                                                  : "--:--"},
        {"WIFI", wifi_state_text(console_state.wifi_status.state)},
        {"SSID", console_state.wifi_status.credentials_present
                     ? console_state.wifi_status.ssid.data()
                     : "-"},
        {"IP ADDRESS", console_state.wifi_status.ip_address[0]
                           ? console_state.wifi_status.ip_address.data()
                           : "-"},
        {"HA STATE", home_assistant_state_text(console_state.home_assistant_status.state)},
        {"HA HOST", console_state.home_assistant_status.host[0]
                        ? console_state.home_assistant_status.host.data()
                        : "-"},
        {"HTTP", http_text},
        {"ENTITY", console_state.home_assistant_status.tracked_entity_state[0]
                       ? console_state.home_assistant_status.tracked_entity_state.data()
                       : "-"},
        {"MQTT", mqtt_state_text(console_state.mqtt_status.state)},
        {"DISCOVERY", console_state.mqtt_status.discovery_published ? "READY" : "-"},
    };

    draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
}

/// @brief Draws the clean settings routing page.
/// @details Like the home page, the center area stays empty so the menu feels
/// like a simple directory of subpages rather than a mixed menu/status screen.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_blank_menu_page(fb, console_state);
}

/// @brief Draws the Wi-Fi information page.
/// @details The Wi-Fi view now matches the same row layout used by the other
/// information pages instead of carrying its own boxed presentation.
void draw_wifi_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    const DetailRow rows[] = {
        {"SSID", console_state.wifi_status.ssid[0] ? console_state.wifi_status.ssid.data()
                                                   : "Not Set"},
        {"STATUS", wifi_state_text(console_state.wifi_status.state)},
        {"IP ADDRESS", console_state.wifi_status.ip_address[0]
                           ? console_state.wifi_status.ip_address.data()
                           : "-"},
        {"AUTH", console_state.wifi_status.auth_mode[0]
                     ? console_state.wifi_status.auth_mode.data()
                     : "-"},
        {"MAC", console_state.wifi_status.mac_address[0]
                    ? console_state.wifi_status.mac_address.data()
                    : "-"},
    };

    draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
}

/// @brief Draws the screen-saver information page.
/// @details This page now follows the same stripped-back presentation as the
/// rest of the information views.
void draw_screen_saver_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_blank_menu_page(fb, console_state);

    if (console_state.screen_saver_timeout_editing)
    {
        draw_screen_saver_scratchpad(fb, console_state);
    }
}

/// @brief Draws the time-zone selection summary page.
/// @details Like the weather-source page, this stays visually empty in the
/// center and lets the surrounding labels do all of the work.
void draw_time_zone_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_blank_menu_page(fb, console_state);
}

/// @brief Draws the keypad-debug diagnostics page.
/// @details The goal here is still hardware bring-up, but the layout now uses
/// the same clean row styling as the status page instead of a boxed panel.
void draw_keypad_debug_page(uint8_t* fb, const ConsoleState& console_state)
{
    char mask_text[16] = {};
    std::snprintf(mask_text, sizeof(mask_text), "0x%04lX",
                  static_cast<unsigned long>(console_state.keypad_debug_status.active_mask));
    char lines_text[24] = {};
    std::snprintf(lines_text, sizeof(lines_text), "%u/%u",
                  static_cast<unsigned>(console_state.keypad_debug_status.active_count),
                  static_cast<unsigned>(console_state.keypad_debug_status.configured_count));
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

    const DetailRow rows[] = {
        {"KEY PRESSED", console_state.keypad_debug_status.pressed_key_name[0]
                            ? console_state.keypad_debug_status.pressed_key_name.data()
                            : "-"},
        {"ACTIVE PINS", console_state.keypad_debug_status.active_panel_pins[0]
                            ? console_state.keypad_debug_status.active_panel_pins.data()
                            : "-"},
        {"ACTIVE MASK", mask_text},
        {"ACTIVE LINES", lines_text},
        {"PROBE DRIVE", drive_text},
        {"PROBE SENSE", console_state.keypad_debug_status.probe_hit_panel_pins[0]
                            ? console_state.keypad_debug_status.probe_hit_panel_pins.data()
                            : "-"},
    };

    draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
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
    draw_softkeys(fb, console_state);

    switch (console_state.active_page)
    {
    case MenuPage::Home:
        draw_home_page(fb, console_state);
        break;
    case MenuPage::Weather:
        draw_weather_page(fb, console_state);
        break;
    case MenuPage::Status:
        draw_status_page(fb, console_state);
        break;
    case MenuPage::Settings:
        draw_settings_page(fb, console_state);
        break;
    case MenuPage::WifiSettings:
        draw_wifi_settings_page(fb, console_state);
        break;
    case MenuPage::ScreenSaverSettings:
        draw_screen_saver_page(fb, console_state);
        break;
    case MenuPage::WeatherSources:
        draw_weather_sources_page(fb, console_state);
        break;
    case MenuPage::TimeZoneSettings:
        draw_time_zone_page(fb, console_state);
        break;
    case MenuPage::Alignment:
        draw_alignment_page(fb, console_state);
        break;
    case MenuPage::KeypadDebug:
        draw_keypad_debug_page(fb, console_state);
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

    // Center cross.
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
