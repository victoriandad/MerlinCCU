#include "console_model.h"

#include <cstddef>

namespace {

constexpr KeyLegend KEY_LEGENDS[] = {
    {"ALERT", nullptr},
    {"TEST", nullptr},
    {"BRT", nullptr},
    {"DIM", nullptr},
    {"LTRS", nullptr},
    {"BACK STEP", nullptr},
    {"LEFT", nullptr},
    {"RIGHT", nullptr},
    {"/", nullptr},
    {"CLR", nullptr},
    {"A", "COMM"},
    {"B", "R NAV"},
    {"C", "PERF"},
    {"D", "AMS"},
    {"E", "MAINT"},
    {"F", "IFF"},
    {"G", "TOTES"},
    {"H", "DSPLY"},
    {"I", "D LINK"},
    {"J", "1"},
    {"K", "2"},
    {"L", "3"},
    {"M", "SONICS"},
    {"N", "RADAR"},
    {"O", "ESM"},
    {"P", "4"},
    {"Q", "5"},
    {"R", "6"},
    {"S", "STORES"},
    {"T", "ADS"},
    {"U", nullptr},
    {"V", "7"},
    {"W", "8"},
    {"X", "9"},
    {"Y", "T NAV"},
    {"Z", "T DATA"},
    {"T FUNC", nullptr},
    {".", nullptr},
    {"0", nullptr},
    {"SPC", nullptr}
};

constexpr SoftKeyMap DEFAULT_SOFTKEYS = {{
    {"L1", SoftKeyRoute::None, false},
    {"L2", SoftKeyRoute::None, false},
    {"L3", SoftKeyRoute::None, false},
    {"L4", SoftKeyRoute::None, false},
    {"L5", SoftKeyRoute::None, false},
    {"R1", SoftKeyRoute::None, false},
    {"R2", SoftKeyRoute::None, false},
    {"R3", SoftKeyRoute::None, false},
    {"R4", SoftKeyRoute::None, false},
    {"R5", SoftKeyRoute::None, false},
}};

}  // namespace

static_assert((sizeof(KEY_LEGENDS) / sizeof(KEY_LEGENDS[0])) == static_cast<size_t>(HardKeyId::Count),
              "Key legend table must match HardKeyId");

const KeyLegend& key_legend(HardKeyId key)
{
    return KEY_LEGENDS[static_cast<size_t>(key)];
}

ConsoleState make_default_console_state()
{
    ConsoleState state = {};
    state.active_page = MenuPage::Home;
    state.letter_mode = LetterMode::Off;
    state.alert_severity = AlertSeverity::None;
    state.test_state = SystemTestState::Idle;
    state.panel_brightness = BrightnessLevel::Medium;
    state.key_backlight_brightness = BrightnessLevel::Medium;
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
    state.home_assistant_status.last_error = 0;
    state.home_assistant_status.last_http_status = 0;
    state.home_assistant_status.host.fill('\0');
    state.time_status.synced = false;
    state.time_status.time_text.fill('\0');
    state.lamps.fill(LampMode::Off);
    state.softkeys = DEFAULT_SOFTKEYS;
    return state;
}
