#pragma once

#include <cstddef>

#include "console_model.h"
#include "environment_sensor_manager.h"

/// @brief Small cross-page-family primitives from `screens.cpp` that other
/// split-out page renderer files (e.g. `status_screens.cpp`) also need.
/// @details Not part of the public `screens.h` API -- only `screens.cpp` and
/// its sibling page-family translation units should include this.
namespace screens
{

/// @brief Returns the Home Assistant state label used on diagnostics screens.
const char* home_assistant_state_text(HomeAssistantConnectionState state);

/// @brief Returns a provider-neutral weather fetch state label.
const char* weather_fetch_state_text(HomeAssistantConnectionState state);

/// @brief One label/value pair rendered by `draw_compact_detail_rows`.
struct DetailRow
{
    const char* label;
    const char* value;
};

/// @brief Draws compact `Label: value` rows for data-dense pages.
void draw_compact_detail_rows(uint8_t* fb, const DetailRow* rows, size_t count, int start_y,
                              int row_pitch);

/// @brief Formats a fixed-point centi-Celsius temperature for the Status page.
void build_environment_temperature_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 humidity as percent relative humidity.
void build_environment_humidity_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

/// @brief Formats BME280 pressure as hPa, which is easier to scan than raw Pa.
void build_environment_pressure_text(
    const environment_sensor_manager::EnvironmentSensorStatus& status, char* out, size_t out_size);

} // namespace screens
