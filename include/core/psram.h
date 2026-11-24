#pragma once

#include <cstddef>
#include <cstdint>

#include "pico.h"

/// @brief Places a zero-initialised object in PSRAM on boards that have it.
/// @details Use on large buffers that only the CPU touches: never DMA, PIO
/// or interrupt-critical data, nor anything read while flash is being
/// written (the QMI serves both chips, so PSRAM is unavailable then). The
/// object must have no constructor and no non-zero initialiser; the section
/// is zeroed by psram::init() (firmware/psram.ld). On boards without PSRAM
/// it expands to nothing and the object stays in SRAM.
#if defined(PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN)
#define MERLIN_PSRAM_BSS __attribute__((section(".psram_bss")))
#else
#define MERLIN_PSRAM_BSS
#endif

/// @brief False on boards whose MERLIN_PSRAM_BSS buffers land in SRAM, so a
/// large optional buffer can be sized down there.
inline constexpr bool kBoardHasPsram =
#if defined(PIMORONI_PICO_PLUS2_W_PSRAM_CS_PIN)
    true;
#else
    false;
#endif

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

/// @brief Result of the boot-time self-test.
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
    /// Destructive check run by init() before anything is placed: size by
    /// aliasing, a pattern word every KiB, and sequential speed.
    SelfTest self_test;
};

/// @brief Bytes of PSRAM taken by MERLIN_PSRAM_BSS objects (0 without PSRAM).
uint32_t placed_bytes();

/// @brief Detects the chip, switches it to quad mode, maps it at kCachedBase,
/// runs the self-test and zeroes the MERLIN_PSRAM_BSS section. Safe to call
/// on a board without PSRAM. Panics on a PSRAM board if the chip is missing
/// or fails the test, since tagged objects would then be unusable.
/// @details The probe runs from RAM with interrupts disabled, because it
/// drives the QMI directly and flash execution stalls meanwhile. Call once,
/// first thing in main() at the final system clock (the timing is derived
/// from clk_sys) and before anything touches a tagged object.
Info init();

} // namespace psram