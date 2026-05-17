#pragma once

#include <cstdint>

#include "i2c_bus.h"

namespace sensors
{

/// @brief One raw SGP40 VOC reading.
/// @details The raw signal is not yet a VOC index. It is the compensated
/// SRAW_VOC value returned directly by the sensor and is suitable for proving
/// the I2C path before adding Sensirion's adaptive gas-index algorithm.
struct Sgp40AirQualityReading
{
    bool valid;
    uint16_t raw_signal;
    int last_error;
};

/// @brief Minimal Sensirion SGP40 driver for compensated raw VOC readings.
class Sgp40AirQualitySensor final
{
public:
    /// @brief Binds the driver to the shared I2C bus without performing I/O.
    void configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us);

    /// @brief Takes one SGP40 raw VOC sample with optional BME compensation.
    /// @details When BME data is unavailable, Sensirion's documented default
    /// compensation values are used so the sensor still returns a raw signal.
    Sgp40AirQualityReading read_raw_signal(int32_t temperature_centi_celsius,
                                           uint32_t humidity_milli_percent,
                                           bool compensation_available) const;

private:
    const I2cBus* bus_ = nullptr;
    uint8_t address_ = 0U;
    uint32_t timeout_us_ = 0U;
};

} // namespace sensors
