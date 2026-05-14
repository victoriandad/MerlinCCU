#include "screens.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "config_manager.h"
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

constexpr uint8_t kSettingsPageCount = 2U;

/// @brief Copies a label while forcing uppercase presentation for UI field names.
/// @details This normalises label casing on rendered info/status pages without
/// mutating the underlying model value strings.
void build_uppercase_label(const char* input, char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0U)
    {
        return;
    }

    output[0] = '\0';
    if (input == nullptr || input[0] == '\0')
    {
        return;
    }

    size_t write_index = 0U;
    while (input[write_index] != '\0' && write_index + 1U < output_size)
    {
        output[write_index] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(input[write_index])));
        ++write_index;
    }

    output[write_index] = '\0';
}

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

/// @brief Returns a consistent yes/no style label for config booleans.
const char* enabled_text(bool enabled)
{
    return enabled ? "Enabled" : "Disabled";
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
        return "Disabled";
    case WifiConnectionState::Unconfigured:
        return "Unconfig";
    case WifiConnectionState::Initializing:
        return "Init";
    case WifiConnectionState::Scanning:
        return "Scan";
    case WifiConnectionState::Connecting:
        return "Connect";
    case WifiConnectionState::WaitingForIp:
        return "DHCP";
    case WifiConnectionState::Connected:
        return "Up";
    case WifiConnectionState::AuthFailed:
        return "Bad auth";
    case WifiConnectionState::NoNetwork:
        return "No net";
    case WifiConnectionState::ConnectFailed:
        return "Fail";
    case WifiConnectionState::Error:
        return "Error";
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
const char* weather_fetch_state_text(HomeAssistantConnectionState state)
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
        return "Fetch";
    case HomeAssistantConnectionState::Connected:
        return "Up";
    case HomeAssistantConnectionState::Unauthorized:
        return "Auth";
    case HomeAssistantConnectionState::Error:
        return "Error";
    }

    return "?";
}

/// @brief Returns the MQTT state label used on the condensed status page.
const char* mqtt_state_text(MqttConnectionState state)
{
    switch (state)
    {
    case MqttConnectionState::Disabled:
        return "Disabled";
    case MqttConnectionState::Unconfigured:
        return "Unconfig";
    case MqttConnectionState::WaitingForWifi:
        return "Wait wifi";
    case MqttConnectionState::Resolving:
        return "Resolve";
    case MqttConnectionState::Connecting:
        return "Connect";
    case MqttConnectionState::Connected:
        return "Up";
    case MqttConnectionState::AuthFailed:
        return "Auth";
    case MqttConnectionState::Error:
        return "Error";
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
    case WeatherSource::OpenMeteo:
    case WeatherSource::MetNorway:
        return "Open-Meteo";
    }

    return "?";
}

/// @brief Returns whether the selected weather source is still only a stub.
bool weather_source_is_stub(WeatherSource source)
{
    (void)source;
    return false;
}

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
        return "HOME ASSISTANT";
    case MenuPage::Settings:
        return "SETTINGS";
    case MenuPage::DeviceSettings:
        return "DEVICE IDENTITY";
    case MenuPage::SecuritySettings:
        return "SECURITY";
    case MenuPage::WifiSettings:
        return "NETWORK";
    case MenuPage::HomeAssistantSettings:
        return "HOME ASSISTANT";
    case MenuPage::MqttSettings:
        return "MQTT DISCOVERY";
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
    case MenuPage::AlertList:
        return "ALERTS";
    case MenuPage::AlertDetail:
        return "ALERT";
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
    if (console_state.active_page == MenuPage::AlertList)
    {
        constexpr uint8_t kAlertsPerPage = 9U;
        const uint8_t page_count = static_cast<uint8_t>(
            (console_state.alert_count == 0U)
                ? 1U
                : ((console_state.alert_count + (kAlertsPerPage - 1U)) / kAlertsPerPage));
        std::snprintf(buffer, buffer_size, "ALERTS %u/%u",
                      static_cast<unsigned>(console_state.alert_list_page_index + 1U),
                      static_cast<unsigned>(page_count));
        return buffer;
    }

    return menu_page_title(console_state.active_page);
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

/// @brief Returns the vertical centre line of the indexed softkey position.
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
    case MenuPage::Calendar:
    case MenuPage::CalendarDetail:
    case MenuPage::DeviceSettings:
    case MenuPage::SecuritySettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
    case MenuPage::Shares:
    case MenuPage::ShareDetail:
        return fonts::FontFace::Font5x7;
    case MenuPage::Weather:
    case MenuPage::Status:
    case MenuPage::Alignment:
    case MenuPage::KeypadDebug:
    case MenuPage::AlertList:
    case MenuPage::AlertDetail:
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
    constexpr size_t kLabelBufferSize = 32U;

    const char* value = (row.value != nullptr && row.value[0] != '\0') ? row.value : "-";
    char label_text[kLabelBufferSize] = {};
    build_uppercase_label(row.label, label_text, sizeof(label_text));
    framebuffer::draw_text(fb, kLabelX, y, label_text, true, fonts::FontFace::FontTitle8x12, 1);
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
    framebuffer::draw_text(fb, kTextX, kScratchpadTopY + kTextInsetY, timeout_text, true,
                           fonts::FontFace::Font5x7, 1);
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

