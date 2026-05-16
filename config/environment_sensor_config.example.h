#pragma once

#include <cstdint>

// Copy this file to config/environment_sensor_config.h before enabling the
// optional Waveshare Pico Environment Sensor board.
//
// The board is intentionally disabled by default because common Pico shield I2C
// pins can clash with the current keypad matrix bench harness. Confirm the
// final keypad wiring and the board's I2C selection jumpers before setting this
// to true.

inline constexpr bool ENVIRONMENT_SENSOR_ENABLED = false;

// Waveshare documentation identifies the board as an I2C device set. Many Pico
// shield examples use I2C0 on GPIO20/GPIO21, but the board also exposes I2C
// selection hardware. Set these values to match the actual jumper position and
// available GPIO budget in the MerlinCCU harness.
inline constexpr int ENVIRONMENT_SENSOR_I2C_BUS = 0;
inline constexpr int ENVIRONMENT_SENSOR_I2C_SDA_GPIO = 20;
inline constexpr int ENVIRONMENT_SENSOR_I2C_SCL_GPIO = 21;
inline constexpr uint32_t ENVIRONMENT_SENSOR_I2C_BAUDRATE_HZ = 100000U;
