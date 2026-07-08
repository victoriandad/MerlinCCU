#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Flash-backed persistence for Pinter vessel tracking state, so a
/// power cycle doesn't lose an in-progress brew (issue #18 follow-up).
/// @details Mirrors config_manager's two-slot, sequence-numbered pattern in
/// its own reserved flash region, immediately below the settings store, so
/// the two never race each other's writes. Pinter events (start, advance,
/// reset) are infrequent user-triggered actions -- at most a handful per
/// vessel per brew cycle -- so saving on every event, rather than batching,
/// keeps a sudden power loss from ever losing more than the one event
/// already applied to the in-memory state.
namespace pinter_store
{

/// @brief Total bytes reserved at the tail of flash for Pinter tracking state.
/// @details pinter_store.cpp statically asserts this matches its real,
/// hardware-derived reservation.
inline constexpr uint32_t kReservedFlashBytes = 4096U * 2U;

/// @brief Loads persisted Pinter state from flash, if any is present.
/// @details Returns false (and leaves `out_pinters` untouched) when flash has
/// no valid saved snapshot yet -- callers should keep their own defaults.
bool load(std::array<PinterStatus, kPinterCount>* out_pinters);

/// @brief Persists the given Pinter state to flash.
bool save(const std::array<PinterStatus, kPinterCount>& pinters);

} // namespace pinter_store