/// @brief Chooses representative rows for a day-focused forecast layout.
/// @details The day period is capped to a handful of rows so each entry can
/// include both headline values and the condition text without clipping.
uint8_t build_day_forecast_sample(const ForecastRenderSlice& forecast_slice,
                                  std::array<uint8_t, 5>& out_forecast_indices)
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
    std::array<uint8_t, kWeatherForecastEntryCount> day_entries = {};
    uint8_t day_entry_count = 0;
    for (uint8_t i = 0; i < forecast_slice.count; ++i)
    {
        if (forecast_slice.entries[i].absolute_minutes > period_end_minutes)
        {
            break;
        }

        day_entries[day_entry_count] = forecast_slice.entries[i].forecast_index;
        ++day_entry_count;
    }

    if (day_entry_count == 0)
    {
        day_entries[0] = forecast_slice.entries[0].forecast_index;
        day_entry_count = 1;
    }

    const uint8_t max_rows = static_cast<uint8_t>(out_forecast_indices.size());
    if (day_entry_count <= max_rows)
    {
        for (uint8_t i = 0; i < day_entry_count; ++i)
        {
            out_forecast_indices[i] = day_entries[i];
        }
        return day_entry_count;
    }

    for (uint8_t row = 0; row < max_rows; ++row)
    {
        const uint8_t sample_index =
            static_cast<uint8_t>((row * (day_entry_count - 1U)) / (max_rows - 1U));
        out_forecast_indices[row] = day_entries[sample_index];
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

/// @brief Formats one day label for the weekly period rows.
const char* week_day_label_text(uint8_t row_index, const char* date_text, char* buffer,
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
        std::snprintf(buffer, buffer_size, "Tmrw");
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

/// @brief Draws text centred around a given x-coordinate.
/// @details Centralizing the centering math keeps titles and status callouts
/// aligned consistently across the different page renderers.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7, int spacing = 1)
{
    framebuffer::draw_text(fb, center_x - (text_width(text, font, spacing) / 2), y, text, on, font,
                           spacing);
}

/// @brief Draws one mirrored softkey label block.
/// @details The label is vertically centred within the physical key slot so
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
        const int kLabelWidth = kBracketedValue
                                    ? (kInnerTextWidth + (kBracketDepth * 2) + (kBracketGap * 2))
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
                                   kLabelWidth + (kHighlightPadX * 2), kHighlightHeight, true);
        }

        if (!kBracketedValue)
        {
            framebuffer::draw_text(fb, kTextX, kTextY, line_text, kTextOn, font, 1);
            return;
        }

        const int kInnerTextX = kTextX + kBracketDepth + kBracketGap;
        draw_softkey_selection_brackets(fb, kTextX, kBracketTopY, kBracketHeight, kLabelWidth, font,
                                        kTextOn);
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

/// @brief Leaves a top-level menu page intentionally blank in the centre area.
/// @details Label-only shells rely on the surrounding softkeys rather than any
/// centre content, which keeps those pages deliberately sparse.
void draw_blank_menu_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Returns the owner label used by Calendar labels and details.
const char* calendar_owner_text(CalendarOwner owner)
{
    switch (owner)
    {
    case CalendarOwner::Combined:
        return "Combined";
    case CalendarOwner::Sean:
        return "Sean";
    case CalendarOwner::Luigina:
        return "Luigina";
    case CalendarOwner::Loris:
        return "Loris";
    case CalendarOwner::Luca:
        return "Luca";
    }

    return "Combined";
}

/// @brief Returns the short weekday label for compact Calendar date text.
const char* weekday_short_text(uint8_t weekday_index)
{
    static constexpr std::array<const char*, 7> kWeekdayLabels = {"Sun", "Mon", "Tue", "Wed",
                                                                  "Thu", "Fri", "Sat"};
    if (weekday_index >= kWeekdayLabels.size())
    {
        return "";
    }

    return kWeekdayLabels[weekday_index];
}

/// @brief Returns the weekday index reached by moving relative to today.
uint8_t shifted_weekday_index(uint8_t today_weekday_index, int8_t day_offset)
{
    int index = static_cast<int>(today_weekday_index) + static_cast<int>(day_offset);
    while (index < 0)
    {
        index += 7;
    }

    return static_cast<uint8_t>(index % 7);
}

/// @brief Returns the human-readable prefix for a week-distance bucket.
const char* calendar_week_prefix(int weeks)
{
    switch (weeks)
    {
    case 1:
        return "Week";
    case 2:
        return "Two Weeks";
    case 3:
        return "Three Weeks";
    case 4:
        return "Four Weeks";
    }

    return "Weeks";
}

