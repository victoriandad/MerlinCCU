#include "config_persistence.h"

#include <cstdio>
#include <cstring>

#include "test_framework.h"
#include "text_utils.h"

using config_persistence::ConfigCandidate;
using config_persistence::ConfigSlot;
using config_persistence::LegacyConfigSlotV1;
using config_persistence::LegacyConfigSlotV2;
using config_persistence::LegacyConfigSlotV3;
using config_persistence::LegacyConfigSlotV4;
using config_persistence::LegacyConfigSlotV5;
using config_persistence::LegacyRuntimeConfigV1;
using config_persistence::LegacyRuntimeConfigV2;
using config_persistence::LegacyRuntimeConfigV3;
using config_persistence::LegacyRuntimeConfigV4;
using config_persistence::LegacyRuntimeConfigV5;

namespace
{

/// @brief Builds a config slot with a correct header and CRC for the given settings.
ConfigSlot make_valid_slot(const RuntimeConfig& settings, uint32_t sequence)
{
    ConfigSlot slot = {};
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kConfigVersion;
    slot.sequence = sequence;
    slot.payload_size = sizeof(RuntimeConfig);
    slot.settings = settings;
    slot.crc32 =
        config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(RuntimeConfig));
    return slot;
}

} // namespace

HOST_TEST(crc32_matches_the_standard_check_value)
{
    // "123456789" is the official CRC-32 (IEEE 802.3 / zlib) check value:
    // 0xCBF43926. This pins the implementation to the standard algorithm,
    // not just to "whatever it currently computes".
    const uint8_t input[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    EXPECT_EQ(config_persistence::crc32(input, sizeof(input)), 0xCBF43926U);
}

HOST_TEST(validate_slot_accepts_a_correctly_built_slot)
{
    const RuntimeConfig settings = config_persistence::make_default_settings();
    const ConfigSlot slot = make_valid_slot(settings, 7U);
    EXPECT_TRUE(config_persistence::validate_slot(slot));
}

HOST_TEST(validate_slot_rejects_wrong_magic)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    ConfigSlot slot = make_valid_slot(settings, 1U);
    slot.magic = 0xDEADBEEFU;
    EXPECT_FALSE(config_persistence::validate_slot(slot));
}

HOST_TEST(validate_slot_rejects_wrong_version)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    ConfigSlot slot = make_valid_slot(settings, 1U);
    slot.version = config_persistence::kConfigVersion + 1U;
    EXPECT_FALSE(config_persistence::validate_slot(slot));
}

HOST_TEST(validate_slot_rejects_a_flipped_bit_in_the_payload)
{
    // Simulates a torn/partial flash write or bit rot: header is intact but the
    // payload no longer matches its stored CRC.
    RuntimeConfig settings = config_persistence::make_default_settings();
    ConfigSlot slot = make_valid_slot(settings, 1U);
    slot.settings.mqtt_port ^= 0x1U;
    EXPECT_FALSE(config_persistence::validate_slot(slot));
}

HOST_TEST(validate_slot_rejects_wrong_payload_size)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    ConfigSlot slot = make_valid_slot(settings, 1U);
    slot.payload_size = sizeof(RuntimeConfig) - 1U;
    EXPECT_FALSE(config_persistence::validate_slot(slot));
}

HOST_TEST(choose_newest_candidate_prefers_higher_sequence_when_both_valid)
{
    const ConfigCandidate older = {true, 3U};
    const ConfigCandidate newer = {true, 4U};

    const ConfigCandidate* chosen = config_persistence::choose_newest_candidate(older, newer);
    EXPECT_TRUE(chosen == &newer);

    const ConfigCandidate* chosen_reversed = config_persistence::choose_newest_candidate(newer, older);
    EXPECT_TRUE(chosen_reversed == &newer);
}

HOST_TEST(choose_newest_candidate_falls_back_to_whichever_slot_is_valid)
{
    const ConfigCandidate valid = {true, 1U};
    const ConfigCandidate invalid = {false, 0U};

    EXPECT_TRUE(config_persistence::choose_newest_candidate(valid, invalid) == &valid);
    EXPECT_TRUE(config_persistence::choose_newest_candidate(invalid, valid) == &valid);
}

