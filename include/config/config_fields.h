#pragma once

#include <cstddef>
#include <cstdint>

#include "config_manager.h"

/// @brief One length limit and character rule per text setting (#185).
/// @details The web form's `maxlength`, the server's check before saving and
/// any hint that states a limit all read the same entry, so they cannot drift
/// apart. Each limit is the smallest of the `RuntimeConfig` field it is stored
/// in and every buffer that consumes it further on; the `static_assert`s here
/// and in the consumers (wifi_manager, mqtt_manager, home_assistant_manager,
/// share_price_manager) fail the build if either side changes. A 21-character
/// device name once boot-looped a unit because NetBIOS takes only 15 (#184).
/// Pure, so host tests cover it.
namespace config_fields
{

/// @brief Characters a field accepts.
enum class Charset : uint8_t
{
    /// Printable ASCII, space to tilde.
    Printable,
    /// Letters, digits, '-' and '_': a network name and part of entity ids.
    DeviceName,
};

/// @brief The text settings on the web config page.
enum class Field : uint8_t
{
    DeviceName,
    DeviceLabel,
    Location,
    Room,
    WifiSsid,
    WifiPassword,
    HomeAssistantHost,
    HomeAssistantToken,
    TrackedEntity,
    SelfEntity,
    MqttHost,
    MqttUsername,
    MqttPassword,
    MqttDiscoveryPrefix,
    MqttBaseTopic,
    ShareSymbol,
    ShareName,
    SharesFeedToken,
    Count,
};

inline constexpr size_t kFieldCount = static_cast<size_t>(Field::Count);

/// @brief Longest NetBIOS name lwIP accepts; it asserts on a longer one
/// (#184). The device name is also the DHCP host name.
inline constexpr size_t kNetbiosNameLength = 15U;

/// @brief Longest MQTT base topic: the Home Assistant integration's own
/// limit (CONF_BASE_TOPIC, max 40, in ha/custom_components/merlinccu), which
/// also fits mqtt_manager's topic buffers.
inline constexpr size_t kMqttBaseTopicLength = 40U;

/// @brief Longest host or URL for Home Assistant and the MQTT broker; the
/// network managers' host buffers hold it.
inline constexpr size_t kHostLength = 63U;

/// @brief What one field accepts.
struct FieldLimit
{
    /// The form field's `name`.
    const char* form_name;
    /// Shown in error messages.
    const char* label;
    size_t max_length;
    bool required;
    Charset charset;
};

/// @brief The limit for `field`.
const FieldLimit& limit(Field field);

/// @brief True when every character of `text` is allowed by `charset`.
bool charset_allows(Charset charset, const char* text);

/// @brief Checks `value` for `field`.
/// @return True when it may be saved; otherwise false, with a sentence for
/// the web page in `error` ("Configuration not saved: ...").
bool validate(Field field, const char* value, char* error, size_t error_size);

// Every limit fits the RuntimeConfig array it is stored in, terminator
// included.
static_assert(kNetbiosNameLength < sizeof(RuntimeConfig::device_name));
static_assert(kHostLength < sizeof(RuntimeConfig::home_assistant_host));
static_assert(kHostLength < sizeof(RuntimeConfig::mqtt_host));
static_assert(kMqttBaseTopicLength < sizeof(RuntimeConfig::mqtt_base_topic));

} // namespace config_fields