/// @brief Formats a single relative day label for the Calendar footer.
/// @details The wording is deliberately compact because this label lives in
/// the narrow bottom footer, not in the surrounding event softkeys.
const char* calendar_day_text(const ConsoleState& console_state, int8_t day_offset, char* buffer,
                              size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    if (day_offset == 0)
    {
        return "Today";
    }
    if (day_offset == 1)
    {
        return "Tomorrow";
    }
    if (day_offset == -1)
    {
        return "Yesterday";
    }

    if (console_state.time_status.synced &&
        console_state.time_status.weekday_index != kInvalidWeekdayIndex)
    {
        const uint8_t weekday =
            shifted_weekday_index(console_state.time_status.weekday_index, day_offset);
        const char* weekday_text = weekday_short_text(weekday);
        if (day_offset < 0)
        {
            const int age_days = -static_cast<int>(day_offset);
            if (age_days < 7)
            {
                std::snprintf(buffer, buffer_size, "Last %s", weekday_text);
                return buffer;
            }

            std::snprintf(buffer, buffer_size, "%s Last %s", calendar_week_prefix(age_days / 7),
                          weekday_text);
            return buffer;
        }

        if (day_offset < 7)
        {
            std::snprintf(buffer, buffer_size, "%s", weekday_text);
            return buffer;
        }

        const int weeks = static_cast<int>(day_offset) / 7;
        const int remainder = static_cast<int>(day_offset) % 7;
        const char* relative_text = weekday_text;
        if (remainder == 0)
        {
            relative_text = "Today";
        }
        else if (remainder == 1)
        {
            relative_text = "Tomorrow";
        }

        std::snprintf(buffer, buffer_size, "%s %s", calendar_week_prefix(weeks), relative_text);
        return buffer;
    }

    if (day_offset > 0)
    {
        std::snprintf(buffer, buffer_size, "%d days", static_cast<int>(day_offset));
        return buffer;
    }

    std::snprintf(buffer, buffer_size, "%d days", static_cast<int>(day_offset));
    return buffer;
}

/// @brief Draws a small left or right arrow independent of font glyph support.
void draw_calendar_footer_arrow(uint8_t* fb, int centre_x, int centre_y, int direction)
{
    constexpr int kArrowLength = 11;
    constexpr int kArrowHead = 4;
    const int tip_x = centre_x + ((direction < 0) ? -kArrowLength / 2 : kArrowLength / 2);
    const int tail_x = centre_x + ((direction < 0) ? kArrowLength / 2 : -kArrowLength / 2);

    framebuffer::draw_hline(fb, tail_x, tip_x, centre_y, true);
    framebuffer::draw_line(fb, tip_x, centre_y, tip_x - (direction * kArrowHead),
                           centre_y - kArrowHead, true);
    framebuffer::draw_line(fb, tip_x, centre_y, tip_x - (direction * kArrowHead),
                           centre_y + kArrowHead, true);
}

/// @brief Draws the bottom Calendar relative-day footer.
/// @details The bottom cursor keys move the selected day, so the footer keeps
/// the active day bracketed by visible left/right arrow markers.
void draw_calendar_navigation_footer(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kFooterY = kUiHeight - 25;
    constexpr int kArrowGap = 12;
    constexpr int kArrowCentreYOffset = 3;
    constexpr fonts::FontFace kFooterFont = fonts::FontFace::Font5x7;
    char day_label[32] = {};
    const char* day_text = calendar_day_text(console_state, console_state.calendar_day_offset,
                                             day_label, sizeof(day_label));
    if (day_text == nullptr || day_text[0] == '\0')
    {
        std::snprintf(day_label, sizeof(day_label), "%d days",
                      static_cast<int>(console_state.calendar_day_offset));
        day_text = day_label;
    }

    const int label_width = text_width(day_text, kFooterFont, 1);
    const int label_x = (kUiWidth - label_width) / 2;
    framebuffer::draw_text(fb, label_x, kFooterY, day_text, true, kFooterFont, 1);
    draw_calendar_footer_arrow(fb, label_x - kArrowGap, kFooterY + kArrowCentreYOffset, -1);
    draw_calendar_footer_arrow(fb, label_x + label_width + kArrowGap,
                               kFooterY + kArrowCentreYOffset, 1);
}

constexpr int kCalendarDetailTextX = 10;
constexpr fonts::FontFace kCalendarDetailFont = fonts::FontFace::Font5x7;

/// @brief Returns the value-column x-coordinate for compact Calendar details.
/// @details The event-detail font is proportional, so spaces cannot be used for
/// visual alignment. Measuring the widest rendered `Label:` gives a stable
/// column for all values.
int calendar_detail_value_x(const DetailRow* rows, size_t count)
{
    constexpr size_t kLabelBufferSize = 32U;
    int widest_label_width = 0;
    for (size_t i = 0; i < count; ++i)
    {
        char label_text[kLabelBufferSize] = {};
        std::snprintf(label_text, sizeof(label_text),
                      "%s:", rows[i].label != nullptr ? rows[i].label : "");
        widest_label_width =
            std::max(widest_label_width, text_width(label_text, kCalendarDetailFont, 1));
    }

    return kCalendarDetailTextX + widest_label_width + text_width(" ", kCalendarDetailFont, 1);
}

