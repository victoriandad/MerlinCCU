#pragma once

#include <cstddef>
#include <cstdint>

#include "console_model.h"
#include "environment_sensor_manager.h"

/// @brief What the Local Conditions pages can show from the environment
/// sensor board, and the one formatter for each reading.
/// @details Pure functions of an `EnvironmentSensorStatus` snapshot, with no
/// hardware dependency, so the softkey builder, the page renderers, Status
/// Sensors and the host tests all read the board the same way (BODY-5,
/// STATE-1).
namespace local_conditions
{

/// @brief What the board can offer the Local Conditions pages.
enum class BoardState : uint8_t
{
    /// The sensor board is turned off in this firmware build.
    SensorOff,
    /// The board was looked for and not found.
    NoBoard,
    /// The board was found but cannot be read.
    Fault,
    /// The board is there but has not produced a reading yet.
    Waiting,
    /// At least one quantity has a reading or history.
    Reading,
};

/// @brief Decides what the board can offer from `status`.
BoardState board_state(const environment_sensor_manager::EnvironmentSensorStatus& status);

/// @brief True when `metric` has a current reading or any history, so its
/// graph has something to show.
bool metric_has_data(LocalConditionMetric metric,
                     const environment_sensor_manager::EnvironmentSensorStatus& status);

/// @brief Uptime of the newest successful reading, or 0 when there is none.
uint32_t last_reading_ms(const environment_sensor_manager::EnvironmentSensorStatus& status);

/// @brief True when the latest read of any quantity succeeded.
bool reading_current(const environment_sensor_manager::EnvironmentSensorStatus& status);

/// @brief Empty-state headline for a board state other than `Reading`.
const char* empty_state_headline(BoardState state);

/// @brief Empty-state hint for a board state other than `Reading`: where to
/// look, or what happens next.
const char* empty_state_hint(BoardState state);

/// @brief Formats temperature, for example `21.50C`, or `-` with no reading.
void format_temperature(const environment_sensor_manager::EnvironmentSensorStatus& status,
                        char* out, size_t out_size);

/// @brief Formats relative humidity, for example `45.2%`, or `-`.
void format_humidity(const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
                     size_t out_size);

/// @brief Formats station pressure in hPa, for example `1013.2hPa`, or `-`.
void format_pressure(const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
                     size_t out_size);

/// @brief Formats the VOC-change band, for example `STABLE`; `-` with no
/// reading, or `ERR` after a failed read.
void format_voc_change(const environment_sensor_manager::EnvironmentSensorStatus& status,
                       char* out, size_t out_size);

/// @brief Formats `metric` with its own formatter above.
void format_metric(LocalConditionMetric metric,
                   const environment_sensor_manager::EnvironmentSensorStatus& status, char* out,
                   size_t out_size);

} // namespace local_conditions
