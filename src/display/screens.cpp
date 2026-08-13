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
#include "panel_config.h"
#include "pinter_screens.h"
#include "screen_banners.h"
#include "screens_shared.h"
#include "settings_screens.h"
#include "shares_screens.h"
#include "status_screens.h"
#include "weather_screens.h"

namespace screens
{

namespace
{

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
/// split-out page families) can call it without duplicating the switch.
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

/// @brief Formats a fixed-point centi-Celsius temperature for the Status page.
/// @details Declared in screens_shared.h -- shared between the Status Sensors
/// page and the Local Conditions history graph.
void build_environment_temperature_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
    size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    const int64_t raw_value = status.bme_temperature_centi_celsius;
    const bool negative = raw_value < 0;
    const uint32_t absolute_value =
        static_cast<uint32_t>(negative ? -raw_value : raw_value);
    std::snprintf(out, out_size, "%s%lu.%02luC", negative ? "-" : "",
                  static_cast<unsigned long>(absolute_value / 100U),
                  static_cast<unsigned long>(absolute_value % 100U));
}

/// @brief Formats BME280 humidity as percent relative humidity.
void build_environment_humidity_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
    size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    const uint32_t tenths_percent = (status.bme_humidity_milli_percent + 50U) / 100U;
    std::snprintf(out, out_size, "%lu.%lu%%",
                  static_cast<unsigned long>(tenths_percent / 10U),
                  static_cast<unsigned long>(tenths_percent % 10U));
}

/// @brief Formats BME280 pressure as hPa, which is easier to scan than raw Pa.
void build_environment_pressure_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
    size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    if (!status.enabled || !status.bme_reading_valid)
    {
        std::snprintf(out, out_size, "-");
        return;
    }

    const uint32_t tenths_hpa = (status.bme_pressure_pa + 5U) / 10U;
    std::snprintf(out, out_size, "%lu.%luhPa",
                  static_cast<unsigned long>(tenths_hpa / 10U),
                  static_cast<unsigned long>(tenths_hpa % 10U));
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

/// @brief Returns the display label for one local condition metric.
const char* local_condition_metric_text(LocalConditionMetric metric)
{
    switch (metric)
    {
    case LocalConditionMetric::Temperature:
        return "TEMP";
    case LocalConditionMetric::Humidity:
        return "HUMIDITY";
    case LocalConditionMetric::AirPressure:
        return "AIR PRESSURE";
    case LocalConditionMetric::AirQuality:
        return "VOC CHANGE";
    }

    return "LOCAL";
}

struct LocalConditionGraphScale
{
    int32_t minimum;
    int32_t middle;
    int32_t maximum;
    const char* minimum_label;
    const char* middle_label;
    const char* maximum_label;
    const char* unit_label;
};

/// @brief Returns the fixed operator-facing graph range for one local metric.
/// @details Fixed ranges make short histories readable and comparable. Pressure
/// is displayed as local station pressure, not corrected sea-level pressure.
LocalConditionGraphScale local_condition_graph_scale(LocalConditionMetric metric)
{
    switch (metric)
    {
    case LocalConditionMetric::Temperature:
        return {-1000, 1500, 4000, "-10C", "15C", "40C", "C"};
    case LocalConditionMetric::Humidity:
        return {0, 50000, 100000, "0%", "50%", "100%", "%RH"};
    case LocalConditionMetric::AirPressure:
        return {95000, 100000, 105000, "950", "1000", "1050", "hPa"};
    case LocalConditionMetric::AirQuality:
        return {0, 50, 100, "0", "50", "100", "VOC score"};
    }

    return {0, 50, 100, "0", "50", "100", ""};
}

/// @brief Draws a graph axis label right-aligned against the plot edge.
void draw_axis_label(uint8_t* fb, int right_x, int y, const char* text)
{
    constexpr fonts::FontFace kAxisFont = fonts::FontFace::Font5x7;
    framebuffer::draw_text(fb, right_x - framebuffer::measure_text(text, kAxisFont, 1), y, text,
                           true, kAxisFont, 1);
}

