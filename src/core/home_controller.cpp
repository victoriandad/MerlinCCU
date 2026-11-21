#include "home_controller.h"

#include <array>

#include "local_conditions.h"
#include "pinter_controller.h"
#include "settings_controller.h"
#include "softkey_builders.h"

namespace home_controller
{

void build_home_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    softkey::set(softkeys, SoftKeyId::Left1, softkey::navigate("STATUS", MenuPage::Status));
    softkey::set(softkeys, SoftKeyId::Left2, softkey::navigate("SHARES", MenuPage::Shares));
    softkey::set(softkeys, SoftKeyId::Left3, softkey::navigate("CALENDAR", MenuPage::Calendar));
    softkey::set(softkeys, SoftKeyId::Left4,
                 softkey::navigate(
                     pinter_controller::build_home_softkey_label(SoftKeyId::Left4, console_state),
                     MenuPage::Pinter));
    softkey::set(softkeys, SoftKeyId::Right1, softkey::navigate("SETTINGS", MenuPage::Settings));
    softkey::set(softkeys, SoftKeyId::Right2,
                 softkey::navigate(SoftKeyId::Right2, "WEATHER",
                                   settings_controller::weather_source_selection_text(console_state),
                                   MenuPage::Weather));
    softkey::set(softkeys, SoftKeyId::Right3,
                 softkey::navigate("LOCAL\nCONDITIONS", MenuPage::LocalConditions));
}

void build_local_conditions_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // One reading key per quantity, opening its graph; the value is the
    // current reading (navigate with summary). A quantity with no reading and
    // no history has nothing to open, so its key is blank (SK-5); with no
    // board at all every key is blank and the body says why. Values come from
    // the one formatter per quantity (BODY-5).
    struct ReadingKey
    {
        SoftKeyId key;
        const char* caption;
        LocalConditionMetric metric;
    };
    constexpr std::array<ReadingKey, 4> kReadingKeys = {{
        {SoftKeyId::Left1, "TEMP", LocalConditionMetric::Temperature},
        {SoftKeyId::Left2, "HUMIDITY", LocalConditionMetric::Humidity},
        {SoftKeyId::Left3, "AIR PRESSURE", LocalConditionMetric::AirPressure},
        {SoftKeyId::Left4, "VOC CHANGE", LocalConditionMetric::AirQuality},
    }};
    const auto& sensor = console_state.environment_sensor_status;
    for (const ReadingKey& reading : kReadingKeys)
    {
        if (!local_conditions::metric_has_data(reading.metric, sensor))
        {
            continue;
        }
        char value[20] = {};
        local_conditions::format_metric(reading.metric, sensor, value, sizeof(value));
        softkey::set(softkeys, reading.key,
                     softkey::navigate(reading.key, reading.caption, value,
                                       SoftKeyRoute::ShowLocalGraph,
                                       static_cast<uint8_t>(reading.metric)));
    }
}

} // namespace home_controller
