#include "i2c_register_device.h"

#include "hardware/i2c.h"
#include "pico/error.h"

namespace sensors
{

void I2cRegisterDevice::configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us)
{
    bus_ = bus;
    address_ = address;
    timeout_us_ = timeout_us;
}

int I2cRegisterDevice::read_register(uint8_t reg, uint8_t& value) const
{
    return read_block(reg, &value, 1U);
}

int I2cRegisterDevice::read_block(uint8_t start_reg, uint8_t* out, size_t out_size) const
{
    if (bus_ == nullptr || !bus_->ready() || out == nullptr || out_size == 0U)
    {
        return PICO_ERROR_INVALID_ARG;
    }

    const int address_result =
        i2c_write_timeout_us(bus_->native(), address_, &start_reg, 1U, true, timeout_us_);
    if (address_result != 1)
    {
        bus_->clear_pending_restart();
        return address_result;
    }

    return i2c_read_timeout_us(bus_->native(), address_, out, out_size, false, timeout_us_);
}

int I2cRegisterDevice::write_register(uint8_t reg, uint8_t value) const
{
    if (bus_ == nullptr || !bus_->ready())
    {
        return PICO_ERROR_INVALID_ARG;
    }

    const uint8_t payload[] = {reg, value};
    return i2c_write_timeout_us(bus_->native(), address_, payload, sizeof(payload), false,
                                timeout_us_);
}

int I2cRegisterDevice::read_chip_id(uint8_t id_register, uint8_t& chip_id) const
{
    return read_register(id_register, chip_id);
}

uint8_t I2cRegisterDevice::address() const
{
    return address_;
}

} // namespace sensors
