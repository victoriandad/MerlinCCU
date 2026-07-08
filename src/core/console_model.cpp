#include "console_model.h"

#include <cstddef>
#include <cstdio>

namespace
{

/// @brief Maps each physical hard key to its primary and alternate panel legends.
/// @details Keeping the legends in enum order lets the UI render the keypad labels without
/// duplicating text tables across multiple screens or diagnostics views.
constexpr KeyLegend kKeyLegends[] = {
    {"ALERT", nullptr},  {"TEST", nullptr},  {"BRT", nullptr},
    {"DIM", nullptr},    {"LTRS", nullptr},  {"BACK STEP", nullptr},
    {"LEFT", nullptr},   {"RIGHT", nullptr}, {"/", nullptr},
    {"CLR", nullptr},    {"A", "COMM"},      {"B", "R NAV"},
    {"C", "PERF"},       {"D", "AMS"},       {"E", "MAINT"},
    {"F", "IFF"},        {"G", "TOTES"},     {"H", "DSPLY"},
    {"I", "D LINK"},     {"J", "1"},         {"K", "2"},
    {"L", "3"},          {"M", "SONICS"},    {"N", "RADAR"},
    {"O", "ESM"},        {"P", "4"},         {"Q", "5"},
    {"R", "6"},          {"S", "STORES"},    {"T", "ADS"},
    {"U", nullptr},      {"V", "7"},         {"W", "8"},
    {"X", "9"},          {"Y", "T NAV"},     {"Z", "T DATA"},
    {"T FUNC", nullptr}, {".", nullptr},     {"0", nullptr},
    {"SPC", nullptr}};

/// @brief Neutral softkey labels and routes used when a page does not own a slot.
/// @details Pages start from this baseline and override only the softkeys they need, which
/// keeps the inactive state consistent across the UI.
constexpr SoftKeyMap kDefaultSoftkeys = {{
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

/// @brief Seeds one display-ready calendar event row.
/// @details Home Assistant calendar ingestion can later overwrite these same
/// fixed rows without changing the Calendar page renderer or routing code.
void set_calendar_event(CalendarEvent& event, CalendarOwner owner, int8_t day_offset,
                        const char* start_time, const char* end_time, const char* title,
                        const char* location, const char* reminder, const char* attendees,
                        const char* description)
{
    event.owner = owner;
    event.day_offset = day_offset;
    event.start_time.fill('\0');
    event.end_time.fill('\0');
    event.title.fill('\0');
    event.location.fill('\0');
    event.reminder.fill('\0');
    event.attendees.fill('\0');
    event.description.fill('\0');
    std::snprintf(event.start_time.data(), event.start_time.size(), "%s", start_time);
    std::snprintf(event.end_time.data(), event.end_time.size(), "%s", end_time);
    std::snprintf(event.title.data(), event.title.size(), "%s", title);
    std::snprintf(event.location.data(), event.location.size(), "%s", location);
    std::snprintf(event.reminder.data(), event.reminder.size(), "%s", reminder);
    std::snprintf(event.attendees.data(), event.attendees.size(), "%s", attendees);
    std::snprintf(event.description.data(), event.description.size(), "%s", description);
}

/// @brief Seeds one manually tracked Pinter vessel with a stable panel label.
void set_pinter(PinterStatus& pinter, const char* label)
{
    pinter.label.fill('\0');
    std::snprintf(pinter.label.data(), pinter.label.size(), "%s", label);
    pinter.state = PinterState::Idle;
    pinter.brew_index = kDefaultPinterBrewIndex;
    pinter.brew_start_day = 0U;
    pinter.cold_crash_start_day = 0U;
    pinter.conditioning_start_day = 0U;
    pinter.ready_day = 0U;
    pinter.planned_brewing_days = 0U;
    pinter.planned_cold_crash_days = 0U;
    pinter.planned_conditioning_days = 0U;
    pinter.cold_crash_used = false;
}

} // namespace

static_assert((sizeof(kKeyLegends) / sizeof(kKeyLegends[0])) ==
                  static_cast<size_t>(HardKeyId::Count),
              "Key legend table must match HardKeyId");

const KeyLegend& key_legend(HardKeyId key)
{
    return kKeyLegends[static_cast<size_t>(key)];
}

ConsoleState make_default_console_state()
{
    ConsoleState state = {};

    // Boot into the top-level home menu so the front panel always opens on the
    // clean navigation shell instead of a diagnostics surface.
    state.active_page = MenuPage::Home;
    state.settings_page_index = 0;
    state.weather_source = WeatherSource::HomeAssistant;
    state.weather_period = WeatherPeriod::Hourly;
    state.local_condition_metric = LocalConditionMetric::Temperature;
    state.calendar_owner = CalendarOwner::Combined;
    state.calendar_day_offset = 0;
    state.selected_calendar_event_index = 0U;
    state.calendar_event_count = 0U;
    state.share_period = SharePeriod::Today;
    state.share_data_configured = false;
    state.share_data_valid = false;
    state.share_data_last_error = 0;
    state.share_data_last_http_status = 0;
    state.share_count = 1U;
    state.selected_share_index = 0U;
    state.selected_pinter_index = 0U;
    state.pinter_catalogue_page_index = 0U;
    state.pinter_pending_brew_index = kDefaultPinterBrewIndex;
    state.pinter_pending_brewing_days = 0U;
    state.pinter_pending_cold_crash_days = 0U;
    state.pinter_pending_conditioning_days = 0U;
    state.pinter_block_reason.fill('\0');
    state.screen_saver_selection = ScreenSaverSelection::Life;
    state.time_zone = TimeZoneSelection::EuropeLondon;
    state.screen_saver_timeout_minutes = 5;
    state.screen_saver_timeout_editing = false;
    state.screen_saver_timeout_edit_minutes = 5;
    state.screen_saver_timeout_replace_on_next_digit = true;
    state.letter_mode = LetterMode::UpperCase;
    state.alert_severity = AlertSeverity::None;
    state.test_state = SystemTestState::Idle;
    state.panel_brightness = BrightnessLevel::Medium;
    state.key_backlight_brightness = BrightnessLevel::Medium;

    // Treat every integration as unavailable until its manager explicitly
    // proves otherwise; this avoids boot code implying connectivity too early.
    state.wifi_status.state = WifiConnectionState::Disabled;
    state.wifi_status.credentials_present = false;
    state.wifi_status.internet_reachable = false;
    state.wifi_status.internet_probe_pending = false;
    state.wifi_status.last_error = 0;
    state.wifi_status.link_status = 0;
    state.wifi_status.internet_rtt_ms = -1;
    state.wifi_status.auth_mode.fill('\0');
    state.wifi_status.mac_address.fill('\0');
    state.wifi_status.ssid.fill('\0');
    state.wifi_status.ip_address.fill('\0');

    state.home_assistant_status.state = HomeAssistantConnectionState::Disabled;
    state.home_assistant_status.configured = false;
    state.home_assistant_status.self_entity_published = false;
    state.home_assistant_status.last_error = 0;
    state.home_assistant_status.last_http_status = 0;
    state.home_assistant_status.host.fill('\0');
    state.home_assistant_status.tracked_entity_id.fill('\0');
    state.home_assistant_status.tracked_entity_state.fill('\0');
    state.home_assistant_status.weather_entity_id.fill('\0');
    state.home_assistant_status.weather_source_hint.fill('\0');
    state.home_assistant_status.weather_condition.fill('\0');
    state.home_assistant_status.weather_temperature.fill('\0');
    state.home_assistant_status.weather_wind_unit.fill('\0');
    state.home_assistant_status.sunrise_text.fill('\0');
    state.home_assistant_status.sunset_text.fill('\0');
    state.home_assistant_status.weather_forecast_count = 0;
    state.home_assistant_status.weather_daily_forecast_count = 0;
    state.home_assistant_status.weather_metrics = {};
    state.home_assistant_status.weather_alert_status = {};

    // Forecast rows are fully cleared so pages can safely treat an empty string
    // as "no data yet" without tracking separate validity flags.
    for (auto& entry : state.home_assistant_status.weather_forecast)
    {
        entry.time_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
    }

    // Next-seven-days rows are cached separately from the hourly/today table
    // so that range can use true daily provider data.
    for (auto& entry : state.home_assistant_status.weather_daily_forecast)
    {
        entry.date_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
    }
    state.home_assistant_status.self_entity_id.fill('\0');

    state.mqtt_status.state = MqttConnectionState::Disabled;
    state.mqtt_status.configured = false;
    state.mqtt_status.discovery_published = false;
    state.mqtt_status.last_error = 0;
    state.mqtt_status.broker.fill('\0');
    state.mqtt_status.device_id.fill('\0');
    state.time_status.synced = false;
    state.time_status.time_text.fill('\0');
    state.time_status.date_text.fill('\0');
    state.time_status.local_epoch_day = 0U;
    state.time_status.weekday_index = kInvalidWeekdayIndex;
    state.main_loop_load_status = {};
    state.heap_status = {};

    // The keypad debug surface is always present, so its snapshot fields start
    // cleared rather than being allocated lazily later.
    state.keypad_debug_status.pressed_key_name.fill('\0');
    state.keypad_debug_status.active_mask = 0;
    state.keypad_debug_status.configured_count = 0;
    state.keypad_debug_status.active_count = 0;
    state.keypad_debug_status.active_panel_pins.fill('\0');
    state.keypad_debug_status.probe_drive_panel_pin = 0;
    state.keypad_debug_status.probe_hit_mask = 0;
    state.keypad_debug_status.probe_hit_count = 0;
    state.keypad_debug_status.probe_hit_panel_pins.fill('\0');
    state.alert_count = 0U;
    state.alert_list_page_index = 0U;
    state.alert_detail_index = 0U;
    state.alert_detail_scroll_line = 0U;
    state.alert_parent_page = MenuPage::Home;

    set_pinter(state.pinters[0], "Pinter 1 (Original)");
    set_pinter(state.pinters[1], "Pinter3 #1");
    set_pinter(state.pinters[2], "Pinter3 #2");
    set_pinter(state.pinters[3], "Pinter3 #3");

    for (auto& alert : state.active_alerts)
    {
        alert.severity = AlertSeverity::None;
        alert.code = 0U;
        alert.sequence = 0U;
        alert.occurred_time_text.fill('\0');
        alert.summary.fill('\0');
        alert.detail.fill('\0');
    }

    for (auto& event : state.calendar_events)
    {
        event.owner = CalendarOwner::Combined;
        event.day_offset = 0;
        event.start_time.fill('\0');
        event.end_time.fill('\0');
        event.title.fill('\0');
        event.location.fill('\0');
        event.reminder.fill('\0');
        event.attendees.fill('\0');
        event.description.fill('\0');
    }

    size_t calendar_event_index = 0U;
    auto add_calendar_event = [&](CalendarOwner owner, int8_t day_offset, const char* start_time,
                                  const char* end_time, const char* title, const char* location,
                                  const char* reminder, const char* attendees,
                                  const char* description)
    {
        if (calendar_event_index >= state.calendar_events.size())
        {
            return;
        }

        set_calendar_event(state.calendar_events[calendar_event_index], owner, day_offset,
                           start_time, end_time, title, location, reminder, attendees, description);
        ++calendar_event_index;
    };

    auto calendar_day_has_event = [&](int8_t day_offset)
    {
        for (size_t i = 0U; i < calendar_event_index; ++i)
        {
            if (state.calendar_events[i].day_offset == day_offset &&
                state.calendar_events[i].title[0] != '\0')
            {
                return true;
            }
        }

        return false;
    };

    add_calendar_event(CalendarOwner::Owner1, 0, "08:30", "09:00", "Demo standup", "Video call",
                       "10 min popup", "Project group", "Synthetic status sample.");
    add_calendar_event(CalendarOwner::Owner2, 0, "09:15", "09:45", "Planning slot", "Room A",
                       "30 min email", "Ops group", "Synthetic planning sample.");
    add_calendar_event(CalendarOwner::Owner3, 0, "15:40", "17:00", "Training block", "Activity room",
                       "1 hour popup", "Training lead", "Synthetic training sample.");
    add_calendar_event(CalendarOwner::Owner4, 0, "16:30", "17:15", "Practice block", "Activity centre",
                       "45 min popup", "Practice lead", "Synthetic practice sample.");
    add_calendar_event(CalendarOwner::Owner1, 0, "19:00", "20:00", "Admin block", "Home base", "None",
                       "Demo group", "End-of-day synthetic sample.");
    add_calendar_event(CalendarOwner::Owner2, 1, "10:00", "10:30", "Check-in", "Room B",
                       "1 day email", "Coordinator", "Synthetic check-in notes.");
    add_calendar_event(CalendarOwner::Owner3, 1, "13:30", "15:30", "Workshop", "Lab",
                       "1 day popup", "Demo cohort", "Synthetic workshop sample.");
    add_calendar_event(CalendarOwner::Owner4, 1, "17:15", "18:30", "Review block", "Room C",
                       "30 min popup", "Reviewer", "Synthetic review sample.");
    add_calendar_event(CalendarOwner::Owner1, 2, "08:45", "17:00", "Focus day", "Remote", "None",
                       "Project group", "Synthetic focus-day sample.");
    add_calendar_event(CalendarOwner::Owner2, 2, "18:00", "19:00", "Exercise block", "Studio",
                       "1 hour popup", "Class group", "Synthetic exercise sample.");
    add_calendar_event(CalendarOwner::Owner3, 3, "11:00", "11:30", "Maintenance slot",
                       "Service desk", "1 day email", "Service team", "Synthetic service sample.");
    add_calendar_event(CalendarOwner::Owner4, 3, "14:00", "16:00", "Demo event", "Hall",
                       "2 hours popup", "Demo group", "Synthetic event sample.");
    add_calendar_event(CalendarOwner::Owner1, -1, "12:30", "13:00", "Vendor call", "Office",
                       "10 min popup", "Supplier", "Synthetic quote review.");
    add_calendar_event(CalendarOwner::Owner2, -1, "18:30", "19:30", "Planning forum",
                       "Meeting room", "30 min popup", "Planning group",
                       "Synthetic planning sample.");
    add_calendar_event(CalendarOwner::Owner1, 4, "09:30", "10:15", "Service slot",
                       "Service desk", "1 day email", "Service team", "Synthetic booking sample.");
    add_calendar_event(CalendarOwner::Owner3, 5, "16:00", "17:30", "Away activity", "Field site",
                       "2 hours popup", "Activity group", "Synthetic away sample.");
    add_calendar_event(CalendarOwner::Owner4, 6, "10:30", "12:00", "Library block",
                       "Resource room", "1 hour popup", "Demo group", "Synthetic library sample.");
    add_calendar_event(CalendarOwner::Owner2, 7, "15:00", "16:00", "Review call", "Video call",
                       "30 min popup", "Review group", "Synthetic catch-up sample.");
    add_calendar_event(CalendarOwner::Owner1, -14, "08:00", "08:30", "Historic rota", "Demo base",
                       "None", "Ops group", "Historic synthetic sample.");
    add_calendar_event(CalendarOwner::Owner2, -7, "11:00", "11:45", "Historic call", "Phone",
                       "15 min popup", "Coordinator", "Historic weekly sample.");
    add_calendar_event(CalendarOwner::Owner1, 14, "09:00", "09:30", "Budget check",
                       "Planning desk", "1 day email", "Accounts", "Two-week synthetic sample.");
    add_calendar_event(CalendarOwner::Owner4, 13, "16:00", "17:00", "Signup slot",
                       "Community room", "1 day popup", "Coordinator",
                       "Next-fortnight synthetic sample.");

    // Fill every empty day in the navigation test window so arrow-key day
    // movement can be verified before live HA data exists. Keep this deliberately
    // small; ConsoleState is built during boot and should not carry bulky
    // synthetic test data.
    struct SampleOwner
    {
        CalendarOwner owner;
        const char* title_prefix;
        const char* attendee;
    };
    constexpr SampleOwner kSampleOwners[] = {
        {CalendarOwner::Owner1, "Owner 1 sample", "Owner 1"},
        {CalendarOwner::Owner2, "Owner 2 sample", "Owner 2"},
        {CalendarOwner::Owner3, "Owner 3 sample", "Owner 3"},
        {CalendarOwner::Owner4, "Owner 4 sample", "Owner 4"},
    };
    for (int day = kCalendarMinDayOffset; day <= kCalendarMaxDayOffset; ++day)
    {
        const int8_t day_offset = static_cast<int8_t>(day);
        if (calendar_day_has_event(day_offset))
        {
            continue;
        }

        const int sample_index = day - kCalendarMinDayOffset;
        const SampleOwner& sample_owner =
            kSampleOwners[static_cast<size_t>(sample_index) %
                          (sizeof(kSampleOwners) / sizeof(kSampleOwners[0]))];
        const int start_hour = 8 + (sample_index % 10);
        char start_time[6] = {};
        char end_time[6] = {};
        char title[32] = {};
        std::snprintf(start_time, sizeof(start_time), "%02d:00", start_hour);
        std::snprintf(end_time, sizeof(end_time), "%02d:30", start_hour);
        std::snprintf(title, sizeof(title), "%s %+d", sample_owner.title_prefix, day);

        add_calendar_event(sample_owner.owner, day_offset, start_time, end_time, title,
                           "Calendar test", "None", sample_owner.attendee,
                           "Generated sample for day paging.");
    }

    state.calendar_event_count = static_cast<uint8_t>(calendar_event_index);

    for (auto& share : state.watched_shares)
    {
        share.display_name.fill('\0');
        share.symbol.fill('\0');
        share.exchange.fill('\0');
        share.currency.fill('\0');
        share.price_text.fill('\0');
        share.change_text.fill('\0');
        share.history_points.fill(0U);
    }

    ShareWatchEntry& bae = state.watched_shares[0];
    std::snprintf(bae.display_name.data(), bae.display_name.size(), "BAE SYSTEMS");
    std::snprintf(bae.symbol.data(), bae.symbol.size(), "BA.L");
    std::snprintf(bae.exchange.data(), bae.exchange.size(), "LSE");
    std::snprintf(bae.currency.data(), bae.currency.size(), "GBX");
    // Seed values keep the page useful while the live Yahoo chart fetcher is disabled.
    std::snprintf(bae.price_text.data(), bae.price_text.size(), "Demo");
    std::snprintf(bae.change_text.data(), bae.change_text.size(), "No live");
    bae.history_points = {1362U, 1364U, 1361U, 1365U, 1363U, 1366U, 1362U, 1367U,
                          1365U, 1368U, 1364U, 1369U, 1367U, 1370U, 1366U, 1371U,
                          1368U, 1372U, 1369U, 1373U, 1370U, 1374U, 1371U, 1372U};
    // Lamps and softkeys are initialized explicitly so the controller can treat
    // the whole state object as immediately usable after construction.
    state.lamps.fill(LampMode::Off);
    state.softkeys = kDefaultSoftkeys;
    return state;
}
