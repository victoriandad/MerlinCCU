#include "config_manager.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace config_manager
{

namespace
{

constexpr uint32_t kConfigMagic = 0x4D434355U; // "MCCU"
constexpr uint16_t kConfigVersion = 2;
constexpr uint16_t kLegacyConfigVersion = 1;
constexpr uint32_t kFlashSlotCount = 2;
constexpr uint32_t kConfigStorageBytes = FLASH_SECTOR_SIZE * kFlashSlotCount;
constexpr uint32_t kConfigStorageOffset = PICO_FLASH_SIZE_BYTES - kConfigStorageBytes;
constexpr uint32_t kSlot0Offset = kConfigStorageOffset;
constexpr uint32_t kSlot1Offset = kConfigStorageOffset + FLASH_SECTOR_SIZE;
constexpr uint16_t kMaxScreenSaverTimeoutMinutes = 120U;
constexpr char kDefaultDeviceName[] = "MerlinCCU";
constexpr char kDefaultAdminPassword[] = "merlin";

/// @brief One flash-backed configuration slot.
/// @details Two slots are alternated by sequence number so a power loss during
/// one write still leaves the previous complete configuration available.
struct ConfigSlot
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    RuntimeConfig settings;
};

static_assert(sizeof(ConfigSlot) <= FLASH_SECTOR_SIZE, "Config slot must fit in one flash sector");

RuntimeConfig g_settings = {};
uint32_t g_sequence = 0;

/// @brief Runtime configuration layout used by config version 1.
/// @details This mirrors the previous flash payload exactly so existing saved
/// settings can be migrated when new fields are added.
struct LegacyRuntimeConfigV1
{
    std::array<char, 32> device_name;
    std::array<char, 32> device_label;
    std::array<char, 32> location;
    std::array<char, 32> room;
    std::array<char, 32> admin_password;
    bool remote_config_enabled;
    bool require_admin_password;

    std::array<char, 33> wifi_ssid;
    std::array<char, 64> wifi_password;

    bool home_assistant_enabled;
    std::array<char, 64> home_assistant_host;
    uint16_t home_assistant_port;
    std::array<char, 128> home_assistant_token;
    std::array<char, 64> home_assistant_entity_id;
    std::array<char, 64> home_assistant_self_entity_id;
    std::array<char, 64> weather_entity_id;
    std::array<char, 64> sun_entity_id;

    bool mqtt_enabled;
    std::array<char, 64> mqtt_host;
    uint16_t mqtt_port;
    std::array<char, 64> mqtt_username;
    std::array<char, 64> mqtt_password;
    std::array<char, 32> mqtt_discovery_prefix;
    std::array<char, 64> mqtt_base_topic;

    WeatherSource weather_source;
    TimeZoneSelection time_zone;
    ScreenSaverSelection screen_saver;
    uint16_t screen_saver_timeout_minutes;
};

struct LegacyConfigSlotV1
{
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
    uint32_t sequence;
    uint32_t payload_size;
    uint32_t crc32;
    LegacyRuntimeConfigV1 settings;
};

/// @brief Copies a C string into one fixed-size config field.
template <size_t N> void copy_text(std::array<char, N>& dest, const char* src)
{
    dest.fill('\0');
    if (src == nullptr)
    {
        return;
    }

    std::snprintf(dest.data(), dest.size(), "%s", src);
}

/// @brief Calculates a standard CRC-32 over a byte buffer.
uint32_t crc32(const uint8_t* data, size_t length)
{
    uint32_t crc = 0xFFFFFFFFU;

    for (size_t i = 0; i < length; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
        {
            crc = (crc >> 1U) ^ ((crc & 1U) ? 0xEDB88320U : 0U);
        }
    }

    return ~crc;
}

/// @brief Builds the factory/default configuration used when flash is empty.
RuntimeConfig make_default_settings()
{
    RuntimeConfig settings = {};
    copy_text(settings.device_name, kDefaultDeviceName);
    copy_text(settings.device_label, "Merlin CCU");
    copy_text(settings.location, "");
    copy_text(settings.room, "");
    copy_text(settings.admin_password, kDefaultAdminPassword);
    settings.remote_config_enabled = true;
    settings.require_admin_password = true;

    copy_text(settings.wifi_ssid, "");
    copy_text(settings.wifi_password, "");

    settings.home_assistant_enabled = false;
    copy_text(settings.home_assistant_host, "");
    settings.home_assistant_port = 8123;
    copy_text(settings.home_assistant_token, "");
    copy_text(settings.home_assistant_entity_id, "");
    copy_text(settings.home_assistant_self_entity_id, "");
    copy_text(settings.weather_entity_id, "");
    copy_text(settings.sun_entity_id, "");
    copy_text(settings.weather_coordinates, "");

    settings.mqtt_enabled = false;
    copy_text(settings.mqtt_host, "");
    settings.mqtt_port = 1883;
    copy_text(settings.mqtt_username, "");
    copy_text(settings.mqtt_password, "");
    copy_text(settings.mqtt_discovery_prefix, "homeassistant");
    copy_text(settings.mqtt_base_topic, "merlinccu");

    settings.weather_source = WeatherSource::HomeAssistant;
    settings.time_zone = TimeZoneSelection::EuropeLondon;
    settings.screen_saver = ScreenSaverSelection::Life;
    settings.screen_saver_timeout_minutes = 5;
    return settings;
}

/// @brief Returns a const pointer to one flash slot.
const ConfigSlot* flash_slot(uint32_t offset)
{
    // XIP flash is memory mapped on the RP2040/RP2350, so this hardware address
    // cast is intentional and cannot be expressed as an ordinary object pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<const ConfigSlot*>(XIP_BASE + offset);
}

/// @brief Returns true when a stored slot header and payload are valid.
bool validate_slot(const ConfigSlot& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kConfigVersion ||
        slot.payload_size != sizeof(RuntimeConfig))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(RuntimeConfig));
    return slot.crc32 == expected;
}

