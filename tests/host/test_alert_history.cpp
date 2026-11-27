#include "alert_history.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

#include "alert_controller.h"
#include "softkey_builders.h"
#include "test_framework.h"

namespace
{

// 24 Nov 2026 is epoch day 20,781.
constexpr uint32_t kDay = 20781U;
constexpr uint32_t kMinutesPerDay = 1440U;

constexpr uint32_t at(uint32_t day, uint32_t hour, uint32_t minute)
{
    return (day * kMinutesPerDay) + (hour * 60U) + minute;
}

ActiveAlert make_alert(uint8_t code, const char* summary, uint32_t raised_minute)
{
    ActiveAlert alert = {};
    alert.code = code;
    alert.severity = AlertSeverity::Warning;
    alert.raised_minute = raised_minute;
    std::snprintf(alert.summary.data(), alert.summary.size(), "%s", summary);
    // History copies the text when it records, so a few rotating buffers
    // outlive every alert a test builds before recording it.
    static std::array<std::array<char, 64>, 8> texts = {};
    static size_t next_text = 0U;
    std::array<char, 64>& text = texts[next_text++ % texts.size()];
    std::snprintf(text.data(), text.size(), "Detail for %s.", summary);
    alert.detail.point_to(text.data());
    return alert;
}

void set_clock(ConsoleState& state, uint32_t minute)
{
    state.time_status.synced = true;
    state.time_status.local_epoch_day = minute / kMinutesPerDay;
    std::snprintf(state.time_status.time_text.data(), state.time_status.time_text.size(),
                  "%02u:%02u", static_cast<unsigned>((minute % kMinutesPerDay) / 60U),
                  static_cast<unsigned>(minute % 60U));
}

std::string label(const SoftKeyMap& keys, SoftKeyId key)
{
    const char* text = keys[static_cast<size_t>(key)].label;
    return text != nullptr ? text : "";
}

} // namespace

HOST_TEST(alert_history_local_minute_needs_a_synced_clock)
{
    ConsoleState state = {};
    EXPECT_EQ(alert_history::local_minute(state.time_status), 0U);
    set_clock(state, at(kDay, 6, 12));
    EXPECT_EQ(alert_history::local_minute(state.time_status), at(kDay, 6, 12));
    state.time_status.synced = false;
    EXPECT_EQ(alert_history::local_minute(state.time_status), 0U);
}

HOST_TEST(alert_history_lists_newest_first_l1_to_l4_then_r1_to_r4)
{
    alert_history::reset();
    for (uint8_t i = 0U; i < 8U; ++i)
    {
        char summary[8] = {};
        std::snprintf(summary, sizeof(summary), "A%u", static_cast<unsigned>(i));
        alert_history::record(make_alert(i, summary, at(kDay, 1, i)),
                              alert_history::Ending::Cleared, at(kDay, 2, i));
    }

    ConsoleState state = {};
    set_clock(state, at(kDay, 3, 0));
    SoftKeyMap keys = {};
    alert_controller::build_history_softkeys(state, keys);
    // A7 ended last, so it is L1; L4 (A4) is newer than R1 (A3); R4 is oldest.
    EXPECT_EQ(label(keys, SoftKeyId::Left1).rfind("A7\n", 0), 0U);
    EXPECT_EQ(label(keys, SoftKeyId::Left4).rfind("A4\n", 0), 0U);
    EXPECT_EQ(label(keys, SoftKeyId::Right1).rfind("A3\n", 0), 0U);
    EXPECT_EQ(label(keys, SoftKeyId::Right4).rfind("A0\n", 0), 0U);
    EXPECT_TRUE(label(keys, SoftKeyId::Left1) == std::string("A7\n[01:07-02:07]"));
    EXPECT_TRUE(label(keys, SoftKeyId::Left5) == std::string("CLEAR ALL"));
    EXPECT_EQ(alert_history::page_count(), 1U);
}

HOST_TEST(alert_history_keeps_the_newest_sixteen)
{
    alert_history::reset();
    for (uint8_t i = 0U; i < 20U; ++i)
    {
        alert_history::record(make_alert(i, "X", at(kDay, 1, i)), alert_history::Ending::Cleared,
                              at(kDay, 2, i));
    }
    EXPECT_EQ(alert_history::count(), alert_history::kCapacity);
    EXPECT_EQ(alert_history::page_count(), 2U);
    EXPECT_EQ(alert_history::entry(0).code, 19U);
    EXPECT_EQ(alert_history::entry(15).code, 4U);
}

