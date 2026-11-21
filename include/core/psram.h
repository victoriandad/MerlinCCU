#pragma once

#include <cstddef>
#include <cstdint>

/// @brief External QSPI PSRAM on the RP2350's second XIP chip select (#162).
/// @details Only boards whose SDK board header names a PSRAM chip-select pin
/// have one; the Pimoroni Pico Plus 2 W carries an 8 MB APS6404L on GPIO 47.
/// On other boards init() reports no PSRAM and nothing is touched.
///
/// Once initialised, the chip is memory-mapped through the XIP cache at
/// kCachedBase and can be read and written like ordinary RAM, only slower.
/// It must not be used for DMA, PIO or ISR-critical buffers.
namespace psram
{

/// @brief Cached, writable window onto chip select 1 (XIP M1).
inline constexpr uintptr_t kCachedBase = 0x11000000U;

/// @brief What init() found.
struct Info
{
    bool present;
    /// Known-good-die byte from the Read ID command; 0x5D for a working
    /// APSxxxx part.
    uint8_t kgd;
    /// Electronic ID byte; its top three bits encode the density.
    uint8_t eid;
    /// Size decoded from the ID, in bytes. 0 when not present.
    uint32_t size_bytes;
};

/// @brief Detects the chip, switches it to quad mode and maps it at
/// kCachedBase. Safe to call on a board without PSRAM.
/// @details Runs from RAM with interrupts disabled, because it drives the QMI
/// directly and flash execution stalls meanwhile. Call once, early in main(),
/// at the final system clock: the timing is derived from clk_sys.
Info init();

/// @brief Result of self_test().
struct SelfTest
{
    bool passed;
    /// Size found by writing distinct markers at power-of-two offsets and
    /// checking they do not alias.
    uint32_t verified_bytes;
    /// First failing address, or 0 when passed.
    uintptr_t first_bad_address;
    uint32_t write_kib_per_s;
    uint32_t read_kib_per_s;
};

/// @brief Destructive check of the mapped PSRAM: size by aliasing, a pattern
/// test with a word every kib, and sequential write and read speed.
/// @details Overwrites the whole chip, so run it before anything lives there.
SelfTest self_test(const Info& info);

} // namespace psram