/// @brief Draws one compact Calendar detail line with pixel-aligned values.
void draw_calendar_detail_line(uint8_t* fb, int y, const char* label, const char* value,
                               int value_x)
{
    constexpr size_t kLabelBufferSize = 32U;
    char label_text[kLabelBufferSize] = {};
    std::snprintf(label_text, sizeof(label_text), "%s:", label != nullptr ? label : "");

    framebuffer::draw_text(fb, kCalendarDetailTextX, y, label_text, true, kCalendarDetailFont, 1);
    framebuffer::draw_text(fb, value_x, y, (value != nullptr && value[0] != '\0') ? value : "-",
                           true, kCalendarDetailFont, 1);
}

/// @brief Produces a short user-facing weather-status fallback string.
/// @details Home Assistant keeps the old end-user wording, while direct
/// providers expose compact transport details because they are the only way to
/// tell an HTTPS/API failure from an unsupported response payload in the field.
const char* weather_status_detail(const ConsoleState& console_state, char* buffer,
                                  size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0)
    {
        return "";
    }

    buffer[0] = '\0';
    const HomeAssistantStatus& status = console_state.home_assistant_status;

    if (status.last_http_status > 0 || status.last_error != 0)
    {
        if (console_state.weather_source == WeatherSource::HomeAssistant)
        {
            std::snprintf(buffer, buffer_size, "NO DATA AVAILABLE");
        }
        else if (status.last_http_status > 0 && status.last_http_status != 200)
        {
            std::snprintf(buffer, buffer_size, "HTTP %d", status.last_http_status);
        }
        else if (status.last_error != 0)
        {
            std::snprintf(buffer, buffer_size, "ERR %d", status.last_error);
        }
        else
        {
            std::snprintf(buffer, buffer_size, "BAD DATA");
        }
        return buffer;
    }

    return console_state.weather_source == WeatherSource::HomeAssistant
               ? home_assistant_state_text(status.state)
               : weather_fetch_state_text(status.state);
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

/// @brief Draws the dense hourly forecast table used by the hour period mode.
bool draw_hour_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                               const ForecastRenderSlice& forecast_slice)
{
    if (forecast_slice.count == 0)
    {
        return false;
    }

    constexpr fonts::FontFace kForecastHeaderFont = fonts::FontFace::Font5x7;
    constexpr fonts::FontFace kForecastBodyFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, 12, 36, "Time", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 60, 36, "Temp", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 102, 36, "Wind mph", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 160, 36, "Conditions", true, kForecastHeaderFont, 1);
    framebuffer::draw_hline(fb, 12, kUiWidth - 12, 46, true);

    char formatted_condition[24] = {};
    for (uint8_t i = 0; i < forecast_slice.count; ++i)
    {
        const WeatherForecastEntry& entry =
            console_state.home_assistant_status
                .weather_forecast[forecast_slice.entries[i].forecast_index];
        const int row_y = 54 + (static_cast<int>(i) * 18);
        format_weather_phrase(entry.condition_text.data(), formatted_condition,
                              sizeof(formatted_condition));
        framebuffer::draw_text(fb, 12, row_y, entry.time_text.data(), true, kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 60, row_y, entry.temperature_text.data(), true,
                               kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 102, row_y, entry.wind_text.data(), true, kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 160, row_y,
                               formatted_condition[0] != '\0' ? formatted_condition
                                                              : entry.condition_text.data(),
                               true, kForecastBodyFont, 1);
    }

    return true;
}

/// @brief Draws the day period layout with room for condition text per row.
bool draw_day_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                              const ForecastRenderSlice& forecast_slice)
{
    std::array<uint8_t, 5> day_sample_indices = {};
    const uint8_t row_count = build_day_forecast_sample(forecast_slice, day_sample_indices);
    if (row_count == 0)
    {
        return false;
    }

    constexpr fonts::FontFace kForecastHeaderFont = fonts::FontFace::Font5x7;
    constexpr fonts::FontFace kForecastBodyFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, 12, 36, "Time", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 64, 36, "Temp", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 112, 36, "Wind", true, kForecastHeaderFont, 1);
    framebuffer::draw_hline(fb, 12, kUiWidth - 12, 46, true);

    char formatted_condition[24] = {};
    for (uint8_t i = 0; i < row_count; ++i)
    {
        const WeatherForecastEntry& entry =
            console_state.home_assistant_status.weather_forecast[day_sample_indices[i]];
        const int row_y = 54 + (static_cast<int>(i) * 36);
        format_weather_phrase(entry.condition_text.data(), formatted_condition,
                              sizeof(formatted_condition));
        framebuffer::draw_text(fb, 12, row_y, entry.time_text.data(), true, kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 64, row_y, entry.temperature_text.data(), true,
                               kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 112, row_y, entry.wind_text.data(), true, kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 12, row_y + 14,
                               formatted_condition[0] != '\0' ? formatted_condition
                                                              : entry.condition_text.data(),
                               true, kForecastBodyFont, 1);

        if (i + 1 < row_count)
        {
            framebuffer::draw_hline(fb, 12, kUiWidth - 12, row_y + 26, true);
        }
    }

    return true;
}

