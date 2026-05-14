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
    state.screen_saver_selection = ScreenSaverSelection::Life;
    state.time_zone = TimeZoneSelection::EuropeLondon;
    state.screen_saver_timeout_minutes = 5;
    state.screen_saver_timeout_editing = false;
    state.screen_saver_timeout_edit_minutes = 5;
    state.screen_saver_timeout_replace_on_next_digit = true;
    state.letter_mode = LetterMode::Off;
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
    state.time_status.weekday_index = kInvalidWeekdayIndex;

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

    add_calendar_event(CalendarOwner::Sean, 0, "08:30", "09:00", "Work standup", "Teams",
                       "10 min popup", "Project team", "Daily status call.");
    add_calendar_event(CalendarOwner::Luigina, 0, "09:15", "09:45", "School admin", "School office",
                       "30 min email", "School office", "Forms and term dates.");
    add_calendar_event(CalendarOwner::Loris, 0, "15:40", "17:00", "Football", "Sports ground",
                       "1 hour popup", "Coach, team", "Training kit needed.");
    add_calendar_event(CalendarOwner::Luca, 0, "16:30", "17:15", "Swimming", "Leisure centre",
                       "45 min popup", "Instructor", "Take towel and goggles.");
    add_calendar_event(CalendarOwner::Sean, 0, "19:00", "20:00", "Dinner prep", "Home", "None",
                       "Family", "Start dinner before clubs end.");
    add_calendar_event(CalendarOwner::Luigina, 1, "10:00", "10:30", "Appointment", "Clinic",
                       "1 day email", "Clinic", "Check appointment notes.");
    add_calendar_event(CalendarOwner::Loris, 1, "13:30", "15:30", "School trip", "Museum",
                       "1 day popup", "Class", "Packed lunch required.");
    add_calendar_event(CalendarOwner::Luca, 1, "17:15", "18:30", "Play date", "Friend's house",
                       "30 min popup", "Parent", "Pickup confirmed by text.");
    add_calendar_event(CalendarOwner::Sean, 2, "08:45", "17:00", "Office", "London", "None", "Work",
                       "Office day.");
    add_calendar_event(CalendarOwner::Luigina, 2, "18:00", "19:00", "Pilates", "Studio",
                       "1 hour popup", "Class", "Bring mat.");
    add_calendar_event(CalendarOwner::Loris, 3, "11:00", "11:30", "Dentist", "Dental surgery",
                       "1 day email", "Dentist", "Routine check-up.");
    add_calendar_event(CalendarOwner::Luca, 3, "14:00", "16:00", "Party", "Soft play",
                       "2 hours popup", "Class friends", "Birthday party.");
    add_calendar_event(CalendarOwner::Sean, -1, "12:30", "13:00", "Lunch call", "Office",
                       "10 min popup", "Supplier", "Review quote.");
    add_calendar_event(CalendarOwner::Luigina, -1, "18:30", "19:30", "Parents group", "School hall",
                       "30 min popup", "Parents", "Planning meeting.");
    add_calendar_event(CalendarOwner::Sean, 4, "09:30", "10:15", "Service slot", "Garage",
                       "1 day email", "Garage", "Car service booking.");
    add_calendar_event(CalendarOwner::Loris, 5, "16:00", "17:30", "Match", "Away pitch",
                       "2 hours popup", "Coach, team", "Bring boots.");
    add_calendar_event(CalendarOwner::Luca, 6, "10:30", "12:00", "Library", "Town library",
                       "1 hour popup", "Family", "Return books.");
    add_calendar_event(CalendarOwner::Luigina, 7, "15:00", "16:00", "Coffee", "High street",
                       "30 min popup", "Friend", "Catch-up.");
    add_calendar_event(CalendarOwner::Sean, -14, "08:00", "08:30", "Old rota", "Home", "None",
                       "Work", "Historic sample event.");
    add_calendar_event(CalendarOwner::Luigina, -7, "11:00", "11:45", "School call", "Phone",
                       "15 min popup", "School office", "Historic weekly sample.");
    add_calendar_event(CalendarOwner::Sean, 14, "09:00", "09:30", "Budget check", "Home office",
                       "1 day email", "Accounts", "Two-week sample event.");
    add_calendar_event(CalendarOwner::Luca, 13, "16:00", "17:00", "Club signup", "Community hall",
                       "1 day popup", "Club leader", "Next fortnight sample.");

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
        {CalendarOwner::Sean, "Sean sample", "Sean"},
        {CalendarOwner::Luigina, "Luigina sample", "Luigina"},
        {CalendarOwner::Loris, "Loris sample", "Loris"},
        {CalendarOwner::Luca, "Luca sample", "Luca"},
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
    // Seed values keep the page useful before the live Yahoo chart fetcher is
    // wired in. Runtime refresh should overwrite these fields in-place.
    std::snprintf(bae.price_text.data(), bae.price_text.size(), "1,348.5");
    std::snprintf(bae.change_text.data(), bae.change_text.size(), "+0.8%%");
    bae.history_points = {1320U, 1324U, 1318U, 1328U, 1336U, 1332U, 1340U, 1346U,
                          1341U, 1348U, 1352U, 1349U, 1355U, 1351U, 1344U, 1348U,
                          1356U, 1360U, 1354U, 1358U, 1364U, 1362U, 1368U, 1372U};
    // Lamps and softkeys are initialized explicitly so the controller can treat
    // the whole state object as immediately usable after construction.
    state.lamps.fill(LampMode::Off);
    state.softkeys = kDefaultSoftkeys;
    return state;
}