HOST_TEST(alert_history_folds_the_same_alert_within_the_hour)
{
    alert_history::reset();
    alert_history::record(make_alert(3, "WX WIND", at(kDay, 6, 12)), alert_history::Ending::Cleared,
                          at(kDay, 6, 20));
    alert_history::record(make_alert(5, "OTHER", at(kDay, 6, 25)), alert_history::Ending::Cleared,
                          at(kDay, 6, 30));
    alert_history::record(make_alert(3, "WX WIND", at(kDay, 6, 40)),
                          alert_history::Ending::Accepted, at(kDay, 7, 5));
    EXPECT_EQ(alert_history::count(), 2U);
    // The folded entry moves to the front with its first raise and new end.
    const alert_history::Entry& wind = alert_history::entry(0);
    EXPECT_EQ(wind.code, 3U);
    EXPECT_EQ(wind.count, 2U);
    EXPECT_EQ(wind.raised_minute, at(kDay, 6, 12));
    EXPECT_EQ(wind.ended_minute, at(kDay, 7, 5));
    EXPECT_TRUE(wind.ending == alert_history::Ending::Accepted);

    char value[24] = {};
    alert_history::format_key_value(wind, at(kDay, 8, 0), value, sizeof(value));
    EXPECT_TRUE(std::string(value) == std::string("[06:12 x2]"));

    // An hour after the entry was raised, it is a new entry.
    alert_history::record(make_alert(3, "WX WIND", at(kDay, 7, 10)), alert_history::Ending::Cleared,
                          at(kDay, 7, 15));
    EXPECT_EQ(alert_history::count(), 3U);
    EXPECT_EQ(alert_history::entry(0).count, 1U);
}

HOST_TEST(alert_history_does_not_fold_without_a_clock)
{
    alert_history::reset();
    alert_history::record(make_alert(3, "WX WIND", 0U), alert_history::Ending::Cleared, 0U);
    alert_history::record(make_alert(3, "WX WIND", 0U), alert_history::Ending::Cleared, 0U);
    EXPECT_EQ(alert_history::count(), 2U);
    char value[24] = {};
    alert_history::format_key_value(alert_history::entry(0), 0U, value, sizeof(value));
    EXPECT_TRUE(std::string(value) == std::string("[--:--]"));
}

HOST_TEST(alert_history_key_value_names_another_day)
{
    alert_history::reset();
    alert_history::record(make_alert(1, "RESTARTED", at(kDay - 1U, 23, 50)),
                          alert_history::Ending::Accepted, at(kDay, 0, 5));
    char value[24] = {};
    alert_history::format_key_value(alert_history::entry(0), at(kDay, 9, 0), value, sizeof(value));
    EXPECT_TRUE(std::string(value) == std::string("[23 Nov 23:50]"));
    // Fits a key at 8x12 (LBL-6): 15 characters at most.
    EXPECT_TRUE(std::strlen(value) <= 15U);
}

HOST_TEST(alert_history_detail_says_how_it_ended)
{
    alert_history::reset();
    ActiveAlert alert = make_alert(2, "WX WIND", at(kDay, 6, 12));
    alert.ignored = true;
    alert.ignored_minute = at(kDay, 6, 30);
    alert_history::record(alert, alert_history::Ending::Cleared, at(kDay, 7, 40));
    char text[520] = {};
    alert_history::format_detail(alert_history::entry(0), at(kDay, 9, 0), text, sizeof(text));
    const std::string detail = text;
    EXPECT_TRUE(detail.find("WX WIND (Warning)") != std::string::npos);
    EXPECT_TRUE(detail.find("Raised: 06:12") != std::string::npos);
    EXPECT_TRUE(detail.find("Ignored: 06:30") != std::string::npos);
    EXPECT_TRUE(detail.find("Cleared: 07:40") != std::string::npos);
    EXPECT_TRUE(detail.find("Detail for WX WIND.") != std::string::npos);
}

HOST_TEST(alert_history_records_cleared_accepted_and_ignored_alerts)
{
    alert_history::reset();
    alert_controller::reset();
    ConsoleState state = {};
    // Joined once since boot, so a drop raises NETWORK at once.
    state.wifi_status.state = WifiConnectionState::Connected;
    alert_controller::sync(state);
    state.wifi_status.state = WifiConnectionState::Disabled;
    set_clock(state, at(kDay, 6, 0));
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 1U);

    // IGNORE keeps it active and notes it; it is history once it clears.
    alert_controller::ignore_alert(state, 0U);
    EXPECT_EQ(alert_history::count(), 0U);
    set_clock(state, at(kDay, 6, 30));
    state.wifi_status.state = WifiConnectionState::Connected;
    alert_controller::sync(state);
    EXPECT_EQ(state.alert_count, 0U);
    EXPECT_EQ(alert_history::count(), 1U);
    EXPECT_TRUE(alert_history::entry(0).ending == alert_history::Ending::Cleared);
    EXPECT_TRUE(alert_history::entry(0).ignored);
    EXPECT_EQ(alert_history::entry(0).ignored_minute, at(kDay, 6, 0));

    // ACCEPT ends it at once.
    state.wifi_status.state = WifiConnectionState::Disabled;
    alert_controller::sync(state);
    alert_controller::accept_alert(state, 0U);
    EXPECT_EQ(alert_history::count(), 1U); // folded: same alert within the hour
    EXPECT_EQ(alert_history::entry(0).count, 2U);
    EXPECT_TRUE(alert_history::entry(0).ending == alert_history::Ending::Accepted);

    // Accepted, its condition clearing later adds nothing more.
    state.wifi_status.state = WifiConnectionState::Connected;
    alert_controller::sync(state);
    EXPECT_EQ(alert_history::entry(0).count, 2U);
}

