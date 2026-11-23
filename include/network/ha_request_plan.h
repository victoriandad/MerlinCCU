#pragma once

#include <cstdint>
#include <optional>

/// @brief Which requests the shared Home Assistant / weather HTTP client
/// makes, and in what order (#165).
/// @details One client and one response buffer serve two independent chains
/// of requests:
/// - the Home Assistant chain: API probe, tracked entity and the self-entity
///   publish;
/// - the Open-Meteo chain: one forecast fetch, when Open-Meteo is the
///   weather source.
/// Weather from Home Assistant arrives over MQTT instead (#153), so it adds
/// no requests here. Each chain has its own schedule and status, so one
/// failing never stops or marks down the other. This header is pure logic so
/// host tests can cover every combination of settings.
namespace ha_request_plan
{

/// @brief One HTTP request the client can make.
enum class RequestKind : uint8_t
{
    ProbeApi = 0,
    FetchTrackedEntity,
    PublishSelfEntity,
    FetchOpenMeteo,
};

/// @brief The two request chains sharing the client.
enum class Chain : uint8_t
{
    HomeAssistant = 0,
    OpenMeteo,
};

inline constexpr uint8_t kChainCount = 2U;

/// @brief Which requests the current settings call for.
struct Plan
{
    /// HA REST is enabled, with a valid host and a token.
    bool home_assistant = false;
    bool tracked_entity = false;
    bool self_entity = false;
    /// Open-Meteo is the weather source and the saved location is valid.
    bool open_meteo = false;
};

/// @brief Returns the chain a request belongs to.
constexpr Chain chain_of(RequestKind kind)
{
    return kind == RequestKind::FetchOpenMeteo ? Chain::OpenMeteo : Chain::HomeAssistant;
}

/// @brief Returns whether the plan runs a chain at all.
bool chain_active(Chain chain, const Plan& plan);

/// @brief Returns the first request of a chain, or nothing if the chain is inactive.
std::optional<RequestKind> first_request(Chain chain, const Plan& plan);

/// @brief Returns the request after `current` in its chain, or nothing when
/// the chain's cycle is complete.
/// @details The HA chain probes the API first, then makes the optional
/// requests in priority order.
std::optional<RequestKind> next_request(RequestKind current, const Plan& plan);

} // namespace ha_request_plan
