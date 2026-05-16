#pragma once

#include <cstdint>

#include "hardware/i2c.h"

namespace sensors
{

/// @brief Small ownership wrapper around one Pico SDK I2C peripheral.
/// @details The class deliberately owns only bus configuration and simple
/// transactions. Sensor-specific register maps and compensation maths belong in
/// concrete sensor drivers so the shared bus layer stays predictable and cheap.
class I2cBus final
{
public:
    /// @brief Records the hardware instance and pins without touching GPIOs.
    void configure(i2c_inst_t* instance, int sda_gpio, int scl_gpio, uint32_t baudrate_hz);

    /// @brief Initialises the configured I2C peripheral and pin functions.
    /// @return Pico SDK style status code, where `0` is success.
    int init();

    /// @brief Returns true once `init()` has successfully configured the bus.
    bool ready() const;

    /// @brief Performs a conservative one-byte read probe against a device address.
    /// @details This is intended for board discovery before real sensor drivers
    /// take ownership. Once periodic sensor reads exist, discovery should be
    /// driven by driver ID-register reads rather than repeated generic probes.
    bool probe_read(uint8_t address, uint32_t timeout_us, int& result) const;

    /// @brief Returns the last Pico SDK style error observed by the wrapper.
    int last_error() const;

    /// @brief Clears the SDK repeated-start marker after an aborted register transaction.
    /// @details The Pico SDK tracks `nostop` transfers in the I2C instance. A
    /// failed register-address write may abort before the matching read happens,
    /// so the next independent probe must start cleanly.
    void clear_pending_restart() const;

    /// @brief Returns the configured native Pico SDK I2C instance.
    i2c_inst_t* native() const;

private:
    i2c_inst_t* instance_ = nullptr;
    int sda_gpio_ = -1;
    int scl_gpio_ = -1;
    uint32_t baudrate_hz_ = 0;
    int last_error_ = 0;
    bool ready_ = false;
};

} // namespace sensors
