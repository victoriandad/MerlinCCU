#include "i2c_bus.h"

#include "hardware/gpio.h"
#include "pico/error.h"
#include "pico/stdlib.h"

namespace sensors
{

void I2cBus::configure(i2c_inst_t* instance, int sda_gpio, int scl_gpio, uint32_t baudrate_hz)
{
    instance_ = instance;
    sda_gpio_ = sda_gpio;
    scl_gpio_ = scl_gpio;
    baudrate_hz_ = baudrate_hz;
    ready_ = false;
    last_error_ = 0;
}

int I2cBus::init()
{
    if (instance_ == nullptr || sda_gpio_ < 0 || scl_gpio_ < 0 || sda_gpio_ == scl_gpio_ ||
        baudrate_hz_ == 0U)
    {
        ready_ = false;
        last_error_ = PICO_ERROR_INVALID_ARG;
        return last_error_;
    }

    i2c_init(instance_, baudrate_hz_);
    gpio_set_function(static_cast<uint>(sda_gpio_), GPIO_FUNC_I2C);
    gpio_set_function(static_cast<uint>(scl_gpio_), GPIO_FUNC_I2C);
    gpio_pull_up(static_cast<uint>(sda_gpio_));
    gpio_pull_up(static_cast<uint>(scl_gpio_));

    ready_ = true;
    last_error_ = PICO_ERROR_NONE;
    return last_error_;
}

bool I2cBus::ready() const
{
    return ready_;
}

bool I2cBus::probe_read(uint8_t address, uint32_t timeout_us, int& result) const
{
    if (!ready_ || instance_ == nullptr || address >= 0x80U)
    {
        result = PICO_ERROR_INVALID_ARG;
        return false;
    }

    uint8_t scratch = 0;
    result = i2c_read_timeout_us(instance_, address, &scratch, 1, false, timeout_us);
    return result == 1;
}

int I2cBus::last_error() const
{
    return last_error_;
}

void I2cBus::clear_pending_restart() const
{
    if (instance_ != nullptr)
    {
        instance_->restart_on_next = false;
    }
}

i2c_inst_t* I2cBus::native() const
{
    return instance_;
}

} // namespace sensors
