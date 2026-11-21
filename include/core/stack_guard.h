#pragma once

#include <cstdint>

/// @brief Core-0 stack sizing, overflow guard and high-water measurement.
/// @details The SDK puts the core-0 stack in SCRATCH_Y (4 KB). Core 1 is never
/// started, so SCRATCH_X (the 4 KB directly below it) holds nothing, and the
/// stack is allowed to grow down into it: 8 KB in all, at no cost to main
/// SRAM. The Cortex-M33 stack limit register (MSPLIM) is set to the bottom of
/// that region, so an overflow raises a fault instead of silently corrupting
/// memory. The fault handler records the fault and reboots through the
/// watchdog; the next boot reports it (#164).
namespace stack_guard
{

/// @brief Fills the unused stack with a canary pattern and arms the limit.
/// @details Must be the first call in main(), before any deep call chain.
/// Panics if anything has been placed in SCRATCH_X (core 1's stack or
/// `__scratch_x` code), since the stack would then overwrite it.
void init();

/// @brief Total bytes the core-0 stack may use: SCRATCH_X plus SCRATCH_Y.
uint32_t capacity_bytes();

/// @brief Fewest bytes of stack left untouched since boot (worst case).
/// @details Scans the canary from the bottom of the region upward. Only ever
/// decreases across a boot.
uint32_t free_bytes();

/// @brief Whether the previous reset was a stack overflow caught by the limit.
/// @details Read and cleared by init(), so it describes the last boot only.
bool last_reset_was_stack_overflow();

/// @brief Configurable Fault Status Register value saved by the last fault, or
/// 0 if the previous reset was not a CPU fault.
uint32_t last_fault_status();

} // namespace stack_guard
