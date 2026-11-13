#include "console_controller.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

#include "alert_controller.h"
#include "alert_ordering.h"
#include "alert_screens.h"
#include "calendar_controller.h"
#include "console_controller_internal.h"
#include "debug_logging.h"
#include "local_conditions.h"
#include "pinter_controller.h"
#include "pinter_scheduling.h"
#include "settings_controller.h"
#include "softkey_builders.h"
#include "status_controller.h"
#include "text_case.h"

namespace console_controller
{

namespace
{  /// @todo what is the purpose of this namespace, we are already inside namespace console_controller?

// The console controller owns the user-facing aggregate state. Subsystem
// managers push snapshots into it, while button events mutate the menu and
// annunciator model from one central place.
ConsoleState g_console_state = {};
bool g_redraw_requested = false;
bool g_user_activity_requested = false;
// Sized for the longest Pinter vessel label: title + "[recipe]" (recipe names
// run up to ~42 characters) + "[stage Nd, ready dd Mon]".
constexpr size_t kSoftkeyLabelCapacity = 112;
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_labels = {};
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_overrides = {};
std::array<bool, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_override_active = {};

void update_softkeys_from_state();
void update_lamps_from_state();
bool button_digit_value(ButtonId id, uint8_t* out_digit);

// Brew Timing duration limits, in days, for a brew started from the panel.
constexpr uint8_t kPinterMinBrewDays = 1U;
constexpr uint8_t kPinterMaxBrewDays = 30U;
constexpr uint8_t kPinterMinConditioningDays = 1U;
constexpr uint8_t kPinterMaxConditioningDays = 45U;
constexpr uint8_t kPinterMinColdCrashDays = 0U;
constexpr uint8_t kPinterMaxColdCrashDays = 3U;

/// @brief List slots in fill order, L1-L4 then R1-R4 (NAV-6).
constexpr std::array<SoftKeyId, 8> kListSlotSoftkeys = {
    SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,  SoftKeyId::Left4,
    SoftKeyId::Right1, SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};

constexpr std::array<SoftKeyRoute, kPinterBrewListVisibleCount> kPinterBrewListRoutes = {
    SoftKeyRoute::SelectPinterListItem1, SoftKeyRoute::SelectPinterListItem2,
    SoftKeyRoute::SelectPinterListItem3, SoftKeyRoute::SelectPinterListItem4,
    SoftKeyRoute::SelectPinterListItem5, SoftKeyRoute::SelectPinterListItem6,
    SoftKeyRoute::SelectPinterListItem7, SoftKeyRoute::SelectPinterListItem8};

/// @brief Converts a lamp enum into a stable array index.
/// @details The controller stores lamp state in dense arrays so the UI update path can avoid
/// repeated switch statements when it touches annunciator data.
constexpr size_t lamp_index(LampId lamp)
{
    return static_cast<size_t>(lamp);
}

/// @brief Converts a softkey enum into a stable array index.
/// @details Softkey labels, routes, and override state all share enum-ordered arrays so one
/// index helper keeps the mapping explicit and consistent.
constexpr size_t softkey_index(SoftKeyId key)
{
    return static_cast<size_t>(key);
}

/// @brief Applies any temporary softkey label overrides onto a page map.
void apply_softkey_label_overrides(SoftKeyMap& softkeys)
{
    // Overrides are applied last so page defaults remain the single source of
    // truth unless a diagnostics or test flow deliberately replaces a label.
    for (size_t i = 0; i < g_softkey_label_override_active.size(); ++i)
    {
        if (!g_softkey_label_override_active[i])
        {
            continue;
        }

        softkeys[i].label = g_softkey_label_overrides[i].data();
    }
}

} // namespace

// Exposed (not anonymous) so feature modules split out of this file, such as
// pinter_controller.cpp, can share the same softkey-label storage and
// formatting rules -- see console_controller_internal.h.
namespace console_controller_internal
{

/// @details The controller owns these buffers so menu pages can rebuild labels
/// whenever integration state changes without leaving dangling pointers behind.
const char* build_selection_softkey_label(SoftKeyId key, const char* title, const char* selection)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* value = (selection != nullptr && selection[0] != '\0') ? selection : "-";
    constexpr size_t kSoftkeyTitleBufferSize = 24U;
    char title_upper[kSoftkeyTitleBufferSize] = {};
    text_case::copy_uppercase(title, title_upper, sizeof(title_upper));
    std::snprintf(buffer.data(), buffer.size(), "%s\n[%s]", title_upper, value);
    return buffer.data();
}

char* dynamic_softkey_label_buffer(SoftKeyId key, size_t& out_capacity)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    out_capacity = buffer.size();
    return buffer.data();
}

bool keypad_digit_value(ButtonId id, uint8_t* out_digit)
{
    return button_digit_value(id, out_digit);
}

} // namespace console_controller_internal

