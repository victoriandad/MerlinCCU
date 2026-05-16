#include "bme_environmental_sensor.h"

#include "pico/error.h"

namespace sensors
{
namespace
{

inline constexpr uint8_t kBmeChipIdRegister = 0xD0U;
inline constexpr uint8_t kBme280ChipId = 0x60U;
inline constexpr uint8_t kBme680ChipId = 0x61U;

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
        return status_;
    }

    status_.state = BmeEnvironmentalSensorState::Unsupported;
    status_.kind = BmeEnvironmentalSensorKind::Unsupported;
    return status_;
}

const BmeEnvironmentalSensorStatus& BmeEnvironmentalSensor::status() const
{
    return status_;
}

} // namespace sensors
