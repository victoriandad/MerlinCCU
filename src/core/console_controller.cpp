#include "console_controller.h"

#include <cstddef>
#include <cstdio>
#include <cstring>

#include "alert_controller.h"
#include "alert_ordering.h"
#include "calendar_controller.h"
#include "console_controller_internal.h"
#include "cursor_navigation.h"
#include "debug_logging.h"
#include "page_table.h"
#include "pinter_controller.h"
#include "screens_shared.h"
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
    // Long enough for a recipe name used as an item caption (#115).
    constexpr size_t kSoftkeyTitleBufferSize = 48U;
    char title_upper[kSoftkeyTitleBufferSize] = {};
    text_case::copy_uppercase(title, title_upper, sizeof(title_upper));
    std::snprintf(buffer.data(), buffer.size(), "%s\n[%s]", title_upper, value);

    // LBL-6: a value too long for its one line is cut short here and ends in
    // "..", so the key shows that it was shortened rather than the renderer
    // silently dropping the end. Measured at the default softkey font, the
    // widest a caption-and-value key can use (#115). The caption keeps the
    // lines it needs (an item key's may wrap); the value gets one more.
    constexpr text_styles::TextStyle kFitStyle = text_styles::TextStyle::Softkey;
    const int value_lines =
        screens::layout_softkey_label(title_upper, kFitStyle, screens::kMaxSoftkeyLabelLines)
            .count +
        1;
    constexpr const char* kShortenedMark = "..";
    int keep = static_cast<int>(std::strlen(value));
    while (keep > 1 && !screens::softkey_label_fits(buffer.data(), kFitStyle, value_lines))
    {
        --keep;
        while (keep > 1 && value[keep - 1] == ' ')
        {
            --keep;
        }
        std::snprintf(buffer.data(), buffer.size(), "%s\n[%.*s%s]", title_upper, keep, value,
                      kShortenedMark);
    }
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