namespace
{

// The rest of this file's dispatch code (update_softkeys_from_state(),
// apply_softkey_route(), and their helpers) builds labels via these
// unqualified for brevity, exactly as before console_controller_internal was
// split out into its own namespace.
using console_controller_internal::build_selection_softkey_label;

/// @brief Returns the parent page for one menu route in the current hierarchy.
MenuPage parent_page(MenuPage page)
{
    switch (page)
    {
    case MenuPage::Home:
    case MenuPage::Weather:
    case MenuPage::AirTraffic:
    case MenuPage::Calendar:
    case MenuPage::Status:
    case MenuPage::LocalConditions:
    case MenuPage::Settings:
    case MenuPage::Pinter:
    case MenuPage::Shares:
        return MenuPage::Home;
    case MenuPage::PinterSelectBrew:
        return MenuPage::Pinter;
    case MenuPage::PinterStartTiming:
        return MenuPage::PinterSelectBrew;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
        return MenuPage::Status;
    case MenuPage::DeviceSettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::AirTrafficSettings:
    case MenuPage::ScreenSaverSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
        return MenuPage::Settings;
    case MenuPage::ScreenSaverOptions:
        return MenuPage::ScreenSaverSettings;
    case MenuPage::AlertList:
        return g_console_state.alert_parent_page;
    case MenuPage::AlertDetail:
        return MenuPage::AlertList;
    case MenuPage::CalendarDetail:
        return MenuPage::Calendar;
    case MenuPage::ShareDetail:
        return MenuPage::Shares;
    case MenuPage::LocalConditionGraph:
        return MenuPage::LocalConditions;
    }

    return MenuPage::Home;
}

/// @brief Moves the active page one level up the menu hierarchy.
bool navigate_up_one_level()
{
    const MenuPage kParentPage = parent_page(g_console_state.active_page);
    if (kParentPage == g_console_state.active_page)
    {
        return false;
    }

    g_console_state.active_page = kParentPage;
    return true;
}

/// @brief Returns the compact label for the current LTRS interpretation mode.
const char* letter_mode_text(LetterMode mode)
{
    switch (mode)
    {
    case LetterMode::UpperCase:
        return "ABC";
    case LetterMode::LowerCase:
        return "abc";
    case LetterMode::Numbers:
        return "123";
    }

    return "ABC";
}

/// @brief Advances the LTRS input mode through upper, lower, and numeric entry.
LetterMode next_letter_mode(LetterMode mode)
{
    switch (mode)
    {
    case LetterMode::UpperCase:
        return LetterMode::Numbers;
    case LetterMode::Numbers:
        return LetterMode::LowerCase;
    case LetterMode::LowerCase:
        return LetterMode::UpperCase;
    }

    return LetterMode::UpperCase;
}

/// @brief Cycles the shared LTRS input mode and reports the state change.
bool cycle_letter_mode()
{
    g_console_state.letter_mode = next_letter_mode(g_console_state.letter_mode);
    return true;
}

/// @brief Returns true when the current LTRS mode maps a button to a digit.
bool button_digit_value(ButtonId id, uint8_t* out_digit)
{
    if (out_digit == nullptr)
    {
        return false;
    }

    if (g_console_state.letter_mode != LetterMode::Numbers)
    {
        return false;
    }

    switch (id)
    {
    case ButtonId::AlphaJ:
        *out_digit = 1;
        return true;
    case ButtonId::AlphaK:
        *out_digit = 2;
        return true;
    case ButtonId::AlphaL:
        *out_digit = 3;
        return true;
    case ButtonId::AlphaP:
        *out_digit = 4;
        return true;
    case ButtonId::AlphaQ:
        *out_digit = 5;
        return true;
    case ButtonId::AlphaR:
        *out_digit = 6;
        return true;
    case ButtonId::AlphaV:
        *out_digit = 7;
        return true;
    case ButtonId::AlphaW:
        *out_digit = 8;
        return true;
    case ButtonId::AlphaX:
        *out_digit = 9;
        return true;
    case ButtonId::Zero:
        *out_digit = 0;
        return true;
    default:
        break;
    }

    return false;
}

/// @brief Maps a physical alpha key to its alphabetical character.
bool alpha_character_from_button(ButtonId id, char* out_character)
{
    if (out_character == nullptr)
    {
        return false;
    }

    // The `AlphaA..AlphaZ` enum values are intentionally contiguous to mirror
    // the physical keypad block. That keeps the conversion table-free while
    // still preserving the physical key identifiers elsewhere in the model.
    const uint8_t value = static_cast<uint8_t>(id);
    const uint8_t kFirstAlpha = static_cast<uint8_t>(ButtonId::AlphaA);
    const uint8_t kLastAlpha = static_cast<uint8_t>(ButtonId::AlphaZ);
    if (value < kFirstAlpha || value > kLastAlpha)
    {
        return false;
    }

    *out_character = static_cast<char>('A' + (value - kFirstAlpha));
    return true;
}

/// @brief Resolves one printable key according to the active LTRS mode.
bool text_character_from_button(ButtonId id, char* out_character)
{
    if (out_character == nullptr)
    {
        return false;
    }

    uint8_t digit = 0U;
    if (button_digit_value(id, &digit))
    {
        *out_character = static_cast<char>('0' + digit);
        return true;
    }

    switch (id)
    {
    case ButtonId::Slash:
        *out_character = '/';
        return true;
    case ButtonId::Dot:
        *out_character = '.';
        return true;
    case ButtonId::Spc:
        *out_character = ' ';
        return true;
    case ButtonId::Zero:
        if (g_console_state.letter_mode == LetterMode::Numbers)
        {
            *out_character = '0';
            return true;
        }
        return false;
    default:
        break;
    }

    if (g_console_state.letter_mode == LetterMode::Numbers)
    {
        return false;
    }

    char alpha = '\0';
    if (!alpha_character_from_button(id, &alpha))
    {
        return false;
    }

    if (g_console_state.letter_mode == LetterMode::LowerCase)
    {
        alpha = static_cast<char>(alpha - 'A' + 'a');
    }
    *out_character = alpha;
    return true;
}

/// @brief Maps a physical bezel button to its logical softkey slot.
SoftKeyId softkey_id_from_button(ButtonId button)
{
    if (static_cast<uint8_t>(button) > static_cast<uint8_t>(ButtonId::RightBottom))
    {
        return SoftKeyId::Left1;
    }

    return static_cast<SoftKeyId>(static_cast<uint8_t>(button));
}

/// @brief Returns whether the input event belongs to one of the ten softkeys.
bool button_maps_to_softkey(ButtonId button)
{
    return static_cast<uint8_t>(button) <= static_cast<uint8_t>(ButtonId::RightBottom);
}

/// @brief Rebuilds the current softkey map from the active console state.
void update_softkeys_from_state()
{
    alert_controller::sync(g_console_state);

    const uint8_t air_traffic_pages = settings_controller::air_traffic_page_count(g_console_state);
    if (g_console_state.air_traffic_page_index >= air_traffic_pages)
    {
        g_console_state.air_traffic_page_index = static_cast<uint8_t>(air_traffic_pages - 1U);
    }

    if (g_console_state.active_page != MenuPage::Pinter)
    {
        g_console_state.pinter_reset_confirming = false;
    }

    SoftKeyMap softkeys = {};
    softkeys.fill(softkey::blank());
    const auto set_key = [&softkeys](SoftKeyId key, const SoftKeyAction& action)
    { softkeys[softkey_index(key)] = action; };
    const RuntimeConfig& config = config_manager::settings();

    // Each page declares only the actions that make sense in that context,
    // and every key is built by its role's builder (SK-1, SK-2). The top-level
    // menus are intentionally sparse, while deeper pages reserve `R5` as a
    // consistent one-press jump home.
    switch (g_console_state.active_page)
    {
    case MenuPage::Home:
        set_key(SoftKeyId::Left1, softkey::navigate("STATUS", SoftKeyRoute::GoStatus));
        set_key(SoftKeyId::Left2, softkey::navigate("SHARES", SoftKeyRoute::GoShares));
        set_key(SoftKeyId::Left3, softkey::navigate("CALENDAR", SoftKeyRoute::GoCalendar));
        set_key(SoftKeyId::Left4, softkey::navigate(pinter_controller::build_home_softkey_label(
                                                        SoftKeyId::Left4, g_console_state),
                                                    SoftKeyRoute::GoPinter));
        set_key(SoftKeyId::Right1, softkey::navigate("SETTINGS", SoftKeyRoute::GoSettings));
        set_key(
            SoftKeyId::Right2,
            softkey::navigate(SoftKeyId::Right2, "WEATHER",
                              settings_controller::weather_source_selection_text(g_console_state),
                              SoftKeyRoute::GoWeather));
        set_key(SoftKeyId::Right3,
                softkey::navigate("LOCAL\nCONDITIONS", SoftKeyRoute::GoLocalConditions));
        set_key(SoftKeyId::Left5, softkey::navigate("ADS-B\nTRAFFIC", SoftKeyRoute::GoAirTraffic));
        break;
    case MenuPage::Calendar:
    {
        constexpr std::array<SoftKeyRoute, kListSlotSoftkeys.size()> routes = {
            SoftKeyRoute::SelectCalendarSlot1, SoftKeyRoute::SelectCalendarSlot2,
            SoftKeyRoute::SelectCalendarSlot3, SoftKeyRoute::SelectCalendarSlot4,
            SoftKeyRoute::SelectCalendarSlot5, SoftKeyRoute::SelectCalendarSlot6,
            SoftKeyRoute::SelectCalendarSlot7, SoftKeyRoute::SelectCalendarSlot8};
        uint8_t visible_index = 0U;
        const uint8_t event_count =
            std::min(g_console_state.calendar_event_count,
                     static_cast<uint8_t>(g_console_state.calendar_events.size()));
        for (uint8_t i = 0U; i < event_count && visible_index < kListSlotSoftkeys.size(); ++i)
        {
            const CalendarEvent& event = g_console_state.calendar_events[i];
            if (!calendar_controller::event_matches_filter(g_console_state, event))
            {
                continue;
            }

            const SoftKeyId slot = kListSlotSoftkeys[visible_index];
            set_key(slot, softkey::item(calendar_controller::build_event_softkey_label(slot, event),
                                        routes[visible_index]));
            ++visible_index;
        }
        set_key(SoftKeyId::Left5,
                softkey::view(SoftKeyId::Left5, "PERSON",
                              calendar_controller::owner_selection_text(g_console_state),
                              SoftKeyRoute::CycleCalendarOwner));
        set_key(SoftKeyId::Right5,
                calendar_controller::filters_are_default(g_console_state)
                    ? softkey::navigate("HOME", SoftKeyRoute::GoHome)
                    : softkey::action("RESET", SoftKeyRoute::ResetCalendarFilters));
        break;
    }
    case MenuPage::CalendarDetail:
        break;
    case MenuPage::Weather:
        set_key(SoftKeyId::Left5,
                softkey::view(SoftKeyId::Left5, "PERIOD",
                              settings_controller::weather_period_selection_text(g_console_state),
                              SoftKeyRoute::CycleWeatherPeriod));
        break;
    case MenuPage::AirTraffic:
        set_key(SoftKeyId::Left5,
                softkey::view(
                    SoftKeyId::Left5, "VIEW",
                    settings_controller::air_traffic_view_mode_selection_text(g_console_state),
                    SoftKeyRoute::ToggleAirTrafficViewMode));
        break;
    case MenuPage::Pinter:
    {
        if (g_console_state.pinter_reset_confirming)
        {
            // Decision state: only decline (L5) and confirm (R5) (NAV-3).
            pinter_controller::update_block_reason(g_console_state);
            set_key(SoftKeyId::Left5, softkey::action("CANCEL", SoftKeyRoute::CancelPinterReset));
            set_key(SoftKeyId::Right5, softkey::action("RESET", SoftKeyRoute::ConfirmPinterReset));
            break;
        }

        constexpr std::array<SoftKeyRoute, kPinterCount> routes = {
            SoftKeyRoute::SelectPinterSlot1,
            SoftKeyRoute::SelectPinterSlot2,
            SoftKeyRoute::SelectPinterSlot3,
            SoftKeyRoute::SelectPinterSlot4,
        };
        for (size_t i = 0U; i < g_console_state.pinters.size(); ++i)
        {
            const SoftKeyId slot = kListSlotSoftkeys[i];
            set_key(slot, softkey::item(pinter_controller::build_slot_softkey_label(
                                            g_console_state, slot, g_console_state.pinters[i]),
                                        routes[i], i == g_console_state.selected_pinter_index));
        }

        // R1 starts a brew on an idle vessel (opening Select Brew) or applies
        // the selected vessel's next event. When it cannot act, it becomes a
        // status key showing why (SK-5), and the footer repeats the reason.
        const bool selected_pinter_idle =
            pinter_controller::selected_const(g_console_state).state == PinterState::Idle;
        const bool primary_can_act =
            selected_pinter_idle
                ? pinter_controller::brew_dock_count(g_console_state) < kPinterBrewDockCapacity
                : pinter_controller::primary_action_enabled(g_console_state);
        const char* primary_label =
            pinter_controller::build_primary_action_label(SoftKeyId::Right1, g_console_state);
        if (!primary_can_act)
        {
            set_key(SoftKeyId::Right1, softkey::status(primary_label));
        }
        else if (selected_pinter_idle)
        {
            set_key(SoftKeyId::Right1,
                    softkey::navigate(primary_label, SoftKeyRoute::GoPinterSelectBrew));
        }
        else
        {
            set_key(SoftKeyId::Right1,
                    softkey::action(primary_label, SoftKeyRoute::ApplyPinterPrimaryAction));
        }
        pinter_controller::update_block_reason(g_console_state);

        // Keys that cannot act are left blank rather than shown as bare
        // labels that do nothing (SK-5).
        if (!selected_pinter_idle)
        {
            set_key(SoftKeyId::Right3, softkey::action("RESET", SoftKeyRoute::RequestPinterReset));
        }
        const uint8_t selected_stage_days = pinter_scheduling::current_stage_planned_days(
            pinter_controller::selected_const(g_console_state));
        if (selected_stage_days > 1U)
        {
            set_key(SoftKeyId::Right2,
                    softkey::step("-1 DAY", SoftKeyRoute::DecreaseSelectedPinterDay));
        }
        if (selected_stage_days > 0U && selected_stage_days < std::numeric_limits<uint8_t>::max())
        {
            set_key(SoftKeyId::Right4,
                    softkey::step("+1 DAY", SoftKeyRoute::IncreaseSelectedPinterDay));
        }
        break;
    }
    case MenuPage::PinterSelectBrew:
    {
        // On-the-fly recipe pick: whichever pack you have in hand right now,
        // straight from the full catalogue -- no pre-planned reservation step.
        const size_t base_index = static_cast<size_t>(g_console_state.pinter_catalogue_page_index) *
                                  kPinterBrewListVisibleCount;
        for (uint8_t i = 0U; i < kPinterBrewListVisibleCount; ++i)
        {
            const size_t brew_index = base_index + i;
            if (brew_index >= pinter_controller::brew_catalogue_count())
            {
                break;
            }
            const SoftKeyId key = kListSlotSoftkeys[i];
            set_key(key, softkey::item(pinter_controller::build_catalogue_item_label(
                                           key, static_cast<uint8_t>(brew_index)),
                                       kPinterBrewListRoutes[i]));
        }
        // R4 carries the eighth list item; BACK STEP returns to Pinter (NAV-5).
        break;
    }
    case MenuPage::PinterStartTiming:
    {
        // Decision state for a brew start: presets on row 1, one duration per
        // row with -/+ on opposite sides, CANCEL at L5 and START at R5 (NAV-3).
        // A step key is blank when its value is at the limit, so every
        // labelled key acts (SK-5).
        set_key(SoftKeyId::Left1,
                softkey::action("MINIMUM", SoftKeyRoute::SelectPinterMinimumTiming));
        set_key(SoftKeyId::Right1,
                softkey::action("RECOMMENDED", SoftKeyRoute::SelectPinterRecommendedTiming));

        struct DurationRow
        {
            SoftKeyId minus_key;
            SoftKeyId plus_key;
            const char* minus_caption;
            const char* plus_caption;
            uint8_t days;
            uint8_t min_days;
            uint8_t max_days;
            SoftKeyRoute minus_route;
            SoftKeyRoute plus_route;
        };
        const std::array<DurationRow, 3> rows = {{
            {SoftKeyId::Left2, SoftKeyId::Right2, "CRASH -", "CRASH +",
             g_console_state.pinter_pending_cold_crash_days, kPinterMinColdCrashDays,
             kPinterMaxColdCrashDays, SoftKeyRoute::DecreasePinterColdCrashDays,
             SoftKeyRoute::IncreasePinterColdCrashDays},
            {SoftKeyId::Left3, SoftKeyId::Right3, "BREW -", "BREW +",
             g_console_state.pinter_pending_brewing_days, kPinterMinBrewDays, kPinterMaxBrewDays,
             SoftKeyRoute::DecreasePinterBrewDays, SoftKeyRoute::IncreasePinterBrewDays},
            {SoftKeyId::Left4, SoftKeyId::Right4, "COND -", "COND +",
             g_console_state.pinter_pending_conditioning_days, kPinterMinConditioningDays,
             kPinterMaxConditioningDays, SoftKeyRoute::DecreasePinterConditioningDays,
             SoftKeyRoute::IncreasePinterConditioningDays},
        }};
        for (const DurationRow& row : rows)
        {
            if (row.days > row.min_days)
            {
                set_key(row.minus_key,
                        softkey::step(pinter_controller::build_days_label(
                                          row.minus_key, row.minus_caption, row.days),
                                      row.minus_route));
            }
            if (row.days < row.max_days)
            {
                set_key(row.plus_key, softkey::step(pinter_controller::build_days_label(
                                                        row.plus_key, row.plus_caption, row.days),
                                                    row.plus_route));
            }
        }

        set_key(SoftKeyId::Left5, softkey::action("CANCEL", SoftKeyRoute::GoPinterSelectBrew));
        if (pinter_controller::can_start(g_console_state))
        {
            set_key(SoftKeyId::Right5,
                    softkey::action(SoftKeyId::Right5, "START",
                                    pinter_controller::selected_const(g_console_state).label.data(),
                                    SoftKeyRoute::ConfirmPinterStart));
        }
        break;
    }
    case MenuPage::Shares:
    {
        // Lists fill L1-L4 then R1-R4, leaving row 5 free (NAV-6).
        constexpr std::array<SoftKeyRoute, kMaxWatchedShares> routes = {
            SoftKeyRoute::SelectShareSlot1, SoftKeyRoute::SelectShareSlot2,
            SoftKeyRoute::SelectShareSlot3, SoftKeyRoute::SelectShareSlot4,
            SoftKeyRoute::SelectShareSlot5, SoftKeyRoute::SelectShareSlot6};
        const size_t share_count = std::min<size_t>(g_console_state.share_count, routes.size());
        for (size_t i = 0U; i < share_count; ++i)
        {
            const SoftKeyId slot = kListSlotSoftkeys[i];
            const ShareWatchEntry& share = g_console_state.watched_shares[i];
            set_key(slot,
                    softkey::item(build_selection_softkey_label(slot, share.display_name.data(),
                                                                share.price_text.data()),
                                  routes[i]));
        }
        break;
    }
    case MenuPage::ShareDetail:
        set_key(SoftKeyId::Left5,
                softkey::view(SoftKeyId::Left5, "PERIOD",
                              settings_controller::share_period_selection_text(g_console_state),
                              SoftKeyRoute::CycleSharePeriod));
        set_key(SoftKeyId::Right4, softkey::navigate("SHARES", SoftKeyRoute::GoShares));
        break;
    case MenuPage::Status:
        set_key(SoftKeyId::Left1, softkey::navigate("OVERVIEW", SoftKeyRoute::GoStatusOverview));
        set_key(SoftKeyId::Left2, softkey::navigate("CONNECT", SoftKeyRoute::GoStatusConnectivity));
        set_key(SoftKeyId::Left3, softkey::navigate("RESOURCES", SoftKeyRoute::GoStatusResources));
        set_key(SoftKeyId::Left4, softkey::navigate("SENSORS", SoftKeyRoute::GoStatusSensors));
        set_key(SoftKeyId::Right1,
                softkey::navigate("INTEGRATIONS", SoftKeyRoute::GoStatusIntegrations));
        set_key(SoftKeyId::Right3, softkey::navigate("GREYSCALE", SoftKeyRoute::GoGreyscaleTest));
        set_key(SoftKeyId::Right4, softkey::navigate("KEYPAD", SoftKeyRoute::GoKeypadDebug));
        break;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        // Body pages: only R5 HOME, added below (PAGE-4). BACK STEP returns to Status.
        break;
    case MenuPage::LocalConditions:
    {
        // One reading key per quantity, opening its graph; the value is the
        // current reading (navigate with summary). A quantity with no reading
        // and no history has nothing to open, so its key is blank (SK-5); with
        // no board at all every key is blank and the body says why. Values
        // come from the one formatter per quantity (BODY-5).
        struct ReadingKey
        {
            SoftKeyId key;
            const char* caption;
            LocalConditionMetric metric;
            SoftKeyRoute route;
        };
        constexpr std::array<ReadingKey, 4> kReadingKeys = {{
            {SoftKeyId::Left1, "TEMP", LocalConditionMetric::Temperature,
             SoftKeyRoute::ShowLocalTemperatureGraph},
            {SoftKeyId::Left2, "HUMIDITY", LocalConditionMetric::Humidity,
             SoftKeyRoute::ShowLocalHumidityGraph},
            {SoftKeyId::Left3, "AIR PRESSURE", LocalConditionMetric::AirPressure,
             SoftKeyRoute::ShowLocalPressureGraph},
            {SoftKeyId::Left4, "VOC CHANGE", LocalConditionMetric::AirQuality,
             SoftKeyRoute::ShowLocalAirQualityGraph},
        }};
        const auto& sensor = g_console_state.environment_sensor_status;
        for (const ReadingKey& reading : kReadingKeys)
        {
            if (!local_conditions::metric_has_data(reading.metric, sensor))
            {
                continue;
            }
            char value[20] = {};
            local_conditions::format_metric(reading.metric, sensor, value, sizeof(value));
            set_key(reading.key,
                    softkey::navigate(reading.key, reading.caption, value, reading.route));
        }
        break;
    }
    case MenuPage::LocalConditionGraph:
        // Body page: BACK STEP returns to Local Conditions (NAV-5); R5 HOME below.
        break;
    case MenuPage::Settings:
        // Section keys open their sub-page and summarise it; WEATHER SOURCE and
        // TIME ZONE open option pages; REMOTE CONFIG switches in place.
        if (g_console_state.settings_page_index == 0U)
        {
            set_key(SoftKeyId::Left1,
                    softkey::navigate(SoftKeyId::Left1, "DEVICE IDENTITY",
                                      settings_controller::device_identity_selection_text(),
                                      SoftKeyRoute::GoDeviceSettings));
            set_key(SoftKeyId::Left2,
                    softkey::toggle(SoftKeyId::Left2, "REMOTE CONFIG", config.remote_config_enabled,
                                    SoftKeyRoute::ToggleRemoteConfig));
            set_key(SoftKeyId::Left3,
                    softkey::navigate(SoftKeyId::Left3, "NETWORK",
                                      settings_controller::wifi_selection_text(g_console_state),
                                      SoftKeyRoute::GoWifiSettings));
            set_key(SoftKeyId::Left4, softkey::navigate(SoftKeyId::Left4, "HOME ASSISTANT",
                                                        settings_controller::enabled_selection_text(
                                                            config.home_assistant_enabled),
                                                        SoftKeyRoute::GoHomeAssistantSettings));
        }
        else
        {
            set_key(SoftKeyId::Left1, softkey::navigate(SoftKeyId::Left1, "MQTT DISCOVERY",
                                                        settings_controller::enabled_selection_text(
                                                            config.mqtt_enabled),
                                                        SoftKeyRoute::GoMqttSettings));
            set_key(
                SoftKeyId::Left2,
                softkey::select(SoftKeyId::Left2, "WEATHER SOURCE",
                                settings_controller::weather_source_selection_text(g_console_state),
                                SoftKeyRoute::GoWeatherSources));
            set_key(SoftKeyId::Left3,
                    softkey::select(SoftKeyId::Left3, "TIME ZONE",
                                    settings_controller::time_zone_selection_text(g_console_state),
                                    SoftKeyRoute::GoTimeZoneSettings));
            set_key(
                SoftKeyId::Left4,
                softkey::navigate(SoftKeyId::Left4, "SCREEN SAVER",
                                  settings_controller::screen_saver_selection_text(g_console_state),
                                  SoftKeyRoute::GoScreenSaverSettings));
            set_key(SoftKeyId::Right1,
                    softkey::navigate(
                        SoftKeyId::Right1, "ADS-B TRAFFIC",
                        settings_controller::enabled_selection_text(config.air_traffic_enabled),
                        SoftKeyRoute::GoAirTrafficSettings));
        }
        break;
    case MenuPage::DeviceSettings:
        // Set in the web config; shown here for reference.
        set_key(SoftKeyId::Left1,
                softkey::status(SoftKeyId::Left1, "NAME", config.device_name.data()));
        set_key(SoftKeyId::Left2,
                softkey::status(SoftKeyId::Left2, "LABEL", config.device_label.data()));
        set_key(SoftKeyId::Left3,
                softkey::status(SoftKeyId::Left3, "LOCATION", config.location.data()));
        set_key(SoftKeyId::Left4, softkey::status(SoftKeyId::Left4, "ROOM", config.room.data()));
        break;
    case MenuPage::WifiSettings:
        set_key(SoftKeyId::Left1,
                softkey::status(SoftKeyId::Left1, "SSID", config.wifi_ssid.data()));
        set_key(SoftKeyId::Left2, softkey::status(SoftKeyId::Left2, "PASSWORD",
                                                  settings_controller::secret_selection_text(
                                                      config.wifi_password[0] != '\0')));
        break;
    case MenuPage::HomeAssistantSettings:
        set_key(SoftKeyId::Left1,
                softkey::toggle(SoftKeyId::Left1, "REST API", config.home_assistant_enabled,
                                SoftKeyRoute::ToggleHomeAssistantEnabled));
        set_key(SoftKeyId::Left2,
                softkey::status(SoftKeyId::Left2, "HOST", config.home_assistant_host.data()));
        set_key(SoftKeyId::Left3,
                softkey::status(SoftKeyId::Left3, "PORT",
                                settings_controller::port_selection_text(
                                    SoftKeyId::Left3, config.home_assistant_port)));
        set_key(SoftKeyId::Left4, softkey::status(SoftKeyId::Left4, "TOKEN",
                                                  settings_controller::secret_selection_text(
                                                      config.home_assistant_token[0] != '\0')));
        set_key(SoftKeyId::Left5, softkey::status(SoftKeyId::Left5, "TRACKED",
                                                  config.home_assistant_entity_id.data()));
        set_key(SoftKeyId::Right1, softkey::status(SoftKeyId::Right1, "SELF",
                                                   config.home_assistant_self_entity_id.data()));
        break;
    case MenuPage::MqttSettings:
        set_key(SoftKeyId::Left1, softkey::toggle(SoftKeyId::Left1, "MQTT", config.mqtt_enabled,
                                                  SoftKeyRoute::ToggleMqttEnabled));
        set_key(SoftKeyId::Left2,
                softkey::status(SoftKeyId::Left2, "BROKER", config.mqtt_host.data()));
        set_key(SoftKeyId::Left3, softkey::status(SoftKeyId::Left3, "PORT",
                                                  settings_controller::port_selection_text(
                                                      SoftKeyId::Left3, config.mqtt_port)));
        set_key(SoftKeyId::Left4,
                softkey::status(SoftKeyId::Left4, "USERNAME", config.mqtt_username.data()));
        set_key(SoftKeyId::Left5, softkey::status(SoftKeyId::Left5, "PASSWORD",
                                                  settings_controller::secret_selection_text(
                                                      config.mqtt_password[0] != '\0')));
        set_key(SoftKeyId::Right1,
                softkey::status(SoftKeyId::Right1, "PREFIX", config.mqtt_discovery_prefix.data()));
        set_key(SoftKeyId::Right2,
                softkey::status(SoftKeyId::Right2, "TOPIC", config.mqtt_base_topic.data()));
        break;
    case MenuPage::AirTrafficSettings:
        set_key(SoftKeyId::Left1,
                softkey::toggle(SoftKeyId::Left1, "ADS-B", config.air_traffic_enabled,
                                SoftKeyRoute::ToggleAirTrafficEnabled));
        set_key(SoftKeyId::Left2,
                softkey::status(SoftKeyId::Left2, "HOST", config.air_traffic_host.data()));
        set_key(SoftKeyId::Left3, softkey::status(SoftKeyId::Left3, "PORT",
                                                  settings_controller::port_selection_text(
                                                      SoftKeyId::Left3, config.air_traffic_port)));
        set_key(SoftKeyId::Left4,
                softkey::status(SoftKeyId::Left4, "RADIUS",
                                settings_controller::radius_nm_selection_text(
                                    SoftKeyId::Left4, config.air_traffic_radius_nm)));
        set_key(SoftKeyId::Left5,
                softkey::status(SoftKeyId::Left5, "COORDS", config.air_traffic_coordinates.data()));
        set_key(SoftKeyId::Right1, softkey::status(SoftKeyId::Right1, "API KEY",
                                                   settings_controller::secret_selection_text(
                                                       config.air_traffic_api_key[0] != '\0')));
        break;
    case MenuPage::ScreenSaverSettings:
        // Two select keys (SK-7). TIMEOUT opens a numeric entry state on this
        // page: the field being edited stays inverse, CANCEL at L5 declines
        // and ENTER at R5 confirms (NAV-3); every other key is blank (SK-5).
        if (g_console_state.screen_saver_timeout_editing)
        {
            set_key(SoftKeyId::Left1,
                    softkey::select_editing(
                        SoftKeyId::Left1, "TIMEOUT",
                        settings_controller::screen_saver_timeout_selection_text(g_console_state)));
            set_key(SoftKeyId::Left5,
                    softkey::action("CANCEL", SoftKeyRoute::CancelScreenSaverTimeout));
            set_key(SoftKeyId::Right5,
                    softkey::action("ENTER", SoftKeyRoute::ConfirmScreenSaverTimeout));
            break;
        }
        set_key(SoftKeyId::Left1,
                softkey::select(
                    SoftKeyId::Left1, "TIMEOUT",
                    settings_controller::screen_saver_timeout_selection_text(g_console_state),
                    SoftKeyRoute::EditScreenSaverTimeout));
        set_key(SoftKeyId::Left2,
                softkey::select(SoftKeyId::Left2, "SAVER",
                                settings_controller::screen_saver_selection_text(g_console_state),
                                SoftKeyRoute::GoScreenSaverOptions));
        break;
    case MenuPage::ScreenSaverOptions:
    {
        // A fixed list of option keys in L1-L4, R1-R4 with the current saver
        // inverse (SK-7, NAV-6); choosing one takes effect at once.
        struct SaverOption
        {
            ScreenSaverSelection selection;
            SoftKeyRoute route;
        };
        constexpr std::array<SaverOption, kListSlotSoftkeys.size()> options = {{
            {ScreenSaverSelection::Life, SoftKeyRoute::SelectScreenSaverLife},
            {ScreenSaverSelection::Clock, SoftKeyRoute::SelectScreenSaverClock},
            {ScreenSaverSelection::Starfield, SoftKeyRoute::SelectScreenSaverStarfield},
            {ScreenSaverSelection::Random, SoftKeyRoute::SelectScreenSaverRandom},
            {ScreenSaverSelection::Matrix, SoftKeyRoute::SelectScreenSaverMatrix},
            {ScreenSaverSelection::Radar, SoftKeyRoute::SelectScreenSaverRadar},
            {ScreenSaverSelection::Rain, SoftKeyRoute::SelectScreenSaverRain},
            {ScreenSaverSelection::Worms, SoftKeyRoute::SelectScreenSaverWorms},
        }};
        for (size_t i = 0U; i < options.size(); ++i)
        {
            set_key(
                kListSlotSoftkeys[i],
                softkey::option(
                    settings_controller::screen_saver_definition(options[i].selection).option_label,
                    options[i].route,
                    g_console_state.screen_saver_selection == options[i].selection));
        }
        break;
    }
    case MenuPage::WeatherSources:
    {
        // Option keys for the source, then the selected source's settings
        // from the web config for reference.
        const bool home_assistant = g_console_state.weather_source == WeatherSource::HomeAssistant;
        set_key(SoftKeyId::Left1,
                softkey::option(
                    settings_controller::weather_source_definition(WeatherSource::HomeAssistant)
                        .option_label,
                    SoftKeyRoute::SelectWeatherHomeAssistant, home_assistant));
        set_key(
            SoftKeyId::Left2,
            softkey::option(settings_controller::weather_source_definition(WeatherSource::OpenMeteo)
                                .option_label,
                            SoftKeyRoute::SelectWeatherOpenMeteo,
                            g_console_state.weather_source == WeatherSource::OpenMeteo));
        set_key(SoftKeyId::Right1,
                softkey::status(SoftKeyId::Right1, home_assistant ? "WEATHER" : "COORDS",
                                home_assistant ? config.weather_entity_id.data()
                                               : config.weather_coordinates.data()));
        set_key(SoftKeyId::Right2,
                softkey::status(SoftKeyId::Right2, "SUN", config.sun_entity_id.data()));
        break;
    }
    case MenuPage::TimeZoneSettings:
    {
        // A fixed, ordered list of option keys: a zone keeps its key whichever
        // zone is selected, and the selected one is shown inverse (SK-7).
        constexpr std::array<SoftKeyRoute, kTimeZoneOptionsPerPage> routes = {
            SoftKeyRoute::SelectTimeZoneSlot1, SoftKeyRoute::SelectTimeZoneSlot2,
            SoftKeyRoute::SelectTimeZoneSlot3, SoftKeyRoute::SelectTimeZoneSlot4,
            SoftKeyRoute::SelectTimeZoneSlot5, SoftKeyRoute::SelectTimeZoneSlot6,
            SoftKeyRoute::SelectTimeZoneSlot7, SoftKeyRoute::SelectTimeZoneSlot8};
        for (uint8_t slot = 0U; slot < kTimeZoneOptionsPerPage; ++slot)
        {
            const settings_controller::TimeZoneDefinition* zone =
                settings_controller::time_zone_for_slot(g_console_state, slot);
            if (zone == nullptr)
            {
                break;
            }
            set_key(kListSlotSoftkeys[slot],
                    softkey::option(zone->option_label, routes[slot],
                                    zone->zone == g_console_state.time_zone));
        }
        break;
    }
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
        break;
    case MenuPage::AlertList:
    {
        std::array<uint8_t, kActiveAlertCapacity> alert_indices = {};
        uint8_t sorted_count = 0U;
        alert_controller::build_display_indices(g_console_state, alert_indices, &sorted_count);
        const uint8_t page_start =
            static_cast<uint8_t>(g_console_state.alert_list_page_index * kAlertsPerPage);
        // The Alert list also uses L5 (NAV-6).
        constexpr std::array<SoftKeyId, kAlertsPerPage> slots = {
            SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,
            SoftKeyId::Left4,  SoftKeyId::Left5,  SoftKeyId::Right1,
            SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};
        constexpr std::array<SoftKeyRoute, kAlertsPerPage> routes = {
            SoftKeyRoute::SelectAlertSlot1, SoftKeyRoute::SelectAlertSlot2,
            SoftKeyRoute::SelectAlertSlot3, SoftKeyRoute::SelectAlertSlot4,
            SoftKeyRoute::SelectAlertSlot5, SoftKeyRoute::SelectAlertSlot6,
            SoftKeyRoute::SelectAlertSlot7, SoftKeyRoute::SelectAlertSlot8,
            SoftKeyRoute::SelectAlertSlot9};

        for (size_t i = 0U; i < slots.size(); ++i)
        {
            const uint8_t index = static_cast<uint8_t>(page_start + i);
            if (index >= sorted_count)
            {
                continue;
            }
            const ActiveAlert& alert = g_console_state.active_alerts[alert_indices[index]];
            set_key(slots[i], softkey::item(alert_controller::build_softkey_label(slots[i], alert),
                                            routes[i]));
        }
        set_key(SoftKeyId::Right5, softkey::navigate("HOME", SoftKeyRoute::GoHome));
        break;
    }
    case MenuPage::AlertDetail:
        set_key(SoftKeyId::Left5, softkey::action("IGNORE", SoftKeyRoute::AlertIgnore));
        set_key(SoftKeyId::Right5, softkey::action("ACCEPT", SoftKeyRoute::AlertAccept));
        break;
    }

    if (g_console_state.active_page != MenuPage::Home &&
        g_console_state.active_page != MenuPage::Calendar &&
        g_console_state.active_page != MenuPage::AlertList &&
        g_console_state.active_page != MenuPage::AlertDetail &&
        !(g_console_state.active_page == MenuPage::ScreenSaverSettings &&
          g_console_state.screen_saver_timeout_editing) &&
        g_console_state.active_page != MenuPage::PinterStartTiming &&
        !(g_console_state.active_page == MenuPage::Pinter &&
          g_console_state.pinter_reset_confirming))
    {
        set_key(SoftKeyId::Right5, softkey::navigate("HOME", SoftKeyRoute::GoHome));
    }

    // Any temporary label overrides are layered on after the page defaults so
    // experiments do not need to duplicate the entire softkey map.
    apply_softkey_label_overrides(softkeys);
    g_console_state.softkeys = softkeys;
}

/// @brief Advances the test annunciator through its demo states.
SystemTestState next_test_state(SystemTestState state)
{
    switch (state)
    {
    case SystemTestState::Idle:
        return SystemTestState::Running;
    case SystemTestState::Running:
        return SystemTestState::Passed;
    case SystemTestState::Passed:
        return SystemTestState::Failed;
    case SystemTestState::Failed:
        return SystemTestState::Idle;
    }

    return SystemTestState::Idle;
}

/// @brief Recomputes lamp outputs from the current logical console state.
void update_lamps_from_state()
{
    alert_controller::sync(g_console_state);

    // Alert and test lamps mirror the current logical state so the front panel
    // behaves like annunciators rather than generic status LEDs.
    const alert_ordering::AnnunciationSummary annunciation =
        alert_controller::annunciation_summary(g_console_state);
    const AlertSeverity highest_severity = annunciation.highest_severity;
    g_console_state.alert_severity = highest_severity;

    if (annunciation.suppressed)
    {
        g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::Off;
    }
    else
    {
        switch (highest_severity)
        {
        case AlertSeverity::None:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::Off;
            break;
        case AlertSeverity::Message:
        case AlertSeverity::Warning:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashSlow;
            break;
        case AlertSeverity::Alert:
            g_console_state.lamps[lamp_index(LampId::AlertLamp)] = LampMode::FlashFast;
            break;
        }
    }

    switch (g_console_state.test_state)
    {
    case SystemTestState::Idle:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::Off;
        break;
    case SystemTestState::Running:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::On;
        break;
    case SystemTestState::Passed:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::FlashSlow;
        break;
    case SystemTestState::Failed:
        g_console_state.lamps[lamp_index(LampId::TestLamp)] = LampMode::FlashFast;
        break;
    }

    // Backlights are modeled as simple on/off lamps for now because the UI only
    // needs to show whether brightness is disabled, not the PWM details.
    g_console_state.lamps[lamp_index(LampId::KeyBacklight)] =
        (g_console_state.key_backlight_brightness == BrightnessLevel::Off) ? LampMode::Off
                                                                           : LampMode::On;

    g_console_state.lamps[lamp_index(LampId::PanelBacklight)] =
        (g_console_state.panel_brightness == BrightnessLevel::Off) ? LampMode::Off : LampMode::On;
}

/// @brief Returns the next brighter backlight level without exceeding the max.
BrightnessLevel brighter(BrightnessLevel level)
{
    if (level == BrightnessLevel::High)
    {
        return BrightnessLevel::High;
    }
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) + 1);
}