/// @brief Formats one local-condition value according to its stored fixed-point unit.
void build_local_condition_value_text(
    LocalConditionMetric metric,
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
    size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }

    switch (metric)
    {
    case LocalConditionMetric::Temperature:
        build_environment_temperature_text(status, out, out_size);
        return;
    case LocalConditionMetric::Humidity:
        build_environment_humidity_text(status, out, out_size);
        return;
    case LocalConditionMetric::AirPressure:
        build_environment_pressure_text(status, out, out_size);
        return;
    case LocalConditionMetric::AirQuality:
        if (!status.enabled || !status.air_quality_score_valid)
        {
            // PICO_ERROR_NONE is stably 0 across the Pico SDK; named locally
            // rather than including pico/error.h so this file has no Pico
            // SDK dependency (issue #71).
            constexpr int kNoErrorCode = 0;
            std::snprintf(out, out_size, "%s",
                          status.air_quality_read_error == kNoErrorCode ? "-" : "ERR");
            return;
        }
        std::snprintf(out, out_size, "%s",
                      environment_sensor_manager::air_quality_band_text(status.air_quality_score));
        return;
    }

    std::snprintf(out, out_size, "-");
}

/// @brief Copies the selected 24-hour five-minute average history into a work buffer.
std::size_t copy_local_condition_history(
    LocalConditionMetric metric,
    const environment_sensor_manager::EnvironmentSensorStatus& status,
    std::array<int32_t, environment_sensor_manager::kLocalConditionHistoryPointCount>& out)
{
    out.fill(0);

    switch (metric)
    {
    case LocalConditionMetric::Temperature:
        for (std::size_t i = 0; i < status.bme_history_count; ++i)
        {
            out[i] = status.bme_temperature_history_centi_celsius[i];
        }
        return status.bme_history_count;
    case LocalConditionMetric::Humidity:
        for (std::size_t i = 0; i < status.bme_history_count; ++i)
        {
            out[i] = static_cast<int32_t>(status.bme_humidity_history_centi_percent[i]) * 10;
        }
        return status.bme_history_count;
    case LocalConditionMetric::AirPressure:
        for (std::size_t i = 0; i < status.bme_history_count; ++i)
        {
            out[i] = static_cast<int32_t>(status.bme_pressure_history_deci_hpa[i]) * 10;
        }
        return status.bme_history_count;
    case LocalConditionMetric::AirQuality:
        for (std::size_t i = 0; i < status.air_quality_history_count; ++i)
        {
            out[i] = static_cast<int32_t>(status.air_quality_history_score[i]);
        }
        return status.air_quality_history_count;
    }

    return 0U;
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
    case MenuPage::WeatherSources:
        return "WEATHER SOURCE";
    case MenuPage::TimeZoneSettings:
        return "TIME ZONE";
    case MenuPage::Alignment:
        return "ALIGN";
    case MenuPage::KeypadDebug:
        return "KEYPAD DEBUG";
    case MenuPage::GreyscaleTest:
        return "GREY TEST";
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
    if (console_state.active_page == MenuPage::AirTraffic &&
        console_state.air_traffic_view_mode == AirTrafficViewMode::Tabular)
    {
        constexpr uint8_t kRowsPerPage = kAirTrafficRowsPerPage;
        const uint8_t count = console_state.air_traffic_status.aircraft_count;
        const uint8_t page_count = static_cast<uint8_t>(
            (count == 0U) ? 1U : ((count + (kRowsPerPage - 1U)) / kRowsPerPage));
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

} // namespace

/// @brief Measures text through one shared helper so layout math stays consistent.
/// @details Declared in screens_shared.h -- used broadly across split-out page
/// families for label centring/width measurement.
int text_width(const char* text, fonts::FontFace font, int spacing)
{
    if (text == nullptr || text[0] == '\0')
    {
        return 0;
    }

    return framebuffer::measure_text(text, font, spacing);
}

/// @brief Draws a graph plot area's border rect.
/// @details Declared in screens_shared.h -- shared by the Shares history
/// graph (shares_screens.cpp) and the Local Conditions history graph (still
/// in this file).
void draw_graph_plot_border(uint8_t* fb, const GraphPlotArea& area)
{
    framebuffer::draw_rect(fb, area.x, area.y, area.width, area.height, true);
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
    case MenuPage::LocalConditions:
    case MenuPage::LocalConditionGraph:
    case MenuPage::DeviceSettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::AirTrafficSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
    case MenuPage::Pinter:
    case MenuPage::PinterSelectBrew:
    case MenuPage::PinterStartTiming:
    case MenuPage::Shares:
    case MenuPage::ShareDetail:
        return fonts::FontFace::Font5x7;
    case MenuPage::Weather:
    case MenuPage::AirTraffic:
    case MenuPage::Status:
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
    case MenuPage::Alignment:
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
    case MenuPage::AlertList:
    case MenuPage::AlertDetail:
        return fonts::FontFace::Font8x12;
    case MenuPage::ScreenSaverSettings:
        return fonts::FontFace::Font5x7;
    }

    return fonts::FontFace::Font5x7;
}