/// @brief Moves the active page one level up the menu hierarchy.
bool navigate_up_one_level()
{
    const MenuPage kParentPage = page_table::parent_page(g_console_state);
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

    if (g_console_state.active_page != MenuPage::Pinter)
    {
        g_console_state.pinter_reset_confirming = false;
    }

    SoftKeyMap softkeys = {};
    softkeys.fill(softkey::blank());
    // Each page's own keys come from its table entry's builder, in its
    // family's controller; every key is built by its role's builder (SK-1,
    // SK-2).
    const page_table::PageDescriptor& page = page_table::describe(g_console_state.active_page);
    if (page.build_softkeys != nullptr)
    {
        page.build_softkeys(g_console_state, softkeys);
    }

    // R5 is HOME on every page except Home itself, a page asking for a
    // decision or entry, where R5 confirms (NAV-2, NAV-3), and a page whose
    // builder gives R5 a page-specific meaning (Calendar's filter RESET).
    SoftKeyAction& r5 = softkeys[softkey_index(SoftKeyId::Right5)];
    if (g_console_state.active_page != MenuPage::Home &&
        !page_is_asking_for_decision(g_console_state) && r5.role == SoftKeyRole::Blank)
    {
        r5 = softkey::navigate("HOME", MenuPage::Home);
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
/// @details `slot` says which page, entry or option a slot route acts on;
/// values outside the route's range are refused rather than cast.
bool apply_softkey_route(SoftKeyRoute route, uint8_t slot)
{
    // Route handling mutates only the console model. Rendering and hardware
    // reactions happen later from the updated shared state.
    switch (route)
    {
    case SoftKeyRoute::None:
        return false;
    case SoftKeyRoute::GoPage:
        return page_table::open(g_console_state, static_cast<MenuPage>(slot));
    case SoftKeyRoute::ShowLocalGraph:
        if (slot > static_cast<uint8_t>(LocalConditionMetric::AirQuality))
        {
            return false;
        }
        g_console_state.local_condition_metric = static_cast<LocalConditionMetric>(slot);
        return page_table::open(g_console_state, MenuPage::LocalConditionGraph);
    case SoftKeyRoute::SelectPinterSlot:
        return pinter_controller::select_slot(g_console_state, slot);
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
    case SoftKeyRoute::SelectPinterListItem:
        return pinter_controller::select_list_item(g_console_state, slot);
    case SoftKeyRoute::SelectPinterMinimumTiming:
        return pinter_controller::set_pending_timing(g_console_state, true);
    case SoftKeyRoute::SelectPinterRecommendedTiming:
        return pinter_controller::set_pending_timing(g_console_state, false);
    case SoftKeyRoute::DecreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       -1, pinter_controller::kMinBrewDays,
                                                       pinter_controller::kMaxBrewDays);
    case SoftKeyRoute::IncreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       1, pinter_controller::kMinBrewDays,
                                                       pinter_controller::kMaxBrewDays);
    case SoftKeyRoute::DecreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, -1,
            pinter_controller::kMinConditioningDays, pinter_controller::kMaxConditioningDays);
    case SoftKeyRoute::IncreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, 1,
            pinter_controller::kMinConditioningDays, pinter_controller::kMaxConditioningDays);
    case SoftKeyRoute::DecreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, -1,
            pinter_controller::kMinColdCrashDays, pinter_controller::kMaxColdCrashDays);
    case SoftKeyRoute::IncreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, 1,
            pinter_controller::kMinColdCrashDays, pinter_controller::kMaxColdCrashDays);
    case SoftKeyRoute::ConfirmPinterStart:
        return pinter_controller::confirm_start(g_console_state);
    case SoftKeyRoute::EditScreenSaverTimeout:
        return settings_controller::start_timeout_editing(g_console_state);
    case SoftKeyRoute::ConfirmScreenSaverTimeout:
        return settings_controller::confirm_timeout_edit(g_console_state);
    case SoftKeyRoute::CancelScreenSaverTimeout:
        // Restores the saved value, as BACK STEP does.
        return settings_controller::stop_timeout_editing(g_console_state);
    case SoftKeyRoute::ToggleRemoteConfig:
        return settings_controller::toggle_remote_config_enabled();
    case SoftKeyRoute::ToggleHomeAssistantEnabled:
        return settings_controller::toggle_home_assistant_enabled();
    case SoftKeyRoute::ToggleMqttEnabled:
        return settings_controller::toggle_mqtt_enabled();
    case SoftKeyRoute::SelectScreenSaver:
        if (slot > static_cast<uint8_t>(ScreenSaverSelection::Random))
        {
            return false;
        }
        return settings_controller::select_screen_saver(static_cast<ScreenSaverSelection>(slot));
    case SoftKeyRoute::SelectWeatherSource:
        // MetNorway is a legacy stored value, not a choice the panel offers.
        if (slot != static_cast<uint8_t>(WeatherSource::HomeAssistant) &&
            slot != static_cast<uint8_t>(WeatherSource::OpenMeteo))
        {
            return false;
        }
        return settings_controller::select_weather_source(static_cast<WeatherSource>(slot));
    case SoftKeyRoute::CycleWeatherPeriod:
        return settings_controller::cycle_weather_period(g_console_state);
    case SoftKeyRoute::SelectShareSlot:
        return settings_controller::select_share_slot(g_console_state, slot);
    case SoftKeyRoute::CycleSharePeriod:
        return settings_controller::cycle_share_period(g_console_state);
    case SoftKeyRoute::CycleCalendarOwner:
        return calendar_controller::cycle_owner(g_console_state);
    case SoftKeyRoute::ResetCalendarFilters:
        return calendar_controller::reset_filters(g_console_state);
    case SoftKeyRoute::SelectCalendarSlot:
        return calendar_controller::open_detail_from_slot(g_console_state, slot);
    case SoftKeyRoute::SelectTimeZoneSlot:
        return settings_controller::select_time_zone_slot(g_console_state, slot);
    case SoftKeyRoute::SelectAlertSlot:
        return alert_controller::open_detail_from_slot(g_console_state, slot);
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
                                       ? apply_softkey_route(SoftKeyRoute::CancelPinterReset, 0U)
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
        // One definition per page of what the cursor keys move through
        // (cursor_navigation), shared with the footer arrows and title.
        const bool changed = cursor_navigation::move(g_console_state, direction);
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
    const bool kRouteChanged = apply_softkey_route(action.route, action.slot);

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