HOST_TEST(choose_newest_candidate_returns_null_when_neither_slot_is_valid)
{
    const ConfigCandidate a = {false, 0U};
    const ConfigCandidate b = {false, 0U};
    EXPECT_TRUE(config_persistence::choose_newest_candidate(a, b) == nullptr);
}

HOST_TEST(migrate_legacy_settings_carries_every_field_forward)
{
    LegacyRuntimeConfigV1 legacy = {};
    text_utils::copy_text(legacy.device_name, "OldName");
    text_utils::copy_text(legacy.wifi_ssid, "OldWifi");
    text_utils::copy_text(legacy.home_assistant_host, "ha.local");
    legacy.home_assistant_port = 9123;
    legacy.mqtt_port = 1884;
    legacy.mqtt_enabled = true;
    legacy.screen_saver_timeout_minutes = 42;
    legacy.weather_source = WeatherSource::OpenMeteo;
    legacy.time_zone = TimeZoneSelection::CentralEuropean;

    const RuntimeConfig migrated = config_persistence::migrate_legacy_settings(legacy);

    EXPECT_TRUE(std::strcmp(migrated.device_name.data(), "OldName") == 0);
    EXPECT_TRUE(std::strcmp(migrated.wifi_ssid.data(), "OldWifi") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_host.data(), "ha.local") == 0);
    EXPECT_EQ(migrated.home_assistant_port, 9123U);
    EXPECT_EQ(migrated.mqtt_port, 1884U);
    EXPECT_TRUE(migrated.mqtt_enabled);
    EXPECT_EQ(migrated.screen_saver_timeout_minutes, 42U);
    EXPECT_TRUE(migrated.weather_source == WeatherSource::OpenMeteo);
    EXPECT_TRUE(migrated.time_zone == TimeZoneSelection::CentralEuropean);
    // A field that did not exist in v1 must come back as an explicit, safe
    // default rather than leftover/uninitialized memory.
    EXPECT_EQ(migrated.weather_coordinates[0], '\0');
    // Watched shares did not exist in v1 either; migration must seed the same
    // single-share demo watchlist a fresh install gets, not an empty list.
    EXPECT_EQ(migrated.watched_share_count, 1U);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].symbol.data(), "BA.L") == 0);
}

HOST_TEST(migrate_legacy_settings_v2_carries_every_field_forward)
{
    LegacyRuntimeConfigV2 legacy = {};
    text_utils::copy_text(legacy.device_name, "OldName2");
    text_utils::copy_text(legacy.wifi_ssid, "OldWifi2");
    legacy.mqtt_port = 1885;
    legacy.screen_saver_timeout_minutes = 17;
    legacy.weather_source = WeatherSource::OpenMeteo;

    const RuntimeConfig migrated = config_persistence::migrate_legacy_settings_v2(legacy);

    EXPECT_TRUE(std::strcmp(migrated.device_name.data(), "OldName2") == 0);
    EXPECT_TRUE(std::strcmp(migrated.wifi_ssid.data(), "OldWifi2") == 0);
    EXPECT_EQ(migrated.mqtt_port, 1885U);
    EXPECT_EQ(migrated.screen_saver_timeout_minutes, 17U);
    EXPECT_TRUE(migrated.weather_source == WeatherSource::OpenMeteo);
    // Watched shares did not exist in v2 either; same fresh-install default applies.
    EXPECT_EQ(migrated.watched_share_count, 1U);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].symbol.data(), "BA.L") == 0);
}

HOST_TEST(validate_legacy_slot_v2_accepts_a_correctly_built_slot)
{
    LegacyRuntimeConfigV2 legacy = {};
    text_utils::copy_text(legacy.device_name, "V2Device");
    LegacyConfigSlotV2 slot = {};
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kLegacyConfigVersionV2;
    slot.sequence = 3U;
    slot.payload_size = sizeof(LegacyRuntimeConfigV2);
    slot.settings = legacy;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                          sizeof(LegacyRuntimeConfigV2));

    EXPECT_TRUE(config_persistence::validate_legacy_slot_v2(slot));
}