HOST_TEST(alert_history_clear_all_empties_it_and_saves)
{
    alert_history::reset();
    alert_history::record(make_alert(1, "A", at(kDay, 1, 0)), alert_history::Ending::Cleared,
                          at(kDay, 1, 5));
    alert_history::mark_saved(1000U);
    alert_history::clear_all();
    EXPECT_EQ(alert_history::count(), 0U);
    EXPECT_FALSE(alert_history::save_due(1000U + alert_history::kNewEntrySaveDelayMs - 1U));
    EXPECT_TRUE(alert_history::save_due(1000U + alert_history::kNewEntrySaveDelayMs));

    ConsoleState state = {};
    SoftKeyMap keys = {};
    alert_controller::build_history_softkeys(state, keys);
    EXPECT_TRUE(label(keys, SoftKeyId::Left5) == std::string(""));
}

HOST_TEST(alert_history_saves_new_entries_soon_and_folds_later)
{
    alert_history::reset();
    EXPECT_FALSE(alert_history::save_due(0U));
    alert_history::record(make_alert(1, "A", at(kDay, 1, 0)), alert_history::Ending::Cleared,
                          at(kDay, 1, 5));
    EXPECT_TRUE(alert_history::save_due(0U)); // never saved yet
    alert_history::mark_saved(5000U);
    EXPECT_FALSE(alert_history::save_due(6000U));

    alert_history::record(make_alert(1, "A", at(kDay, 1, 10)), alert_history::Ending::Cleared,
                          at(kDay, 1, 15)); // folds
    EXPECT_FALSE(alert_history::save_due(5000U + alert_history::kNewEntrySaveDelayMs));
    EXPECT_TRUE(alert_history::save_due(5000U + alert_history::kFoldSaveDelayMs));
}

HOST_TEST(alert_history_image_survives_a_restart)
{
    alert_history::reset();
    alert_history::record(make_alert(4, "STORM", at(kDay, 5, 0)), alert_history::Ending::Accepted,
                          at(kDay, 5, 30));
    alert_history::record(make_alert(6, "WIFI", at(kDay, 6, 0)), alert_history::Ending::Cleared,
                          at(kDay, 6, 1));
    auto saved = std::make_unique<alert_history::Image>(alert_history::image_for_save(7U));
    EXPECT_TRUE(alert_history::image_valid(*saved));

    alert_history::reset();
    EXPECT_EQ(alert_history::count(), 0U);
    EXPECT_TRUE(alert_history::restore(*saved));
    EXPECT_EQ(alert_history::count(), 2U);
    EXPECT_TRUE(std::string(alert_history::entry(0).summary.data()) == std::string("WIFI"));
    EXPECT_TRUE(std::string(alert_history::entry(1).detail.data()) ==
                std::string("Detail for STORM."));

    // New entries carry on numbering after the restored ones.
    const uint32_t newest = alert_history::entry(0).sequence;
    alert_history::record(make_alert(8, "NEW", at(kDay, 7, 0)), alert_history::Ending::Cleared,
                          at(kDay, 7, 1));
    EXPECT_TRUE(alert_history::entry(0).sequence > newest);

    saved->entries[1].detail[0] = 'x';
    alert_history::reset();
    EXPECT_FALSE(alert_history::restore(*saved));
    EXPECT_EQ(alert_history::count(), 0U);
}

HOST_TEST(a_latched_acceptance_survives_a_restart_and_an_old_image_has_none)
{
    alert_history::reset();
    const auto code = static_cast<uint8_t>(AlertCode::EnvironmentSensorFault);
    alert_history::mark_saved(0U);
    alert_history::set_latched(code, true);
    EXPECT_TRUE(alert_history::latched(code));
    EXPECT_TRUE(alert_history::save_due(alert_history::kNewEntrySaveDelayMs));
    auto saved = std::make_unique<alert_history::Image>(alert_history::image_for_save(3U));

    alert_history::reset();
    EXPECT_FALSE(alert_history::latched(code));
    EXPECT_TRUE(alert_history::restore(*saved));
    EXPECT_TRUE(alert_history::latched(code));

    // A version 1 image ends at the entries; erased flash follows it.
    saved->version = alert_history::kImageVersionWithoutLatches;
    saved->latched_codes = 0xFFFFFFFFU;
    saved->latched_codes_check = 0xFFFFFFFFU;
    alert_history::reset();
    EXPECT_TRUE(alert_history::restore(*saved));
    EXPECT_FALSE(alert_history::latched(code));
    alert_history::reset();
}