/// @brief Returns the next dimmer backlight level without going below off.
BrightnessLevel dimmer(BrightnessLevel level)
{
    if (level == BrightnessLevel::Off)
    {
        return BrightnessLevel::Off;
    }
    return static_cast<BrightnessLevel>(static_cast<uint8_t>(level) - 1);
}

/// @brief Applies one softkey action to the console state.
bool apply_softkey_route(SoftKeyRoute route)
{
    // Route handling mutates only the console model. Rendering and hardware
    // reactions happen later from the updated shared state.
    switch (route)
    {
    case SoftKeyRoute::None:
        return false;
    case SoftKeyRoute::GoHome:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Home;
        return true;
    case SoftKeyRoute::GoCalendar:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Calendar;
        return true;
    case SoftKeyRoute::GoWeather:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Weather;
        return true;
    case SoftKeyRoute::GoAirTraffic:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::AirTraffic;
        g_console_state.air_traffic_page_index = 0U;
        return true;
    case SoftKeyRoute::GoPinter:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Pinter;
        return true;
    case SoftKeyRoute::GoPinterSelectBrew:
        settings_controller::stop_timeout_editing(g_console_state);
        pinter_controller::clamp_list_page(g_console_state.pinter_catalogue_page_index,
                                           pinter_controller::brew_catalogue_count());
        g_console_state.active_page = MenuPage::PinterSelectBrew;
        return true;
    case SoftKeyRoute::SelectPinterSlot1:
        return pinter_controller::select_slot(g_console_state, 0U);
    case SoftKeyRoute::SelectPinterSlot2:
        return pinter_controller::select_slot(g_console_state, 1U);
    case SoftKeyRoute::SelectPinterSlot3:
        return pinter_controller::select_slot(g_console_state, 2U);
    case SoftKeyRoute::SelectPinterSlot4:
        return pinter_controller::select_slot(g_console_state, 3U);
    case SoftKeyRoute::ApplyPinterPrimaryAction:
        return pinter_controller::apply_primary_action(g_console_state);
    case SoftKeyRoute::RequestPinterReset:
        if (pinter_controller::selected_const(g_console_state).state == PinterState::Idle)
        {
            return false;
        }
        g_console_state.pinter_reset_confirming = true;
        return true;
    case SoftKeyRoute::ConfirmPinterReset:
        g_console_state.pinter_reset_confirming = false;
        (void)pinter_controller::reset_selected(g_console_state);
        return true;
    case SoftKeyRoute::CancelPinterReset:
        g_console_state.pinter_reset_confirming = false;
        return true;
    case SoftKeyRoute::DecreaseSelectedPinterDay:
        return pinter_controller::nudge_selected_day(g_console_state, -1);
    case SoftKeyRoute::IncreaseSelectedPinterDay:
        return pinter_controller::nudge_selected_day(g_console_state, 1);
    case SoftKeyRoute::SelectPinterListItem1:
        return pinter_controller::select_list_item(g_console_state, 0U);
    case SoftKeyRoute::SelectPinterListItem2:
        return pinter_controller::select_list_item(g_console_state, 1U);
    case SoftKeyRoute::SelectPinterListItem3:
        return pinter_controller::select_list_item(g_console_state, 2U);
    case SoftKeyRoute::SelectPinterListItem4:
        return pinter_controller::select_list_item(g_console_state, 3U);
    case SoftKeyRoute::SelectPinterListItem5:
        return pinter_controller::select_list_item(g_console_state, 4U);
    case SoftKeyRoute::SelectPinterListItem6:
        return pinter_controller::select_list_item(g_console_state, 5U);
    case SoftKeyRoute::SelectPinterListItem7:
        return pinter_controller::select_list_item(g_console_state, 6U);
    case SoftKeyRoute::SelectPinterListItem8:
        return pinter_controller::select_list_item(g_console_state, 7U);
    case SoftKeyRoute::SelectPinterMinimumTiming:
        return pinter_controller::set_pending_timing(g_console_state, true);
    case SoftKeyRoute::SelectPinterRecommendedTiming:
        return pinter_controller::set_pending_timing(g_console_state, false);
    case SoftKeyRoute::DecreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       -1, kPinterMinBrewDays, kPinterMaxBrewDays);
    case SoftKeyRoute::IncreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       1, kPinterMinBrewDays, kPinterMaxBrewDays);
    case SoftKeyRoute::DecreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, -1, kPinterMinConditioningDays,
            kPinterMaxConditioningDays);
    case SoftKeyRoute::IncreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, 1, kPinterMinConditioningDays,
            kPinterMaxConditioningDays);
    case SoftKeyRoute::DecreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, -1, kPinterMinColdCrashDays,
            kPinterMaxColdCrashDays);
    case SoftKeyRoute::IncreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, 1, kPinterMinColdCrashDays,
            kPinterMaxColdCrashDays);
    case SoftKeyRoute::ConfirmPinterStart:
        return pinter_controller::confirm_start(g_console_state);
    case SoftKeyRoute::GoShares:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Shares;
        return true;
    case SoftKeyRoute::GoStatus:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Status;
        return true;
    case SoftKeyRoute::GoStatusOverview:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::StatusOverview;
        return true;
    case SoftKeyRoute::GoStatusConnectivity:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::StatusConnectivity;
        return true;
    case SoftKeyRoute::GoStatusResources:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::StatusResources;
        return true;
    case SoftKeyRoute::GoStatusSensors:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::StatusSensors;
        return true;
    case SoftKeyRoute::GoStatusIntegrations:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::StatusIntegrations;
        return true;
    case SoftKeyRoute::GoLocalConditions:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::LocalConditions;
        return true;
    case SoftKeyRoute::ShowLocalTemperatureGraph:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.local_condition_metric = LocalConditionMetric::Temperature;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalHumidityGraph:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.local_condition_metric = LocalConditionMetric::Humidity;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalPressureGraph:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.local_condition_metric = LocalConditionMetric::AirPressure;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::ShowLocalAirQualityGraph:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.local_condition_metric = LocalConditionMetric::AirQuality;
        g_console_state.active_page = MenuPage::LocalConditionGraph;
        return true;
    case SoftKeyRoute::GoSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::Settings;
        g_console_state.settings_page_index = 0;
        return true;
    case SoftKeyRoute::GoDeviceSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::DeviceSettings;
        return true;
    case SoftKeyRoute::GoWifiSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::WifiSettings;
        return true;
    case SoftKeyRoute::GoHomeAssistantSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::HomeAssistantSettings;
        return true;
    case SoftKeyRoute::GoMqttSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::MqttSettings;
        return true;
    case SoftKeyRoute::GoAirTrafficSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::AirTrafficSettings;
        return true;
    case SoftKeyRoute::GoScreenSaverSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::ScreenSaverSettings;
        return true;
    case SoftKeyRoute::GoScreenSaverOptions:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::ScreenSaverOptions;
        return true;
    case SoftKeyRoute::EditScreenSaverTimeout:
        return settings_controller::start_timeout_editing(g_console_state);
    case SoftKeyRoute::ConfirmScreenSaverTimeout:
        return settings_controller::confirm_timeout_edit(g_console_state);
    case SoftKeyRoute::CancelScreenSaverTimeout:
        // Restores the saved value, as BACK STEP does.
        return settings_controller::stop_timeout_editing(g_console_state);
    case SoftKeyRoute::GoWeatherSources:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::WeatherSources;
        return true;
    case SoftKeyRoute::GoTimeZoneSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::TimeZoneSettings;
        g_console_state.time_zone_page_index =
            settings_controller::time_zone_page_of_current(g_console_state);
        return true;
    case SoftKeyRoute::GoKeypadDebug:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::KeypadDebug;
        return true;
    case SoftKeyRoute::GoGreyscaleTest:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::GreyscaleTest;
        return true;
    case SoftKeyRoute::ToggleRemoteConfig:
        return settings_controller::toggle_remote_config_enabled();
    case SoftKeyRoute::ToggleHomeAssistantEnabled:
        return settings_controller::toggle_home_assistant_enabled();
    case SoftKeyRoute::ToggleMqttEnabled:
        return settings_controller::toggle_mqtt_enabled();
    case SoftKeyRoute::ToggleAirTrafficEnabled:
        return settings_controller::toggle_air_traffic_enabled();
    case SoftKeyRoute::SelectScreenSaverLife:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Life);
    case SoftKeyRoute::SelectScreenSaverClock:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Clock);
    case SoftKeyRoute::SelectScreenSaverStarfield:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Starfield);
    case SoftKeyRoute::SelectScreenSaverMatrix:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Matrix);
    case SoftKeyRoute::SelectScreenSaverRadar:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Radar);
    case SoftKeyRoute::SelectScreenSaverRain:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Rain);
    case SoftKeyRoute::SelectScreenSaverWorms:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Worms);
    case SoftKeyRoute::SelectScreenSaverRandom:
        return settings_controller::select_screen_saver(ScreenSaverSelection::Random);
    case SoftKeyRoute::SelectWeatherHomeAssistant:
        return settings_controller::select_weather_source(WeatherSource::HomeAssistant);
    case SoftKeyRoute::SelectWeatherOpenMeteo:
        return settings_controller::select_weather_source(WeatherSource::OpenMeteo);
    case SoftKeyRoute::CycleWeatherPeriod:
        return settings_controller::cycle_weather_period(g_console_state);
    case SoftKeyRoute::SelectShareSlot1:
        return settings_controller::select_share_slot(g_console_state, 0U);
    case SoftKeyRoute::SelectShareSlot2:
        return settings_controller::select_share_slot(g_console_state, 1U);
    case SoftKeyRoute::SelectShareSlot3:
        return settings_controller::select_share_slot(g_console_state, 2U);
    case SoftKeyRoute::SelectShareSlot4:
        return settings_controller::select_share_slot(g_console_state, 3U);
    case SoftKeyRoute::SelectShareSlot5:
        return settings_controller::select_share_slot(g_console_state, 4U);
    case SoftKeyRoute::SelectShareSlot6:
        return settings_controller::select_share_slot(g_console_state, 5U);
    case SoftKeyRoute::CycleSharePeriod:
        return settings_controller::cycle_share_period(g_console_state);
    case SoftKeyRoute::ToggleAirTrafficViewMode:
        return settings_controller::toggle_air_traffic_view_mode(g_console_state);
    case SoftKeyRoute::CycleCalendarOwner:
        return calendar_controller::cycle_owner(g_console_state);
    case SoftKeyRoute::ResetCalendarFilters:
        return calendar_controller::reset_filters(g_console_state);
    case SoftKeyRoute::SelectCalendarSlot1:
        return calendar_controller::open_detail_from_slot(g_console_state, 0U);
    case SoftKeyRoute::SelectCalendarSlot2:
        return calendar_controller::open_detail_from_slot(g_console_state, 1U);
    case SoftKeyRoute::SelectCalendarSlot3:
        return calendar_controller::open_detail_from_slot(g_console_state, 2U);
    case SoftKeyRoute::SelectCalendarSlot4:
        return calendar_controller::open_detail_from_slot(g_console_state, 3U);
    case SoftKeyRoute::SelectCalendarSlot5:
        return calendar_controller::open_detail_from_slot(g_console_state, 4U);
    case SoftKeyRoute::SelectCalendarSlot6:
        return calendar_controller::open_detail_from_slot(g_console_state, 5U);
    case SoftKeyRoute::SelectCalendarSlot7:
        return calendar_controller::open_detail_from_slot(g_console_state, 6U);
    case SoftKeyRoute::SelectCalendarSlot8:
        return calendar_controller::open_detail_from_slot(g_console_state, 7U);
    case SoftKeyRoute::SelectTimeZoneSlot1:
        return settings_controller::select_time_zone_slot(g_console_state, 0U);
    case SoftKeyRoute::SelectTimeZoneSlot2:
        return settings_controller::select_time_zone_slot(g_console_state, 1U);
    case SoftKeyRoute::SelectTimeZoneSlot3:
        return settings_controller::select_time_zone_slot(g_console_state, 2U);
    case SoftKeyRoute::SelectTimeZoneSlot4:
        return settings_controller::select_time_zone_slot(g_console_state, 3U);
    case SoftKeyRoute::SelectTimeZoneSlot5:
        return settings_controller::select_time_zone_slot(g_console_state, 4U);
    case SoftKeyRoute::SelectTimeZoneSlot6:
        return settings_controller::select_time_zone_slot(g_console_state, 5U);
    case SoftKeyRoute::SelectTimeZoneSlot7:
        return settings_controller::select_time_zone_slot(g_console_state, 6U);
    case SoftKeyRoute::SelectTimeZoneSlot8:
        return settings_controller::select_time_zone_slot(g_console_state, 7U);
    case SoftKeyRoute::SelectAlertSlot1:
        return alert_controller::open_detail_from_slot(g_console_state, 0U);
    case SoftKeyRoute::SelectAlertSlot2:
        return alert_controller::open_detail_from_slot(g_console_state, 1U);
    case SoftKeyRoute::SelectAlertSlot3:
        return alert_controller::open_detail_from_slot(g_console_state, 2U);
    case SoftKeyRoute::SelectAlertSlot4:
        return alert_controller::open_detail_from_slot(g_console_state, 3U);
    case SoftKeyRoute::SelectAlertSlot5:
        return alert_controller::open_detail_from_slot(g_console_state, 4U);
    case SoftKeyRoute::SelectAlertSlot6:
        return alert_controller::open_detail_from_slot(g_console_state, 5U);
    case SoftKeyRoute::SelectAlertSlot7:
        return alert_controller::open_detail_from_slot(g_console_state, 6U);
    case SoftKeyRoute::SelectAlertSlot8:
        return alert_controller::open_detail_from_slot(g_console_state, 7U);
    case SoftKeyRoute::SelectAlertSlot9:
        return alert_controller::open_detail_from_slot(g_console_state, 8U);
    case SoftKeyRoute::AlertAccept:
        if (g_console_state.active_page != MenuPage::AlertDetail ||
            g_console_state.alert_detail_index >= g_console_state.alert_count)
        {
            return false;
        }
        alert_controller::accept_alert(g_console_state, g_console_state.alert_detail_index);
        g_console_state.active_page = MenuPage::AlertList;
        return true;
    case SoftKeyRoute::AlertIgnore:
        if (g_console_state.active_page != MenuPage::AlertDetail)
        {
            return false;
        }
        g_console_state.active_page = MenuPage::AlertList;
        return true;
    }

    return false;
}

} // namespace