bool validate_legacy_slot_v1(const LegacyConfigSlotV1& slot)
{
    if (slot.magic != kConfigMagic || slot.version != kLegacyConfigVersion ||
        slot.payload_size != sizeof(LegacyRuntimeConfigV1))
    {
        return false;
    }

    const uint32_t expected =
        crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(LegacyRuntimeConfigV1));
    return slot.crc32 == expected;
}

RuntimeConfig migrate_legacy_settings(const LegacyRuntimeConfigV1& legacy)
{
    RuntimeConfig migrated = {};
    migrated.device_name = legacy.device_name;
    migrated.device_label = legacy.device_label;
    migrated.location = legacy.location;
    migrated.room = legacy.room;
    migrated.admin_password = legacy.admin_password;
    migrated.remote_config_enabled = legacy.remote_config_enabled;
    migrated.require_admin_password = legacy.require_admin_password;
    migrated.wifi_ssid = legacy.wifi_ssid;
    migrated.wifi_password = legacy.wifi_password;
    migrated.home_assistant_enabled = legacy.home_assistant_enabled;
    migrated.home_assistant_host = legacy.home_assistant_host;
    migrated.home_assistant_port = legacy.home_assistant_port;
    migrated.home_assistant_token = legacy.home_assistant_token;
    migrated.home_assistant_entity_id = legacy.home_assistant_entity_id;
    migrated.home_assistant_self_entity_id = legacy.home_assistant_self_entity_id;
    migrated.weather_entity_id = legacy.weather_entity_id;
    migrated.sun_entity_id = legacy.sun_entity_id;
    copy_text(migrated.weather_coordinates, "");
    migrated.mqtt_enabled = legacy.mqtt_enabled;
    migrated.mqtt_host = legacy.mqtt_host;
    migrated.mqtt_port = legacy.mqtt_port;
    migrated.mqtt_username = legacy.mqtt_username;
    migrated.mqtt_password = legacy.mqtt_password;
    migrated.mqtt_discovery_prefix = legacy.mqtt_discovery_prefix;
    migrated.mqtt_base_topic = legacy.mqtt_base_topic;
    migrated.weather_source = legacy.weather_source;
    migrated.time_zone = legacy.time_zone;
    migrated.screen_saver = legacy.screen_saver;
    migrated.screen_saver_timeout_minutes = legacy.screen_saver_timeout_minutes;
    return migrated;
}

/// @brief Returns whether HA should be auto-enabled from configured credentials.
/// @details A previous web-form truncation bug could clear only the HA enable
/// checkbox while leaving host, token, and entity fields intact in flash. This
/// guard repairs that partial-save state so integration resumes automatically.
bool should_auto_enable_home_assistant(const RuntimeConfig& settings)
{
    return !settings.home_assistant_enabled && settings.home_assistant_host[0] != '\0' &&
           settings.home_assistant_token[0] != '\0' && settings.home_assistant_entity_id[0] != '\0';
}

struct ConfigCandidate
{
    bool valid;
    uint32_t sequence;
    RuntimeConfig settings;
};

ConfigCandidate read_candidate(uint32_t offset)
{
    const ConfigSlot* slot = flash_slot(offset);
    if (validate_slot(*slot))
    {
        return {true, slot->sequence, slot->settings};
    }

    const auto* legacy_slot = reinterpret_cast<const LegacyConfigSlotV1*>(slot);
    if (validate_legacy_slot_v1(*legacy_slot))
    {
        return {true, legacy_slot->sequence, migrate_legacy_settings(legacy_slot->settings)};
    }

    return {false, 0, {}};
}