/// @brief Draws a week period summary using provider-native daily forecast rows.
bool draw_week_forecast_period(uint8_t* fb, const ConsoleState& console_state,
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

    constexpr fonts::FontFace kForecastHeaderFont = fonts::FontFace::Font5x7;
    constexpr fonts::FontFace kForecastBodyFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, 12, 36, "Day", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 56, 36, "Temp", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 106, 36, "Wind", true, kForecastHeaderFont, 1);
    framebuffer::draw_text(fb, 148, 36, "Conditions", true, kForecastHeaderFont, 1);
    framebuffer::draw_hline(fb, 12, kUiWidth - 12, 46, true);

    char day_label[16] = {};
    char formatted_condition[24] = {};
    for (uint8_t i = 0; i < row_count; ++i)
    {
        const WeatherDailyForecastEntry& entry =
            console_state.home_assistant_status.weather_daily_forecast[i];
        const int row_y = 54 + (static_cast<int>(i) * 24);
        format_weather_phrase(entry.condition_text.data(), formatted_condition,
                              sizeof(formatted_condition));

        framebuffer::draw_text(
            fb, 12, row_y,
            week_day_label_text(i, entry.date_text.data(), day_label, sizeof(day_label)), true,
            kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 56, row_y, entry.temperature_text.data(), true,
                               kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 106, row_y, entry.wind_text.data(), true, kForecastBodyFont, 1);
        framebuffer::draw_text(fb, 148, row_y,
                               formatted_condition[0] != '\0' ? formatted_condition
                                                              : entry.condition_text.data(),
                               true, kForecastBodyFont, 1);

        if (i + 1 < row_count)
        {
            framebuffer::draw_hline(fb, 12, kUiWidth - 12, row_y + 16, true);
        }
    }

    return true;
}

/// @brief Draws weather forecast content for the active hour/day/week period.
bool draw_weather_forecast_period(uint8_t* fb, const ConsoleState& console_state,
                                  const ForecastRenderSlice& forecast_slice)
{
    switch (console_state.weather_period)
    {
    case WeatherPeriod::Hour:
        return draw_hour_forecast_period(fb, console_state, forecast_slice);
    case WeatherPeriod::Day:
        return draw_day_forecast_period(fb, console_state, forecast_slice);
    case WeatherPeriod::Week:
        return draw_week_forecast_period(fb, console_state, forecast_slice);
    }

    return false;
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
                           home_ip_status_text(console_state.wifi_status, ip_text, sizeof(ip_text)),
                           true, kHomeIpFont, 1);
}

/// @brief Draws the family calendar overview selected from Home.
/// @details Event summaries live on the surrounding softkeys. The centre area
/// deliberately stays blank so the page remains a label-driven CCU view.
void draw_calendar_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_calendar_navigation_footer(fb, console_state);
}

/// @brief Draws detailed data for the selected family calendar event.
/// @details The fields mirror upstream calendar properties that are useful on
/// a small operational display: time, owner, location, reminders, attendees,
/// and free-form description text.
void draw_calendar_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.selected_calendar_event_index >= console_state.calendar_events.size())
    {
        draw_centered_text(fb, kUiWidth / 2, 112, "NO EVENT", true, fonts::FontFace::Font8x12, 1);
        return;
    }

    const CalendarEvent& event =
        console_state.calendar_events[console_state.selected_calendar_event_index];
    if (event.title[0] == '\0')
    {
        draw_centered_text(fb, kUiWidth / 2, 112, "NO EVENT", true, fonts::FontFace::Font8x12, 1);
        return;
    }

    char owner_line[48] = {};
    char time_line[48] = {};
    std::snprintf(owner_line, sizeof(owner_line), "%s", calendar_owner_text(event.owner));
    std::snprintf(time_line, sizeof(time_line), "%s-%s",
                  event.start_time[0] != '\0' ? event.start_time.data() : "--:--",
                  event.end_time[0] != '\0' ? event.end_time.data() : "--:--");

    const DetailRow rows[] = {
        {"Event", event.title.data()},
        {"Time", time_line},
        {"Owner", owner_line},
        {"Location", event.location[0] != '\0' ? event.location.data() : "-"},
        {"Alarm", event.reminder[0] != '\0' ? event.reminder.data() : "-"},
        {"Attendees", event.attendees[0] != '\0' ? event.attendees.data() : "-"},
        {"Detail", event.description[0] != '\0' ? event.description.data() : "-"},
    };
    constexpr int kStartY = 42;
    constexpr int kRowPitch = 18;
    constexpr size_t kRowCount = sizeof(rows) / sizeof(rows[0]);
    const int value_x = calendar_detail_value_x(rows, kRowCount);

    for (size_t i = 0; i < kRowCount; ++i)
    {
        draw_calendar_detail_line(fb, kStartY + (static_cast<int>(i) * kRowPitch), rows[i].label,
                                  rows[i].value, value_x);
    }
}

