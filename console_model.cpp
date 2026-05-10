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
    state.weather_period = WeatherPeriod::Hour;
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

    // Weekly rows are cached separately from the hourly/day table so the
    // week period can use true daily provider data.
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

