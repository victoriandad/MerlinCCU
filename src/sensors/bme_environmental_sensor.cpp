#include "bme_environmental_sensor.h"

#include "pico/error.h"
#include "pico/stdlib.h"

namespace sensors
{
namespace
{

inline constexpr uint8_t kBmeChipIdRegister = 0xD0U;
inline constexpr uint8_t kBme280ChipId = 0x60U;
inline constexpr uint8_t kBme680ChipId = 0x61U;
inline constexpr uint8_t kBme280CalibrationRegister = 0x88U;
inline constexpr uint8_t kBme280HumidityCalibrationRegister = 0xE1U;
inline constexpr uint8_t kBme280Humidity1CalibrationRegister = 0xA1U;
inline constexpr uint8_t kBme280CtrlHumidityRegister = 0xF2U;
inline constexpr uint8_t kBme280StatusRegister = 0xF3U;
inline constexpr uint8_t kBme280CtrlMeasureRegister = 0xF4U;
inline constexpr uint8_t kBme280DataRegister = 0xF7U;
inline constexpr uint8_t kBme280MeasuringMask = 0x08U;
inline constexpr uint8_t kBme280OversampleX1 = 0x01U;
inline constexpr uint8_t kBme280ForcedMode = 0x01U;
inline constexpr uint8_t kBme280CtrlMeasureForced =
    static_cast<uint8_t>((kBme280OversampleX1 << 5U) | (kBme280OversampleX1 << 2U) |
                         kBme280ForcedMode);
inline constexpr uint8_t kBme280CtrlHumidityX1 = kBme280OversampleX1;
inline constexpr uint32_t kBme280PollDelayMs = 2U;
inline constexpr uint8_t kBme280MeasurePollLimit = 12U;

uint16_t read_u16_le(const uint8_t* data, size_t offset)
{
    return static_cast<uint16_t>(static_cast<uint16_t>(data[offset]) |
                                 (static_cast<uint16_t>(data[offset + 1U]) << 8U));
}

int16_t read_s16_le(const uint8_t* data, size_t offset)
{
    return static_cast<int16_t>(read_u16_le(data, offset));
}

int16_t sign_extend_12(uint16_t value)
{
    if ((value & 0x0800U) != 0U)
    {
        value |= 0xF000U;
    }
    return static_cast<int16_t>(value);
}

} // namespace

void BmeEnvironmentalSensor::configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us)
{
    registers_.configure(bus, address, timeout_us);
    status_ = {
        .state = BmeEnvironmentalSensorState::Unprobed,
        .kind = BmeEnvironmentalSensorKind::None,
        .address = address,
        .chip_id = 0U,
        .last_error = PICO_ERROR_NONE,
    };
    bme280_calibration_ = {};
    bme280_calibration_loaded_ = false;
}

BmeEnvironmentalSensorStatus BmeEnvironmentalSensor::probe()
{
    uint8_t chip_id = 0U;
    const int result = registers_.read_chip_id(kBmeChipIdRegister, chip_id);
    status_.last_error = result == 1 ? PICO_ERROR_NONE : result;
    status_.chip_id = result == 1 ? chip_id : 0U;

    if (result != 1)
    {
        status_.state =
            result == PICO_ERROR_GENERIC ? BmeEnvironmentalSensorState::Missing
                                         : BmeEnvironmentalSensorState::Fault;
        status_.kind = BmeEnvironmentalSensorKind::None;
        bme280_calibration_loaded_ = false;
        return status_;
    }

    if (chip_id == kBme280ChipId)
    {
        status_.state = BmeEnvironmentalSensorState::Present;
        status_.kind = BmeEnvironmentalSensorKind::Bme280;
        return status_;
    }

    if (chip_id == kBme680ChipId)
    {
        status_.state = BmeEnvironmentalSensorState::Present;
        status_.kind = BmeEnvironmentalSensorKind::Bme680;
        bme280_calibration_loaded_ = false;
        return status_;
    }

    status_.state = BmeEnvironmentalSensorState::Unsupported;
    status_.kind = BmeEnvironmentalSensorKind::Unsupported;
    bme280_calibration_loaded_ = false;
    return status_;
}

const BmeEnvironmentalSensorStatus& BmeEnvironmentalSensor::status() const
{
    return status_;
}

