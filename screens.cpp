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

namespace screens {

namespace {

const char* letter_mode_text(LetterMode mode)
{
    return (mode == LetterMode::On) ? "ON" : "OFF";
}

const char* alert_severity_text(AlertSeverity severity)
{
    switch (severity) {
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

const char* test_state_text(SystemTestState state)
{
    switch (state) {
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

const char* brightness_text(BrightnessLevel level)
{
    switch (level) {
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

const char* lamp_mode_text(LampMode mode)
{
    switch (mode) {
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

const char* wifi_state_text(WifiConnectionState state)
{
    switch (state) {
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

const char* home_assistant_state_text(HomeAssistantConnectionState state)
{
    switch (state) {
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

const char* mqtt_state_text(MqttConnectionState state)
{
    switch (state) {
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

const char* menu_page_title(MenuPage page)
{
    switch (page) {
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
    }

    return "MENU";
}

int text_width(const char* text,
               fonts::FontFace font = fonts::FontFace::Font5x7,
               int spacing = 1)
{
    if (text == nullptr || text[0] == '\0') {
        return 0;
    }

    return framebuffer::measure_text(text, font, spacing);
}

struct SoftkeyLayout {
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

constexpr int softkey_y_for_index(int index)
{
    return kSoftkeyLayout.top_y + (index * kSoftkeyLayout.pitch);
}

constexpr int softkey_center_y_for_index(int index)
{
    return softkey_y_for_index(index) + (kSoftkeyLayout.height / 2);
}

fonts::FontFace softkey_label_font(MenuPage page)
{
    return (page == MenuPage::Alignment) ? fonts::FontFace::Font8x12 : fonts::FontFace::Font5x7;
}

constexpr int softkey_label_max_width()
{
    return (UI_WIDTH / 2) - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset;
}

constexpr int weather_source_footer_left_x()
{
    return 12;
}

constexpr int weather_source_footer_bottom_y()
{
    return UI_HEIGHT - 18;
}

constexpr int weather_sun_times_y()
{
    return 244;
}

constexpr int weather_source_footer_max_width()
{
    return UI_WIDTH - (weather_source_footer_left_x() * 2);
}

struct WrappedSoftkeyLabel {
    char line_one[48];
    char line_two[48];
    int line_count;
};

void copy_softkey_label_slice(char* dest, size_t dest_size, const char* src, size_t length)
{
    if (dest_size == 0) {
        return;
    }

    const size_t copy_length = (length < (dest_size - 1)) ? length : (dest_size - 1);
    std::memcpy(dest, src, copy_length);
    dest[copy_length] = '\0';
}

size_t fit_wrapped_label_prefix(const char* text, fonts::FontFace font, int max_width)
{
    char candidate[48] = {};
    size_t length = 0;

    while (text[length] != '\0' && text[length] != '\n') {
        copy_softkey_label_slice(candidate, sizeof(candidate), text, length + 1);
        if (text_width(candidate, font) > max_width) {
            break;
        }
        ++length;
    }

    return length;
}

size_t find_wrapped_label_split(const char* text, size_t fit_length)
{
    if (fit_length == 0) {
        return 0;
    }

    if (text[fit_length] == '\n' || text[fit_length] == '\0') {
        return fit_length;
    }

    for (size_t i = fit_length; i > 0; --i) {
        if (text[i - 1] == ' ') {
            return i - 1;
        }
    }

    return fit_length;
}

size_t skip_wrapped_label_breaks(const char* text, size_t start)
{
    while (text[start] == ' ' || text[start] == '\n') {
        ++start;
    }

    return start;
}

WrappedSoftkeyLabel wrap_label_two_lines(const char* label, fonts::FontFace font, int max_width)
{
    WrappedSoftkeyLabel wrapped = {};
    wrapped.line_count = 0;

    if (label == nullptr || label[0] == '\0') {
        return wrapped;
    }

    const size_t first_fit_length = fit_wrapped_label_prefix(label, font, max_width);
    if (first_fit_length == 0) {
        return wrapped;
    }

    size_t first_length = find_wrapped_label_split(label, first_fit_length);
    while (first_length > 0 && label[first_length - 1] == ' ') {
        --first_length;
    }

    copy_softkey_label_slice(wrapped.line_one, sizeof(wrapped.line_one), label, first_length);
    wrapped.line_count = 1;

    size_t second_start = skip_wrapped_label_breaks(label, first_length);
    if (label[second_start] == '\0') {
        return wrapped;
    }

    const size_t second_fit_length = fit_wrapped_label_prefix(label + second_start, font, max_width);
    if (second_fit_length == 0) {
        return wrapped;
    }

    size_t second_length = second_fit_length;
    if (label[second_start + second_fit_length] != '\0' && label[second_start + second_fit_length] != '\n') {
        const size_t second_split = find_wrapped_label_split(label + second_start, second_fit_length);
        if (second_split > 0) {
            second_length = second_split;
        }
    }

    while (second_length > 0 && label[second_start + second_length - 1] == ' ') {
        --second_length;
    }

    copy_softkey_label_slice(wrapped.line_two, sizeof(wrapped.line_two), label + second_start, second_length);
    wrapped.line_count = 2;
    return wrapped;
}

WrappedSoftkeyLabel wrap_softkey_label(const char* label, fonts::FontFace font)
{
    return wrap_label_two_lines(label, font, softkey_label_max_width());
}

bool parse_clock_text_minutes(const char* text, int* out_minutes)
{
    if (text == nullptr || out_minutes == nullptr || std::strlen(text) < 5 || text[2] != ':') {
        return false;
    }

    const char hour_tens = text[0];
    const char hour_ones = text[1];
    const char minute_tens = text[3];
    const char minute_ones = text[4];

    if (hour_tens < '0' || hour_tens > '9' ||
        hour_ones < '0' || hour_ones > '9' ||
        minute_tens < '0' || minute_tens > '9' ||
        minute_ones < '0' || minute_ones > '9') {
        return false;
    }

    const int hours = ((hour_tens - '0') * 10) + (hour_ones - '0');
    const int minutes = ((minute_tens - '0') * 10) + (minute_ones - '0');
    if (hours < 0 || hours > 23 || minutes < 0 || minutes > 59) {
        return false;
    }

    *out_minutes = (hours * 60) + minutes;
    return true;
}

struct ForecastDisplayWindow {
    uint8_t first_index;
    uint8_t count;
};

ForecastDisplayWindow active_forecast_window(const ConsoleState& console_state)
{
    ForecastDisplayWindow window = {0, console_state.home_assistant_status.weather_forecast_count};
    if (!console_state.time_status.synced || window.count == 0) {
        return window;
    }

    int current_minutes = 0;
    if (!parse_clock_text_minutes(console_state.time_status.time_text.data(), &current_minutes)) {
        return window;
    }

    const int current_hour_floor = (current_minutes / 60) * 60;
    const int kDayMinutes = 24 * 60;
    const int kOffsetCandidates[] = {-kDayMinutes, 0, kDayMinutes};
    int previous_forecast_minutes = -100000;
    int day_offset = 0;
    bool have_previous_entry = false;

    while (window.first_index < console_state.home_assistant_status.weather_forecast_count) {
        int raw_forecast_minutes = 0;
        const WeatherForecastEntry& entry = console_state.home_assistant_status.weather_forecast[window.first_index];
        if (!parse_clock_text_minutes(entry.time_text.data(), &raw_forecast_minutes)) {
            break;
        }

        if (!have_previous_entry) {
            int best_distance = 0x7fffffff;
            for (const int candidate_offset : kOffsetCandidates) {
                const int candidate_minutes = raw_forecast_minutes + candidate_offset;
                const int distance = candidate_minutes > current_hour_floor
                                         ? (candidate_minutes - current_hour_floor)
                                         : (current_hour_floor - candidate_minutes);
                if (distance < best_distance) {
                    best_distance = distance;
                    day_offset = candidate_offset;
                }
            }
        }

        int forecast_minutes = raw_forecast_minutes + day_offset;
        while (have_previous_entry && forecast_minutes < previous_forecast_minutes) {
            day_offset += kDayMinutes;
            forecast_minutes = raw_forecast_minutes + day_offset;
        }

        if (forecast_minutes >= current_hour_floor) {
            break;
        }

        previous_forecast_minutes = forecast_minutes;
        have_previous_entry = true;
        ++window.first_index;
    }

    window.count = (window.first_index < console_state.home_assistant_status.weather_forecast_count)
                       ? static_cast<uint8_t>(console_state.home_assistant_status.weather_forecast_count -
                                              window.first_index)
                       : 0;
    return window;
}

void draw_centered_text(uint8_t* fb,
                        int center_x,
                        int y,
                        const char* text,
                        bool on,
                        fonts::FontFace font = fonts::FontFace::Font5x7,
                        int spacing = 1)
{
    framebuffer::draw_text(fb, center_x - (text_width(text, font, spacing) / 2), y, text, on, font, spacing);
}

void draw_softkey_label(uint8_t* fb, int y, const SoftKeyAction& action, bool left_side, fonts::FontFace font)
{
    if (action.label == nullptr || action.label[0] == '\0') {
        return;
    }

    const WrappedSoftkeyLabel wrapped = wrap_softkey_label(action.label, font);
    const int line_height = framebuffer::font_height(font);
    const int block_height = (wrapped.line_count * line_height) + ((wrapped.line_count - 1) * kSoftkeyLayout.line_gap);
    const int block_top_y = y + ((kSoftkeyLayout.height - block_height) / 2);

    auto draw_line = [&](const char* line_text, int line_index) {
        if (line_text == nullptr || line_text[0] == '\0') {
            return;
        }

        const int label_width = text_width(line_text, font);
        const int text_x =
            left_side ? (kSoftkeyLayout.left_x + kSoftkeyLayout.text_inset)
                      : (UI_WIDTH - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset - label_width);
        const int text_y = block_top_y + (line_index * (line_height + kSoftkeyLayout.line_gap));
        framebuffer::draw_text(fb, text_x, text_y, line_text, true, font, 1);
    };

    draw_line(wrapped.line_one, 0);
    if (wrapped.line_count > 1) {
        draw_line(wrapped.line_two, 1);
    }
}

constexpr size_t softkey_map_index(SoftKeyId key)
{
    return static_cast<size_t>(key);
}

const char* weather_source_label_text(const ConsoleState& console_state)
{
    if (console_state.home_assistant_status.weather_source_hint[0] != '\0') {
        return console_state.home_assistant_status.weather_source_hint.data();
    }

    return HOME_ASSISTANT_WEATHER_SOURCE_LABEL[0] ? HOME_ASSISTANT_WEATHER_SOURCE_LABEL : "Weather";
}

bool weather_sun_times_available(const ConsoleState& console_state)
{
    return console_state.home_assistant_status.sunrise_text[0] != '\0' ||
           console_state.home_assistant_status.sunset_text[0] != '\0';
}

void draw_weather_sun_times(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace sun_font = fonts::FontFace::Font5x7;
    char sunrise_label[24] = {};
    char sunset_label[24] = {};

    if (console_state.home_assistant_status.sunrise_text[0] != '\0') {
        std::snprintf(sunrise_label,
                      sizeof(sunrise_label),
                      "SUNRISE %s",
                      console_state.home_assistant_status.sunrise_text.data());
    }

    if (console_state.home_assistant_status.sunset_text[0] != '\0') {
        std::snprintf(sunset_label,
                      sizeof(sunset_label),
                      "SUNSET %s",
                      console_state.home_assistant_status.sunset_text.data());
    }

    if (sunrise_label[0] != '\0' && sunset_label[0] != '\0') {
        framebuffer::draw_hline(fb, 12, UI_WIDTH - 12, weather_sun_times_y() - 10, true);
        framebuffer::draw_text(fb, 12, weather_sun_times_y(), sunrise_label, true, sun_font, 1);
        framebuffer::draw_text(fb,
                               UI_WIDTH - 12 - text_width(sunset_label, sun_font),
                               weather_sun_times_y(),
                               sunset_label,
                               true,
                               sun_font,
                               1);
        return;
    }

    const char* label = sunrise_label[0] != '\0' ? sunrise_label : sunset_label;
    if (label[0] == '\0') {
        return;
    }

    framebuffer::draw_hline(fb, 12, UI_WIDTH - 12, weather_sun_times_y() - 10, true);
    draw_centered_text(fb, UI_WIDTH / 2, weather_sun_times_y(), label, true, sun_font, 1);
}

void draw_softkey_guide(uint8_t* fb, int index, bool left_side)
{
    const int center_y = softkey_center_y_for_index(index);
    const int guide_x = left_side ? 1 : (UI_WIDTH - 2);
    const int fill_x = left_side ? 0 : (UI_WIDTH - 3);

    framebuffer::draw_vline(fb, guide_x, center_y - 6, center_y + 6, true);
    framebuffer::fill_rect(fb, fill_x, center_y - 1, 3, 3, true);
}

void draw_softkey_guides(uint8_t* fb)
{
    for (int i = 0; i < 5; ++i) {
        draw_softkey_guide(fb, i, true);
        draw_softkey_guide(fb, i, false);
    }
}

void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    const fonts::FontFace label_font = softkey_label_font(console_state.active_page);

    for (int i = 0; i < 5; ++i) {
        draw_softkey_label(fb,
                           softkey_y_for_index(i),
                           console_state.softkeys[i],
                           true,
                           label_font);
        draw_softkey_label(fb,
                           softkey_y_for_index(i),
                           console_state.softkeys[i + 5],
                           false,
                           label_font);
    }
}

void draw_panel_frame(uint8_t* fb)
{
    framebuffer::draw_rect(fb, 40, 40, UI_WIDTH - 80, 206, true);
    framebuffer::draw_rect(fb, 46, 46, UI_WIDTH - 92, 194, true);
}

void draw_weather_source_footer(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr fonts::FontFace footer_font = fonts::FontFace::Font5x7;
    constexpr int footer_line_gap = 2;

    const WrappedSoftkeyLabel wrapped =
        wrap_label_two_lines(weather_source_label_text(console_state), footer_font, weather_source_footer_max_width());
    const int line_height = framebuffer::font_height(footer_font);
    const int first_line_y =
        weather_source_footer_bottom_y() - ((wrapped.line_count - 1) * (line_height + footer_line_gap));

    if (wrapped.line_one[0] != '\0') {
        framebuffer::draw_text(fb, weather_source_footer_left_x(), first_line_y, wrapped.line_one, true, footer_font, 1);
    }

    if (wrapped.line_count > 1 && wrapped.line_two[0] != '\0') {
        framebuffer::draw_text(fb,
                               weather_source_footer_left_x(),
                               first_line_y + line_height + footer_line_gap,
                               wrapped.line_two,
                               true,
                               footer_font,
                               1);
    }
}

void draw_home_page_softkey(uint8_t* fb, const ConsoleState& console_state)
{
    const SoftKeyAction& action = console_state.softkeys[softkey_map_index(SoftKeyId::Right5)];
    if (action.label == nullptr || action.label[0] == '\0') {
        return;
    }

    draw_softkey_label(fb,
                       softkey_y_for_index(4),
                       action,
                       false,
                       softkey_label_font(console_state.active_page));
    draw_softkey_guide(fb, 4, false);
}

const char* weather_status_detail(const HomeAssistantStatus& status, char* buffer, size_t buffer_size)
{
    if (buffer == nullptr || buffer_size == 0) {
        return "";
    }

    buffer[0] = '\0';

    if (status.last_http_status > 0) {
        std::snprintf(buffer, buffer_size, "HTTP %d", status.last_http_status);
        return buffer;
    }

    if (status.last_error != 0) {
        std::snprintf(buffer, buffer_size, "NET ERR %d", status.last_error);
        return buffer;
    }

    return home_assistant_state_text(status.state);
}

void draw_home_page(uint8_t* fb, const ConsoleState& console_state)
{
    char status_detail[24] = {};
    const bool weather_configured = console_state.home_assistant_status.weather_entity_id[0] != '\0';
    const ForecastDisplayWindow forecast_window = active_forecast_window(console_state);
    const char* weather_condition = "WEATHER OFF";
    const char* weather_temperature = "";
    const char* weather_footer = "";

    if (weather_configured) {
        weather_condition = console_state.home_assistant_status.weather_condition[0]
                                ? console_state.home_assistant_status.weather_condition.data()
                                : (console_state.home_assistant_status.state == HomeAssistantConnectionState::Connected
                                       ? "NO DATA"
                                       : weather_status_detail(console_state.home_assistant_status,
                                                               status_detail,
                                                               sizeof(status_detail)));
        weather_temperature = console_state.home_assistant_status.weather_temperature.data();

        if (console_state.home_assistant_status.state == HomeAssistantConnectionState::Connected) {
            weather_footer = "NO HOURLY FORECAST";
        } else if (console_state.home_assistant_status.state == HomeAssistantConnectionState::Resolving ||
                   console_state.home_assistant_status.state == HomeAssistantConnectionState::Connecting ||
                   console_state.home_assistant_status.state == HomeAssistantConnectionState::Authorizing ||
                   console_state.home_assistant_status.state == HomeAssistantConnectionState::WaitingForWifi) {
            weather_footer = "WAITING FOR WEATHER";
        } else {
            weather_footer = weather_status_detail(console_state.home_assistant_status,
                                                   status_detail,
                                                   sizeof(status_detail));
        }
    }

    if (forecast_window.count > 0) {
        framebuffer::draw_text(fb, 12, 36, "TIME", true, 1, 1);
        framebuffer::draw_text(fb, 60, 36, "TEMP", true, 1, 1);
        framebuffer::draw_text(fb, 102, 36, "WIND mph", true, 1, 1);
        framebuffer::draw_text(fb, 160, 36, "CONDITIONS", true, 1, 1);
        framebuffer::draw_hline(fb, 12, UI_WIDTH - 12, 46, true);

        for (uint8_t i = 0; i < forecast_window.count; ++i) {
            const WeatherForecastEntry& entry =
                console_state.home_assistant_status.weather_forecast[forecast_window.first_index + i];
            const int row_y = 54 + (static_cast<int>(i) * 18);
            framebuffer::draw_text(fb, 12, row_y, entry.time_text.data(), true, fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 60, row_y, entry.temperature_text.data(), true, fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 102, row_y, entry.wind_text.data(), true, fonts::FontFace::Font8x12, 1);
            framebuffer::draw_text(fb, 160, row_y + 3, entry.condition_text.data(), true, 1, 1);
        }
        if (weather_configured && weather_sun_times_available(console_state)) {
            draw_weather_sun_times(fb, console_state);
        }
        if (weather_configured) {
            draw_weather_source_footer(fb, console_state);
        }
        return;
    }

    draw_centered_text(fb, UI_WIDTH / 2, 92, weather_condition, true, fonts::FontFace::Font8x12, 1);
    if (weather_temperature[0] != '\0') {
        draw_centered_text(fb, UI_WIDTH / 2, 120, weather_temperature, true, fonts::FontFace::Font8x14, 1);
    }

    if (weather_configured && weather_footer[0] != '\0') {
        draw_centered_text(fb, UI_WIDTH / 2, 162, weather_footer, true, fonts::FontFace::Font5x7, 1);
    }

    if (weather_configured) {
        draw_weather_source_footer(fb, console_state);
    }
}

void draw_weather_sources_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "SOURCES", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 84, "CURRENT SOURCE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 100, weather_source_label_text(console_state), true, 1, 1);
    framebuffer::draw_text(fb,
                           54,
                           116,
                           console_state.home_assistant_status.weather_entity_id[0]
                               ? console_state.home_assistant_status.weather_entity_id.data()
                               : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_hline(fb, 54, UI_WIDTH - 54, 136, true);
    framebuffer::draw_text(fb, 54, 152, "SOURCE SLOT 2", true, 1, 1);
    framebuffer::draw_text(fb, 144, 152, "RESERVED", true, 1, 1);
    framebuffer::draw_text(fb, 54, 168, "SOURCE SLOT 3", true, 1, 1);
    framebuffer::draw_text(fb, 144, 168, "RESERVED", true, 1, 1);

    framebuffer::draw_text(fb, 54, 204, "Selection UI placeholder", true, 1, 1);
    framebuffer::draw_text(fb, 54, 220, "source switching not yet wired", true, 1, 1);
}

void draw_status_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "STATUS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 84, "TIME", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           84,
                           console_state.time_status.synced ? console_state.time_status.time_text.data() : "--:--",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 100, "WIFI", true, 1, 1);
    framebuffer::draw_text(fb, 116, 100, wifi_state_text(console_state.wifi_status.state), true, 1, 1);

    framebuffer::draw_text(fb, 54, 116, "SSID", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           116,
                           console_state.wifi_status.credentials_present ? console_state.wifi_status.ssid.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 132, "IP", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           132,
                           console_state.wifi_status.ip_address[0] ? console_state.wifi_status.ip_address.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 148, "HA", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           148,
                           home_assistant_state_text(console_state.home_assistant_status.state),
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 164, "HOST", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           164,
                           console_state.home_assistant_status.host[0] ? console_state.home_assistant_status.host.data() : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 180, "HTTP", true, 1, 1);
    if (console_state.home_assistant_status.last_http_status > 0) {
        char http_text[8] = {};
        std::snprintf(http_text, sizeof(http_text), "%d", console_state.home_assistant_status.last_http_status);
        framebuffer::draw_text(fb, 116, 180, http_text, true, 1, 1);
    } else {
        framebuffer::draw_text(fb, 116, 180, "-", true, 1, 1);
    }

    framebuffer::draw_text(fb, 54, 196, "ENT", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           196,
                           console_state.home_assistant_status.tracked_entity_state[0]
                               ? console_state.home_assistant_status.tracked_entity_state.data()
                               : "-",
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 212, "MQTT", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           212,
                           mqtt_state_text(console_state.mqtt_status.state),
                           true,
                           1,
                           1);

    framebuffer::draw_text(fb, 54, 228, "DISC", true, 1, 1);
    framebuffer::draw_text(fb,
                           116,
                           228,
                           console_state.mqtt_status.discovery_published ? "READY" : "-",
                           true,
                           1,
                           1);
}

void draw_settings_page(uint8_t* fb, const ConsoleState& console_state)
{
    draw_panel_frame(fb);
    draw_centered_text(fb, UI_WIDTH / 2, 56, "SETTINGS", true, fonts::FontFace::FontTitle8x12, 1);

    framebuffer::draw_text(fb, 54, 86, "RIGHT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 102, "ALERT  LTRS  TEST", true, 1, 1);
    framebuffer::draw_text(fb, 54, 118, "PANEL + PANEL -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 146, "LEFT SIDE", true, 1, 1);
    framebuffer::draw_text(fb, 54, 162, "HOME  STATUS  RESET", true, 1, 1);
    framebuffer::draw_text(fb, 54, 178, "KEYS + KEYS -", true, 1, 1);

    framebuffer::draw_text(fb, 54, 206, "LTRS", true, 1, 1);
    framebuffer::draw_text(fb, 116, 206, letter_mode_text(console_state.letter_mode), true, 1, 1);

    framebuffer::draw_text(fb, 54, 222, "ALERT", true, 1, 1);
    framebuffer::draw_text(fb, 116, 222, alert_severity_text(console_state.alert_severity), true, 1, 1);
}

void draw_alignment_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

}  // namespace

/// @brief Draws a simple geometry and fill-pattern test screen.
void draw_demo_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);
    framebuffer::draw_rect(fb, 10, 10, UI_WIDTH - 20, UI_HEIGHT - 20, true);

    framebuffer::fill_rect(fb, 20, 20, 60, 40, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 80, 30, 40, 70, true);

    framebuffer::draw_diag(fb, true);

    for (int i = 0; i < 10; ++i) {
        framebuffer::fill_rect(fb, 5, 20 + i * 28, 6, 12, true);
        framebuffer::fill_rect(fb, UI_WIDTH - 11, 20 + i * 28, 6, 12, true);
    }

    framebuffer::fill_rect(fb, 0, UI_HEIGHT - 16, UI_WIDTH, 16, true);
    framebuffer::fill_rect(fb, 8, UI_HEIGHT - 12, 100, 8, false);
}

/// @brief Draws the active menu page and contextual softkey labels.
void draw_menu_screen(uint8_t* fb, const ConsoleState& console_state)
{
    framebuffer::clear(fb, false);

    screen_banners::draw_standard_banners(fb, console_state, menu_page_title(console_state.active_page));
    if (console_state.active_page != MenuPage::Home) {
        draw_softkeys(fb, console_state);
    }

    switch (console_state.active_page) {
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
    }

    if (console_state.active_page == MenuPage::Home) {
        draw_home_page_softkey(fb, console_state);
    } else {
        draw_softkey_guides(fb);
    }
}

/// @brief Draws a static calibration screen for alignment and extent testing.
void draw_calibration_screen(uint8_t* fb)
{
    framebuffer::clear(fb, false);

    const int mid_x = UI_WIDTH / 2;
    const int mid_y = UI_HEIGHT / 2;
    const int q1_x = UI_WIDTH / 4;
    const int q3_x = (UI_WIDTH * 3) / 4;
    const int q1_y = UI_HEIGHT / 4;
    const int q3_y = (UI_HEIGHT * 3) / 4;

    // Full outer border of the logical UI.
    framebuffer::draw_rect(fb, 0, 0, UI_WIDTH, UI_HEIGHT, true);

    // Inner border to make clipping easier to see in photos.
    framebuffer::draw_rect(fb, 4, 4, UI_WIDTH - 8, UI_HEIGHT - 8, true);

    // Corner markers.
    framebuffer::fill_rect(fb, 0, 0, 8, 8, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 8, 0, 8, 8, true);
    framebuffer::fill_rect(fb, 0, UI_HEIGHT - 8, 8, 8, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 8, UI_HEIGHT - 8, 8, 8, true);

    // Center cross.
    framebuffer::draw_vline(fb, mid_x, 0, UI_HEIGHT - 1, true);
    framebuffer::draw_hline(fb, 0, UI_WIDTH - 1, mid_y, true);

    // Quarter lines.
    framebuffer::draw_vline(fb, q1_x, 16, UI_HEIGHT - 17, true);
    framebuffer::draw_vline(fb, q3_x, 16, UI_HEIGHT - 17, true);
    framebuffer::draw_hline(fb, 16, UI_WIDTH - 17, q1_y, true);
    framebuffer::draw_hline(fb, 16, UI_WIDTH - 17, q3_y, true);

    // Small edge ticks every 32 logical pixels.
    for (int x = 0; x < UI_WIDTH; x += 32) {
        framebuffer::draw_vline(fb, x, 0, 5, true);
        framebuffer::draw_vline(fb, x, UI_HEIGHT - 6, UI_HEIGHT - 1, true);
    }

    for (int y = 0; y < UI_HEIGHT; y += 32) {
        framebuffer::draw_hline(fb, 0, 5, y, true);
        framebuffer::draw_hline(fb, UI_WIDTH - 6, UI_WIDTH - 1, y, true);
    }

    // Central box.
    framebuffer::draw_rect(fb, mid_x - 30, mid_y - 20, 60, 40, true);

    // Top and bottom labels for orientation.
    framebuffer::draw_text(fb, 12, 12, "TOP", true, 1, 1);
    framebuffer::draw_text(fb, UI_WIDTH - 34, 12, "R", true, 2, 1);
    framebuffer::draw_text(fb, 12, UI_HEIGHT - 20, "BOTTOM", true, 1, 1);

    // A few filled blocks for checking edge visibility and stability.
    framebuffer::fill_rect(fb, 20, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, UI_WIDTH - 32, mid_y - 10, 12, 20, true);
    framebuffer::fill_rect(fb, mid_x - 10, 24, 20, 12, true);
    framebuffer::fill_rect(fb, mid_x - 10, UI_HEIGHT - 36, 20, 12, true);
}

}  // namespace screens