/// @brief Returns how many lines one softkey label may wrap onto for a page.
/// @details Most pages cap this at two lines so long labels degrade
/// gracefully; the Pinter page opts into a third line so each vessel's slot
/// key can carry a stage countdown alongside its state and recipe name.
constexpr int softkey_label_max_lines(MenuPage page)
{
    return page == MenuPage::Pinter ? 3 : 2;
}

/// @brief Returns the maximum drawable width for one softkey label line.
constexpr int softkey_label_max_width()
{
    return (kUiWidth / 2) - kSoftkeyLayout.left_x - kSoftkeyLayout.text_inset;
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

} // namespace

/// @brief Wraps one softkey label into at most `max_lines` renderable lines.
/// @details Softkeys are deliberately capped (two lines for most pages, three
/// where a page opts in) so long labels degrade gracefully without taking
/// over the rest of the screen layout. Each line greedily fits as much text
/// as the width allows, preferring to break on whitespace. Declared in
/// screens_shared.h -- split-out page families (weather footer wrapping, etc.)
/// need it too.
WrappedSoftkeyLabel wrap_label_lines(const char* label, fonts::FontFace font, int max_width,
                                     int max_lines)
{
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

/// @brief Applies the current softkey width and line-count policy for a page
/// to one label string.
/// @details This keeps callers from hard-coding layout limits in multiple places
/// when the bezel or font rules change.
WrappedSoftkeyLabel wrap_softkey_label(const char* label, fonts::FontFace font, int max_lines)
{
    return wrap_label_lines(label, font, softkey_label_max_width(), max_lines);
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

} // namespace

/// @brief Draws the standard row-based presentation used by information pages.
/// @details The top banner carries the page title, so the body can stay clean
/// and consistent without extra boxes, subtitles, or divider lines. Declared
/// in screens_shared.h -- used by split-out page families too (Weather, etc.).
void draw_info_page_rows(uint8_t* fb, const DetailRow* rows, size_t count)
{
    constexpr int kInfoPageStartY = 42;
    constexpr int kInfoPageRowPitch = 18;
    draw_detail_rows(fb, rows, count, kInfoPageStartY, kInfoPageRowPitch, false);
}

/// @brief Draws shared left/right page-navigation arrows used by paged menus.
/// @details One shared helper keeps all paged screens visually consistent and
/// allows global position tweaks from a single location. Declared in
/// screens_shared.h -- used by split-out page families too (Air Traffic).
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

namespace
{

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

} // namespace

/// @brief Draws one oversized square bracket pair around a softkey value.
/// @details The brackets are rendered as simple line primitives so they stay
/// readable even when the underlying bitmap font has tiny punctuation glyphs.
/// Declared in screens_shared.h -- used both by the core softkey label
/// renderer below and by Settings' screen-saver timeout scratchpad
/// (settings_screens.cpp).
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

namespace
{

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

} // namespace

/// @brief Draws text centred around a given x-coordinate.
/// @details Centralizing the centering math keeps titles and status callouts
/// aligned consistently across the different page renderers. Declared in
/// screens_shared.h -- used by split-out page families too.
void draw_centered_text(uint8_t* fb, int center_x, int y, const char* text, bool on,
                        fonts::FontFace font, int spacing)
{
    framebuffer::draw_text(fb, center_x - (text_width(text, font, spacing) / 2), y, text, on, font,
                           spacing);
}

namespace
{

/// @brief Draws one mirrored softkey label block.
/// @details The label is vertically centred within the physical key slot so
/// wrapped text still reads like it belongs to one button location.
void draw_softkey_label(uint8_t* fb, int y, const SoftKeyAction& action, bool left_side,
                        fonts::FontFace font, int max_lines)
{
    if (action.label == nullptr || action.label[0] == '\0')
    {
        return;
    }

    const WrappedSoftkeyLabel kWrapped = wrap_softkey_label(action.label, font, max_lines);
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
    if (kWrapped.line_count > 2)
    {
        draw_line(kWrapped.line_three, 2);
    }
}

/// @brief Draws every softkey label for the current menu page.
/// @details Labels are rendered separately from the page body so most pages can
/// share the same bezel framing while only the content region changes.
void draw_softkeys(uint8_t* fb, const ConsoleState& console_state)
{
    const fonts::FontFace kLabelFont = softkey_label_font(console_state.active_page);
    const int kMaxLines = softkey_label_max_lines(console_state.active_page);

    for (int i = 0; i < 5; ++i)
    {
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i], true, kLabelFont,
                           kMaxLines);
        draw_softkey_label(fb, softkey_y_for_index(i), console_state.softkeys[i + 5], false,
                           kLabelFont, kMaxLines);
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

constexpr int kCompactDetailTextX = 10;
constexpr fonts::FontFace kCompactDetailFont = fonts::FontFace::Font5x7;

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
            std::max(widest_label_width, text_width(label_text, kCompactDetailFont, 1));
    }

    return kCompactDetailTextX + widest_label_width + text_width(" ", kCompactDetailFont, 1);
}

