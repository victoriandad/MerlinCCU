#include "page_table.h"

#include <array>
#include <cstddef>

#include "alert_controller.h"
#include "alert_screens.h"
#include "calendar_controller.h"
#include "calendar_screens.h"
#include "home_controller.h"
#include "local_conditions_screens.h"
#include "pinter_controller.h"
#include "pinter_screens.h"
#include "screens.h"
#include "screens_shared.h"
#include "settings_controller.h"
#include "settings_screens.h"
#include "shares_screens.h"
#include "status_controller.h"
#include "status_screens.h"
#include "weather_screens.h"

namespace page_table
{

namespace
{

using cursor_navigation::kAlertDetailScroll;
using cursor_navigation::kAlertListPages;
using cursor_navigation::kBrewCataloguePages;
using cursor_navigation::kCalendarDay;
using cursor_navigation::kSettingsPages;
using cursor_navigation::kTimeZonePages;
using text_styles::TextStyle;

/// @brief Adapts a renderer that does not show data age to PageRenderer.
template <void (*Draw)(uint8_t*, const ConsoleState&)>
void without_time(uint8_t* fb, const ConsoleState& console_state, uint32_t /*now_ms*/)
{
    Draw(fb, console_state);
}

/// @brief The default softkey font, 8x12, used by every page but those below.
constexpr TextStyle kStd = TextStyle::Softkey;

/// @brief The dense 5x7 opt-in (TXT-2), for pages whose labels cannot fit at
/// 8x12. Only the Pinter page uses it: its vessel keys need five lines, and
/// five 8x12 lines are taller than the 57-pixel key pitch. Listed in
/// docs/ui_requirements.md.
constexpr TextStyle kDense = TextStyle::SoftkeyDense;

/// @brief Lines a softkey label may wrap onto on most pages.
constexpr uint8_t kLines = 2U;

/// @brief The brew catalogue's item keys allow a two-line caption over their
/// value, so long recipe names such as "Adnams Ghost Ship Remixed" keep every
/// word at 8x12 (LBL-1 lets row 1-4 item keys use more lines). Row 5 holds no
/// items on that page.
constexpr uint8_t kItemLines = 3U;

/// @brief The Pinter page allows five lines, so each vessel key can carry its
/// name, a recipe wrapped over up to three lines, and the stage with its
/// countdown (LBL-1). A five-line block still ends above the next key's
/// label at the 57-pixel key pitch; row 5 keeps one or two lines.
constexpr uint8_t kPinterLines = static_cast<uint8_t>(screens::kMaxSoftkeyLabelLines);

constexpr PageType kKey = PageType::Key;
constexpr PageType kBody = PageType::Body;
constexpr PageType kDiagnostic = PageType::Diagnostic;

constexpr Refresh kOnChange = Refresh::OnChange;
constexpr Refresh kDataAge = Refresh::DataAge;

constexpr ShareFetch kNoFetch = ShareFetch::None;

// clang-format off
/// @brief One entry per page, in MenuPage order (checked below).
/// Columns: page, title, parent, type, label style, label lines, softkey
/// builder, body renderer, on_enter hook, cursor, refresh, share fetch.
constexpr std::array<PageDescriptor, kMenuPageCount> kPages = {{
    {MenuPage::Home, "HOME", MenuPage::Home, kKey, kStd, kLines,
     home_controller::build_home_softkeys, screens::draw_home_page,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::Calendar, "CALENDAR", MenuPage::Home, kKey, kStd, kLines,
     calendar_controller::build_calendar_softkeys,
     without_time<calendar_screens::draw_calendar_page>,
     nullptr, &kCalendarDay, kOnChange, kNoFetch},
    {MenuPage::CalendarDetail, "EVENT", MenuPage::Calendar, kBody, kStd, kLines,
     nullptr, without_time<calendar_screens::draw_calendar_detail_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::Weather, "WEATHER", MenuPage::Home, kBody, kStd, kLines,
     settings_controller::build_weather_softkeys, weather_screens::draw_weather_page,
     nullptr, nullptr, kDataAge, kNoFetch},
    {MenuPage::Status, "STATUS", MenuPage::Home, kKey, kStd, kLines,
     status_controller::build_status_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::StatusOverview, "OVERVIEW", MenuPage::Status, kBody, kStd, kLines,
     nullptr, status_screens::draw_status_page,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::StatusConnectivity, "CONNECT", MenuPage::Status, kBody, kStd, kLines,
     nullptr, status_screens::draw_status_page,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::StatusResources, "RESOURCES", MenuPage::Status, kBody, kStd, kLines,
     nullptr, status_screens::draw_status_page,
     nullptr, nullptr, Refresh::Telemetry, kNoFetch},
    {MenuPage::StatusSensors, "SENSORS", MenuPage::Status, kBody, kStd, kLines,
     nullptr, status_screens::draw_status_page,
     nullptr, nullptr, kDataAge, kNoFetch},
    {MenuPage::StatusIntegrations, "INTEGRATIONS", MenuPage::Status, kBody, kStd, kLines,
     nullptr, status_screens::draw_status_page,
     nullptr, nullptr, kDataAge, kNoFetch},
    {MenuPage::LocalConditions, "LOCAL CONDITIONS", MenuPage::Home, kKey, kStd, kLines,
     home_controller::build_local_conditions_softkeys,
     local_conditions_screens::draw_local_conditions_page,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::LocalConditionGraph, "LOCAL GRAPH", MenuPage::LocalConditions, kBody, kStd, kLines,
     nullptr, local_conditions_screens::draw_local_condition_history_graph,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::Settings, "SETTINGS", MenuPage::Home, kKey, kStd, kLines,
     settings_controller::build_settings_softkeys, nullptr,
     settings_controller::enter_settings, &kSettingsPages, kOnChange, kNoFetch},
    {MenuPage::DeviceSettings, "DEVICE IDENTITY", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_device_softkeys,
     without_time<settings_screens::draw_device_identity_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::WifiSettings, "NETWORK", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_wifi_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::HomeAssistantSettings, "HOME ASSISTANT", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_home_assistant_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::MqttSettings, "MQTT DISCOVERY", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_mqtt_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::ScreenSaverSettings, "SCREEN SAVER", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_screen_saver_softkeys,
     without_time<settings_screens::draw_screen_saver_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::ScreenSaverOptions, "SAVER", MenuPage::ScreenSaverSettings, kKey, kStd, kLines,
     settings_controller::build_screen_saver_options_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::WeatherSources, "WEATHER SOURCE", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_weather_sources_softkeys, nullptr,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::TimeZoneSettings, "TIME ZONE", MenuPage::Settings, kKey, kStd, kLines,
     settings_controller::build_time_zone_softkeys, nullptr,
     settings_controller::enter_time_zone_settings, &kTimeZonePages, kOnChange, kNoFetch},
    {MenuPage::KeypadDebug, "KEYPAD DEBUG", MenuPage::Status, kDiagnostic, kStd, kLines,
     nullptr, without_time<settings_screens::draw_keypad_debug_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::GreyscaleTest, "GREYSCALE", MenuPage::Status, kDiagnostic, kStd, kLines,
     nullptr, without_time<settings_screens::draw_greyscale_test_card>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::AlertList, "ALERTS", MenuPage::Home, kKey, kStd, kLines,
     alert_controller::build_list_softkeys, without_time<alert_screens::draw_alert_list_page>,
     nullptr, &kAlertListPages, kOnChange, kNoFetch},
    {MenuPage::AlertDetail, "ALERT", MenuPage::AlertList, kBody, kStd, kLines,
     alert_controller::build_detail_softkeys, without_time<alert_screens::draw_alert_detail_page>,
     nullptr, &kAlertDetailScroll, kOnChange, kNoFetch},
    {MenuPage::Pinter, "PINTER", MenuPage::Home, kKey, kDense, kPinterLines,
     pinter_controller::build_pinter_softkeys, without_time<pinter_screens::draw_pinter_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::PinterSelectBrew, "SELECT BREW", MenuPage::Pinter, kKey, kStd, kItemLines,
     pinter_controller::build_select_brew_softkeys, nullptr,
     pinter_controller::enter_select_brew, &kBrewCataloguePages, kOnChange, kNoFetch},
    {MenuPage::PinterStartTiming, "BREW TIMING", MenuPage::PinterSelectBrew, kKey, kStd, kLines,
     pinter_controller::build_start_timing_softkeys,
     without_time<pinter_screens::draw_pinter_page>,
     nullptr, nullptr, kOnChange, kNoFetch},
    {MenuPage::Shares, "SHARES", MenuPage::Home, kKey, kStd, kLines,
     settings_controller::build_shares_softkeys, without_time<shares_screens::draw_shares_page>,
     nullptr, nullptr, kDataAge, ShareFetch::Today},
    {MenuPage::ShareDetail, "SHARE", MenuPage::Shares, kBody, kStd, kLines,
     settings_controller::build_share_detail_softkeys, shares_screens::draw_share_detail_page,
     nullptr, nullptr, kDataAge, ShareFetch::SelectedPeriod},
}};
// clang-format on

/// @brief True when every entry sits at its own page's index, so a page
/// added to MenuPage without an entry, or out of order, fails the build.
constexpr bool entries_in_page_order()
{
    for (size_t i = 0U; i < kPages.size(); ++i)
    {
        if (static_cast<size_t>(kPages[i].page) != i)
        {
            return false;
        }
    }
    return true;
}
static_assert(entries_in_page_order(), "page_table::kPages must list every MenuPage in order");

} // namespace

const PageDescriptor& describe(MenuPage page)
{
    const size_t index = static_cast<size_t>(page);
    return kPages[index < kPages.size() ? index : 0U];
}

MenuPage parent_page(const ConsoleState& console_state)
{
    // The Alert list is opened by the ALERT hard key from any page, so it
    // returns to whichever page that was.
    if (console_state.active_page == MenuPage::AlertList)
    {
        return console_state.alert_parent_page;
    }
    return describe(console_state.active_page).parent;
}

bool open(ConsoleState& console_state, MenuPage page)
{
    if (static_cast<size_t>(page) >= kPages.size())
    {
        return false;
    }
    (void)settings_controller::stop_timeout_editing(console_state);
    const PageDescriptor& descriptor = describe(page);
    if (descriptor.on_enter != nullptr)
    {
        descriptor.on_enter(console_state);
    }
    console_state.active_page = page;
    return true;
}

} // namespace page_table
