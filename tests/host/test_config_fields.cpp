// Web config text limits (#185): one table drives the form's maxlength and
// the save check, so an over-long value is refused rather than cut short
// or passed on to a smaller buffer.

#include "config_fields.h"

#include <array>
#include <cstring>
#include <string>

#include "test_framework.h"

using config_fields::Field;

namespace
{

constexpr std::array<const char*, config_fields::kFieldCount> kFormNames = {
    "device_name",  "device_label",  "location",      "room",
    "wifi_ssid",    "wifi_password", "ha_host",       "ha_token",
    "ha_entity",    "ha_self",       "mqtt_host",     "mqtt_username",
    "mqtt_password", "mqtt_prefix",  "mqtt_topic",    "share_symbol",
    "share_name",   "shares_feed_token",
};

} // namespace

HOST_TEST(config_fields_table_is_in_field_order)
{
    for (size_t i = 0U; i < config_fields::kFieldCount; ++i)
    {
        EXPECT_TRUE(std::strcmp(config_fields::limit(static_cast<Field>(i)).form_name,
                                kFormNames[i]) == 0);
    }
}

HOST_TEST(config_fields_refuse_one_character_too_many_for_every_field)
{
    char error[160] = {};
    for (size_t i = 0U; i < config_fields::kFieldCount; ++i)
    {
        const auto field = static_cast<Field>(i);
        const size_t max_length = config_fields::limit(field).max_length;
        EXPECT_TRUE(max_length > 0U);
        // Letters suit every charset, including the device name's.
        const std::string longest(max_length, 'a');
        EXPECT_TRUE(config_fields::validate(field, longest.c_str(), error, sizeof(error)));
        const std::string too_long(max_length + 1U, 'a');
        error[0] = '\0';
        EXPECT_FALSE(config_fields::validate(field, too_long.c_str(), error, sizeof(error)));
        EXPECT_TRUE(std::strstr(error, "too long") != nullptr);
        EXPECT_TRUE(std::strstr(error, config_fields::limit(field).label) != nullptr);
    }
}

HOST_TEST(config_fields_limits_follow_the_tightest_consumer)
{
    // NetBIOS (#184) and the HA integration's base-topic limit.
    EXPECT_EQ(config_fields::limit(Field::DeviceName).max_length, size_t{15U});
    EXPECT_EQ(config_fields::limit(Field::MqttBaseTopic).max_length, size_t{40U});
    EXPECT_EQ(config_fields::limit(Field::DeviceLabel).max_length,
              sizeof(RuntimeConfig::device_label) - 1U);
}

HOST_TEST(config_fields_check_characters_and_required_values)
{
    char error[160] = {};
    EXPECT_FALSE(config_fields::validate(Field::DeviceName, "", error, sizeof(error)));
    EXPECT_TRUE(std::strstr(error, "required") != nullptr);
    EXPECT_FALSE(config_fields::validate(Field::DeviceName, "merlin ground", error, sizeof(error)));
    EXPECT_TRUE(std::strstr(error, "unsupported") != nullptr);
    EXPECT_TRUE(config_fields::validate(Field::DeviceName, "merlin-ground_2", error, sizeof(error)));

    EXPECT_TRUE(config_fields::validate(Field::DeviceLabel, "", error, sizeof(error)));
    EXPECT_TRUE(config_fields::validate(Field::DeviceLabel, "Ground floor", error, sizeof(error)));
    EXPECT_FALSE(config_fields::validate(Field::DeviceLabel, "Caf\xC3\xA9", error, sizeof(error)));
    EXPECT_FALSE(config_fields::validate(Field::Room, "a\tb", error, sizeof(error)));
}