BmeEnvironmentalReading BmeEnvironmentalSensor::read_environment()
{
    BmeEnvironmentalReading reading = {
        .valid = false,
        .temperature_centi_celsius = 0,
        .pressure_pa = 0U,
        .humidity_milli_percent = 0U,
        .last_error = PICO_ERROR_NONE,
    };

    if (status_.state != BmeEnvironmentalSensorState::Present ||
        status_.kind != BmeEnvironmentalSensorKind::Bme280)
    {
        reading.last_error = PICO_ERROR_NOT_PERMITTED;
        return reading;
    }

    if (!bme280_calibration_loaded_)
    {
        const int calibration_result = load_bme280_calibration();
        if (calibration_result != PICO_ERROR_NONE)
        {
            reading.last_error = calibration_result;
            status_.last_error = calibration_result;
            return reading;
        }
    }

    int result = registers_.write_register(kBme280CtrlHumidityRegister, kBme280CtrlHumidityX1);
    if (result != 2)
    {
        reading.last_error = result;
        status_.last_error = result;
        return reading;
    }

    result = registers_.write_register(kBme280CtrlMeasureRegister, kBme280CtrlMeasureForced);
    if (result != 2)
    {
        reading.last_error = result;
        status_.last_error = result;
        return reading;
    }

    sleep_ms(kBme280PollDelayMs);

    bool measurement_complete = false;
    for (uint8_t attempt = 0U; attempt < kBme280MeasurePollLimit; ++attempt)
    {
        uint8_t status = 0U;
        result = registers_.read_register(kBme280StatusRegister, status);
        if (result != 1)
        {
            reading.last_error = result;
            status_.last_error = result;
            return reading;
        }

        if ((status & kBme280MeasuringMask) == 0U)
        {
            measurement_complete = true;
            break;
        }

        sleep_ms(kBme280PollDelayMs);
    }

    if (!measurement_complete)
    {
        reading.last_error = PICO_ERROR_TIMEOUT;
        status_.last_error = reading.last_error;
        return reading;
    }

    uint8_t raw_data[8] = {};
    result = registers_.read_block(kBme280DataRegister, raw_data, sizeof(raw_data));
    if (result != static_cast<int>(sizeof(raw_data)))
    {
        reading.last_error = result;
        status_.last_error = result;
        return reading;
    }

    const int32_t adc_pressure = (static_cast<int32_t>(raw_data[0]) << 12) |
                                 (static_cast<int32_t>(raw_data[1]) << 4) |
                                 (static_cast<int32_t>(raw_data[2]) >> 4);
    const int32_t adc_temperature = (static_cast<int32_t>(raw_data[3]) << 12) |
                                    (static_cast<int32_t>(raw_data[4]) << 4) |
                                    (static_cast<int32_t>(raw_data[5]) >> 4);
    const int32_t adc_humidity =
        (static_cast<int32_t>(raw_data[6]) << 8) | static_cast<int32_t>(raw_data[7]);

    reading = compensate_bme280(adc_temperature, adc_pressure, adc_humidity);
    status_.last_error = reading.last_error;
    return reading;
}

int BmeEnvironmentalSensor::load_bme280_calibration()
{
    uint8_t calibration[26] = {};
    int result =
        registers_.read_block(kBme280CalibrationRegister, calibration, sizeof(calibration));
    if (result != static_cast<int>(sizeof(calibration)))
    {
        bme280_calibration_loaded_ = false;
        return result;
    }

    uint8_t humidity1 = 0U;
    result = registers_.read_register(kBme280Humidity1CalibrationRegister, humidity1);
    if (result != 1)
    {
        bme280_calibration_loaded_ = false;
        return result;
    }

    uint8_t humidity_calibration[7] = {};
    result = registers_.read_block(kBme280HumidityCalibrationRegister, humidity_calibration,
                                   sizeof(humidity_calibration));
    if (result != static_cast<int>(sizeof(humidity_calibration)))
    {
        bme280_calibration_loaded_ = false;
        return result;
    }

    bme280_calibration_.dig_t1 = read_u16_le(calibration, 0U);
    bme280_calibration_.dig_t2 = read_s16_le(calibration, 2U);
    bme280_calibration_.dig_t3 = read_s16_le(calibration, 4U);
    bme280_calibration_.dig_p1 = read_u16_le(calibration, 6U);
    bme280_calibration_.dig_p2 = read_s16_le(calibration, 8U);
    bme280_calibration_.dig_p3 = read_s16_le(calibration, 10U);
    bme280_calibration_.dig_p4 = read_s16_le(calibration, 12U);
    bme280_calibration_.dig_p5 = read_s16_le(calibration, 14U);
    bme280_calibration_.dig_p6 = read_s16_le(calibration, 16U);
    bme280_calibration_.dig_p7 = read_s16_le(calibration, 18U);
    bme280_calibration_.dig_p8 = read_s16_le(calibration, 20U);
    bme280_calibration_.dig_p9 = read_s16_le(calibration, 22U);
    bme280_calibration_.dig_h1 = humidity1;
    bme280_calibration_.dig_h2 = read_s16_le(humidity_calibration, 0U);
    bme280_calibration_.dig_h3 = humidity_calibration[2];
    bme280_calibration_.dig_h4 =
        sign_extend_12(static_cast<uint16_t>(
            (static_cast<uint16_t>(humidity_calibration[3]) << 4U) |
            (static_cast<uint16_t>(humidity_calibration[4]) & 0x0FU)));
    bme280_calibration_.dig_h5 =
        sign_extend_12(static_cast<uint16_t>(
            (static_cast<uint16_t>(humidity_calibration[5]) << 4U) |
            (static_cast<uint16_t>(humidity_calibration[4]) >> 4U)));
    bme280_calibration_.dig_h6 = static_cast<int8_t>(humidity_calibration[6]);

    bme280_calibration_loaded_ = bme280_calibration_.dig_p1 != 0U;
    return bme280_calibration_loaded_ ? PICO_ERROR_NONE : PICO_ERROR_INVALID_ARG;
}

