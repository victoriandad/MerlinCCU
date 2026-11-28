#include "weather_acks.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "alert_controller.h"
#include "alert_history.h"
#include "test_framework.h"

namespace
{

/// @brief A healthy state with an amber official warning in force.
ConsoleState with_warning(const char* detail)
{
    alert_controller::reset();
    alert_history::reset();
    ConsoleState state = {};
    state.wifi_status.state = WifiConnectionState::Connected;
    state.time_status.synced = true;
    state.time_status.local_epoch_day = 20784U;
    alert_controller::sync(state);
    state.weather_warnings.level = WarningLevel::Amber;
    std::snprintf(state.weather_warnings.summary.data(), state.weather_warnings.summary.size(),
                  "AMBER RAIN");
    std::snprintf(state.weather_warnings.detail.data(), state.weather_warnings.detail.size(), "%s",
                  detail);
    return state;
}

bool raised(const ConsoleState& state, AlertCode code)
{
    for (uint8_t i = 0U; i < state.alert_count; ++i)
    {
        if (state.active_alerts[i].code == static_cast<uint8_t>(code))
        {
            return true;
        }
    }
    return false;
}

} // namespace

HOST_TEST(weather_ack_ids_follow_their_key)
{
    const weather_acks::Id a = weather_acks::id_for("wind|20784");
    EXPECT_EQ(std::strlen(a.data()), weather_acks::kIdLength);
    EXPECT_TRUE(a == weather_acks::id_for("wind|20784"));
    EXPECT_FALSE(a == weather_acks::id_for("wind|20785"));
    // `more` is the key's tail after a newline.
    EXPECT_TRUE(weather_acks::id_for("a", "b") == weather_acks::id_for("a\nb"));
    // CRC-32 of "123456789", the standard check value.
    EXPECT_TRUE(std::string(weather_acks::id_for("123456789").data()) == "cbf43926");
}

HOST_TEST(weather_acks_payload_is_read_and_an_ack_written)
{
    weather_acks::Accepted accepted = {};
    EXPECT_TRUE(weather_acks::apply(
        "{\"v\":1,\"official\":\"1a2b3c4d\",\"wx_wind\":\"0000beef\",\"wx_temp\":\"nothex!!\"}",
        accepted));
    EXPECT_TRUE(std::string(accepted[0].data()) == "1a2b3c4d");
    EXPECT_TRUE(std::string(accepted[3].data()) == "0000beef");
    EXPECT_EQ(accepted[2][0], '\0');
    EXPECT_EQ(accepted[1][0], '\0');

    EXPECT_FALSE(weather_acks::apply("{\"v\":2,\"official\":\"ffffffff\"}", accepted));
    EXPECT_TRUE(std::string(accepted[0].data()) == "1a2b3c4d");

    char out[96] = {};
    EXPECT_TRUE(weather_acks::format_ack(weather_acks::Kind::Temperature,
                                         weather_acks::id_for("temp:cold|1"), "merlinccu2", out,
                                         sizeof(out)));
    EXPECT_TRUE(std::strstr(out, "{\"alert\":\"wx_temp\",\"id\":\"") == out);
    EXPECT_TRUE(std::strstr(out, "\"unit\":\"merlinccu2\"}") != nullptr);
}

HOST_TEST(a_warning_accepted_on_another_unit_clears_here_and_a_new_one_is_raised)
{
    ConsoleState state = with_warning("Heavy rain until 18:00.");
    alert_controller::sync(state);
    EXPECT_TRUE(raised(state, AlertCode::OfficialWeatherWarning));
    const weather_acks::Id id = alert_controller::weather_ack_id(weather_acks::Kind::Official);
    EXPECT_EQ(std::strlen(id.data()), weather_acks::kIdLength);

    // HA lists it as accepted: it leaves the list, into History as accepted.
    state.weather_warnings.acked[static_cast<size_t>(weather_acks::Kind::Official)] = id;
    alert_controller::sync(state);
    EXPECT_FALSE(raised(state, AlertCode::OfficialWeatherWarning));
    EXPECT_EQ(alert_history::count(), 1U);
    EXPECT_TRUE(alert_history::entry(0).ending == alert_history::Ending::Accepted);

    // HA republishing the same warnings changes nothing.
    alert_controller::sync(state);
    EXPECT_FALSE(raised(state, AlertCode::OfficialWeatherWarning));

    // A changed warning is a new id, raised again.
    std::snprintf(state.weather_warnings.detail.data(), state.weather_warnings.detail.size(),
                  "Heavy rain until 22:00.");
    alert_controller::sync(state);
    EXPECT_TRUE(raised(state, AlertCode::OfficialWeatherWarning));
    alert_history::reset();
}

HOST_TEST(accepting_a_weather_alert_here_offers_its_id_for_ha)
{
    ConsoleState state = with_warning("Heavy rain until 18:00.");
    alert_controller::sync(state);
    EXPECT_FALSE(alert_controller::acknowledged(AlertCode::OfficialWeatherWarning));
    alert_controller::accept_alert(state, 0U);
    alert_controller::sync(state);
    // Still in force here, accepted: mqtt_manager sends the id until HA lists it.
    EXPECT_TRUE(alert_controller::acknowledged(AlertCode::OfficialWeatherWarning));
    EXPECT_TRUE(alert_controller::weather_ack_id(weather_acks::Kind::Official)[0] != '\0');
    EXPECT_TRUE(alert_controller::weather_ack_code(weather_acks::Kind::Wind) ==
                AlertCode::WeatherWindWarning);

    // Once the warnings end there is nothing to send.
    state.weather_warnings.level = WarningLevel::None;
    alert_controller::sync(state);
    EXPECT_EQ(alert_controller::weather_ack_id(weather_acks::Kind::Official)[0], '\0');
    alert_history::reset();
}
