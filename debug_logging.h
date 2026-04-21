#pragma once

#include <cstdio>

namespace debug_logging
{

/// @brief Compile-time switch for low-priority serial diagnostics.
/// @details These logs are useful during bring-up and state-machine debugging,
/// but leaving them enabled all the time would flood the USB serial output and
/// can distort the timing of code that is normally lightweight. Keeping the
/// switch as a `constexpr` lets the compiler remove the logging path entirely
/// in normal builds instead of paying a runtime branch at every call site.
inline constexpr bool kEnablePeriodicSerialOutput = false;

} // namespace debug_logging

/// @brief Emits optional bring-up diagnostics without changing normal control flow.
/// @details The project uses a macro here for two reasons:
/// - Call sites can look like plain `printf` usage, which keeps long format
///   strings readable in the networking and state-machine code.
/// - The `do { ... } while (0)` wrapper makes the macro behave like a single
///   statement, so enabling or disabling logging never changes how surrounding
///   `if`/`else` blocks parse.
///
/// When `kEnablePeriodicSerialOutput` is `false`, the compiler discards the
/// body at compile time. That keeps these diagnostics available for future
/// bring-up work without forcing noisy serial traffic during normal use.
#define PERIODIC_LOG(...)                                                                          \
    do                                                                                             \
    {                                                                                              \
        if constexpr (debug_logging::kEnablePeriodicSerialOutput)                                  \
        {                                                                                          \
            std::printf(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