/// @brief Draws the live weather page reached directly from Home.
/// @details This keeps the richer forecast-first presentation that existed
/// before the menu cleanup, including the no-data summary path.
void draw_weather_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (weather_source_is_stub(console_state.weather_source))
    {
        const DetailRow rows[] = {{"SOURCE", weather_source_text(console_state.weather_source)},
                                  {"STATUS", "Stub only"},
                                  {"DETAIL", "Provider integration pending"}};
        draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
        return;
    }

    char status_detail[24] = {};
    char formatted_condition[32] = {};
    char direct_config_detail[48] = {};
    const bool kWeatherConfigured =
        (console_state.weather_source == WeatherSource::HomeAssistant)
            ? (console_state.home_assistant_status.weather_entity_id[0] != '\0')
            : true;
    const ForecastRenderSlice kForecastSlice = build_forecast_render_slice(console_state);
    const char* weather_condition = "WEATHER OFF";
    const char* weather_temperature = "";
    const char* weather_footer = "";

    if (kWeatherConfigured)
    {
        if (console_state.weather_source != WeatherSource::HomeAssistant &&
            console_state.home_assistant_status.state == HomeAssistantConnectionState::Unconfigured)
        {
            weather_condition = "SET COORDS";
            if (console_state.home_assistant_status.weather_entity_id[0] != '\0')
            {
                std::snprintf(direct_config_detail, sizeof(direct_config_detail), "GOT %.32s",
                              console_state.home_assistant_status.weather_entity_id.data());
                weather_footer = direct_config_detail;
            }
            else
            {
                weather_footer = "NO COORDS SAVED";
            }
        }
        else
        {
            weather_condition = console_state.home_assistant_status.weather_condition[0]
                                    ? console_state.home_assistant_status.weather_condition.data()
                                    : (console_state.home_assistant_status.state ==
                                               HomeAssistantConnectionState::Connected
                                           ? "NO DATA AVAILABLE"
                                           : weather_status_detail(console_state, status_detail,
                                                                   sizeof(status_detail)));
            weather_temperature = console_state.home_assistant_status.weather_temperature.data();

            if (console_state.home_assistant_status.state ==
                HomeAssistantConnectionState::Connected)
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
                weather_footer =
                    weather_status_detail(console_state, status_detail, sizeof(status_detail));
            }

            if (console_state.home_assistant_status.weather_condition[0] != '\0')
            {
                format_weather_phrase(console_state.home_assistant_status.weather_condition.data(),
                                      formatted_condition, sizeof(formatted_condition));
                weather_condition = formatted_condition;
            }
        }
    }

    const bool have_active_period_data =
        (console_state.weather_period == WeatherPeriod::Week)
            ? (console_state.home_assistant_status.weather_daily_forecast_count > 0)
            : (kForecastSlice.count > 0);
    if (have_active_period_data && draw_weather_forecast_period(fb, console_state, kForecastSlice))
    {
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

/// @brief Draws the share watchlist page.
/// @details Shares are selected from the surrounding softkeys. The centre of the
/// watchlist page intentionally stays clear so the selected share name and price
/// are not duplicated beside the L1 label.
void draw_shares_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.share_count == 0U)
    {
        draw_centered_text(fb, kUiWidth / 2, 112, "NO SHARES", true, fonts::FontFace::Font8x12, 1);
        draw_centered_text(fb, kUiWidth / 2, 142, "ADD FLOW PENDING", true,
                           fonts::FontFace::Font5x7, 1);
        return;
    }

    (void)fb;
}

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

