#include "home_controller.h"

#include <array>
#include <cstdio>
#include <cstring>

#include "console_controller_internal.h"
#include "home_controls.h"
#include "local_conditions.h"
#include "pinter_controller.h"
#include "screens_shared.h"
#include "settings_controller.h"
#include "softkey_builders.h"
#include "text_case.h"

namespace home_controller
{

namespace
{

namespace cci = console_controller::console_controller_internal;

/// @brief Uppercases an entity name and shortens it, ending in "..", until
/// it fits one caption line (LBL-6, LBL-7). Names come from HA, so any
/// length can arrive.
void fit_caption(const char* name, char* out, size_t out_size)
{
    constexpr text_styles::TextStyle kStyle = text_styles::TextStyle::Softkey;
    constexpr const char* kShortenedMark = "..";
    char upper[home_controls::kNameLength + 1U] = {};
    text_case::copy_uppercase(name[0] != '\0' ? name : "-", upper, sizeof(upper));
    std::snprintf(out, out_size, "%s", upper);
    int keep = static_cast<int>(std::strlen(upper));
    while (keep > 1 && !screens::softkey_label_fits(out, kStyle, 1))
    {
        --keep;
        while (keep > 1 && upper[keep - 1] == ' ')
        {
            --keep;
        }
        std::snprintf(out, out_size, "%.*s%s", keep, upper, kShortenedMark);
    }
}

} // namespace

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
    softkey::set(softkeys, SoftKeyId::Right4, softkey::navigate("CONTROLS", MenuPage::Controls));
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

void enter_controls(ConsoleState& console_state)
{
    HomeControls& controls = console_state.home_controls;
    const uint8_t pages = home_controls::page_count(controls);
    if (controls.page_index >= pages)
    {
        controls.page_index = static_cast<uint8_t>(pages - 1U);
    }
}

void build_controls_softkeys(ConsoleState& console_state, SoftKeyMap& softkeys)
{
    // One key per entity, L1-L4 then R1-R4 (NAV-6). Lights and switches are
    // toggles showing On or Off; scenes and scripts are actions named for
    // themselves. A key that cannot act now, waiting for HA or for an
    // unavailable entity, is a status key saying why (SK-5). With no link to
    // HA there are no keys at all and the body says why.
    const HomeControls& controls = console_state.home_controls;
    if (!controls.link_up || !controls.received)
    {
        return;
    }
    const size_t first = static_cast<size_t>(controls.page_index) * home_controls::kControlsPerPage;
    for (size_t slot = 0U; slot < home_controls::kControlsPerPage; ++slot)
    {
        const size_t index = first + slot;
        if (index >= controls.count)
        {
            break;
        }
        const HomeControl& item = controls.items[index];
        const SoftKeyId key = softkey::kListSlots[slot];
        char caption[home_controls::kNameLength + 3U] = {};
        fit_caption(item.name.data(), caption, sizeof(caption));
        const bool run = item.kind == ControlKind::Run;
        if (controls.pending == index)
        {
            softkey::set(softkeys, key,
                         softkey::status(key, caption, run ? "Running" : "Switching"));
        }
        else if (item.state == ControlState::Unavailable)
        {
            softkey::set(softkeys, key, softkey::status(key, caption, "Offline"));
        }
        else if (run)
        {
            size_t capacity = 0U;
            char* label = cci::dynamic_softkey_label_buffer(key, capacity);
            std::snprintf(label, capacity, "%s", caption);
            softkey::set(softkeys, key,
                         softkey::action(label, SoftKeyRoute::PressHomeControl,
                                         static_cast<uint8_t>(slot)));
        }
        else
        {
            softkey::set(softkeys, key,
                         softkey::toggle(key, caption,
                                         item.state == ControlState::On ? "On" : "Off",
                                         SoftKeyRoute::PressHomeControl,
                                         static_cast<uint8_t>(slot)));
        }
    }
}

} // namespace home_controller