HOST_TEST(migrate_legacy_settings_v3_carries_every_field_forward)
{
    LegacyRuntimeConfigV3 legacy = {};
    text_utils::copy_text(legacy.device_name, "OldName3");
    text_utils::copy_text(legacy.home_assistant_host, "ha3.local");
    legacy.watched_share_count = 2U;
    text_utils::copy_text(legacy.watched_shares[0].symbol, "AAA");
    text_utils::copy_text(legacy.watched_shares[0].display_name, "Alpha Co");
    text_utils::copy_text(legacy.watched_shares[1].symbol, "BBB");
    legacy.mqtt_port = 1886;
    legacy.screen_saver_timeout_minutes = 9;

    const RuntimeConfig migrated = config_persistence::migrate_legacy_settings_v3(legacy);

    EXPECT_TRUE(std::strcmp(migrated.device_name.data(), "OldName3") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_host.data(), "ha3.local") == 0);
    EXPECT_EQ(migrated.watched_share_count, 2U);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].symbol.data(), "AAA") == 0);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].display_name.data(), "Alpha Co") == 0);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[1].symbol.data(), "BBB") == 0);
    EXPECT_EQ(migrated.mqtt_port, 1886U);
    EXPECT_EQ(migrated.screen_saver_timeout_minutes, 9U);
    // The share feed did not exist in v3 either; migration must come back
    // with an explicit, safe default (disabled, no token) rather than
    // leftover/uninitialized memory.
    EXPECT_FALSE(migrated.shares_feed_enabled);
    EXPECT_EQ(migrated.shares_feed_token[0], '\0');
}

HOST_TEST(validate_legacy_slot_v3_accepts_a_correctly_built_slot)
{
    LegacyRuntimeConfigV3 legacy = {};
    text_utils::copy_text(legacy.device_name, "V3Device");
    LegacyConfigSlotV3 slot = {};
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kLegacyConfigVersionV3;
    slot.sequence = 4U;
    slot.payload_size = sizeof(LegacyRuntimeConfigV3);
    slot.settings = legacy;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                          sizeof(LegacyRuntimeConfigV3));

    EXPECT_TRUE(config_persistence::validate_legacy_slot_v3(slot));
}

HOST_TEST(migrate_legacy_settings_v4_carries_every_field_forward)
{
    LegacyRuntimeConfigV4 legacy = {};
    text_utils::copy_text(legacy.device_name, "OldName4");
    text_utils::copy_text(legacy.home_assistant_host, "ha.lan");
    legacy.home_assistant_port = 80;
    legacy.home_assistant_enabled = true;
    text_utils::copy_text(legacy.home_assistant_token, "short-old-token");
    text_utils::copy_text(legacy.home_assistant_entity_id, "sensor.thing");
    legacy.watched_share_count = 1U;
    text_utils::copy_text(legacy.watched_shares[0].symbol, "CCC");
    legacy.shares_feed_enabled = true;
    text_utils::copy_text(legacy.shares_feed_token, "feed-token");
    legacy.mqtt_port = 1887;

    const RuntimeConfig migrated = config_persistence::migrate_legacy_settings_v4(legacy);

    EXPECT_TRUE(std::strcmp(migrated.device_name.data(), "OldName4") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_host.data(), "ha.lan") == 0);
    EXPECT_EQ(migrated.home_assistant_port, 80U);
    EXPECT_TRUE(migrated.home_assistant_enabled);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_token.data(), "short-old-token") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_entity_id.data(), "sensor.thing") == 0);
    EXPECT_EQ(migrated.watched_share_count, 1U);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].symbol.data(), "CCC") == 0);
    EXPECT_TRUE(migrated.shares_feed_enabled);
    EXPECT_TRUE(std::strcmp(migrated.shares_feed_token.data(), "feed-token") == 0);
    EXPECT_EQ(migrated.mqtt_port, 1887U);
}

HOST_TEST(validate_legacy_slot_v4_accepts_a_correctly_built_slot)
{
    LegacyRuntimeConfigV4 legacy = {};
    text_utils::copy_text(legacy.device_name, "V4Device");
    LegacyConfigSlotV4 slot = {};
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kLegacyConfigVersionV4;
    slot.sequence = 5U;
    slot.payload_size = sizeof(LegacyRuntimeConfigV4);
    slot.settings = legacy;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                          sizeof(LegacyRuntimeConfigV4));

    EXPECT_TRUE(config_persistence::validate_legacy_slot_v4(slot));
}