/// @brief Draws one full-width share history graph in the centre detail region.
void draw_share_history_graph(uint8_t* fb, const ShareWatchEntry& share, SharePeriod period)
{
    constexpr int kGraphX = 12;
    constexpr int kGraphY = 44;
    const int kGraphWidth = kUiWidth - (kGraphX * 2);
    constexpr int kGraphHeight = 120;
    constexpr int kPointCount = 24;
    constexpr int kGraphMinLabelGapY = 8;
    constexpr int kGraphPlotLeftInset = 1;
    constexpr int kGraphPlotRightInset = 1;

    uint16_t min_value = share.history_points[0];
    uint16_t max_value = share.history_points[0];
    for (uint16_t value : share.history_points)
    {
        min_value = std::min(min_value, value);
        max_value = std::max(max_value, value);
    }

    const uint16_t range =
        (max_value > min_value) ? static_cast<uint16_t>(max_value - min_value) : 1U;
    int previous_x = kGraphX + kGraphPlotLeftInset;
    int previous_y = kGraphY + kGraphHeight - 1;
    for (int i = 0; i < kPointCount; ++i)
    {
        const uint16_t value = share.history_points[static_cast<size_t>(i)];
        const int x = kGraphX + kGraphPlotLeftInset +
                      ((kGraphWidth - kGraphPlotLeftInset - kGraphPlotRightInset - 1) * i) /
                          (kPointCount - 1);
        const int normalised =
            ((static_cast<int>(value - min_value)) * (kGraphHeight - 1)) / static_cast<int>(range);
        const int y = kGraphY + kGraphHeight - 1 - normalised;
        if (i > 0)
        {
            framebuffer::draw_line(fb, previous_x, previous_y, x, y, true);
        }
        previous_x = x;
        previous_y = y;
    }

    const char* period_label = "TODAY";
    switch (period)
    {
    case SharePeriod::Today:
        period_label = "TODAY";
        break;
    case SharePeriod::Week:
        period_label = "WEEK";
        break;
    case SharePeriod::Month:
        period_label = "MONTH";
        break;
    case SharePeriod::Year:
        period_label = "YEAR";
        break;
    case SharePeriod::AllTime:
        period_label = "ALL-TIME";
        break;
    }
    framebuffer::draw_text(fb, kGraphX, kGraphY + 2, period_label, true, fonts::FontFace::Font5x7,
                           1);

    char min_value_text[16] = {};
    char max_value_text[16] = {};
    char min_label[24] = {};
    char max_label[24] = {};
    format_share_graph_value(min_value, min_value_text, sizeof(min_value_text));
    format_share_graph_value(max_value, max_value_text, sizeof(max_value_text));
    std::snprintf(min_label, sizeof(min_label), "MIN %s", min_value_text);
    std::snprintf(max_label, sizeof(max_label), "MAX %s", max_value_text);

    const int label_y = kGraphY + kGraphHeight + kGraphMinLabelGapY;
    framebuffer::draw_text(fb, kGraphX, label_y, min_label, true, fonts::FontFace::Font5x7, 1);
    const int max_label_width = text_width(max_label, fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, kGraphX + kGraphWidth - max_label_width, label_y, max_label, true,
                           fonts::FontFace::Font5x7, 1);
}

/// @brief Draws one watched share's detail page.
void draw_share_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.selected_share_index >= console_state.share_count)
    {
        draw_centered_text(fb, kUiWidth / 2, 112, "NO SHARE", true, fonts::FontFace::Font8x12, 1);
        return;
    }

    const ShareWatchEntry& share = console_state.watched_shares[console_state.selected_share_index];
    draw_share_history_graph(fb, share, console_state.share_period);
    framebuffer::draw_text(fb, 42, 184, share.display_name.data(), true, fonts::FontFace::Font8x12,
                           1);
    framebuffer::draw_text(fb, 42, 210, share.symbol.data(), true, fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, 86, 210, share.price_text.data(), true, fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, 150, 210, share.change_text.data(), true, fonts::FontFace::Font5x7,
                           1);
}

/// @brief Draws the weather-source selection page under Settings.
/// @details Settings subpages keep values on the surrounding softkeys so the
/// centre of the display stays free of duplicate status text.
void draw_weather_sources_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Draws the Home Assistant status page.
/// @details The page keeps the useful integration data but avoids extra title
/// blocks and row dividers now that the top banner already carries the heading.
void draw_status_page(uint8_t* fb, const ConsoleState& console_state)
{
    const RuntimeConfig& config = config_manager::settings();
    const char* ha_rest_state = "Unconfig";
    if (!config.home_assistant_enabled)
    {
        ha_rest_state = "Disabled";
    }
    else if (console_state.weather_source == WeatherSource::HomeAssistant)
    {
        ha_rest_state = home_assistant_state_text(console_state.home_assistant_status.state);
    }
    else if (config.home_assistant_host[0] != '\0' && config.home_assistant_token[0] != '\0')
    {
        ha_rest_state = "Enabled";
    }

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
        {"TIME",
         console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--"},
        {"WIFI", wifi_state_text(console_state.wifi_status.state)},
        {"SSID", console_state.wifi_status.credentials_present
                     ? console_state.wifi_status.ssid.data()
                     : "-"},
        {"IP ADDRESS", console_state.wifi_status.ip_address[0]
                           ? console_state.wifi_status.ip_address.data()
                           : "-"},
        {"HA REST", enabled_text(config.home_assistant_enabled)},
        {"HA REST ST", ha_rest_state},
        {"HA REST HOST",
         config.home_assistant_host[0] != '\0' ? config.home_assistant_host.data() : "-"},
        {"WX FETCH", weather_fetch_state_text(console_state.home_assistant_status.state)},
        {"WX HOST", console_state.home_assistant_status.host[0]
                        ? console_state.home_assistant_status.host.data()
                        : "-"},
        {"HTTP", http_text},
        {"HA MQTT", mqtt_state_text(console_state.mqtt_status.state)},
    };

    draw_info_page_rows(fb, rows, sizeof(rows) / sizeof(rows[0]));
}

