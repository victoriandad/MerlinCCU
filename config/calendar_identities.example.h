#pragma once

/// @brief Local calendar participant labels for MerlinCCU bring-up.
/// @details Copy this file to `config/calendar_identities.h`, fill in your
/// participant labels, and keep that local file out of version control.
///
/// The firmware treats this as a configurable list of participant identities.
/// Leave unused entries blank. The calendar UI can use as few as one entry or
/// as many as the fixed capacity below.
///
/// Quick start:
/// 1. Copy this file to `config/calendar_identities.h`.
/// 2. Replace the placeholder labels in `kLocalCalendarIdentities`.
/// 3. Leave unused entries empty.

#include <array>

inline constexpr size_t kCalendarIdentityCapacity = 8U;

inline constexpr std::array<const char*, kCalendarIdentityCapacity> kLocalCalendarIdentities = {
    "Owner 1", "Owner 2", "Owner 3", "Owner 4", "", "", "", "",
};
