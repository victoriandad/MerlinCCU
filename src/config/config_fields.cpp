#include "config_fields.h"

#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace config_fields
{

namespace
{

/// @brief Longest text a character array of `bytes` bytes holds.
constexpr size_t stored(size_t bytes)
{
    return bytes - 1U;
}

// In Field order (host-tested). Fields whose value is only checked when
// one is given (passwords and tokens, which the page never echoes) are not
// marked required.
constexpr std::array<FieldLimit, kFieldCount> kLimits = {{
    {"device_name", "Device name", kNetbiosNameLength, true, Charset::DeviceName},
    {"device_label", "Display label", stored(sizeof(RuntimeConfig::device_label)), false, Charset::Printable},
    {"location", "Location", stored(sizeof(RuntimeConfig::location)), false, Charset::Printable},
    {"room", "Room", stored(sizeof(RuntimeConfig::room)), false, Charset::Printable},
    {"wifi_ssid", "Wi-Fi SSID", stored(sizeof(RuntimeConfig::wifi_ssid)), false, Charset::Printable},
    {"wifi_password", "Wi-Fi password", stored(sizeof(RuntimeConfig::wifi_password)), false, Charset::Printable},
    {"ha_host", "Home Assistant host", kHostLength, false, Charset::Printable},
    {"ha_token", "Home Assistant token", stored(sizeof(RuntimeConfig::home_assistant_token)), false,
     Charset::Printable},
    {"ha_entity", "Tracked entity", stored(sizeof(RuntimeConfig::home_assistant_entity_id)), false,
     Charset::Printable},
    {"ha_self", "Self entity", stored(sizeof(RuntimeConfig::home_assistant_self_entity_id)), false,
     Charset::Printable},
    {"mqtt_host", "MQTT broker host", kHostLength, false, Charset::Printable},
    {"mqtt_username", "MQTT username", stored(sizeof(RuntimeConfig::mqtt_username)), false, Charset::Printable},
    {"mqtt_password", "MQTT password", stored(sizeof(RuntimeConfig::mqtt_password)), false, Charset::Printable},
    {"mqtt_prefix", "MQTT discovery prefix", stored(sizeof(RuntimeConfig::mqtt_discovery_prefix)), false,
     Charset::Printable},
    {"mqtt_topic", "MQTT base topic", kMqttBaseTopicLength, false, Charset::Printable},
    {"share_symbol", "Share symbol", stored(sizeof(WatchedShareConfig::symbol)), false,
     Charset::Printable},
    {"share_name", "Share display name", stored(sizeof(WatchedShareConfig::display_name)), false,
     Charset::Printable},
    {"shares_feed_token", "Share feed token", stored(sizeof(RuntimeConfig::shares_feed_token)), false,
     Charset::Printable},
}};

} // namespace

const FieldLimit& limit(Field field)
{
    const size_t index = static_cast<size_t>(field);
    return kLimits[index < kLimits.size() ? index : 0U];
}

bool charset_allows(Charset charset, const char* text)
{
    if (text == nullptr)
    {
        return false;
    }
    for (size_t i = 0U; text[i] != '\0'; ++i)
    {
        const auto ch = static_cast<unsigned char>(text[i]);
        const bool allowed = charset == Charset::DeviceName
                                 ? (std::isalnum(ch) != 0 || ch == '-' || ch == '_')
                                 : (ch >= 0x20U && ch <= 0x7EU);
        if (!allowed)
        {
            return false;
        }
    }
    return true;
}

bool validate(Field field, const char* value, char* error, size_t error_size)
{
    const FieldLimit& rule = limit(field);
    const char* text = value != nullptr ? value : "";
    if (rule.required && text[0] == '\0')
    {
        std::snprintf(error, error_size, "Configuration not saved: %s is required.", rule.label);
        return false;
    }
    if (std::strlen(text) > rule.max_length)
    {
        std::snprintf(error, error_size,
                      "Configuration not saved: %s is too long; maximum is %u characters.",
                      rule.label, static_cast<unsigned>(rule.max_length));
        return false;
    }
    if (!charset_allows(rule.charset, text))
    {
        std::snprintf(error, error_size,
                      "Configuration not saved: %s contains unsupported characters.", rule.label);
        return false;
    }
    return true;
}

} // namespace config_fields
