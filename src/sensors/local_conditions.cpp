#include "local_conditions.h"

#include <algorithm>
#include <cstdio>

namespace local_conditions
{

namespace
{

using environment_sensor_manager::EnvironmentSensorHealth;
using environment_sensor_manager::EnvironmentSensorStatus;

// PICO_ERROR_NONE is stably 0 across the Pico SDK; named here rather than
// including pico/error.h so this file stays host-testable.
constexpr int kNoErrorCode = 0;

constexpr const char* kNoValueText = "-";

/// @brief True when the BME temperature, humidity and pressure reading is usable.
bool bme_reading_present(const EnvironmentSensorStatus& status)
{
    return status.enabled && status.bme_reading_valid;
}

/// @brief True when the VOC-change score is usable.
bool voc_reading_present(const EnvironmentSensorStatus& status)
{
    return status.enabled && status.air_quality_score_valid;
}

} // namespace

BoardState board_state(const EnvironmentSensorStatus& status)
{
    if (!status.enabled)
    {
        return BoardState::SensorOff;
    }

    constexpr LocalConditionMetric kMetrics[] = {
        LocalConditionMetric::Temperature, LocalConditionMetric::Humidity,
        LocalConditionMetric::AirPressure, LocalConditionMetric::AirQuality};
    if (std::any_of(std::begin(kMetrics), std::end(kMetrics),
                    [&status](LocalConditionMetric metric)
                    { return metric_has_data(metric, status); }))
    {
        return BoardState::Reading;
    }

    switch (status.health)
    {
    case EnvironmentSensorHealth::BoardMissing:
        return BoardState::NoBoard;
    case EnvironmentSensorHealth::Fault:
        return BoardState::Fault;
    case EnvironmentSensorHealth::Disabled:
        return BoardState::SensorOff;
    case EnvironmentSensorHealth::BusReady:
    case EnvironmentSensorHealth::Partial:
    case EnvironmentSensorHealth::BoardDetected:
        break;
    }
    return BoardState::Waiting;
}

bool metric_has_data(LocalConditionMetric metric, const EnvironmentSensorStatus& status)
{
    if (!status.enabled)
    {
        return false;
    }
    switch (metric)
    {
    case LocalConditionMetric::Temperature:
    case LocalConditionMetric::Humidity:
    case LocalConditionMetric::AirPressure:
        return status.bme_reading_valid || status.bme_history_count > 0U;
    case LocalConditionMetric::AirQuality:
        return status.air_quality_score_valid || status.air_quality_history_count > 0U;
    }
    return false;
}

uint32_t last_reading_ms(const EnvironmentSensorStatus& status)
{
    if (!status.enabled)
    {
        return 0U;
    }
    return std::max(status.bme_reading_valid ? status.bme_last_read_ms : 0U,
                    status.air_quality_score_valid ? status.air_quality_last_read_ms : 0U);
}

bool reading_current(const EnvironmentSensorStatus& status)
{
    return bme_reading_present(status) || voc_reading_present(status);
}

const char* empty_state_headline(BoardState state)
{
    switch (state)
    {
    case BoardState::SensorOff:
        return "SENSOR BOARD OFF";
    case BoardState::NoBoard:
        return "NO SENSOR BOARD";
    case BoardState::Fault:
        return "SENSOR BOARD FAULT";
    case BoardState::Waiting:
        return "WAITING FOR SENSOR";
    case BoardState::Reading:
        break;
    }
    return "";
}

const char* empty_state_hint(BoardState state)
{
    switch (state)
    {
    case BoardState::SensorOff:
        return "Turned off in this firmware build";
    case BoardState::NoBoard:
    case BoardState::Fault:
        return "Details in Status > Sensors";
    case BoardState::Waiting:
        return "The first reading is due shortly";
    case BoardState::Reading:
        break;
    }
    return "";
}

void format_temperature(const EnvironmentSensorStatus& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    if (!bme_reading_present(status))
    {
        std::snprintf(out, out_size, "%s", kNoValueText);
        return;
    }

    const int64_t raw_value = status.bme_temperature_centi_celsius;
    const bool negative = raw_value < 0;
    const uint32_t absolute_value = static_cast<uint32_t>(negative ? -raw_value : raw_value);
    std::snprintf(out, out_size, "%s%lu.%02luC", negative ? "-" : "",
                  static_cast<unsigned long>(absolute_value / 100U),
                  static_cast<unsigned long>(absolute_value % 100U));
}

void format_humidity(const EnvironmentSensorStatus& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    if (!bme_reading_present(status))
    {
        std::snprintf(out, out_size, "%s", kNoValueText);
        return;
    }

    const uint32_t tenths_percent = (status.bme_humidity_milli_percent + 50U) / 100U;
    std::snprintf(out, out_size, "%lu.%lu%%", static_cast<unsigned long>(tenths_percent / 10U),
                  static_cast<unsigned long>(tenths_percent % 10U));
}

void format_pressure(const EnvironmentSensorStatus& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    if (!bme_reading_present(status))
    {
        std::snprintf(out, out_size, "%s", kNoValueText);
        return;
    }

    const uint32_t tenths_hpa = (status.bme_pressure_pa + 5U) / 10U;
    std::snprintf(out, out_size, "%lu.%luhPa", static_cast<unsigned long>(tenths_hpa / 10U),
                  static_cast<unsigned long>(tenths_hpa % 10U));
}

void format_voc_change(const EnvironmentSensorStatus& status, char* out, size_t out_size)
{
    if (out == nullptr || out_size == 0U)
    {
        return;
    }
    if (!voc_reading_present(status))
    {
        std::snprintf(out, out_size, "%s",
                      status.air_quality_read_error == kNoErrorCode ? kNoValueText : "ERR");
        return;
    }
    std::snprintf(out, out_size, "%s",
                  environment_sensor_manager::air_quality_band_text(status.air_quality_score));
}

void format_metric(LocalConditionMetric metric, const EnvironmentSensorStatus& status, char* out,
                   size_t out_size)
{
    switch (metric)
    {
    case LocalConditionMetric::Temperature:
        format_temperature(status, out, out_size);
        return;
    case LocalConditionMetric::Humidity:
        format_humidity(status, out, out_size);
        return;
    case LocalConditionMetric::AirPressure:
        format_pressure(status, out, out_size);
        return;
    case LocalConditionMetric::AirQuality:
        format_voc_change(status, out, out_size);
        return;
    }
    if (out != nullptr && out_size > 0U)
    {
        std::snprintf(out, out_size, "%s", kNoValueText);
    }
}

} // namespace local_conditions
