#pragma once

#include <cstddef>
#include <cstdint>

#include "i2c_bus.h"

namespace sensors
{

/// @brief Helper for simple register-addressed I2C devices.
/// @details This class deliberately avoids owning sensor state. Concrete
/// drivers use it for bounded register transactions while keeping chip-specific
/// interpretation in their own modules.
class I2cRegisterDevice final
{
public:
    /// @brief Binds the helper to one bus address without performing I/O.
    void configure(const I2cBus* bus, uint8_t address, uint32_t timeout_us);

    /// @brief Reads one register byte.
    /// @return Number of data bytes read, or a negative Pico SDK error code.
    int read_register(uint8_t reg, uint8_t& value) const;

    /// @brief Reads a contiguous register block.
    /// @return Number of data bytes read, or a negative Pico SDK error code.
    int read_block(uint8_t start_reg, uint8_t* out, size_t out_size) const;

    /// @brief Writes one register byte.
    /// @return Number of bytes written, or a negative Pico SDK error code.
    int write_register(uint8_t reg, uint8_t value) const;

    /// @brief Reads a chip-identification register.
    /// @return Number of data bytes read, or a negative Pico SDK error code.
    int read_chip_id(uint8_t id_register, uint8_t& chip_id) const;

    /// @brief Returns the configured 7-bit I2C address.
    uint8_t address() const;

private:
    const I2cBus* bus_ = nullptr;
    uint8_t address_ = 0U;
    uint32_t timeout_us_ = 0U;
};

} // namespace sensors
