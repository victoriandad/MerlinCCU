#include "sgp40_air_quality_sensor.h"

#include <algorithm>

#include "hardware/i2c.h"
#include "pico/error.h"
#include "pico/stdlib.h"

namespace sensors
{
namespace
{

inline constexpr uint8_t kSgp40MeasureRawCommandMsb = 0x26U;
inline constexpr uint8_t kSgp40MeasureRawCommandLsb = 0x0FU;
inline constexpr uint16_t kDefaultHumidityTicks = 0x8000U;
inline constexpr uint16_t kDefaultTemperatureTicks = 0x6666U;
inline constexpr uint8_t kCrcInitial = 0xFFU;
inline constexpr uint8_t kCrcPolynomial = 0x31U;
inline constexpr uint32_t kMeasureRawDelayMs = 50U;
inline constexpr int32_t kMinCompensationTemperatureCentiCelsius = -4500;
inline constexpr int32_t kMaxCompensationTemperatureCentiCelsius = 13000;
inline constexpr uint32_t kMaxCompensationHumidityMilliPercent = 100000U;

/// @brief Calculates the Sensirion CRC-8 byte for one two-byte data word.
uint8_t word_crc(uint16_t value)
{
    uint8_t crc = kCrcInitial;
    const uint8_t bytes[] = {static_cast<uint8_t>(value >> 8U),
                             static_cast<uint8_t>(value & 0xFFU)};
    for (uint8_t byte : bytes)
    {
        crc ^= byte;
        for (uint8_t bit = 0U; bit < 8U; ++bit)
        {
            if ((crc & 0x80U) != 0U)
            {
                crc = static_cast<uint8_t>((crc << 1U) ^ kCrcPolynomial);
            }
            else
            {
                crc = static_cast<uint8_t>(crc << 1U);
            }
        }
    }

    return crc;
}

/// @brief Converts relative humidity in thousandths of a percent to SGP40 ticks.
uint16_t humidity_ticks(uint32_t humidity_milli_percent)
{
    const uint32_t clamped =
        std::min(humidity_milli_percent, kMaxCompensationHumidityMilliPercent);
    const uint64_t scaled =
        (static_cast<uint64_t>(clamped) * 65535ULL) + (kMaxCompensationHumidityMilliPercent / 2U);
    return static_cast<uint16_t>(scaled / kMaxCompensationHumidityMilliPercent);
}

/// @brief Converts temperature in hundredths of a degree Celsius to SGP40 ticks.
uint16_t temperature_ticks(int32_t temperature_centi_celsius)
{
    const int32_t clamped =
        std::min(std::max(temperature_centi_celsius, kMinCompensationTemperatureCentiCelsius),
                 kMaxCompensationTemperatureCentiCelsius);
    const uint64_t shifted = static_cast<uint64_t>(clamped - kMinCompensationTemperatureCentiCelsius);
    return static_cast<uint16_t>(((shifted * 65535ULL) + 8750ULL) / 17500ULL);
}

} // namespace

void Sgp40AirQualitySensor::configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us)
{
    bus_ = bus;
    address_ = address;
    timeout_us_ = timeout_us;
}

Sgp40AirQualityReading Sgp40AirQualitySensor::read_raw_signal(
    int32_t temperature_centi_celsius, uint32_t humidity_milli_percent,
    bool compensation_available) const
{
    Sgp40AirQualityReading reading = {
        .valid = false,
        .raw_signal = 0U,
        .last_error = PICO_ERROR_NONE,
    };

    if (bus_ == nullptr || !bus_->ready() || address_ >= 0x80U)
    {
        reading.last_error = PICO_ERROR_INVALID_ARG;
        return reading;
    }

    const uint16_t humidity =
        compensation_available ? humidity_ticks(humidity_milli_percent) : kDefaultHumidityTicks;
    const uint16_t temperature = compensation_available ? temperature_ticks(temperature_centi_celsius)
                                                        : kDefaultTemperatureTicks;
    const uint8_t command[] = {
        kSgp40MeasureRawCommandMsb,
        kSgp40MeasureRawCommandLsb,
        static_cast<uint8_t>(humidity >> 8U),
        static_cast<uint8_t>(humidity & 0xFFU),
        word_crc(humidity),
        static_cast<uint8_t>(temperature >> 8U),
        static_cast<uint8_t>(temperature & 0xFFU),
        word_crc(temperature),
    };

    int result =
        i2c_write_timeout_us(bus_->native(), address_, command, sizeof(command), false, timeout_us_);
    if (result != static_cast<int>(sizeof(command)))
    {
        reading.last_error = result;
        return reading;
    }

    sleep_ms(kMeasureRawDelayMs);

    uint8_t response[3] = {};
    result = i2c_read_timeout_us(bus_->native(), address_, response, sizeof(response), false,
                                 timeout_us_);
    if (result != static_cast<int>(sizeof(response)))
    {
        reading.last_error = result;
        return reading;
    }

    const uint16_t raw_signal =
        static_cast<uint16_t>((static_cast<uint16_t>(response[0]) << 8U) | response[1]);
    if (word_crc(raw_signal) != response[2])
    {
        reading.last_error = PICO_ERROR_GENERIC;
        return reading;
    }

    reading.valid = true;
    reading.raw_signal = raw_signal;
    return reading;
}

} // namespace sensors
