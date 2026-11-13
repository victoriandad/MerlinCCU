#include "local_conditions_screens.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "console_model.h"
#include "environment_sensor_manager.h"
#include "framebuffer.h"
#include "local_conditions.h"
#include "panel_config.h"
#include "screens_shared.h"
#include "ui_layout.h"

namespace local_conditions_screens
{

namespace
{

using screens::TextStyle;

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

/// @brief Copies the selected 24-hour five-minute average history into a work buffer.
std::size_t copy_local_condition_history(
    LocalConditionMetric metric, const environment_sensor_manager::EnvironmentSensorStatus& status,
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

/// @brief Draws the sensor board's freshness in the footer (STATE-3).
/// @details The stale age comes from environment_sensor_manager (STATE-4).
/// Drawn only once the board has produced something, so the empty states
/// carry the whole message.
void draw_local_conditions_footer(uint8_t* fb,
                                  const environment_sensor_manager::EnvironmentSensorStatus& status,
                                  uint32_t now_ms)
{
    char freshness_text[16] = {};
    screens::build_data_freshness_text(local_conditions::reading_current(status),
                                       local_conditions::last_reading_ms(status), now_ms,
                                       environment_sensor_manager::kLocalConditionStaleAfterMs,
                                       freshness_text, sizeof(freshness_text));
    char footer_text[32] = {};
    std::snprintf(footer_text, sizeof(footer_text), "Sensor board - %s", freshness_text);
    screens::draw_text(fb, ui_layout::kMarginX, ui_layout::kFooterLineY, footer_text,
                       TextStyle::Footer);
}

} // namespace

/// @brief Draws the Local Conditions body.
/// @details With readings, the values are on the softkeys and the body holds
/// only the footer freshness line. Without, the shared empty state says why
/// and where to look, and the keys are blank (SK-5).
void draw_local_conditions_page(uint8_t* fb, const ConsoleState& console_state, uint32_t now_ms)
{
    const auto& status = console_state.environment_sensor_status;
    const local_conditions::BoardState kState = local_conditions::board_state(status);
    if (kState != local_conditions::BoardState::Reading)
    {
        screens::draw_empty_state(fb, local_conditions::empty_state_headline(kState),
                                  local_conditions::empty_state_hint(kState));
        return;
    }
    draw_local_conditions_footer(fb, status, now_ms);
}

/// @brief Draws one local-condition history graph using the compact status history.
/// @details With fewer than two history points there is nothing to plot, so
/// the page shows the shared empty state: the board's own state when it has
/// no readings, otherwise that history is still building, with the current
/// reading as the hint.
void draw_local_condition_history_graph(uint8_t* fb, const ConsoleState& console_state,
                                        uint32_t now_ms)
{
    constexpr int kGraphHeight = 128;

    const auto& status = console_state.environment_sensor_status;
    std::array<int32_t, environment_sensor_manager::kLocalConditionHistoryPointCount> values = {};
    const std::size_t count =
        copy_local_condition_history(console_state.local_condition_metric, status, values);
    const LocalConditionGraphScale scale =
        local_condition_graph_scale(console_state.local_condition_metric);
    if (count < 2U)
    {
        const local_conditions::BoardState kState = local_conditions::board_state(status);
        if (kState != local_conditions::BoardState::Reading)
        {
            screens::draw_empty_state(fb, local_conditions::empty_state_headline(kState),
                                      local_conditions::empty_state_hint(kState));
            return;
        }

        char current_text[20] = {};
        local_conditions::format_metric(console_state.local_condition_metric, status, current_text,
                                        sizeof(current_text));
        char hint[32] = {};
        if (std::strcmp(current_text, "-") != 0)
        {
            std::snprintf(hint, sizeof(hint), "Now: %s", current_text);
        }
        else
        {
            std::snprintf(hint, sizeof(hint), "A point is added every 5 min");
        }
        screens::draw_empty_state(fb, "NO HISTORY YET", hint);
        draw_local_conditions_footer(fb, status, now_ms);
        return;
    }

    const screens::GraphPlotArea plot_area = screens::draw_history_graph_frame(
        fb, kGraphHeight,
        {scale.maximum_label, scale.middle_label, scale.minimum_label, "24H AGO", "NOW"});

    int previous_x = plot_area.x + plot_area.inset;
    int previous_y = plot_area.y + plot_area.height - 1 - plot_area.inset;
    for (std::size_t i = 0U; i < count; ++i)
    {
        const int x =
            screens::graph_plot_x(plot_area, static_cast<int>(i), static_cast<int>(count));
        const int y = screens::graph_plot_y(plot_area, values[i], scale.minimum, scale.maximum);
        if (i > 0U)
        {
            framebuffer::draw_line(fb, previous_x, previous_y, x, y, true);
        }
        previous_x = x;
        previous_y = y;
    }

    char current_text[20] = {};
    local_conditions::format_metric(console_state.local_condition_metric, status, current_text,
                                    sizeof(current_text));
    draw_local_conditions_footer(fb, status, now_ms);

    const int kCaptionY = screens::history_graph_caption_y(kGraphHeight);
    const char* metric_text = local_condition_metric_text(console_state.local_condition_metric);
    screens::draw_text(fb, plot_area.x, kCaptionY, metric_text, TextStyle::Annotation);
    const int unit_x = plot_area.x + screens::text_width(metric_text, TextStyle::Annotation) +
                       screens::text_width(" ", TextStyle::Annotation);
    screens::draw_text(fb, unit_x, kCaptionY, scale.unit_label, TextStyle::Annotation);
    // The large value shares a bottom edge with the small metric label.
    const int kValueY = kCaptionY + text_styles::line_height(TextStyle::Annotation) -
                        text_styles::line_height(TextStyle::EmphasisValue) + 2;
    screens::draw_right_aligned_text(fb, plot_area.x + plot_area.width, kValueY, current_text,
                                     TextStyle::EmphasisValue);
}

} // namespace local_conditions_screens
