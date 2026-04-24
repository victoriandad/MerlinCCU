#include "config_manager.h"

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
constexpr uint16_t kConfigVersion = 1;
constexpr uint32_t kFlashSlotCount = 2;
constexpr uint32_t kConfigStorageBytes = FLASH_SECTOR_SIZE * kFlashSlotCount;
constexpr uint32_t kConfigStorageOffset = PICO_FLASH_SIZE_BYTES - kConfigStorageBytes;
constexpr uint32_t kSlot0Offset = kConfigStorageOffset;
constexpr uint32_t kSlot1Offset = kConfigStorageOffset + FLASH_SECTOR_SIZE;
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

/// @brief Copies a C string into one fixed-size config field.
template <size_t N>
void copy_text(std::array<char, N>& dest, const char* src)
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

/// @brief Chooses the newest valid config slot from flash.
bool load_from_flash(RuntimeConfig* out_settings, uint32_t* out_sequence)
{
    if (out_settings == nullptr || out_sequence == nullptr)
    {
        return false;
    }

    const ConfigSlot* slot0 = flash_slot(kSlot0Offset);
    const ConfigSlot* slot1 = flash_slot(kSlot1Offset);
    const bool slot0_valid = validate_slot(*slot0);
    const bool slot1_valid = validate_slot(*slot1);
    std::printf("Config flash scan: slot0=%s slot1=%s\n", slot0_valid ? "valid" : "invalid",
                slot1_valid ? "valid" : "invalid");

    if (!slot0_valid && !slot1_valid)
    {
        return false;
    }

    const ConfigSlot* chosen = nullptr;
    if (slot0_valid && slot1_valid)
    {
        chosen = (slot1->sequence > slot0->sequence) ? slot1 : slot0;
    }
    else
    {
        chosen = slot0_valid ? slot0 : slot1;
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
    if (sanitized.screen_saver_timeout_minutes > 120)
    {
        sanitized.screen_saver_timeout_minutes = 120;
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
