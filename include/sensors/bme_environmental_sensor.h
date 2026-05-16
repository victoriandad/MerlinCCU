#pragma once

#include <cstdint>

#include "i2c_register_device.h"

namespace sensors
{

/// @brief Bosch BME-family device kind detected at the environment board address.
enum class BmeEnvironmentalSensorKind : uint8_t
{
    None = 0,
    Bme280,
    Bme680,
    Unsupported,
};

/// @brief Probe state for the BME environmental sensor driver scaffold.
enum class BmeEnvironmentalSensorState : uint8_t
{
    Unprobed = 0,
    Missing,
    Present,
    Unsupported,
    Fault,
};

/// @brief Compact BME probe result with no raw readings or calibration payload.
struct BmeEnvironmentalSensorStatus
{
    BmeEnvironmentalSensorState state;
    BmeEnvironmentalSensorKind kind;
    uint8_t address;
    uint8_t chip_id;
    int last_error;
};

/// @brief ID-only scaffold for BME280/BME680 devices on the Waveshare board.
/// @details Real measurement setup, calibration loading, and compensation maths
/// will be added in later slices. This class currently establishes the register
/// access pattern and distinguishes the board variant safely.
class BmeEnvironmentalSensor final
{
public:
    /// @brief Binds the driver to a bus address without performing I/O.
    void configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us);

    /// @brief Reads the chip ID and classifies the BME device variant.
    BmeEnvironmentalSensorStatus probe();

    /// @brief Returns the last BME probe status.
    const BmeEnvironmentalSensorStatus& status() const;

private:
    I2cRegisterDevice registers_;
    BmeEnvironmentalSensorStatus status_{};
};

} // namespace sensors