/// @brief Initializes the console controller state and derived outputs.
void init()
{
    make_default_console_state(g_console_state);
    g_redraw_requested = false;
    alert_controller::reset();
    g_softkey_label_override_active.fill(false);

    for (auto& label : g_dynamic_softkey_labels)
    {
        label.fill('\0');
    }

    // Override storage is cleared explicitly so later strcmp checks can safely
    // treat an all-zero buffer as "no custom label".
    for (auto& label : g_softkey_label_overrides)
    {
        label.fill('\0');
    }
    update_softkeys_from_state();
    update_lamps_from_state();
}

const ConsoleState& state()
{
    return g_console_state;
}

/// @brief Marks the menu as needing a redraw on the next main-loop pass.
void request_redraw()
{
    update_softkeys_from_state();
    g_redraw_requested = true;
}

const PinterBrewTiming& pinter_brew_timing(uint8_t brew_index)
{
    return pinter_controller::brew_timing(brew_index);
}

/// @brief Returns and clears the pending redraw flag.
bool consume_redraw_request()
{
    const bool requested = g_redraw_requested;
    g_redraw_requested = false;
    return requested;
}

/// @brief Marks that non-hardware input should count as user activity.
void request_user_activity()
{
    g_user_activity_requested = true;
}

/// @brief Returns and clears the pending non-hardware user activity flag.
bool consume_user_activity_request()
{
    const bool requested = g_user_activity_requested;
    g_user_activity_requested = false;
    return requested;
}

