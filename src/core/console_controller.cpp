#include "console_controller.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>

#include "alert_controller.h"
#include "alert_ordering.h"
#include "calendar_controller.h"
#include "console_controller_internal.h"
#include "debug_logging.h"
#include "pinter_controller.h"
#include "pinter_scheduling.h"
#include "settings_controller.h"

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
// Sized for the longest three-line Pinter slot label: title + "[state name]"
// (recipe names run up to ~42 characters) + "[Nd - rdy dd mon]".
constexpr size_t kSoftkeyLabelCapacity = 96;
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_dynamic_softkey_labels = {};
std::array<std::array<char, kSoftkeyLabelCapacity>, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_overrides = {};
std::array<bool, static_cast<size_t>(SoftKeyId::Count)> g_softkey_label_override_active = {};

void update_softkeys_from_state();
void update_lamps_from_state();
bool button_digit_value(ButtonId id, uint8_t* out_digit);

constexpr std::array<SoftKeyId, kPinterBrewListVisibleCount> kPinterBrewListSoftkeys = {
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

/// @brief Compares environment sensor snapshots without relying on structure padding.
bool environment_sensor_status_matches(
    const environment_sensor_manager::EnvironmentSensorStatus& lhs,
    const environment_sensor_manager::EnvironmentSensorStatus& rhs)
{
    if (lhs.enabled !=                                  rhs.enabled || 
        lhs.board !=                                    rhs.board || 
        lhs.health !=                                   rhs.health ||
        lhs.detected_device_count !=                    rhs.detected_device_count ||
        lhs.last_scan_ms !=                             rhs.last_scan_ms ||
        lhs.successful_scan_count !=                    rhs.successful_scan_count ||
        lhs.failed_scan_count !=                        rhs.failed_scan_count || 
        lhs.last_error !=                               rhs.last_error ||
        lhs.i2c_bus !=                                  rhs.i2c_bus || 
        lhs.sda_gpio !=                                 rhs.sda_gpio ||
        lhs.scl_gpio !=                                 rhs.scl_gpio || 
        lhs.baudrate_hz !=                              rhs.baudrate_hz ||
        lhs.bme_variant !=                              rhs.bme_variant || 
        lhs.bme_chip_id !=                              rhs.bme_chip_id ||
        lhs.bme_last_error !=                           rhs.bme_last_error ||
        lhs.bme_reading_valid !=                        rhs.bme_reading_valid ||
        lhs.bme_temperature_centi_celsius !=            rhs.bme_temperature_centi_celsius ||
        lhs.bme_pressure_pa !=                          rhs.bme_pressure_pa ||
        lhs.bme_humidity_milli_percent !=               rhs.bme_humidity_milli_percent ||
        lhs.bme_last_read_ms !=                         rhs.bme_last_read_ms ||
        lhs.bme_read_error !=                           rhs.bme_read_error ||
        lhs.bme_history_count !=                        rhs.bme_history_count ||
        lhs.bme_temperature_history_centi_celsius !=    rhs.bme_temperature_history_centi_celsius ||
        lhs.bme_pressure_history_deci_hpa !=            rhs.bme_pressure_history_deci_hpa ||
        lhs.bme_humidity_history_centi_percent !=       rhs.bme_humidity_history_centi_percent ||
        lhs.air_quality_raw_valid !=                    rhs.air_quality_raw_valid ||
        lhs.air_quality_raw_signal !=                   rhs.air_quality_raw_signal ||
        lhs.air_quality_baseline_raw_signal !=          rhs.air_quality_baseline_raw_signal ||
        lhs.air_quality_score_valid !=                  rhs.air_quality_score_valid ||
        lhs.air_quality_score !=                        rhs.air_quality_score ||
        lhs.air_quality_last_read_ms !=                 rhs.air_quality_last_read_ms ||
        lhs.air_quality_read_error !=                   rhs.air_quality_read_error ||
        lhs.air_quality_history_count !=                rhs.air_quality_history_count ||
        lhs.air_quality_history_score !=                rhs.air_quality_history_score)
    {
        return false;
    }

    for (size_t index = 0; index < lhs.devices.size(); ++index)
    {
        if (lhs.devices[index].device !=        rhs.devices[index].device ||
            lhs.devices[index].i2c_address !=   rhs.devices[index].i2c_address ||
            lhs.devices[index].detected !=      rhs.devices[index].detected)
        {
            return false;
        }
    }

    return true;
}

/// @brief Formats the list of currently active keypad panel pins.
void build_active_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                 std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;

    // These helpers flatten the electrical keypad snapshot into compact text so
    // the debug page can show bench-oriented panel-pin numbers directly.
    for (const auto& line : keypad_status.lines)
    {
        if (!line.configured || !line.active)
        {
            continue;
        }

        const int kWritten = std::snprintf(
            out_text.data() + used, 
            out_text.size() - used, 
            "%s%u", 
            (used == 0) ? "" : " ", 
            static_cast<unsigned>(line.panel_pin));

        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
}

/// @brief Formats the panel-pin list for the current probe hit mask.
void build_probe_hit_panel_pin_text(const KeypadMonitorStatus& keypad_status,
                                    std::array<char, 48>& out_text)
{
    out_text.fill('\0');
    size_t used = 0;
    for (size_t i = 0; i < keypad_status.lines.size(); ++i)
    {
        if ((keypad_status.probe_hit_mask & (1U << i)) == 0)
        {
            continue;
        }

        const int kWritten = std::snprintf(out_text.data() + used, out_text.size() - used, "%s%u",
                                           (used == 0) ? "" : " ",
                                           static_cast<unsigned>(keypad_status.lines[i].panel_pin));
        if (kWritten <= 0)
        {
            break;
        }

        const size_t kWriteSize = static_cast<size_t>(kWritten);
        if (kWriteSize >= (out_text.size() - used))
        {
            used = out_text.size() - 1;
            break;
        }
        used += kWriteSize;
    }
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

void build_uppercase_title(const char* input, char* output, size_t output_size)
{
    if (output == nullptr || output_size == 0U)
    {
        return;
    }

    output[0] = '\0';
    if (input == nullptr || input[0] == '\0')
    {
        return;
    }

    size_t write_index = 0U;
    while (input[write_index] != '\0' && write_index + 1U < output_size)
    {
        output[write_index] =
            static_cast<char>(std::toupper(static_cast<unsigned char>(input[write_index])));
        ++write_index;
    }

    output[write_index] = '\0';
}

/// @details The controller owns these buffers so menu pages can rebuild labels
/// whenever integration state changes without leaving dangling pointers behind.
const char* build_selection_softkey_label(SoftKeyId key, const char* title, const char* selection)
{
    auto& buffer = g_dynamic_softkey_labels[softkey_index(key)];
    const char* value = (selection != nullptr && selection[0] != '\0') ? selection : "-";
    constexpr size_t kSoftkeyTitleBufferSize = 24U;
    char title_upper[kSoftkeyTitleBufferSize] = {};
    build_uppercase_title(title, title_upper, sizeof(title_upper));
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
using console_controller_internal::build_uppercase_title;

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
    case MenuPage::Alignment:
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
        return MenuPage::Status;
    case MenuPage::DeviceSettings:
    case MenuPage::WifiSettings:
    case MenuPage::HomeAssistantSettings:
    case MenuPage::MqttSettings:
    case MenuPage::AirTrafficSettings:
    case MenuPage::ScreenSaverSettings:
    case MenuPage::WeatherSources:
    case MenuPage::TimeZoneSettings:
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
        return MenuPage::Settings;
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

    SoftKeyMap softkeys = {{
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
        {"", SoftKeyRoute::None, false},
    }};

    // Each page declares only the actions that make sense in that context. The
    // top-level menus are intentionally sparse, while deeper pages reserve `R5`
    // as a consistent one-press jump home.
    switch (g_console_state.active_page)
    {
    case MenuPage::Home:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"SHARES", SoftKeyRoute::GoShares, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"CALENDAR", SoftKeyRoute::GoCalendar, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            pinter_controller::build_home_softkey_label(SoftKeyId::Left4, g_console_state),
            SoftKeyRoute::GoPinter, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {"SETTINGS", SoftKeyRoute::GoSettings, true};
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "WEATHER",
                                          settings_controller::weather_source_selection_text(g_console_state)),
            SoftKeyRoute::GoWeather,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            "LOCAL\nCONDITIONS", SoftKeyRoute::GoLocalConditions, true};
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            "ADS-B\nTRAFFIC", SoftKeyRoute::GoAirTraffic, true};
        break;
    case MenuPage::Calendar:
    {
        constexpr std::array<SoftKeyId, 8> slots = {
            SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,  SoftKeyId::Left4,
            SoftKeyId::Right1, SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};
        constexpr std::array<SoftKeyRoute, 8> routes = {
            SoftKeyRoute::SelectCalendarSlot1, SoftKeyRoute::SelectCalendarSlot2,
            SoftKeyRoute::SelectCalendarSlot3, SoftKeyRoute::SelectCalendarSlot4,
            SoftKeyRoute::SelectCalendarSlot5, SoftKeyRoute::SelectCalendarSlot6,
            SoftKeyRoute::SelectCalendarSlot7, SoftKeyRoute::SelectCalendarSlot8};
        uint8_t visible_index = 0U;
        const uint8_t event_count =
            std::min(g_console_state.calendar_event_count,
                     static_cast<uint8_t>(g_console_state.calendar_events.size()));
        for (uint8_t i = 0U; i < event_count && visible_index < slots.size(); ++i)
        {
            const CalendarEvent& event = g_console_state.calendar_events[i];
            if (!calendar_controller::event_matches_filter(g_console_state, event))
            {
                continue;
            }

            const SoftKeyId slot = slots[visible_index];
            softkeys[softkey_index(slot)] = {
                calendar_controller::build_event_softkey_label(slot, event),
                routes[visible_index],
                true,
            };
            ++visible_index;
        }
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERSON",
                                          calendar_controller::owner_selection_text(g_console_state)),
            SoftKeyRoute::CycleCalendarOwner,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right5)] =
            calendar_controller::filters_are_default(g_console_state)
                ? SoftKeyAction{"HOME", SoftKeyRoute::GoHome, true}
                : SoftKeyAction{"RESET", SoftKeyRoute::ResetCalendarFilters, true};
        break;
    }
    case MenuPage::CalendarDetail:
        break;
    case MenuPage::Weather:
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERIOD",
                                          settings_controller::weather_period_selection_text(g_console_state)),
            SoftKeyRoute::CycleWeatherPeriod,
            true,
        };
        break;
    case MenuPage::AirTraffic:
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "VIEW",
                                          settings_controller::air_traffic_view_mode_selection_text(g_console_state)),
            SoftKeyRoute::ToggleAirTrafficViewMode,
            true,
        };
        break;
    case MenuPage::Pinter:
    {
        constexpr std::array<SoftKeyId, kPinterCount> slots = {
            SoftKeyId::Left1,
            SoftKeyId::Left2,
            SoftKeyId::Left3,
            SoftKeyId::Left4,
        };
        constexpr std::array<SoftKeyRoute, kPinterCount> routes = {
            SoftKeyRoute::SelectPinterSlot1,
            SoftKeyRoute::SelectPinterSlot2,
            SoftKeyRoute::SelectPinterSlot3,
            SoftKeyRoute::SelectPinterSlot4,
        };
        for (size_t i = 0U; i < g_console_state.pinters.size(); ++i)
        {
            softkeys[softkey_index(slots[i])] = {
                pinter_controller::build_slot_softkey_label(g_console_state, slots[i],
                                                            g_console_state.pinters[i]),
                routes[i],
                true,
                i == g_console_state.selected_pinter_index,
            };
        }
        const bool selected_pinter_idle =
            pinter_controller::selected_const(g_console_state).state == PinterState::Idle;
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            pinter_controller::build_primary_action_label(SoftKeyId::Right1, g_console_state),
            selected_pinter_idle ? SoftKeyRoute::GoPinterSelectBrew
                                 : SoftKeyRoute::ApplyPinterPrimaryAction,
            selected_pinter_idle
                ? pinter_controller::brew_dock_count(g_console_state) < kPinterBrewDockCapacity
                : pinter_controller::primary_action_enabled(g_console_state),
        };
        pinter_controller::update_block_reason(g_console_state);
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            "RESET",
            SoftKeyRoute::ResetSelectedPinter,
            pinter_controller::selected_const(g_console_state).state != PinterState::Idle,
        };
        {
            const uint8_t selected_stage_days = pinter_scheduling::current_stage_planned_days(
                pinter_controller::selected_const(g_console_state));
            softkeys[softkey_index(SoftKeyId::Right2)] = {
                "-1 DAY",
                SoftKeyRoute::DecreaseSelectedPinterDay,
                selected_stage_days > 1U,
            };
            softkeys[softkey_index(SoftKeyId::Right4)] = {
                "+1 DAY",
                SoftKeyRoute::IncreaseSelectedPinterDay,
                selected_stage_days > 0U &&
                    selected_stage_days < std::numeric_limits<uint8_t>::max(),
            };
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
            const SoftKeyId key = kPinterBrewListSoftkeys[i];
            softkeys[softkey_index(key)] = {
                pinter_controller::build_catalogue_item_label(key, static_cast<uint8_t>(brew_index)),
                kPinterBrewListRoutes[i],
                true,
            };
        }
        softkeys[softkey_index(SoftKeyId::Right4)] = {"PINTER", SoftKeyRoute::GoPinter, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    }
    case MenuPage::PinterStartTiming:
    {
        const PinterBrewTiming& brew =
            pinter_controller::brew_timing(g_console_state.pinter_pending_brew_index);
        softkeys[softkey_index(SoftKeyId::Left1)] = {"MINIMUM",
                                                     SoftKeyRoute::SelectPinterMinimumTiming,
                                                     true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"RECOMM",
                                                     SoftKeyRoute::SelectPinterRecommendedTiming,
                                                     true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            pinter_controller::build_days_label(SoftKeyId::Left3, "BREW -",
                                                g_console_state.pinter_pending_brewing_days),
            SoftKeyRoute::DecreasePinterBrewDays,
            g_console_state.pinter_pending_brewing_days > 1U,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            pinter_controller::build_days_label(SoftKeyId::Right3, "BREW +",
                                                g_console_state.pinter_pending_brewing_days),
            SoftKeyRoute::IncreasePinterBrewDays,
            g_console_state.pinter_pending_brewing_days < 30U,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            pinter_controller::build_days_label(SoftKeyId::Left4, "COND -",
                                                g_console_state.pinter_pending_conditioning_days),
            SoftKeyRoute::DecreasePinterConditioningDays,
            g_console_state.pinter_pending_conditioning_days > 1U,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            pinter_controller::build_days_label(SoftKeyId::Right4, "COND +",
                                                g_console_state.pinter_pending_conditioning_days),
            SoftKeyRoute::IncreasePinterConditioningDays,
            g_console_state.pinter_pending_conditioning_days < 45U,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            pinter_controller::build_days_label(SoftKeyId::Left5, "CRASH -",
                                                g_console_state.pinter_pending_cold_crash_days),
            SoftKeyRoute::DecreasePinterColdCrashDays,
            g_console_state.pinter_pending_cold_crash_days > 0U,
        };
        softkeys[softkey_index(SoftKeyId::Right5)] = {
            pinter_controller::build_days_label(SoftKeyId::Right5, "CRASH +",
                                                g_console_state.pinter_pending_cold_crash_days),
            SoftKeyRoute::IncreasePinterColdCrashDays,
            g_console_state.pinter_pending_cold_crash_days < 3U,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1, "START",
                pinter_controller::selected_const(g_console_state).label.data()),
            SoftKeyRoute::ConfirmPinterStart,
            pinter_controller::can_start(g_console_state),
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "BREW", brew.name),
            SoftKeyRoute::GoPinterSelectBrew,
            true,
        };
        break;
    }
    case MenuPage::Shares:
    {
        constexpr std::array<SoftKeyId, kMaxWatchedShares> slots = {
            SoftKeyId::Left1, SoftKeyId::Left2, SoftKeyId::Left3,
            SoftKeyId::Left4, SoftKeyId::Left5, SoftKeyId::Right3};
        constexpr std::array<SoftKeyRoute, kMaxWatchedShares> routes = {
            SoftKeyRoute::SelectShareSlot1, SoftKeyRoute::SelectShareSlot2,
            SoftKeyRoute::SelectShareSlot3, SoftKeyRoute::SelectShareSlot4,
            SoftKeyRoute::SelectShareSlot5, SoftKeyRoute::SelectShareSlot6};

        for (size_t i = 0U; i < slots.size(); ++i)
        {
            if (i >= g_console_state.share_count)
            {
                break;
            }
            const ShareWatchEntry& share = g_console_state.watched_shares[i];
            softkeys[softkey_index(slots[i])] = {
                build_selection_softkey_label(slots[i], share.display_name.data(),
                                              share.price_text.data()),
                routes[i],
                true,
            };
        }
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            "HISTORY", SoftKeyRoute::GoSelectedShareDetail, g_console_state.share_count > 0U};
        softkeys[softkey_index(SoftKeyId::Right2)] = {"REMOVE", SoftKeyRoute::None, false};
        break;
    }
    case MenuPage::ShareDetail:
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "PERIOD",
                                          settings_controller::share_period_selection_text(g_console_state)),
            SoftKeyRoute::CycleSharePeriod,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {"SHARES", SoftKeyRoute::GoShares, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    case MenuPage::Status:
        softkeys[softkey_index(SoftKeyId::Left1)] = {"OVERVIEW", SoftKeyRoute::GoStatusOverview, true};
        softkeys[softkey_index(SoftKeyId::Left2)] = {"CONNECT", SoftKeyRoute::GoStatusConnectivity, true};
        softkeys[softkey_index(SoftKeyId::Left3)] = {"RESOURCES", SoftKeyRoute::GoStatusResources, true};
        softkeys[softkey_index(SoftKeyId::Left4)] = {"SENSORS", SoftKeyRoute::GoStatusSensors, true};
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            "INTEGR.", SoftKeyRoute::GoStatusIntegrations, true};
        softkeys[softkey_index(SoftKeyId::Right3)] = {"GREYSCL", SoftKeyRoute::GoGreyscaleTest, true};
        softkeys[softkey_index(SoftKeyId::Right4)] = {"KEYPAD", SoftKeyRoute::GoKeypadDebug, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    case MenuPage::StatusOverview:
    case MenuPage::StatusConnectivity:
    case MenuPage::StatusResources:
    case MenuPage::StatusSensors:
    case MenuPage::StatusIntegrations:
        if (g_console_state.active_page == MenuPage::StatusSensors)
        {
            softkeys[softkey_index(SoftKeyId::Right4)] = {"KEYPAD", SoftKeyRoute::GoKeypadDebug, true};
        }
        softkeys[softkey_index(SoftKeyId::Right5)] = {"STATUS", SoftKeyRoute::GoStatus, true};
        break;
    case MenuPage::LocalConditions:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "TEMP",
                                          settings_controller::local_temperature_selection_text(g_console_state)),
            SoftKeyRoute::ShowLocalTemperatureGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "HUMIDITY",
                                          settings_controller::local_humidity_selection_text(g_console_state)),
            SoftKeyRoute::ShowLocalHumidityGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(SoftKeyId::Left3, "AIR PRESSURE",
                                          settings_controller::local_pressure_selection_text(g_console_state)),
            SoftKeyRoute::ShowLocalPressureGraph,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "VOC CHANGE",
                                          settings_controller::local_air_quality_selection_text(g_console_state)),
            SoftKeyRoute::ShowLocalAirQualityGraph,
            true,
        };
        break;
    case MenuPage::LocalConditionGraph:
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            "BACK", SoftKeyRoute::GoLocalConditions, true};
        break;
    case MenuPage::Settings:
        if (g_console_state.settings_page_index == 0U)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {
                build_selection_softkey_label(SoftKeyId::Left1, "DEVICE IDENTITY",
                                              settings_controller::device_identity_selection_text()),
                SoftKeyRoute::GoDeviceSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left2)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left2, "REMOTE CONFIG",
                    settings_controller::enabled_selection_text(config_manager::settings().remote_config_enabled)),
                SoftKeyRoute::ToggleRemoteConfig,
                true,
                config_manager::settings().remote_config_enabled,
            };
            softkeys[softkey_index(SoftKeyId::Left3)] = {
                build_selection_softkey_label(SoftKeyId::Left3, "NETWORK",
                                              settings_controller::wifi_selection_text(g_console_state)),
                SoftKeyRoute::GoWifiSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left4)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left4, "HOME ASSISTANT",
                    settings_controller::enabled_selection_text(config_manager::settings().home_assistant_enabled)),
                SoftKeyRoute::GoHomeAssistantSettings,
                true,
            };
        }
        else
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {
                build_selection_softkey_label(
                    SoftKeyId::Left1, "MQTT DISCOVERY",
                    settings_controller::enabled_selection_text(config_manager::settings().mqtt_enabled)),
                SoftKeyRoute::GoMqttSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left2)] = {
                build_selection_softkey_label(SoftKeyId::Left2, "WEATHER SOURCE",
                                              settings_controller::weather_source_selection_text(g_console_state)),
                SoftKeyRoute::GoWeatherSources,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left3)] = {
                build_selection_softkey_label(SoftKeyId::Left3, "DISPLAY & TIME",
                                              settings_controller::time_zone_selection_text(g_console_state)),
                SoftKeyRoute::GoTimeZoneSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Left4)] = {
                build_selection_softkey_label(SoftKeyId::Left4, "SCREEN SAVER",
                                              settings_controller::screen_saver_selection_text(g_console_state)),
                SoftKeyRoute::GoScreenSaverSettings,
                true,
            };
            softkeys[softkey_index(SoftKeyId::Right1)] = {
                build_selection_softkey_label(
                    SoftKeyId::Right1, "ADS-B TRAFFIC",
                    settings_controller::enabled_selection_text(config_manager::settings().air_traffic_enabled)),
                SoftKeyRoute::GoAirTrafficSettings,
                true,
            };
        }
        break;
    case MenuPage::DeviceSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "NAME",
                                          config_manager::settings().device_name.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "LABEL",
                                          config_manager::settings().device_label.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(SoftKeyId::Left3, "LOCATION",
                                          config_manager::settings().location.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "ROOM",
                                          config_manager::settings().room.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::WifiSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "SSID",
                                          config_manager::settings().wifi_ssid.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(
                SoftKeyId::Left2, "PASSWORD",
                settings_controller::secret_selection_text(config_manager::settings().wifi_password[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::HomeAssistantSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "REST API",
                settings_controller::enabled_selection_text(config_manager::settings().home_assistant_enabled)),
            SoftKeyRoute::ToggleHomeAssistantEnabled,
            true,
            config_manager::settings().home_assistant_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "HOST",
                                          config_manager::settings().home_assistant_host.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "PORT",
                settings_controller::port_selection_text(SoftKeyId::Left3,
                                    config_manager::settings().home_assistant_port)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(
                SoftKeyId::Left4, "TOKEN",
                settings_controller::secret_selection_text(config_manager::settings().home_assistant_token[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(
                SoftKeyId::Left5, "TRACKED",
                config_manager::settings().home_assistant_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1, "SELF",
                config_manager::settings().home_assistant_self_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::MqttSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "MQTT",
                settings_controller::enabled_selection_text(config_manager::settings().mqtt_enabled)),
            SoftKeyRoute::ToggleMqttEnabled,
            true,
            config_manager::settings().mqtt_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "BROKER",
                                          config_manager::settings().mqtt_host.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "PORT",
                settings_controller::port_selection_text(SoftKeyId::Left3, config_manager::settings().mqtt_port)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(SoftKeyId::Left4, "USERNAME",
                                          config_manager::settings().mqtt_username.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(
                SoftKeyId::Left5, "PASSWORD",
                settings_controller::secret_selection_text(config_manager::settings().mqtt_password[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(SoftKeyId::Right1, "PREFIX",
                                          config_manager::settings().mqtt_discovery_prefix.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "TOPIC",
                                          config_manager::settings().mqtt_base_topic.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::AirTrafficSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(
                SoftKeyId::Left1, "ADS-B",
                settings_controller::enabled_selection_text(config_manager::settings().air_traffic_enabled)),
            SoftKeyRoute::ToggleAirTrafficEnabled,
            true,
            config_manager::settings().air_traffic_enabled,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            build_selection_softkey_label(SoftKeyId::Left2, "HOST",
                                          config_manager::settings().air_traffic_host.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            build_selection_softkey_label(
                SoftKeyId::Left3, "PORT",
                settings_controller::port_selection_text(SoftKeyId::Left3, config_manager::settings().air_traffic_port)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            build_selection_softkey_label(
                SoftKeyId::Left4, "RADIUS",
                settings_controller::radius_nm_selection_text(SoftKeyId::Left4,
                                         config_manager::settings().air_traffic_radius_nm)),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            build_selection_softkey_label(SoftKeyId::Left5, "COORDS",
                                          config_manager::settings().air_traffic_coordinates.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1, "API KEY",
                settings_controller::secret_selection_text(config_manager::settings().air_traffic_api_key[0] != '\0')),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::ScreenSaverSettings:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            build_selection_softkey_label(SoftKeyId::Left1, "TIMEOUT PERIOD",
                                          settings_controller::screen_saver_timeout_selection_text(g_console_state)),
            SoftKeyRoute::EditScreenSaverTimeout,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_timeout_editing,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Life).option_label,
            SoftKeyRoute::SelectScreenSaverLife,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Life,
        };
        softkeys[softkey_index(SoftKeyId::Left3)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Clock).option_label,
            SoftKeyRoute::SelectScreenSaverClock,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Clock,
        };
        softkeys[softkey_index(SoftKeyId::Left4)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Starfield).option_label,
            SoftKeyRoute::SelectScreenSaverStarfield,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Starfield,
        };
        softkeys[softkey_index(SoftKeyId::Left5)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Random).option_label,
            SoftKeyRoute::SelectScreenSaverRandom,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Random,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Matrix).option_label,
            SoftKeyRoute::SelectScreenSaverMatrix,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Matrix,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Radar).option_label,
            SoftKeyRoute::SelectScreenSaverRadar,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Radar,
        };
        softkeys[softkey_index(SoftKeyId::Right3)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Rain).option_label,
            SoftKeyRoute::SelectScreenSaverRain,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Rain,
        };
        softkeys[softkey_index(SoftKeyId::Right4)] = {
            settings_controller::screen_saver_definition(ScreenSaverSelection::Worms).option_label,
            SoftKeyRoute::SelectScreenSaverWorms,
            !g_console_state.screen_saver_timeout_editing,
            g_console_state.screen_saver_selection == ScreenSaverSelection::Worms,
        };
        if (g_console_state.screen_saver_timeout_editing)
        {
            softkeys[softkey_index(SoftKeyId::Right5)] = {
                "ENTER",
                SoftKeyRoute::ConfirmScreenSaverTimeout,
                true,
            };
        }
        break;
    case MenuPage::WeatherSources:
        softkeys[softkey_index(SoftKeyId::Left1)] = {
            settings_controller::weather_source_definition(WeatherSource::HomeAssistant).option_label,
            SoftKeyRoute::SelectWeatherHomeAssistant,
            true,
            g_console_state.weather_source == WeatherSource::HomeAssistant,
        };
        softkeys[softkey_index(SoftKeyId::Left2)] = {
            settings_controller::weather_source_definition(WeatherSource::OpenMeteo).option_label,
            SoftKeyRoute::SelectWeatherOpenMeteo,
            true,
            g_console_state.weather_source == WeatherSource::OpenMeteo,
        };
        softkeys[softkey_index(SoftKeyId::Right1)] = {
            build_selection_softkey_label(
                SoftKeyId::Right1,
                g_console_state.weather_source == WeatherSource::HomeAssistant ? "WEATHER"
                                                                               : "COORDS",
                g_console_state.weather_source == WeatherSource::HomeAssistant
                    ? config_manager::settings().weather_entity_id.data()
                    : config_manager::settings().weather_coordinates.data()),
            SoftKeyRoute::None,
            true,
        };
        softkeys[softkey_index(SoftKeyId::Right2)] = {
            build_selection_softkey_label(SoftKeyId::Right2, "SUN",
                                          config_manager::settings().sun_entity_id.data()),
            SoftKeyRoute::None,
            true,
        };
        break;
    case MenuPage::TimeZoneSettings:
    {
        const settings_controller::TimeZoneDefinition* west_one = settings_controller::relative_time_zone_definition(g_console_state, -1);
        const settings_controller::TimeZoneDefinition* west_two = settings_controller::relative_time_zone_definition(g_console_state, -2);
        const settings_controller::TimeZoneDefinition* west_three = settings_controller::relative_time_zone_definition(g_console_state, -3);
        const settings_controller::TimeZoneDefinition* west_four = settings_controller::relative_time_zone_definition(g_console_state, -4);
        const settings_controller::TimeZoneDefinition* east_one = settings_controller::relative_time_zone_definition(g_console_state, 1);
        const settings_controller::TimeZoneDefinition* east_two = settings_controller::relative_time_zone_definition(g_console_state, 2);
        const settings_controller::TimeZoneDefinition* east_three = settings_controller::relative_time_zone_definition(g_console_state, 3);
        const settings_controller::TimeZoneDefinition* east_four = settings_controller::relative_time_zone_definition(g_console_state, 4);

        if (west_one != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left1)] = {west_one->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest1, true};
        }
        if (west_two != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left2)] = {west_two->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest2, true};
        }
        if (west_three != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left3)] = {west_three->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest3, true};
        }
        if (west_four != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Left4)] = {west_four->option_label,
                                                         SoftKeyRoute::SelectTimeZoneWest4, true};
        }
        if (east_one != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right1)] = {east_one->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast1, true};
        }
        if (east_two != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right2)] = {east_two->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast2, true};
        }
        if (east_three != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right3)] = {
                east_three->option_label,
                SoftKeyRoute::SelectTimeZoneEast3,
                true,
            };
        }
        if (east_four != nullptr)
        {
            softkeys[softkey_index(SoftKeyId::Right4)] = {east_four->option_label,
                                                          SoftKeyRoute::SelectTimeZoneEast4, true};
        }
        break;
    }
    case MenuPage::Alignment:
    case MenuPage::KeypadDebug:
    case MenuPage::GreyscaleTest:
        break;
    case MenuPage::AlertList:
    {
        std::array<uint8_t, kActiveAlertCapacity> alert_indices = {};
        uint8_t sorted_count = 0U;
        alert_controller::build_display_indices(g_console_state, alert_indices, &sorted_count);
        constexpr uint8_t kAlertsPerPage = 9U;
        const uint8_t page_start =
            static_cast<uint8_t>(g_console_state.alert_list_page_index * kAlertsPerPage);
        const std::array<SoftKeyId, 9> slots = {
            SoftKeyId::Left1,  SoftKeyId::Left2,  SoftKeyId::Left3,
            SoftKeyId::Left4,  SoftKeyId::Left5,  SoftKeyId::Right1,
            SoftKeyId::Right2, SoftKeyId::Right3, SoftKeyId::Right4};
        const std::array<SoftKeyRoute, 9> routes = {
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
            softkeys[softkey_index(slots[i])] = {alert_controller::build_softkey_label(slots[i], alert),
                                                 routes[i], true};
        }
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
        break;
    }
    case MenuPage::AlertDetail:
        softkeys[softkey_index(SoftKeyId::Left5)] = {"IGNORE", SoftKeyRoute::AlertIgnore, true};
        softkeys[softkey_index(SoftKeyId::Right5)] = {"ACCEPT", SoftKeyRoute::AlertAccept, true};
        break;
    }

    if (g_console_state.active_page != MenuPage::Home &&
        g_console_state.active_page != MenuPage::Calendar &&
        g_console_state.active_page != MenuPage::AlertList &&
        g_console_state.active_page != MenuPage::AlertDetail &&
        !(g_console_state.active_page == MenuPage::ScreenSaverSettings &&
          g_console_state.screen_saver_timeout_editing))
    {
        softkeys[softkey_index(SoftKeyId::Right5)] = {"HOME", SoftKeyRoute::GoHome, true};
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
    case SoftKeyRoute::ResetSelectedPinter:
        return pinter_controller::reset_selected(g_console_state);
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
    case SoftKeyRoute::PinterListPreviousPage:
        return pinter_controller::change_list_page(g_console_state, -1);
    case SoftKeyRoute::PinterListNextPage:
        return pinter_controller::change_list_page(g_console_state, 1);
    case SoftKeyRoute::SelectPinterMinimumTiming:
        return pinter_controller::set_pending_timing(g_console_state, true);
    case SoftKeyRoute::SelectPinterRecommendedTiming:
        return pinter_controller::set_pending_timing(g_console_state, false);
    case SoftKeyRoute::DecreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       -1, 1U, 30U);
    case SoftKeyRoute::IncreasePinterBrewDays:
        return pinter_controller::adjust_pending_days(g_console_state.pinter_pending_brewing_days,
                                                       1, 1U, 30U);
    case SoftKeyRoute::DecreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, -1, 1U, 45U);
    case SoftKeyRoute::IncreasePinterConditioningDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_conditioning_days, 1, 1U, 45U);
    case SoftKeyRoute::DecreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, -1, 0U, 3U);
    case SoftKeyRoute::IncreasePinterColdCrashDays:
        return pinter_controller::adjust_pending_days(
            g_console_state.pinter_pending_cold_crash_days, 1, 0U, 3U);
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
    case SoftKeyRoute::EditScreenSaverTimeout:
        return settings_controller::start_timeout_editing(g_console_state);
    case SoftKeyRoute::ConfirmScreenSaverTimeout:
        return settings_controller::confirm_timeout_edit(g_console_state);
    case SoftKeyRoute::GoWeatherSources:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::WeatherSources;
        return true;
    case SoftKeyRoute::GoTimeZoneSettings:
        settings_controller::stop_timeout_editing(g_console_state);
        g_console_state.active_page = MenuPage::TimeZoneSettings;
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
    case SoftKeyRoute::GoSelectedShareDetail:
        return settings_controller::open_selected_share_detail(g_console_state);
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
    case SoftKeyRoute::SelectCalendarSlot9:
        return calendar_controller::open_detail_from_slot(g_console_state, 8U);
    case SoftKeyRoute::SelectTimeZoneWest1:
        return settings_controller::select_relative_time_zone(g_console_state, -1);
    case SoftKeyRoute::SelectTimeZoneWest2:
        return settings_controller::select_relative_time_zone(g_console_state, -2);
    case SoftKeyRoute::SelectTimeZoneWest3:
        return settings_controller::select_relative_time_zone(g_console_state, -3);
    case SoftKeyRoute::SelectTimeZoneWest4:
        return settings_controller::select_relative_time_zone(g_console_state, -4);
    case SoftKeyRoute::SelectTimeZoneEast1:
        return settings_controller::select_relative_time_zone(g_console_state, 1);
    case SoftKeyRoute::SelectTimeZoneEast2:
        return settings_controller::select_relative_time_zone(g_console_state, 2);
    case SoftKeyRoute::SelectTimeZoneEast3:
        return settings_controller::select_relative_time_zone(g_console_state, 3);
    case SoftKeyRoute::SelectTimeZoneEast4:
        return settings_controller::select_relative_time_zone(g_console_state, 4);
    case SoftKeyRoute::CycleAlert:
        return alert_controller::open_list_page(g_console_state);
    case SoftKeyRoute::ToggleLetters:
        return cycle_letter_mode();
    case SoftKeyRoute::CycleTest:
        g_console_state.test_state = next_test_state(g_console_state.test_state);
        return true;
    case SoftKeyRoute::ResetConsoleState:
        make_default_console_state(g_console_state);
        alert_controller::reset();
        return true;
    case SoftKeyRoute::ClearAlert:
        if (g_console_state.alert_severity == AlertSeverity::None)
        {
            return false;
        }
        g_console_state.alert_severity = AlertSeverity::None;
        return true;
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
        alert_controller::suppress_alert_code(
            g_console_state.active_alerts[g_console_state.alert_detail_index].code);
        alert_controller::erase_active_alert(g_console_state, g_console_state.alert_detail_index);
        g_console_state.active_page = MenuPage::AlertList;
        return true;
    case SoftKeyRoute::AlertIgnore:
        if (g_console_state.active_page != MenuPage::AlertDetail)
        {
            return false;
        }
        g_console_state.active_page = MenuPage::AlertList;
        return true;
    case SoftKeyRoute::PanelBrighter:
        if (g_console_state.panel_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.panel_brightness = brighter(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::PanelDimmer:
        if (g_console_state.panel_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.panel_brightness = dimmer(g_console_state.panel_brightness);
        return true;
    case SoftKeyRoute::KeysBrighter:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::High)
        {
            return false;
        }
        g_console_state.key_backlight_brightness =
            brighter(g_console_state.key_backlight_brightness);
        return true;
    case SoftKeyRoute::KeysDimmer:
        if (g_console_state.key_backlight_brightness == BrightnessLevel::Off)
        {
            return false;
        }
        g_console_state.key_backlight_brightness = dimmer(g_console_state.key_backlight_brightness);
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
    // These setters short-circuit unchanged snapshots so the UI does not redraw
    // every loop when the subsystem state is stable.
    const bool kChanged =
        g_console_state.wifi_status.state != wifi_status.state ||
        g_console_state.wifi_status.credentials_present != wifi_status.credentials_present ||
        g_console_state.wifi_status.internet_reachable != wifi_status.internet_reachable ||
        g_console_state.wifi_status.internet_probe_pending != wifi_status.internet_probe_pending ||
        g_console_state.wifi_status.last_error != wifi_status.last_error ||
        g_console_state.wifi_status.link_status != wifi_status.link_status ||
        g_console_state.wifi_status.internet_rtt_ms != wifi_status.internet_rtt_ms ||
        g_console_state.wifi_status.auth_mode != wifi_status.auth_mode ||
        g_console_state.wifi_status.mac_address != wifi_status.mac_address ||
        g_console_state.wifi_status.ssid != wifi_status.ssid ||
        g_console_state.wifi_status.ip_address != wifi_status.ip_address;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.wifi_status = wifi_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached time snapshot in the console model.
bool set_time_status(const TimeStatus& time_status)
{
    const bool kChanged = g_console_state.time_status.synced != time_status.synced ||
                          g_console_state.time_status.time_text != time_status.time_text ||
                          g_console_state.time_status.date_text != time_status.date_text ||
                          g_console_state.time_status.local_epoch_day !=
                              time_status.local_epoch_day ||
                          g_console_state.time_status.weekday_index != time_status.weekday_index;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.time_status = time_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached Home Assistant snapshot in the console model.
bool set_home_assistant_status(const HomeAssistantStatus& home_assistant_status)
{
    // operator== deliberately excludes weather_last_success_ms (see its
    // declaration) so that field alone doesn't count as a UI-visible change --
    // but it must still be copied into g_console_state every call, or it would
    // never reach the page that reads it on ticks where nothing else changed.
    const bool changed = !(g_console_state.home_assistant_status == home_assistant_status);
    g_console_state.home_assistant_status = home_assistant_status;
    if (!changed)
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached MQTT snapshot in the console model.
bool set_mqtt_status(const MqttStatus& mqtt_status)
{
    if (g_console_state.mqtt_status == mqtt_status)
    {
        return false;
    }

    g_console_state.mqtt_status = mqtt_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached local air-traffic snapshot in the console model.
bool set_air_traffic_status(const AirTrafficStatus& air_traffic_status)
{
    // See set_home_assistant_status() above: last_success_ms is deliberately
    // excluded from operator== but must still always be copied through.
    const bool changed = !(g_console_state.air_traffic_status == air_traffic_status);
    g_console_state.air_traffic_status = air_traffic_status;
    if (!changed)
    {
        return false;
    }

    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached share market-data snapshot in the console model.
bool set_share_market_status(const ShareMarketStatus& share_market_status)
{
    const bool kChanged =
        g_console_state.share_data_configured != share_market_status.configured ||
        g_console_state.share_data_valid != share_market_status.data_valid ||
        g_console_state.share_data_last_error != share_market_status.last_error ||
        g_console_state.share_data_last_http_status != share_market_status.last_http_status ||
        g_console_state.share_count != share_market_status.share_count ||
        g_console_state.watched_shares != share_market_status.watched_shares;

    // last_success_ms is deliberately excluded from kChanged above (see its
    // declaration) but must still always be copied through, or it would never
    // reach the page that reads it on ticks where nothing else changed.
    g_console_state.share_data_last_success_ms = share_market_status.last_success_ms;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.share_data_configured = share_market_status.configured;
    g_console_state.share_data_valid = share_market_status.data_valid;
    g_console_state.share_data_last_error = share_market_status.last_error;
    g_console_state.share_data_last_http_status = share_market_status.last_http_status;
    g_console_state.share_count = share_market_status.share_count;
    g_console_state.watched_shares = share_market_status.watched_shares;
    if (g_console_state.selected_share_index >= g_console_state.share_count)
    {
        g_console_state.selected_share_index = 0U;
    }
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the cached environment sensor discovery snapshot in the console model.
bool set_environment_sensor_status(
    const environment_sensor_manager::EnvironmentSensorStatus& environment_sensor_status)
{
    if (environment_sensor_status_matches(g_console_state.environment_sensor_status,
                                          environment_sensor_status))
    {
        return false;
    }

    g_console_state.environment_sensor_status = environment_sensor_status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates the keypad diagnostics snapshot shown by the UI.
bool set_keypad_monitor_status(const KeypadMonitorStatus& keypad_status)
{
    std::array<char, 48> active_panel_pins = {};
    std::array<char, 48> probe_hit_panel_pins = {};
    std::array<char, 24> pressed_key_name = {};

    // Build the display strings up front so the change detection compares the
    // exact text the diagnostics page will eventually render.
    build_active_panel_pin_text(keypad_status, active_panel_pins);
    build_probe_hit_panel_pin_text(keypad_status, probe_hit_panel_pins);
    std::snprintf(pressed_key_name.data(), pressed_key_name.size(), "%s",
                  settings_controller::decoded_pressed_key(keypad_status));

    const bool kChanged =
        g_console_state.keypad_debug_status.active_mask != keypad_status.active_mask ||
        g_console_state.keypad_debug_status.configured_count != keypad_status.configured_count ||
        g_console_state.keypad_debug_status.active_count != keypad_status.active_count ||
        g_console_state.keypad_debug_status.pressed_key_name != pressed_key_name ||
        g_console_state.keypad_debug_status.active_panel_pins != active_panel_pins ||
        g_console_state.keypad_debug_status.probe_drive_panel_pin !=
            keypad_status.probe_drive_panel_pin ||
        g_console_state.keypad_debug_status.probe_hit_mask != keypad_status.probe_hit_mask ||
        g_console_state.keypad_debug_status.probe_hit_count != keypad_status.probe_hit_count ||
        g_console_state.keypad_debug_status.probe_hit_panel_pins != probe_hit_panel_pins;

    if (!kChanged)
    {
        return false;
    }

    g_console_state.keypad_debug_status.active_mask = keypad_status.active_mask;
    g_console_state.keypad_debug_status.configured_count = keypad_status.configured_count;
    g_console_state.keypad_debug_status.active_count = keypad_status.active_count;
    g_console_state.keypad_debug_status.pressed_key_name = pressed_key_name;
    g_console_state.keypad_debug_status.active_panel_pins = active_panel_pins;
    g_console_state.keypad_debug_status.probe_drive_panel_pin = keypad_status.probe_drive_panel_pin;
    g_console_state.keypad_debug_status.probe_hit_mask = keypad_status.probe_hit_mask;
    g_console_state.keypad_debug_status.probe_hit_count = keypad_status.probe_hit_count;
    g_console_state.keypad_debug_status.probe_hit_panel_pins = probe_hit_panel_pins;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates foreground main-loop load telemetry shown by Resources status.
bool set_main_loop_load_status(const MainLoopLoadStatus& status)
{
    if (g_console_state.main_loop_load_status.valid == status.valid &&
        g_console_state.main_loop_load_status.load_percent == status.load_percent &&
        g_console_state.main_loop_load_status.sample_ms == status.sample_ms)
    {
        return false;
    }

    g_console_state.main_loop_load_status = status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates live heap usage telemetry shown by Resources status.
bool set_heap_status(const HeapStatus& status)
{
    if (g_console_state.heap_status.valid == status.valid &&
        g_console_state.heap_status.used_bytes == status.used_bytes &&
        g_console_state.heap_status.arena_bytes == status.arena_bytes)
    {
        return false;
    }

    g_console_state.heap_status = status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates worst-case core-0 stack headroom telemetry shown by Resources status.
bool set_stack_status(const StackStatus& status)
{
    if (g_console_state.stack_status.valid == status.valid &&
        g_console_state.stack_status.free_bytes == status.free_bytes)
    {
        return false;
    }

    g_console_state.stack_status = status;
    update_softkeys_from_state();
    return true;
}

/// @brief Updates panel scanout timing telemetry shown by Resources status.
bool set_display_timing_status(const DisplayTimingStatus& status)
{
    if (g_console_state.display_timing_status.valid == status.valid &&
        g_console_state.display_timing_status.frame_rate_hz == status.frame_rate_hz &&
        g_console_state.display_timing_status.last_rebuild_us == status.last_rebuild_us &&
        g_console_state.display_timing_status.present_skipped_count ==
            status.present_skipped_count)
    {
        return false;
    }

    g_console_state.display_timing_status = status;
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
        const bool kRouteChanged = navigate_up_one_level();
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
        else if (g_console_state.active_page == MenuPage::AlertDetail)
        {
            const ActiveAlert& alert =
                g_console_state.active_alerts[g_console_state.alert_detail_index];
            uint8_t max_lines = 0U;
            for (char c : alert.detail)
            {
                if (c == '\0')
                {
                    break;
                }
                if (c == '\n')
                {
                    ++max_lines;
                }
            }
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
        else if (g_console_state.active_page == MenuPage::PinterSelectBrew)
        {
            changed = pinter_controller::change_list_page(g_console_state, direction);
        }
        else if (g_console_state.active_page == MenuPage::Shares && direction > 0)
        {
            changed = settings_controller::open_selected_share_detail(g_console_state);
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
                     static_cast<unsigned>(settings_controller::kSettingsPageCount),
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
