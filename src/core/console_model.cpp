#include "console_model.h"

#include <cstddef>
#include <cstdio>

#include "alert_history_store.h"
#include "calendar_demo.h"
#include "config_manager.h"
#include "hardware/flash.h"
#include "pinter_store.h"

namespace
{

// Linker-provided symbols read once here (not in status_screens.cpp/
// screens.cpp) so the render layer stays free of any hardware/linker
// dependency -- issue #71. Flash/RAM totals are fixed at link time, so a
// single read at console-state construction is correct, unlike the
// HeapStatus/StackStatus periodic re-sampling in MerlinCCU.cpp.
extern "C"
{
extern const uint8_t __flash_binary_start;
extern const uint8_t __flash_binary_end;
extern const uint8_t __end__;
extern const uint8_t __StackLimit;
}

// RP2040/RP2350 SRAM is architecturally mapped at this fixed base for every
// Pico/Pico W/Pico 2/Pico 2 W board; there is no linker symbol for the
// region's own origin (only its length), so this is paired with
// __StackLimit below to recover the true usable RAM size.
constexpr uintptr_t kRamBaseAddress = 0x20000000U;

/// @brief Returns the linked image footprint that occupies programme flash.
uint32_t program_flash_bytes()
{
    const uintptr_t start = reinterpret_cast<uintptr_t>(&__flash_binary_start);
    const uintptr_t end = reinterpret_cast<uintptr_t>(&__flash_binary_end);
    return (end > start) ? static_cast<uint32_t>(end - start) : 0U;
}

/// @brief Returns total static RAM used by the linked image (.data+.bss).
uint32_t static_ram_bytes()
{
    const uintptr_t end = reinterpret_cast<uintptr_t>(&__end__);
    return (end > kRamBaseAddress) ? static_cast<uint32_t>(end - kRamBaseAddress) : 0U;
}

/// @brief Returns the linker's usable contiguous RAM region size.
/// @details This is the RP2350's main 512KiB SRAM bank as mapped by the
/// default Pico SDK linker script; it is 8KiB smaller than the chip's full
/// 520KB physical SRAM total since SRAM8/SRAM9 sit outside this contiguous
/// region and are not part of the linked `RAM` output section.
uint32_t total_ram_bytes()
{
    const uintptr_t limit = reinterpret_cast<uintptr_t>(&__StackLimit);
    return (limit > kRamBaseAddress) ? static_cast<uint32_t>(limit - kRamBaseAddress) : 0U;
}

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

void make_default_console_state(ConsoleState& out)
{
    out = {};

    // Boot into the top-level home menu so the front panel always opens on the
    // clean navigation shell instead of a diagnostics surface.
    out.active_page = MenuPage::Home;
    out.settings_page_index = 0;
    out.weather_source = WeatherSource::HomeAssistant;
    out.weather_period = WeatherPeriod::Hourly;
    out.local_condition_metric = LocalConditionMetric::Temperature;
    out.calendar_owner = CalendarOwner::Combined;
    out.calendar_day_offset = 0;
    out.selected_calendar_event_index = 0U;
    out.calendar_event_count = 0U;
    out.share_period = SharePeriod::Today;
    out.share_data_configured = false;
    out.share_data_valid = false;
    out.share_data_last_error = 0;
    out.share_data_last_http_status = 0;
    out.share_count = 1U;
    out.selected_share_index = 0U;
    out.selected_pinter_index = 0U;
    out.pinter_catalogue_page_index = 0U;
    out.pinter_pending_brew_index = kDefaultPinterBrewIndex;
    out.pinter_pending_brewing_days = 0U;
    out.pinter_pending_cold_crash_days = 0U;
    out.pinter_pending_conditioning_days = 0U;
    out.pinter_block_reason.fill('\0');
    out.screen_saver_selection = ScreenSaverSelection::Life;
    out.time_zone = TimeZoneSelection::EuropeLondon;
    out.screen_saver_timeout_minutes = 5;
    out.screen_saver_timeout_editing = false;
    out.screen_saver_timeout_edit_minutes = 5;
    out.screen_saver_timeout_replace_on_next_digit = true;
    out.letter_mode = LetterMode::UpperCase;
    out.alert_severity = AlertSeverity::None;
    out.test_state = SystemTestState::Idle;
    out.panel_brightness = BrightnessLevel::Medium;
    out.key_backlight_brightness = BrightnessLevel::Medium;

    // Treat every integration as unavailable until its manager explicitly
    // proves otherwise; this avoids boot code implying connectivity too early.
    out.wifi_status.state = WifiConnectionState::Disabled;
    out.wifi_status.credentials_present = false;
    out.wifi_status.internet_reachable = false;
    out.wifi_status.internet_probe_pending = false;
    out.wifi_status.last_error = 0;
    out.wifi_status.link_status = 0;
    out.wifi_status.internet_rtt_ms = -1;
    out.wifi_status.auth_mode.fill('\0');
    out.wifi_status.mac_address.fill('\0');
    out.wifi_status.ssid.fill('\0');
    out.wifi_status.ip_address.fill('\0');
    out.wifi_status.host_name.fill('\0');

    out.home_assistant_status.state = HomeAssistantConnectionState::Disabled;
    out.home_assistant_status.configured = false;
    out.home_assistant_status.self_entity_published = false;
    out.home_assistant_status.last_error = 0;
    out.home_assistant_status.last_http_status = 0;
    out.home_assistant_status.host.fill('\0');
    out.home_assistant_status.weather_state = HomeAssistantConnectionState::Disabled;
    out.home_assistant_status.weather_http_status = 0;
    out.home_assistant_status.weather_host.fill('\0');
    out.home_assistant_status.tracked_entity_id.fill('\0');
    out.home_assistant_status.tracked_entity_state.fill('\0');
    out.home_assistant_status.weather_entity_id.fill('\0');
    out.home_assistant_status.weather_source_hint.fill('\0');
    out.home_assistant_status.weather_condition.fill('\0');
    out.home_assistant_status.weather_temperature.fill('\0');
    out.home_assistant_status.weather_wind_unit.fill('\0');
    out.home_assistant_status.sunrise_text.fill('\0');
    out.home_assistant_status.sunset_text.fill('\0');
    out.home_assistant_status.weather_forecast_count = 0;
    out.home_assistant_status.weather_daily_forecast_count = 0;
    out.home_assistant_status.weather_metrics = {};
    out.home_assistant_status.weather_alert_status = {};

    // Forecast rows are fully cleared so pages can safely treat an empty string
    // as "no data yet" without tracking separate validity flags.
    for (auto& entry : out.home_assistant_status.weather_forecast)
    {
        entry.time_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
    }

    // Next-seven-days rows are cached separately from the hourly/today table
    // so that range can use true daily provider data.
    for (auto& entry : out.home_assistant_status.weather_daily_forecast)
    {
        entry.date_text.fill('\0');
        entry.temperature_text.fill('\0');
        entry.wind_text.fill('\0');
        entry.condition_text.fill('\0');
    }
    out.home_assistant_status.self_entity_id.fill('\0');

    out.mqtt_status.state = MqttConnectionState::Disabled;
    out.mqtt_status.configured = false;
    out.mqtt_status.discovery_published = false;
    out.mqtt_status.last_error = 0;
    out.mqtt_status.broker.fill('\0');
    out.mqtt_status.device_id.fill('\0');
    out.time_status.synced = false;
    out.time_status.time_text.fill('\0');
    out.time_status.date_text.fill('\0');
    out.time_status.local_epoch_day = 0U;
    out.time_status.weekday_index = kInvalidWeekdayIndex;
    out.main_loop_load_status = {};
    out.heap_status = {};
    out.stack_status = {};
    out.boot_status = {};
    out.home_controls = {};
    out.unit_fleet = {};
    out.firmware_status = {};
    out.web_pin_clear_confirming = false;
    out.lamp_overrides.fill(LampOverride::Auto);
    out.ha_message = {};
    out.display_timing_status = {};

    // The keypad debug surface is always present, so its snapshot fields start
    // cleared rather than being allocated lazily later.
    out.keypad_debug_status.pressed_key_name.fill('\0');
    out.keypad_debug_status.active_mask = 0;
    out.keypad_debug_status.configured_count = 0;
    out.keypad_debug_status.active_count = 0;
    out.keypad_debug_status.active_panel_pins.fill('\0');
    out.keypad_debug_status.probe_drive_panel_pin = 0;
    out.keypad_debug_status.probe_hit_mask = 0;
    out.keypad_debug_status.probe_hit_count = 0;
    out.keypad_debug_status.probe_hit_panel_pins.fill('\0');
    out.alert_count = 0U;
    out.alert_list_page_index = 0U;
    out.alert_detail_index = 0U;
    out.alert_detail_scroll_line = 0U;
    out.alert_parent_page = MenuPage::Home;
    out.alert_history_page_index = 0U;
    out.alert_history_detail_sequence = 0U;
    out.alert_history_scroll_line = 0U;
    out.alert_history_clear_confirming = false;

    set_pinter(out.pinters[0], "Pinter 1 (Original)");
    set_pinter(out.pinters[1], "Pinter3 #1");
    set_pinter(out.pinters[2], "Pinter3 #2");
    set_pinter(out.pinters[3], "Pinter3 #3");

    for (auto& alert : out.active_alerts)
    {
        alert.severity = AlertSeverity::None;
        alert.code = 0U;
        alert.sequence = 0U;
        alert.occurred_time_text.fill('\0');
        alert.summary.fill('\0');
        alert.detail = {};
    }

    // Only the day on screen is held; Calendar loads a day as it is shown.
    calendar_demo::load_day(0, out.calendar_events, out.calendar_event_count);

    for (auto& share : out.watched_shares)
    {
        share.display_name.fill('\0');
        share.symbol.fill('\0');
        share.exchange.fill('\0');
        share.currency.fill('\0');
        share.price_text.fill('\0');
        share.change_text.fill('\0');
        share.history_points.fill(0.0F);
    }

    ShareWatchEntry& bae = out.watched_shares[0];
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
    out.lamps.fill(LampMode::Off);
    out.softkeys = kDefaultSoftkeys;

    // Flash/RAM totals never change after linking, so this is computed once
    // here rather than re-sampled every tick like HeapStatus/StackStatus --
    // see ImageFootprintStatus's own comment.
    constexpr uint32_t kReservedFlashBytes = config_manager::kReservedFlashBytes +
                                             pinter_store::kReservedFlashBytes +
                                             alert_history_store::kReservedFlashBytes;
    out.image_footprint_status.valid = true;
    out.image_footprint_status.program_flash_bytes = program_flash_bytes();
    out.image_footprint_status.flash_budget_bytes =
        static_cast<uint32_t>(PICO_FLASH_SIZE_BYTES) - kReservedFlashBytes;
    out.image_footprint_status.reserved_flash_bytes = kReservedFlashBytes;
    out.image_footprint_status.static_ram_bytes = static_ram_bytes();
    out.image_footprint_status.total_ram_bytes = total_ram_bytes();
}
