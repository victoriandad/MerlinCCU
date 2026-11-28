#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

/// @brief Weather alerts accepted once for every unit, through Home
/// Assistant (#210).
/// @details Every unit raises the same weather alerts from the same data
/// HA sends. Each alert carries an id: a fingerprint of what it is about, so
/// every unit works out the same id for the same weather. Accepting one on a
/// panel sends its id to HA (`<base>/house/acks/set`). HA records it, and
/// publishes every accepted id, retained, on `<base>/house/weather/acks`;
/// a unit whose alert has an accepted id clears it. A new or different
/// warning, or a new day, gives a new id, and is raised again.
/// Schema: docs/mqtt-household-data.md. Pure logic, so host tests cover it.
namespace weather_acks
{

/// @brief The weather alerts that can be accepted for every unit.
enum class Kind : uint8_t
{
    Official = 0, ///< Met Office and flood warnings (OfficialWeatherWarning).
    Provider,     ///< Severe conditions in the forecast (WeatherProviderWarning).
    Temperature,  ///< Frost or heat in the forecast (WeatherTemperatureWarning).
    Wind,         ///< Gales in the forecast (WeatherWindWarning).
};

inline constexpr size_t kKindCount = 4U;
/// @brief Hex digits in an id.
inline constexpr size_t kIdLength = 8U;
/// @brief Largest acks payload the CCU accepts, in bytes.
inline constexpr size_t kMaxAcksPayloadBytes = 256U;

/// @brief An id as text, terminated; empty for none.
using Id = std::array<char, kIdLength + 1U>;
/// @brief The accepted id of each kind, indexed by Kind.
using Accepted = std::array<Id, kKindCount>;

/// @brief The kind's name in the payloads: `official`, `wx_provider`,
/// `wx_temp` or `wx_wind`.
const char* name(Kind kind);

/// @brief The id for `key`, a text that names the alert's subject (the
/// warnings in force, or the kind of hazard and the day), followed by a
/// newline and `more` when that is not empty.
Id id_for(std::string_view key, std::string_view more = {});

/// @brief Reads HA's retained acks payload, `{"v":1,"official":"1a2b3c4d",...}`,
/// into `accepted`; a kind it leaves out has none.
/// @return False, leaving `accepted` as it was, for another schema version.
bool apply(const char* json, Accepted& accepted);

/// @brief Writes the message a unit sends HA on accepting one:
/// `{"alert":"wx_temp","id":"1a2b3c4d","unit":"merlinccu2"}`.
/// @return False when `out` is too small.
bool format_ack(Kind kind, const Id& id, const char* unit, char* out, size_t out_size);

} // namespace weather_acks