/// @brief Applies persisted runtime preferences to the visible console state.
bool apply_runtime_config(const RuntimeConfig& settings)
{
    bool changed = false;

    const WeatherSource weather_source = settings.weather_source == WeatherSource::MetNorway
                                             ? WeatherSource::OpenMeteo
                                             : settings.weather_source;
    if (g_console_state.weather_source != weather_source)
    {
        g_console_state.weather_source = weather_source;
        changed = true;
    }
    if (g_console_state.time_zone != settings.time_zone)
    {
        g_console_state.time_zone = settings.time_zone;
        changed = true;
    }
    if (g_console_state.screen_saver_selection != settings.screen_saver)
    {
        g_console_state.screen_saver_selection = settings.screen_saver;
        changed = true;
    }
    if (g_console_state.screen_saver_timeout_minutes != settings.screen_saver_timeout_minutes)
    {
        g_console_state.screen_saver_timeout_minutes = settings.screen_saver_timeout_minutes;
        g_console_state.screen_saver_timeout_edit_minutes = settings.screen_saver_timeout_minutes;
        changed = true;
    }

    if (!changed)
    {
        return false;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    return true;
}

/// @brief Overwrites Pinter vessel state with previously persisted data.
void apply_persisted_pinters(const std::array<PinterStatus, kPinterCount>& pinters)
{
    g_console_state.pinters = pinters;
}

/// @brief Writes any pending Pinter state to flash, if a save is due.
/// @details Callers must invoke this from a point that is not nested inside
/// network request handling -- see pinter_controller::flush_pending_save()'s
/// doc comment for why.
bool flush_pending_pinter_save()
{
    return pinter_controller::flush_pending_save(g_console_state);
}

/// @brief Updates the cached Wi-Fi snapshot in the console model.
bool set_wifi_status(const WifiStatus& wifi_status)
{
    if (!status_controller::set_wifi_status(g_console_state, wifi_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached time snapshot in the console model.
bool set_time_status(const TimeStatus& time_status)
{
    if (!status_controller::set_time_status(g_console_state, time_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached Home Assistant snapshot in the console model.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status)
{
    if (!status_controller::set_home_assistant_status(g_console_state, home_assistant_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached MQTT snapshot in the console model.
bool set_mqtt_status(const MqttStatus& mqtt_status)
{
    if (!status_controller::set_mqtt_status(g_console_state, mqtt_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached local air-traffic snapshot in the console model.
bool set_air_traffic_status(const AirTrafficStatus& air_traffic_status)
{
    if (!status_controller::set_air_traffic_status(g_console_state, air_traffic_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached share market-data snapshot in the console model.
bool set_share_market_status(const ShareMarketStatus& share_market_status)
{
    if (!status_controller::set_share_market_status(g_console_state, share_market_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached environment sensor discovery snapshot in the console model.
bool set_environment_sensor_status(
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status)
{
    if (!status_controller::set_environment_sensor_status(g_console_state,
                                                           environment_sensor_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the keypad diagnostics snapshot shown by the UI.
bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status)
{
    if (!status_controller::set_keypad_monitor_status(g_console_state, keypad_status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates foreground main-loop load telemetry shown by Resources status.
bool set_main_loop_load_status(const MainLoopLoadStatus& status)
{
    if (!status_controller::set_main_loop_load_status(g_console_state, status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates live heap usage telemetry shown by Resources status.
bool set_heap_status(const HeapStatus& status)
{
    if (!status_controller::set_heap_status(g_console_state, status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates worst-case core-0 stack headroom telemetry shown by Resources status.
bool set_stack_status(const StackStatus& status)
{
    if (!status_controller::set_stack_status(g_console_state, status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates panel scanout timing telemetry shown by Resources status.
bool set_display_timing_status(const DisplayTimingStatus& status)
{
    if (!status_controller::set_display_timing_status(g_console_state, status))
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Applies or clears a temporary label override for one softkey.
bool set_softkey_label(SoftKeyId key, const char* label)
{
    const size_t kIndex = softkey_index(key);
    const bool kClearOverride = (label == nullptr) || (label[0] == '\0');

    // Clearing the override falls back to the page-defined label instead of
    // keeping an empty string that would mask the underlying softkey action.
    if (kClearOverride)
    {
        if (!g_softkey_label_override_active[kIndex] &&
            g_softkey_label_overrides[kIndex][0] == '\0')
        {
            return false;
        }

        g_softkey_label_override_active[kIndex] = false;
        g_softkey_label_overrides[kIndex][0] = '\0';
        update_softkeys_from_state();
        return true;
    }

    char copied_label[kSoftkeyLabelCapacity] = {};
    std::snprintf(copied_label, sizeof(copied_label), "%s", label);

    const bool kChanged = !g_softkey_label_override_active[kIndex] ||
                          std::strcmp(g_softkey_label_overrides[kIndex].data(), copied_label) != 0;
    if (!kChanged)
    {
        return false;
    }

    std::snprintf(g_softkey_label_overrides[kIndex].data(),
                  g_softkey_label_overrides[kIndex].size(), "%s", copied_label);
    g_softkey_label_override_active[kIndex] = true;
    update_softkeys_from_state();
    return true;
}

/// @brief Returns true for hard keys that represent text-entry surfaces.
bool is_text_entry_button(ButtonId id)
{
    const uint8_t value = static_cast<uint8_t>(id);
    if (value >= static_cast<uint8_t>(ButtonId::AlphaA) &&
        value <= static_cast<uint8_t>(ButtonId::AlphaZ))
    {
        return true;
    }

    switch (id)
    {
    case ButtonId::Slash:
    case ButtonId::TFunc:
    case ButtonId::Dot:
    case ButtonId::Zero:
    case ButtonId::Spc:
        return true;
    default:
        break;
    }

    return false;
}

/// @brief Applies globally active hard-key behaviours before softkey routing.
bool handle_direct_hard_key_event(ButtonId id)
{
    switch (id)
    {
    case ButtonId::Alert:
        return alert_controller::open_list_page(g_console_state);
    case ButtonId::Test:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        return true;
    case ButtonId::Brt:
        if (g_console_state.panel_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        return true;
    case ButtonId::Dim:
        if (g_console_state.panel_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        return true;
    case ButtonId::Ltrs:
        if (!cycle_letter_mode())
        {
            return false;
        }
        std::printf("LTRS mode -> %s\n", letter_mode_text(g_console_state.letter_mode));
        return true;
    default:
        break;
    }

    if (!is_text_entry_button(id))
    {
        return false;
    }

    char character = '\0';
    if (text_character_from_button(id, &character))
    {
        PERIODIC_LOG("Text key accepted: mode=%s char=%c\n",
                     letter_mode_text(g_console_state.letter_mode),
                     (character == ' ') ? '_' : character);
    }
    else
    {
        PERIODIC_LOG("Text key accepted: mode=%s key=%s\n",
                     letter_mode_text(g_console_state.letter_mode), input::button_name(id));
    }
    return true;
}

/// @brief Records one button event and applies any enabled softkey action.
bool handle_button_event(const ButtonEvent& event)
{
    if (event.type == ButtonEventType::None)
    {
        return false;
    }

    // Only press events drive menu navigation. The keypad debug page now shows
    // the live matrix decode directly, so release edges are ignored here.
    if (event.type != ButtonEventType::Pressed)
    {
        return false;
    }

    if (event.id == ButtonId::Alert || event.id == ButtonId::Test || event.id == ButtonId::Brt ||
        event.id == ButtonId::Dim || event.id == ButtonId::Ltrs)
    {
        const bool kHardKeyChanged = handle_direct_hard_key_event(event.id);
        if (!kHardKeyChanged)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
        return true;
    }

    if (g_console_state.screen_saver_timeout_editing)
    {
        const bool kEditChanged = settings_controller::handle_timeout_edit_event(g_console_state, event);
        if (kEditChanged)
        {
            update_softkeys_from_state();
            update_lamps_from_state();
            PERIODIC_LOG("Console state updated: page=%u timeout=%u edit=%s\n",
                         static_cast<unsigned>(g_console_state.active_page),
                         static_cast<unsigned>(g_console_state.screen_saver_timeout_minutes),
                         g_console_state.screen_saver_timeout_editing ? "on" : "off");
            return true;
        }

        if (!button_maps_to_softkey(event.id))
        {
            return false;
        }
    }

    if (event.id == ButtonId::BackStep)
    {
        // BACK STEP declines a pending reset rather than leaving the page.
        const bool kRouteChanged = g_console_state.pinter_reset_confirming
                                       ? apply_softkey_route(SoftKeyRoute::CancelPinterReset)
                                       : navigate_up_one_level();
        if (!kRouteChanged)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
        return true;
    }

    if (event.id == ButtonId::CursorLeft || event.id == ButtonId::CursorRight)
    {
        const int direction = (event.id == ButtonId::CursorLeft) ? -1 : 1;
        bool changed = false;
        if (g_console_state.active_page == MenuPage::Settings)
        {
            changed = settings_controller::change_page(g_console_state, direction);
        }
        else if (g_console_state.active_page == MenuPage::Calendar)
        {
            changed = calendar_controller::change_day(g_console_state, direction);
        }
        else if (g_console_state.active_page == MenuPage::AlertList)
        {
            const int next_page =
                static_cast<int>(g_console_state.alert_list_page_index) + direction;
            if (next_page >= 0 && next_page < static_cast<int>(alert_controller::page_count(g_console_state)))
            {
                g_console_state.alert_list_page_index = static_cast<uint8_t>(next_page);
                changed = true;
            }
        }
        else if (g_console_state.active_page == MenuPage::AlertDetail &&
                 g_console_state.alert_detail_index < g_console_state.alert_count)
        {
            const ActiveAlert& alert =
                g_console_state.active_alerts[g_console_state.alert_detail_index];
            // Scroll range comes from the renderer's own line wrapping so the
            // two cannot disagree.
            const uint8_t max_lines = alert_screens::detail_max_scroll(alert);
            if (direction < 0 && g_console_state.alert_detail_scroll_line > 0U)
            {
                --g_console_state.alert_detail_scroll_line;
                changed = true;
            }
            else if (direction > 0 && g_console_state.alert_detail_scroll_line < max_lines)
            {
                ++g_console_state.alert_detail_scroll_line;
                changed = true;
            }
        }
        else if (g_console_state.active_page == MenuPage::TimeZoneSettings)
        {
            changed = settings_controller::change_time_zone_page(g_console_state, direction);
        }
        else if (g_console_state.active_page == MenuPage::PinterSelectBrew)
        {
            changed = pinter_controller::change_list_page(g_console_state, direction);
        }
        else if (g_console_state.active_page == MenuPage::AirTraffic &&
                g_console_state.air_traffic_view_mode == AirTrafficViewMode::Tabular)
        {
            const int next_page = static_cast<int>(g_console_state.air_traffic_page_index) + direction;
            if (next_page >= 0 && next_page < static_cast<int>(settings_controller::air_traffic_page_count(g_console_state)))
            {
                g_console_state.air_traffic_page_index = static_cast<uint8_t>(next_page);
                changed = true;
            }
        }

        if (!changed)
        {
            return false;
        }

        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u settings=%u/%u calendar_day=%d\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     static_cast<unsigned>(g_console_state.settings_page_index + 1U),
                     static_cast<unsigned>(kSettingsPageCount),
                     static_cast<int>(g_console_state.calendar_day_offset));
        return true;
    }

    if (handle_direct_hard_key_event(event.id))
    {
        update_softkeys_from_state();
        update_lamps_from_state();
        PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                     static_cast<unsigned>(g_console_state.active_page),
                     letter_mode_text(g_console_state.letter_mode),
                     static_cast<unsigned>(g_console_state.alert_severity),
                     static_cast<unsigned>(g_console_state.test_state),
                     static_cast<unsigned>(g_console_state.panel_brightness),
                     static_cast<unsigned>(g_console_state.key_backlight_brightness));
        return true;
    }

    if (!button_maps_to_softkey(event.id))
    {
        return false;
    }

    const SoftKeyId kEy = softkey_id_from_button(event.id);
    const SoftKeyAction& action = g_console_state.softkeys[softkey_index(kEy)];
    if (!action.enabled)
    {
        return false;
    }

    // The route mutates the logical console state first, then softkeys and lamp
    // outputs are recomputed from that new state as a separate step.
    const bool kRouteChanged = apply_softkey_route(action.route);

    if (!kRouteChanged)
    {
        return false;
    }

    update_softkeys_from_state();
    update_lamps_from_state();
    PERIODIC_LOG("Console state updated: page=%u ltrs=%s alert=%u test=%u panel=%u keys=%u\n",
                 static_cast<unsigned>(g_console_state.active_page),
                 letter_mode_text(g_console_state.letter_mode),
                 static_cast<unsigned>(g_console_state.alert_severity),
                 static_cast<unsigned>(g_console_state.test_state),
                 static_cast<unsigned>(g_console_state.panel_brightness),
                 static_cast<unsigned>(g_console_state.key_backlight_brightness));
    return true;
}

/// @brief Cycles the test annunciator state for web preview bring-up.
bool cycle_test_lamp_preview()
{
    g_console_state.test_state = next_test_state(g_console_state.test_state);
    update_lamps_from_state();
    return true;
}

/// @brief Opens the alert list page on demand, preserving the caller page for IGNORE.
bool open_alert_page()
{
    const bool changed = alert_controller::open_list_page(g_console_state);
    if (changed)
    {
        update_softkeys_from_state();
        update_lamps_from_state();
    }
    return changed;
}

} // namespace console_controller