BmeEnvironmentalReading BmeEnvironmentalSensor::compensate_bme280(
    int32_t adc_temperature, int32_t adc_pressure, int32_t adc_humidity)
{
    const Bme280Calibration& c = bme280_calibration_;
    BmeEnvironmentalReading reading = {
        .valid = false,
        .temperature_centi_celsius = 0,
        .pressure_pa = 0U,
        .humidity_milli_percent = 0U,
        .last_error = PICO_ERROR_NONE,
    };

    const int32_t temperature_var1 =
        ((((adc_temperature >> 3) - (static_cast<int32_t>(c.dig_t1) << 1)) *
          static_cast<int32_t>(c.dig_t2)) >>
         11);
    const int32_t temperature_delta = (adc_temperature >> 4) - static_cast<int32_t>(c.dig_t1);
    const int32_t temperature_var2 =
        (((temperature_delta * temperature_delta) >> 12) * static_cast<int32_t>(c.dig_t3)) >> 14;
    const int32_t t_fine = temperature_var1 + temperature_var2;

    int64_t pressure_var1 = static_cast<int64_t>(t_fine) - 128000;
    int64_t pressure_var2 =
        pressure_var1 * pressure_var1 * static_cast<int64_t>(c.dig_p6);
    pressure_var2 += (pressure_var1 * static_cast<int64_t>(c.dig_p5)) << 17;
    pressure_var2 += static_cast<int64_t>(c.dig_p4) << 35;
    pressure_var1 = ((pressure_var1 * pressure_var1 * static_cast<int64_t>(c.dig_p3)) >> 8) +
                    ((pressure_var1 * static_cast<int64_t>(c.dig_p2)) << 12);
    pressure_var1 =
        ((((static_cast<int64_t>(1) << 47) + pressure_var1)) * static_cast<int64_t>(c.dig_p1)) >>
        33;
    if (pressure_var1 == 0)
    {
        reading.last_error = PICO_ERROR_INVALID_ARG;
        return reading;
    }

    int64_t pressure = 1048576 - adc_pressure;
    pressure = (((pressure << 31) - pressure_var2) * 3125) / pressure_var1;
    pressure_var1 = (static_cast<int64_t>(c.dig_p9) * (pressure >> 13) * (pressure >> 13)) >> 25;
    pressure_var2 = (static_cast<int64_t>(c.dig_p8) * pressure) >> 19;
    pressure = ((pressure + pressure_var1 + pressure_var2) >> 8) +
               (static_cast<int64_t>(c.dig_p7) << 4);

    int32_t humidity = t_fine - 76800;
    humidity =
        (((((adc_humidity << 14) - (static_cast<int32_t>(c.dig_h4) << 20) -
            (static_cast<int32_t>(c.dig_h5) * humidity)) +
           16384) >>
          15) *
         (((((((humidity * static_cast<int32_t>(c.dig_h6)) >> 10) *
              (((humidity * static_cast<int32_t>(c.dig_h3)) >> 11) + 32768)) >>
             10) +
            2097152) *
               static_cast<int32_t>(c.dig_h2) +
           8192) >>
          14));
    humidity =
        humidity - (((((humidity >> 15) * (humidity >> 15)) >> 7) *
                     static_cast<int32_t>(c.dig_h1)) >>
                    4);
    if (humidity < 0)
    {
        humidity = 0;
    }
    if (humidity > 419430400)
    {
        humidity = 419430400;
    }

    reading.valid = true;
    reading.temperature_centi_celsius = (t_fine * 5 + 128) >> 8;
    reading.pressure_pa = static_cast<uint32_t>(pressure >> 8);
    reading.humidity_milli_percent =
        static_cast<uint32_t>(((humidity >> 12) * 1000 + 512) / 1024);
    return reading;
}

} // namespace sensors
