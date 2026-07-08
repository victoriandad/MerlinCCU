#include "config_persistence.h"

#include <cstring>

#include "test_framework.h"
#include "text_utils.h"

using config_persistence::ConfigCandidate;
using config_persistence::ConfigSlot;
using config_persistence::LegacyConfigSlotV1;
using config_persistence::LegacyRuntimeConfigV1;

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
    const RuntimeConfig settings = config_persistence::make_default_settings();
    const ConfigCandidate older = {true, 3U, settings};
    const ConfigCandidate newer = {true, 4U, settings};

    const ConfigCandidate* chosen = config_persistence::choose_newest_candidate(older, newer);
    EXPECT_TRUE(chosen == &newer);

    const ConfigCandidate* chosen_reversed = config_persistence::choose_newest_candidate(newer, older);
    EXPECT_TRUE(chosen_reversed == &newer);
}

HOST_TEST(choose_newest_candidate_falls_back_to_whichever_slot_is_valid)
{
    const RuntimeConfig settings = config_persistence::make_default_settings();
    const ConfigCandidate valid = {true, 1U, settings};
    const ConfigCandidate invalid = {false, 0U, {}};

    EXPECT_TRUE(config_persistence::choose_newest_candidate(valid, invalid) == &valid);
    EXPECT_TRUE(config_persistence::choose_newest_candidate(invalid, valid) == &valid);
}

HOST_TEST(choose_newest_candidate_returns_null_when_neither_slot_is_valid)
{
    const ConfigCandidate a = {false, 0U, {}};
    const ConfigCandidate b = {false, 0U, {}};
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
}

HOST_TEST(sanitize_settings_fills_empty_device_name_and_password)
{
    RuntimeConfig settings = config_persistence::make_default_settings();
    settings.device_name.fill('\0');
    settings.admin_password.fill('\0');

    const RuntimeConfig sanitized = config_persistence::sanitize_settings(settings);

    EXPECT_TRUE(std::strcmp(sanitized.device_name.data(), config_persistence::kDefaultDeviceName) == 0);
    EXPECT_TRUE(
        std::strcmp(sanitized.admin_password.data(), config_persistence::kDefaultAdminPassword) == 0);
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

HOST_TEST(constant_time_password_matches_functional_correctness)
{
    std::array<char, 32> stored = {};
    text_utils::copy_text(stored, "merlin");

    // Real call sites always pass an oversized, zero-initialized buffer (see
    // web_config_server.cpp's `char current_password[40] = {}`), so tests use
    // the same shape rather than a bare string literal.
    std::array<char, 40> correct = {};
    text_utils::copy_text(correct, "merlin");
    EXPECT_TRUE(config_persistence::constant_time_password_matches(correct.data(), stored));

    std::array<char, 40> wrong = {};
    text_utils::copy_text(wrong, "wrong-password");
    EXPECT_FALSE(config_persistence::constant_time_password_matches(wrong.data(), stored));

    std::array<char, 40> shorter = {};
    text_utils::copy_text(shorter, "merli");
    EXPECT_FALSE(config_persistence::constant_time_password_matches(shorter.data(), stored));

    std::array<char, 40> longer = {};
    text_utils::copy_text(longer, "merlin2");
    EXPECT_FALSE(config_persistence::constant_time_password_matches(longer.data(), stored));

    EXPECT_FALSE(config_persistence::constant_time_password_matches(nullptr, stored));
}
