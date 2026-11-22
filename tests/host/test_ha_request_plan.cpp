#include "ha_request_plan.h"

#include <vector>

#include "test_framework.h"

namespace
{

using ha_request_plan::Chain;
using ha_request_plan::Plan;
using ha_request_plan::RequestKind;

/// @brief Walks one chain from its first request to the end of its cycle.
std::vector<RequestKind> walk(Chain chain, const Plan& plan)
{
    std::vector<RequestKind> requests;
    std::optional<RequestKind> kind = ha_request_plan::first_request(chain, plan);
    while (kind.has_value())
    {
        requests.push_back(*kind);
        kind = ha_request_plan::next_request(*kind, plan);
    }
    return requests;
}

Plan home_assistant_plan()
{
    Plan plan;
    plan.home_assistant = true;
    plan.tracked_entity = true;
    plan.self_entity = true;
    plan.sun_entity = true;
    return plan;
}

} // namespace

HOST_TEST(ha_plan_runs_presence_and_tracked_entity_with_open_meteo_weather)
{
    // #165: with Open-Meteo weather, HA still gets probed, the tracked
    // entity fetched and the self entity published.
    Plan plan = home_assistant_plan();
    plan.open_meteo = true;

    const std::vector<RequestKind> expected = {RequestKind::ProbeApi,
                                               RequestKind::FetchTrackedEntity,
                                               RequestKind::PublishSelfEntity};
    EXPECT_TRUE(walk(Chain::HomeAssistant, plan) == expected);
    EXPECT_TRUE(walk(Chain::OpenMeteo, plan) ==
                std::vector<RequestKind>{RequestKind::FetchOpenMeteo});
}

HOST_TEST(ha_plan_fetches_ha_weather_in_order_when_ha_is_the_source)
{
    Plan plan = home_assistant_plan();
    plan.home_assistant_weather = true;

    const std::vector<RequestKind> expected = {
        RequestKind::ProbeApi,           RequestKind::FetchTrackedEntity,
        RequestKind::FetchWeatherEntity, RequestKind::FetchWeatherForecast,
        RequestKind::FetchWeatherDailyForecast, RequestKind::FetchSunEntity,
        RequestKind::PublishSelfEntity};
    EXPECT_TRUE(walk(Chain::HomeAssistant, plan) == expected);
    EXPECT_FALSE(ha_request_plan::chain_active(Chain::OpenMeteo, plan));
}

HOST_TEST(ha_plan_skips_optional_requests_that_are_not_set)
{
    Plan plan;
    plan.home_assistant = true;
    EXPECT_TRUE(walk(Chain::HomeAssistant, plan) ==
                std::vector<RequestKind>{RequestKind::ProbeApi});

    // The sun entity only comes with HA weather.
    plan.sun_entity = true;
    EXPECT_TRUE(walk(Chain::HomeAssistant, plan) ==
                std::vector<RequestKind>{RequestKind::ProbeApi});
}

HOST_TEST(ha_plan_open_meteo_runs_without_home_assistant)
{
    Plan plan;
    plan.open_meteo = true;
    plan.tracked_entity = true; // ignored while HA itself is not set up

    EXPECT_FALSE(ha_request_plan::chain_active(Chain::HomeAssistant, plan));
    EXPECT_TRUE(walk(Chain::OpenMeteo, plan) ==
                std::vector<RequestKind>{RequestKind::FetchOpenMeteo});
}

HOST_TEST(ha_plan_with_nothing_set_up_runs_nothing)
{
    const Plan plan;
    EXPECT_FALSE(ha_request_plan::chain_active(Chain::HomeAssistant, plan));
    EXPECT_FALSE(ha_request_plan::chain_active(Chain::OpenMeteo, plan));
}

HOST_TEST(ha_plan_requests_belong_to_their_own_chain)
{
    EXPECT_TRUE(ha_request_plan::chain_of(RequestKind::FetchOpenMeteo) == Chain::OpenMeteo);
    EXPECT_TRUE(ha_request_plan::chain_of(RequestKind::FetchWeatherEntity) ==
                Chain::HomeAssistant);
    // The last HA request never hands over to the Open-Meteo chain.
    Plan plan = home_assistant_plan();
    plan.open_meteo = true;
    EXPECT_FALSE(ha_request_plan::next_request(RequestKind::PublishSelfEntity, plan).has_value());
}