/// @brief Chooses the newest valid config slot from flash.
bool load_from_flash(RuntimeConfig* out_settings, uint32_t* out_sequence)
{
    if (out_settings == nullptr || out_sequence == nullptr)
    {
        return false;
    }

    const ConfigCandidate slot0 = read_candidate(kSlot0Offset);
    const ConfigCandidate slot1 = read_candidate(kSlot1Offset);
    std::printf("Config flash scan: slot0=%s slot1=%s\n", slot0.valid ? "valid" : "invalid",
                slot1.valid ? "valid" : "invalid");

    if (!slot0.valid && !slot1.valid)
    {
        return false;
    }

    const ConfigCandidate* chosen = nullptr;
    if (slot0.valid && slot1.valid)
    {
        chosen = (slot1.sequence > slot0.sequence) ? &slot1 : &slot0;
    }
    else
    {
        chosen = slot0.valid ? &slot0 : &slot1;
    }

    *out_settings = chosen->settings;
    *out_sequence = chosen->sequence;
    std::printf("Config loaded from flash sequence %lu\n",
                static_cast<unsigned long>(chosen->sequence));
    return true;
}

/// @brief Writes one complete slot to flash.
bool write_slot(uint32_t offset, const RuntimeConfig& settings, uint32_t sequence)
{
    alignas(4) static uint8_t flash_buffer[FLASH_SECTOR_SIZE] = {};
    std::memset(flash_buffer, 0xFF, sizeof(flash_buffer));

    ConfigSlot slot = {};
    slot.magic = kConfigMagic;
    slot.version = kConfigVersion;
    slot.sequence = sequence;
    slot.payload_size = sizeof(RuntimeConfig);
    slot.settings = settings;
    slot.crc32 = crc32(reinterpret_cast<const uint8_t*>(&slot.settings), sizeof(RuntimeConfig));
    std::memcpy(flash_buffer, &slot, sizeof(slot));

    std::printf("Config save: writing sequence %lu to flash offset 0x%08lX\n",
                static_cast<unsigned long>(sequence), static_cast<unsigned long>(offset));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, flash_buffer, sizeof(flash_buffer));
    restore_interrupts(interrupts);

    const ConfigSlot* stored = flash_slot(offset);
    const bool valid = validate_slot(*stored) && stored->sequence == sequence;
    std::printf("Config save: verify %s for sequence %lu\n", valid ? "ok" : "failed",
                static_cast<unsigned long>(sequence));
    return valid;
}

/// @brief Returns the next flash slot offset for a save operation.
uint32_t next_save_slot_offset()
{
    return (g_sequence % 2U) == 0U ? kSlot1Offset : kSlot0Offset;
}

} // namespace

void init()
{
    RuntimeConfig loaded = {};
    uint32_t sequence = 0;

    if (load_from_flash(&loaded, &sequence))
    {
        if (loaded.weather_source == WeatherSource::MetNorway)
        {
            loaded.weather_source = WeatherSource::OpenMeteo;
        }
        if (should_auto_enable_home_assistant(loaded))
        {
            loaded.home_assistant_enabled = true;
            std::printf("Config load: repaired HA enable flag from saved credentials\n");
        }
        g_settings = loaded;
        g_sequence = sequence;
        return;
    }

    std::printf("Config flash empty or invalid; writing defaults\n");
    g_settings = make_default_settings();
    g_sequence = 0;
    (void)save(g_settings);
}

const RuntimeConfig& settings()
{
    return g_settings;
}

bool save(const RuntimeConfig& settings)
{
    RuntimeConfig sanitized = settings;
    if (sanitized.device_name[0] == '\0')
    {
        copy_text(sanitized.device_name, kDefaultDeviceName);
    }
    if (sanitized.admin_password[0] == '\0')
    {
        copy_text(sanitized.admin_password, kDefaultAdminPassword);
    }
    if (sanitized.home_assistant_port == 0)
    {
        sanitized.home_assistant_port = 8123;
    }
    if (sanitized.mqtt_port == 0)
    {
        sanitized.mqtt_port = 1883;
    }
    sanitized.screen_saver_timeout_minutes =
        std::min(sanitized.screen_saver_timeout_minutes, kMaxScreenSaverTimeoutMinutes);
    if (sanitized.weather_source == WeatherSource::MetNorway)
    {
        sanitized.weather_source = WeatherSource::OpenMeteo;
    }
    if (should_auto_enable_home_assistant(sanitized))
    {
        sanitized.home_assistant_enabled = true;
    }

    const uint32_t next_sequence = g_sequence + 1U;
    if (!write_slot(next_save_slot_offset(), sanitized, next_sequence))
    {
        std::printf("Config save failed before active settings were updated\n");
        return false;
    }

    g_settings = sanitized;
    g_sequence = next_sequence;
    std::printf("Config save complete: sequence %lu device='%s' label='%s'\n",
                static_cast<unsigned long>(g_sequence), g_settings.device_name.data(),
                g_settings.device_label.data());
    return true;
}

bool reset_to_defaults()
{
    return save(make_default_settings());
}

bool admin_password_matches(const char* password)
{
    if (!g_settings.require_admin_password)
    {
        return true;
    }

    return password != nullptr && std::strcmp(password, g_settings.admin_password.data()) == 0;
}

const char* device_name()
{
    return g_settings.device_name[0] != '\0' ? g_settings.device_name.data() : kDefaultDeviceName;
}

} // namespace config_manager
