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

/// @brief Last compensated BME280 environmental reading.
/// @details Values are kept in fixed-point integer units so display and network
/// code can use them without pulling floating-point formatting into hot paths.
struct BmeEnvironmentalReading
{
    bool valid;
    int32_t temperature_centi_celsius;
    uint32_t pressure_pa;
    uint32_t humidity_milli_percent;
    int last_error;
};

/// @brief BME280/BME680 device driver for the Waveshare board pressure sensor.
/// @details BME280 measurement support is implemented here because it requires
/// chip-specific calibration and compensation. BME680 detection stays in place
/// but readings are deliberately unsupported until its separate gas-sensor path
/// is added.
class BmeEnvironmentalSensor final
{
public:
    /// @brief Binds the driver to a bus address without performing I/O.
    void configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us);

    /// @brief Reads the chip ID and classifies the BME device variant.
    BmeEnvironmentalSensorStatus probe();

    /// @brief Returns the last BME probe status.
    const BmeEnvironmentalSensorStatus& status() const;

    /// @brief Takes one forced-mode BME280 sample and returns compensated data.
    BmeEnvironmentalReading read_environment();

private:
    struct Bme280Calibration
    {
        uint16_t dig_t1;
        int16_t dig_t2;
        int16_t dig_t3;
        uint16_t dig_p1;
        int16_t dig_p2;
        int16_t dig_p3;
        int16_t dig_p4;
        int16_t dig_p5;
        int16_t dig_p6;
        int16_t dig_p7;
        int16_t dig_p8;
        int16_t dig_p9;
        uint8_t dig_h1;
        int16_t dig_h2;
        uint8_t dig_h3;
        int16_t dig_h4;
        int16_t dig_h5;
        int8_t dig_h6;
    };

    /// @brief Loads BME280 trimming values from non-volatile sensor registers.
    int load_bme280_calibration();

    /// @brief Compensates one raw BME280 sample using cached calibration data.
    BmeEnvironmentalReading compensate_bme280(int32_t adc_temperature, int32_t adc_pressure,
                                              int32_t adc_humidity);

    I2cRegisterDevice registers_;
    BmeEnvironmentalSensorStatus status_{};
    Bme280Calibration bme280_calibration_{};
    bool bme280_calibration_loaded_ = false;
};

} // namespace sensors
