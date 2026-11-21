/* Extra linker script for boards with QSPI PSRAM on XIP chip select 1 (#162).
 *
 * Added after the SDK's memmap with -T, and merged into it by INSERT, so the
 * SDK script itself is not copied. Objects tagged MERLIN_PSRAM_BSS (psram.h)
 * land here. The section is NOLOAD: nothing is stored in flash, and
 * psram::init() zeroes it once the chip is set up, before any use.
 *
 * Only zero-initialised data without constructors may go here: C++ static
 * constructors run before main(), when the PSRAM is not yet mapped.
 */
MEMORY
{
    PSRAM(rw) : ORIGIN = 0x11000000, LENGTH = 8M
}

SECTIONS
{
    .psram_bss (NOLOAD) :
    {
        . = ALIGN(8);
        __psram_bss_start__ = .;
        *(.psram_bss*)
        . = ALIGN(8);
        __psram_bss_end__ = .;
    } > PSRAM
}
INSERT AFTER .heap;