HOST_TEST(migrate_legacy_settings_v5_drops_air_traffic_and_keeps_everything_else)
{
    LegacyRuntimeConfigV5 legacy = {};
    text_utils::copy_text(legacy.device_name, "OldName5");
    text_utils::copy_text(legacy.home_assistant_host, "ha.lan");
    legacy.home_assistant_port = 80;
    legacy.home_assistant_enabled = true;
    text_utils::copy_text(legacy.home_assistant_token, "token-five");
    text_utils::copy_text(legacy.home_assistant_self_entity_id, "sensor.merlinccu_status");
    legacy.air_traffic_enabled = true;
    text_utils::copy_text(legacy.air_traffic_host, "api.adsb.lol");
    legacy.screen_saver_timeout_minutes = 11;
    legacy.watched_share_count = 1U;
    text_utils::copy_text(legacy.watched_shares[0].symbol, "DDD");
    legacy.shares_feed_enabled = true;
    text_utils::copy_text(legacy.shares_feed_token, "feed-five");

    const RuntimeConfig migrated = config_persistence::migrate_legacy_settings_v5(legacy);

    EXPECT_TRUE(std::strcmp(migrated.device_name.data(), "OldName5") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_host.data(), "ha.lan") == 0);
    EXPECT_EQ(migrated.home_assistant_port, 80U);
    EXPECT_TRUE(migrated.home_assistant_enabled);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_token.data(), "token-five") == 0);
    EXPECT_TRUE(std::strcmp(migrated.home_assistant_self_entity_id.data(),
                            "sensor.merlinccu_status") == 0);
    EXPECT_EQ(migrated.screen_saver_timeout_minutes, 11U);
    EXPECT_TRUE(std::strcmp(migrated.watched_shares[0].symbol.data(), "DDD") == 0);
    EXPECT_TRUE(migrated.shares_feed_enabled);
    EXPECT_TRUE(std::strcmp(migrated.shares_feed_token.data(), "feed-five") == 0);
}

HOST_TEST(validate_legacy_slot_v5_accepts_a_correctly_built_slot)
{
    LegacyRuntimeConfigV5 legacy = {};
    text_utils::copy_text(legacy.device_name, "V5Device");
    LegacyConfigSlotV5 slot = {};
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kLegacyConfigVersionV5;
    slot.sequence = 6U;
    slot.payload_size = sizeof(LegacyRuntimeConfigV5);
    slot.settings = legacy;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                          sizeof(LegacyRuntimeConfigV5));

    EXPECT_TRUE(config_persistence::validate_legacy_slot_v5(slot));
}

HOST_TEST(current_slot_round_trips_a_full_length_home_assistant_token)
{
    // Real HA long-lived tokens are ~183 characters; the old 128-byte field
    // could not hold one, so the web form always refused to save HA settings.
    constexpr size_t kTypicalHaTokenLength = 183U;
    std::array<char, kTypicalHaTokenLength + 1U> token = {};
    for (size_t i = 0U; i < kTypicalHaTokenLength; ++i)
    {
        token[i] = static_cast<char>('a' + (i % 26U));
    }

    RuntimeConfig settings = config_persistence::make_default_settings();
    text_utils::copy_text(settings.home_assistant_token, token.data());
    const ConfigSlot slot = make_valid_slot(settings, 9U);

    EXPECT_TRUE(config_persistence::validate_slot(slot));
    EXPECT_EQ(std::strlen(slot.settings.home_assistant_token.data()), kTypicalHaTokenLength);
    EXPECT_TRUE(std::strcmp(slot.settings.home_assistant_token.data(), token.data()) == 0);
}

HOST_TEST(make_default_settings_disables_the_shares_feed_by_default)
{
    const RuntimeConfig settings = config_persistence::make_default_settings();

    EXPECT_FALSE(settings.shares_feed_enabled);
    EXPECT_EQ(settings.shares_feed_token[0], '\0');
}

