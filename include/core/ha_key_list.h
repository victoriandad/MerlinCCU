#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "input.h"

/// @brief Which keys belong to Home Assistant (#151), kept free of Pico SDK
/// calls so host tests can cover it.
/// @details An "HA key" does nothing on the panel; each press is published
/// to MQTT and shows in HA's automation editor as a device trigger. The set
/// is stored as a bit mask over ButtonId and edited on the config page as a
/// comma-separated list of the key legends, e.g. "F1, T FUNC, A".
namespace ha_keys
{

/// @brief A key's legend, e.g. "L1" or "T FUNC"; input::button_name in the
/// firmware.
using NameFn = const char* (*)(ButtonId id);

static_assert(static_cast<size_t>(ButtonId::Count) <= 64U, "the HA key mask is 64 bits");

/// @brief The mask bit for a key.
constexpr uint64_t bit(ButtonId id)
{
    return uint64_t{1} << static_cast<unsigned>(id);
}

/// @brief True when the key is an HA key.
constexpr bool contains(uint64_t mask, ButtonId id)
{
    return id < ButtonId::Count && (mask & bit(id)) != 0U;
}

/// @brief Reads a comma-separated list of legends, ignoring case and spaces
/// around each item.
/// @return the mask, or nullopt naming the first unknown key in `bad_item`.
std::optional<uint64_t> parse_list(std::string_view text, NameFn name_of,
                                   std::string_view* bad_item = nullptr);

/// @brief Writes the mask as a list, e.g. "F1, A"; empty for no keys.
void format_list(uint64_t mask, NameFn name_of, char* out, size_t out_size);

/// @brief A topic-safe id for a key: its legend in lower case, spaces as
/// underscores, e.g. "t_func".
void slug(const char* legend, char* out, size_t out_size);

} // namespace ha_keys