/// @brief Draws shared left/right page-navigation arrows used by paged menus.
/// @details One shared helper keeps all paged screens visually consistent and allows
/// global position tweaks from a single location.
void draw_page_navigation_arrows(uint8_t* fb, bool show_left, bool show_right)
{
    constexpr int kArrowY = kUiHeight - 18;
    constexpr int kArrowHalfWidth = 5;
    constexpr int kArrowHalfHeight = 6;
    constexpr int kLeftArrowX = (kUiWidth / 2) - 26;
    constexpr int kRightArrowX = (kUiWidth / 2) + 26;

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

/// @brief Draws the top-level settings routing page.
/// @details The root page intentionally leaves the centre clear. Section state
/// belongs in the bracketed softkey labels; detailed values are shown only
/// after the operator opens a focused settings subpage.
void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_page_navigation_arrows(fb, console_state.settings_page_index > 0U,
                                (console_state.settings_page_index + 1U) < kSettingsPageCount);
}

/// @brief Leaves the device identity settings body blank.
/// @details The surrounding softkeys carry each visible identity value.
void draw_device_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Leaves the security settings body blank.
/// @details Security state is shown on the bracketed softkey labels.
void draw_security_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Leaves the network settings body blank.
/// @details Configured Wi-Fi values are shown as softkey attributes only.
void draw_wifi_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Leaves the Home Assistant settings body blank.
/// @details Integration settings are exposed as bracketed softkey attributes.
void draw_home_assistant_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Leaves the MQTT discovery settings body blank.
/// @details Broker and discovery values are shown around the bezel.
void draw_mqtt_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Draws only active screen-saver editing UI.
/// @details When not editing, the selected saver and timeout live on softkeys.
void draw_screen_saver_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.screen_saver_timeout_editing)
    {
        draw_screen_saver_scratchpad(fb, console_state);
    }
}

/// @brief Leaves the time-zone settings body blank.
/// @details Available zones are presented as softkey choices.
void draw_time_zone_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
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

/// @brief Draws compact status lines for the alert-list page.
void draw_alert_list_page(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr uint8_t kAlertsPerPage = 9U;
    const uint8_t page_count = static_cast<uint8_t>(
        (console_state.alert_count == 0U)
            ? 1U
            : ((console_state.alert_count + (kAlertsPerPage - 1U)) / kAlertsPerPage));
    draw_page_navigation_arrows(fb, console_state.alert_list_page_index > 0U,
                                (console_state.alert_list_page_index + 1U) < page_count);
}

/// @brief Draws the selected alert detail text with line-based scrolling.
void draw_alert_detail_page(uint8_t* fb, const ConsoleState& console_state)
{
    if (console_state.alert_detail_index >= console_state.alert_count)
    {
        framebuffer::draw_text(fb, 18, 44, "No alert selected", true, fonts::FontFace::Font8x12, 1);
        return;
    }

    const ActiveAlert& alert = console_state.active_alerts[console_state.alert_detail_index];
    constexpr int kTextX = 18;
    constexpr int kStartY = 44;
    constexpr int kPitch = 28;
    constexpr int kVisibleLines = 8;
    constexpr fonts::FontFace kFont = fonts::FontFace::Font8x12;
    uint8_t logical_line = 0U;
    const char* cursor = alert.detail.data();
    int drawn = 0;
    while (cursor != nullptr && cursor[0] != '\0' && drawn < kVisibleLines)
    {
        const char* eol = std::strchr(cursor, '\n');
        char line[80] = {};
        if (eol == nullptr)
        {
            std::snprintf(line, sizeof(line), "%s", cursor);
        }
        else
        {
            const size_t len = static_cast<size_t>(eol - cursor);
            std::snprintf(line, sizeof(line), "%.*s", static_cast<int>(len), cursor);
        }

        if (logical_line >= console_state.alert_detail_scroll_line)
        {
            framebuffer::draw_text(fb, kTextX, kStartY + (drawn * kPitch), line, true, kFont, 1);
            ++drawn;
        }

        ++logical_line;
        cursor = (eol == nullptr) ? nullptr : (eol + 1);
    }
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
        draw_calendar_page(fb, console_state);
        break;
    case MenuPage::CalendarDetail:
        draw_calendar_detail_page(fb, console_state);
        break;
    case MenuPage::Weather:
        draw_weather_page(fb, console_state);
        break;
    case MenuPage::Shares:
        draw_shares_page(fb, console_state);
        break;
    case MenuPage::ShareDetail:
        draw_share_detail_page(fb, console_state);
        break;
    case MenuPage::Status:
        draw_status_page(fb, console_state);
        break;
    case MenuPage::Settings:
        draw_settings_page(fb, console_state);
        break;
    case MenuPage::DeviceSettings:
        draw_device_settings_page(fb, console_state);
        break;
    case MenuPage::SecuritySettings:
        draw_security_settings_page(fb, console_state);
        break;
    case MenuPage::WifiSettings:
        draw_wifi_settings_page(fb, console_state);
        break;
    case MenuPage::HomeAssistantSettings:
        draw_home_assistant_settings_page(fb, console_state);
        break;
    case MenuPage::MqttSettings:
        draw_mqtt_settings_page(fb, console_state);
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
    case MenuPage::AlertList:
        draw_alert_list_page(fb, console_state);
        break;
    case MenuPage::AlertDetail:
        draw_alert_detail_page(fb, console_state);
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