/// @brief Draws one compact detail line with pixel-aligned values.
void draw_compact_detail_line(uint8_t* fb, int y, const char* label, const char* value, int value_x)
{
    constexpr size_t kLabelBufferSize = 32U;
    char label_text[kLabelBufferSize] = {};
    std::snprintf(label_text, sizeof(label_text), "%s:", label != nullptr ? label : "");

    framebuffer::draw_text(fb, kCompactDetailTextX, y, label_text, true, kCompactDetailFont, 1);
    framebuffer::draw_text(fb, value_x, y, (value != nullptr && value[0] != '\0') ? value : "-",
                           true, kCompactDetailFont, 1);
}

} // namespace

/// @brief Draws compact `Label: value` rows for data-dense pages.
/// @details Declared in screens_shared.h -- used by every split-out page
/// family that renders dense key/value diagnostics (Status, Sensors, etc.).
void draw_compact_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y,
                              int row_pitch)
{
    const int value_x = compact_detail_value_x(rows, count);
    for (size_t i = 0; i < count; ++i)
    {
        draw_compact_detail_line(fb, start_y + (static_cast<int>(i) * row_pitch), rows[i].label,
                                 rows[i].value, value_x);
    }
}

namespace
{

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

/// @brief Draws the local environment summary page.
void draw_local_conditions_page(uint8_t* fb, const ConsoleState& console_state)
{
    (void)fb;
    (void)console_state;
}

/// @brief Draws one local-condition history graph using the compact status history.
void draw_local_condition_history_graph(uint8_t* fb, const ConsoleState& console_state)
{
    constexpr int kGraphX = 42;
    constexpr int kGraphY = 52;
    constexpr int kGraphWidth = kUiWidth - (kGraphX * 2);
    constexpr int kGraphHeight = 128;
    constexpr int kTimelineLabelY = kGraphY + kGraphHeight + 5;
    constexpr int kMetricLabelY = kTimelineLabelY + 13;
    constexpr int kPlotInset = 1;
    constexpr int kAxisLabelRightX = kGraphX - 6;

    std::array<int32_t, environment_sensor_manager::kLocalConditionHistoryPointCount> values = {};
    const std::size_t count = copy_local_condition_history(
        console_state.local_condition_metric, console_state.environment_sensor_status, values);
    const LocalConditionGraphScale scale =
        local_condition_graph_scale(console_state.local_condition_metric);
    if (count < 2U)
    {
        const char* metric_label = local_condition_metric_text(console_state.local_condition_metric);
        char current_text[20] = {};
        build_local_condition_value_text(console_state.local_condition_metric,
                                         console_state.environment_sensor_status, current_text,
                                         sizeof(current_text));
        draw_centered_text(fb, kUiWidth / 2, 106, metric_label, true, fonts::FontFace::Font8x12,
                           1);
        draw_centered_text(fb, kUiWidth / 2, 136,
                           current_text[0] != '\0' && std::strcmp(current_text, "-") != 0
                               ? current_text
                               : "NO DATA",
                           true, fonts::FontFace::Font8x12, 1);
        return;
    }

    const GraphPlotArea plot_area{kGraphX, kGraphY, kGraphWidth, kGraphHeight, kPlotInset};
    draw_graph_plot_border(fb, plot_area);
    draw_axis_label(fb, kAxisLabelRightX, kGraphY - 3, scale.maximum_label);
    draw_axis_label(fb, kAxisLabelRightX, kGraphY + (kGraphHeight / 2) - 3,
                    scale.middle_label);
    draw_axis_label(fb, kAxisLabelRightX, kGraphY + kGraphHeight - 8, scale.minimum_label);
    framebuffer::draw_hline(fb, kGraphX - 3, kGraphX - 1, kGraphY + (kGraphHeight / 2), true);

    int previous_x = kGraphX + kPlotInset;
    int previous_y = kGraphY + kGraphHeight - 1 - kPlotInset;
    for (std::size_t i = 0U; i < count; ++i)
    {
        const int x = graph_plot_x(plot_area, static_cast<int>(i), static_cast<int>(count));
        const int y = graph_plot_y(plot_area, values[i], scale.minimum, scale.maximum);
        if (i > 0U)
        {
            framebuffer::draw_line(fb, previous_x, previous_y, x, y, true);
        }
        previous_x = x;
        previous_y = y;
    }

    char current_text[20] = {};
    build_local_condition_value_text(console_state.local_condition_metric,
                                     console_state.environment_sensor_status, current_text,
                                     sizeof(current_text));

    framebuffer::draw_text(fb, kGraphX, kTimelineLabelY, "24H AGO", true,
                           fonts::FontFace::Font5x7, 1);
    const int now_width = text_width("NOW", fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, kGraphX + kGraphWidth - now_width, kTimelineLabelY, "NOW", true,
                           fonts::FontFace::Font5x7, 1);

    framebuffer::draw_text(fb, kGraphX, kMetricLabelY,
                           local_condition_metric_text(console_state.local_condition_metric), true,
                           fonts::FontFace::Font5x7, 1);
    const int unit_x =
        kGraphX + text_width(local_condition_metric_text(console_state.local_condition_metric),
                             fonts::FontFace::Font5x7, 1) +
        text_width(" ", fonts::FontFace::Font5x7, 1);
    framebuffer::draw_text(fb, unit_x, kMetricLabelY, scale.unit_label, true,
                           fonts::FontFace::Font5x7, 1);
    const int current_width = text_width(current_text, fonts::FontFace::Font8x12, 1);
    framebuffer::draw_text(fb, kGraphX + kGraphWidth - current_width, kMetricLabelY - 3,
                           current_text, true, fonts::FontFace::Font8x12, 1);
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
    case MenuPage::Status:
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        status_screens::draw_status_page(fb, console_state, now_ms);
        break;
    case MenuPage::LocalConditions:
        draw_local_conditions_page(fb, console_state);
        break;
    case MenuPage::LocalConditionGraph:
        draw_local_condition_history_graph(fb, console_state);
        break;
    case MenuPage::Settings:
        settings_screens::draw_settings_page(fb, console_state);
        break;
    case MenuPage::DeviceSettings:
        settings_screens::draw_device_settings_page(fb, console_state);
        break;
    case MenuPage::WifiSettings:
        settings_screens::draw_wifi_settings_page(fb, console_state);
        break;
    case MenuPage::HomeAssistantSettings:
        settings_screens::draw_home_assistant_settings_page(fb, console_state);
        break;
    case MenuPage::MqttSettings:
        settings_screens::draw_mqtt_settings_page(fb, console_state);
        break;
    case MenuPage::AirTrafficSettings:
        settings_screens::draw_air_traffic_settings_page(fb, console_state);
        break;
    case MenuPage::ScreenSaverSettings:
        settings_screens::draw_screen_saver_page(fb, console_state);
        break;
    case MenuPage::WeatherSources:
        settings_screens::draw_weather_sources_page(fb, console_state);
        break;
    case MenuPage::TimeZoneSettings:
        settings_screens::draw_time_zone_page(fb, console_state);
        break;
    case MenuPage::Alignment:
        settings_screens::draw_alignment_page(fb, console_state);
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
