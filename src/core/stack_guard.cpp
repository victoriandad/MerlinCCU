#include "stack_guard.h"

#include <cstdio>

#include "hardware/regs/m33.h"
#include "hardware/structs/scb.h"
#include "hardware/watchdog.h"
#include "pico/stdlib.h"

// Linker symbols from the SDK's RP2350 memmap (pico_crt0/rp2350/*.ld).
extern "C" uint32_t __scratch_x_start__;
extern "C" uint32_t __scratch_x_end__;
extern "C" uint32_t __StackOneBottom;
extern "C" uint32_t __StackOneTop;
extern "C" uint32_t __StackTop;

namespace stack_guard
{

namespace
{

constexpr uint32_t kStackFillPattern = 0xEEEEEEEEU;
// Bytes below the stack pointer at fill time are left alone, so the frames
// already live in crt0 and main()'s prologue are never overwritten.
constexpr uint32_t kStackFillSafetyMarginBytes = 64U;
// MSPLIM ignores its low three bits; the limit must be 8-byte aligned.
constexpr uintptr_t kStackLimitAlignment = 8U;

// Watchdog scratch registers 0-3 are left to the application by the SDK,
// which uses 4-7 for its own reboot bookkeeping.
constexpr uint32_t kFaultMagicScratch = 0U;
constexpr uint32_t kFaultStatusScratch = 1U;
constexpr uint32_t kFaultMagic = 0x5354414BU; // "STAK"

bool g_last_reset_was_stack_overflow = false;
uint32_t g_last_fault_status = 0U;

/// @brief Returns a linker symbol's address as a plain number.
/// @details The asm barrier stops the compiler from assuming that two
/// distinct declared objects can never share an address, which would fold
/// the empty-section checks in init() to a constant.
uintptr_t symbol_address(const void* symbol)
{
    auto address = reinterpret_cast<uintptr_t>(symbol);
    asm volatile("" : "+r"(address));
    return address;
}

/// @brief Lowest address the core-0 stack may reach.
uint32_t* region_bottom()
{
    const uintptr_t bottom = symbol_address(&__scratch_x_end__);
    return reinterpret_cast<uint32_t*>((bottom + kStackLimitAlignment - 1U) &
                                       ~(kStackLimitAlignment - 1U));
}

} // namespace

void init()
{
    // The region below SCRATCH_Y is only free while nothing is linked into
    // SCRATCH_X: no core 1 stack and no __scratch_x code.
    if (symbol_address(&__scratch_x_end__) != symbol_address(&__scratch_x_start__) ||
        symbol_address(&__StackOneBottom) != symbol_address(&__StackOneTop))
    {
        panic("SCRATCH_X is in use; the core-0 stack cannot extend into it");
    }

    uint32_t sp = 0U;
    asm volatile("mov %0, sp" : "=r"(sp));
    auto* end = reinterpret_cast<uint32_t*>(sp - kStackFillSafetyMarginBytes);
    for (uint32_t* p = region_bottom(); p < end; ++p)
    {
        *p = kStackFillPattern;
    }

    asm volatile("msr msplim, %0" : : "r"(region_bottom()));

    if (watchdog_hw->scratch[kFaultMagicScratch] == kFaultMagic)
    {
        g_last_fault_status = watchdog_hw->scratch[kFaultStatusScratch];
        g_last_reset_was_stack_overflow = (g_last_fault_status & M33_CFSR_UFSR_STKOF_BITS) != 0U;
    }
    watchdog_hw->scratch[kFaultMagicScratch] = 0U;
    watchdog_hw->scratch[kFaultStatusScratch] = 0U;
}

uint32_t capacity_bytes()
{
    return static_cast<uint32_t>(symbol_address(&__StackTop) -
                                 reinterpret_cast<uintptr_t>(region_bottom()));
}

uint32_t free_bytes()
{
    const uint32_t* begin = region_bottom();
    const uint32_t* end = &__StackTop;
    const uint32_t* p = begin;
    while (p < end && *p == kStackFillPattern)
    {
        ++p;
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p) -
                                 reinterpret_cast<uintptr_t>(begin));
}

bool last_reset_was_stack_overflow()
{
    return g_last_reset_was_stack_overflow;
}

uint32_t last_fault_status()
{
    return g_last_fault_status;
}

} // namespace stack_guard

/// @brief Saves the fault status for the next boot and reboots.
/// @details Runs on a stack reset to the top by isr_hardfault(), so it works
/// even when the fault was the stack hitting its limit.
extern "C" [[noreturn]] void stack_guard_record_fault_and_reboot()
{
    watchdog_hw->scratch[stack_guard::kFaultMagicScratch] = stack_guard::kFaultMagic;
    watchdog_hw->scratch[stack_guard::kFaultStatusScratch] = scb_hw->cfsr;
    watchdog_reboot(0U, 0U, 0U);
    while (true)
    {
        __asm volatile("nop");
    }
}

/// @brief HardFault handler: replaces the SDK's breakpoint stub.
/// @details A stack overflow leaves SP at the limit, so this handler cannot
/// push anything before moving SP back to the top of the stack. It never
/// returns, so discarding the faulting context is safe. A UsageFault (which a
/// stack limit violation is) escalates here because it is not enabled
/// separately.
extern "C" [[gnu::naked]] void isr_hardfault()
{
    asm volatile("movw r0, #:lower16:__StackTop\n"
                 "movt r0, #:upper16:__StackTop\n"
                 "mov sp, r0\n"
                 "b stack_guard_record_fault_and_reboot\n");
}