HOST_TEST(sanitize_settings_drops_blank_symbol_rows_and_compacts_the_count)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.watched_share_count = 3U;
    text_utils::copy_text(settings.watched_shares[0].symbol, "AAA");
    text_utils::copy_text(settings.watched_shares[0].display_name, "Alpha");
    settings.watched_shares[1].symbol.fill('\0');
    text_utils::copy_text(settings.watched_shares[2].symbol, "CCC");
    text_utils::copy_text(settings.watched_shares[2].display_name, "Charlie");

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_EQ(sanitized.watched_share_count, 2U);
    EXPECT_TRUE(std::strcmp(sanitized.watched_shares[0].symbol.data(), "AAA") == 0);
    EXPECT_TRUE(std::strcmp(sanitized.watched_shares[1].symbol.data(), "CCC") == 0);
}

HOST_TEST(sanitize_settings_fills_blank_display_name_from_symbol)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.watched_share_count = 1U;
    text_utils::copy_text(settings.watched_shares[0].symbol, "MSFT");
    settings.watched_shares[0].display_name.fill('\0');

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_TRUE(std::strcmp(sanitized.watched_shares[0].display_name.data(), "MSFT") == 0);
}

HOST_TEST(sanitize_settings_clamps_watched_share_count_to_the_array_capacity)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.watched_share_count = 255U;
    for (size_t i = 0; i < settings.watched_shares.size(); ++i)
    {
        char symbol[8] = {};
        std::snprintf(symbol, sizeof(symbol), "S%zu", i);
        text_utils::copy_text(settings.watched_shares[i].symbol, symbol);
    }

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_EQ(sanitized.watched_share_count, settings.watched_shares.size());
}

HOST_TEST(make_default_settings_seeds_the_demo_watchlist)
{
    const RuntimeConfig settings = config_persistence::make_default_settings();

    EXPECT_EQ(settings.watched_share_count, 1U);
    EXPECT_TRUE(std::strcmp(settings.watched_shares[0].symbol.data(), "BA.L") == 0);
    EXPECT_TRUE(std::strcmp(settings.watched_shares[0].display_name.data(), "BAE SYSTEMS") == 0);
}

HOST_TEST(sanitize_settings_fills_empty_device_name)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.device_name.fill('\0');

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_TRUE(std::strcmp(sanitized.device_name.data(), config_persistence::kDefaultDeviceName) == 0);
}

HOST_TEST(sanitize_settings_clamps_screen_saver_timeout_to_the_maximum)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.screen_saver_timeout_minutes = 9999U;

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_EQ(sanitized.screen_saver_timeout_minutes,
             config_persistence::kMaxScreenSaverTimeoutMinutes);
}

HOST_TEST(sanitize_settings_migrates_retired_met_norway_weather_source)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.weather_source = WeatherSource::MetNorway;

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_TRUE(sanitized.weather_source == WeatherSource::OpenMeteo);
}

HOST_TEST(sanitize_settings_repairs_partially_saved_home_assistant_state)
{
    // Regression case named directly in the source comment: a prior web-form
    // truncation bug could clear only the HA enable checkbox while leaving
    // host/token/entity fields intact.
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.home_assistant_enabled = false;
    text_utils::copy_text(settings.home_assistant_host, "ha.local");
    text_utils::copy_text(settings.home_assistant_token, "token123");
    text_utils::copy_text(settings.home_assistant_entity_id, "sensor.x");

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_TRUE(sanitized.home_assistant_enabled);
}

HOST_TEST(should_auto_enable_home_assistant_requires_all_three_fields)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.home_assistant_enabled = false;
    EXPECT_FALSE(config_persistence::should_auto_enable_home_assistant(settings));

    text_utils::copy_text(settings.home_assistant_host, "ha.local");
    EXPECT_FALSE(config_persistence::should_auto_enable_home_assistant(settings));

    text_utils::copy_text(settings.home_assistant_token, "token123");
    EXPECT_FALSE(config_persistence::should_auto_enable_home_assistant(settings));

    text_utils::copy_text(settings.home_assistant_entity_id, "sensor.x");
    EXPECT_TRUE(config_persistence::should_auto_enable_home_assistant(settings));

    // Already-enabled should not be reported as "needs auto-enable".
    settings.home_assistant_enabled = true;
    EXPECT_FALSE(config_persistence::should_auto_enable_home_assistant(settings));
}
