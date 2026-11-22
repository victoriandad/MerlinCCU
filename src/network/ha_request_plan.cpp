#include "ha_request_plan.h"

namespace ha_request_plan
{

namespace
{

/// @brief Returns whether a request is wanted by the plan, ignoring order.
bool wanted(RequestKind kind, const Plan& plan)
{
    switch (kind)
    {
    case RequestKind::ProbeApi:
        return plan.home_assistant;
    case RequestKind::FetchTrackedEntity:
        return plan.home_assistant && plan.tracked_entity;
    case RequestKind::FetchWeatherEntity:
    case RequestKind::FetchWeatherForecast:
    case RequestKind::FetchWeatherDailyForecast:
        return plan.home_assistant && plan.home_assistant_weather;
    case RequestKind::FetchSunEntity:
        return plan.home_assistant && plan.home_assistant_weather && plan.sun_entity;
    case RequestKind::PublishSelfEntity:
        return plan.home_assistant && plan.self_entity;
    case RequestKind::FetchOpenMeteo:
        return plan.open_meteo;
    }
    return false;
}

/// @brief Every request in chain order; each chain's requests are contiguous.
constexpr RequestKind kOrder[] = {
    RequestKind::ProbeApi,
    RequestKind::FetchTrackedEntity,
    RequestKind::FetchWeatherEntity,
    RequestKind::FetchWeatherForecast,
    RequestKind::FetchWeatherDailyForecast,
    RequestKind::FetchSunEntity,
    RequestKind::PublishSelfEntity,
    RequestKind::FetchOpenMeteo,
};

constexpr int kOrderCount = static_cast<int>(sizeof(kOrder) / sizeof(kOrder[0]));

/// @brief Returns the first wanted request at or after `start` in the same chain.
std::optional<RequestKind> first_wanted_from(int start, Chain chain, const Plan& plan)
{
    for (int i = start; i < kOrderCount; ++i)
    {
        if (chain_of(kOrder[i]) == chain && wanted(kOrder[i], plan))
        {
            return kOrder[i];
        }
    }
    return std::nullopt;
}

} // namespace

bool chain_active(Chain chain, const Plan& plan)
{
    return first_request(chain, plan).has_value();
}

std::optional<RequestKind> first_request(Chain chain, const Plan& plan)
{
    return first_wanted_from(0, chain, plan);
}

std::optional<RequestKind> next_request(RequestKind current, const Plan& plan)
{
    for (int i = 0; i < kOrderCount; ++i)
    {
        if (kOrder[i] == current)
        {
            return first_wanted_from(i + 1, chain_of(current), plan);
        }
    }
    return std::nullopt;
}

} // namespace ha_request_plan
