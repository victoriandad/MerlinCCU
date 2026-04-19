#pragma once

#include <cstdio>

namespace debug_logging {

inline constexpr bool kEnablePeriodicSerialOutput = false;

}  // namespace debug_logging

#define PERIODIC_LOG(...)                                                                          \
    do {                                                                                           \
        if constexpr (debug_logging::kEnablePeriodicSerialOutput) {                                \
            std::printf(__VA_ARGS__);                                                              \
        }                                                                                          \
    } while (0)
