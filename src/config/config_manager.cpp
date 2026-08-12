#include "config_manager.h"

#include <cstdio>
#include <cstring>

#include "config_persistence.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/stdlib.h"

namespace config_manager
{

namespace
{

using config_persistence::ConfigCandidate;
using config_persistence::ConfigSlot;
using config_persistence::LegacyConfigSlotV1;
using config_persistence::LegacyConfigSlotV2;

constexpr uint32_t kFlashSlotCount = 2;
constexpr uint32_t kConfigStorageBytes = FLASH_SECTOR_SIZE * kFlashSlotCount;
constexpr uint32_t kConfigStorageOffset = PICO_FLASH_SIZE_BYTES - kConfigStorageBytes;
constexpr uint32_t kSlot0Offset = kConfigStorageOffset;
constexpr uint32_t kSlot1Offset = kConfigStorageOffset + FLASH_SECTOR_SIZE;

static_assert(sizeof(ConfigSlot) <= FLASH_SECTOR_SIZE, "Config slot must fit in one flash sector");
static_assert(kConfigStorageBytes == config_manager::kReservedFlashBytes,
              "config_manager::kReservedFlashBytes must track this store's real reservation");

RuntimeConfig g_settings = {};
uint32_t g_sequence = 0;
bool g_save_pending = false;

/// @brief Returns a const pointer to one flash slot.
const ConfigSlot* flash_slot(uint32_t offset)
{
    // XIP flash is memory mapped on the RP2040/RP2350, so this hardware address
    // cast is intentional and cannot be expressed as an ordinary object pointer.
    // NOLINTNEXTLINE(performance-no-int-to-ptr)
    return reinterpret_cast<const ConfigSlot*>(XIP_BASE + offset);
}

ConfigCandidate read_candidate(uint32_t offset)
{
    const ConfigSlot* slot = flash_slot(offset);
    if (config_persistence::validate_slot(*slot))
    {
        return {true, slot->sequence, slot->settings};
    }

    const auto* legacy_slot_v2 = reinterpret_cast<const LegacyConfigSlotV2*>(slot);
    if (config_persistence::validate_legacy_slot_v2(*legacy_slot_v2))
    {
        return {true, legacy_slot_v2->sequence,
                config_persistence::migrate_legacy_settings_v2(legacy_slot_v2->settings)};
    }

    const auto* legacy_slot = reinterpret_cast<const LegacyConfigSlotV1*>(slot);
    if (config_persistence::validate_legacy_slot_v1(*legacy_slot))
    {
        return {true, legacy_slot->sequence,
                config_persistence::migrate_legacy_settings(legacy_slot->settings)};
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

    const ConfigCandidate* chosen = config_persistence::choose_newest_candidate(slot0, slot1);
    if (chosen == nullptr)
    {
        return false;
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
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kConfigVersion;
    slot.sequence = sequence;
    slot.payload_size = sizeof(RuntimeConfig);
    slot.settings = settings;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                           sizeof(RuntimeConfig));
    std::memcpy(flash_buffer, &slot, sizeof(slot));

    std::printf("Config save: writing sequence %lu to flash offset 0x%08lX\n",
                static_cast<unsigned long>(sequence), static_cast<unsigned long>(offset));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, flash_buffer, sizeof(flash_buffer));
    restore_interrupts(interrupts);

    const ConfigSlot* stored = flash_slot(offset);
    const bool valid = config_persistence::validate_slot(*stored) && stored->sequence == sequence;
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
        if (config_persistence::should_auto_enable_home_assistant(loaded))
        {
            loaded.home_assistant_enabled = true;
            std::printf("Config load: repaired HA enable flag from saved credentials\n");
        }
        g_settings = loaded;
        g_sequence = sequence;
        return;
    }

    std::printf("Config flash empty or invalid; writing defaults\n");
    g_settings = config_persistence::make_default_settings();
    g_sequence = 0;
    // Safe to write synchronously here: this runs before wifi_manager::init(),
    // so there is no network stack yet for a flash write to disrupt.
    (void)save(g_settings);
    (void)flush_pending_save();
}

const RuntimeConfig& settings()
{
    return g_settings;
}

bool save(const RuntimeConfig& settings)
{
    g_settings = config_persistence::sanitize_settings(settings);
    g_save_pending = true;
    return true;
}

bool flush_pending_save()
{
    if (!g_save_pending)
    {
        return false;
    }
    g_save_pending = false;

    const uint32_t next_sequence = g_sequence + 1U;
    if (!write_slot(next_save_slot_offset(), g_settings, next_sequence))
    {
        std::printf("Config save failed\n");
        return false;
    }

    g_sequence = next_sequence;
    std::printf("Config save complete: sequence %lu device='%s' label='%s'\n",
                static_cast<unsigned long>(g_sequence), g_settings.device_name.data(),
                g_settings.device_label.data());
    return true;
}

bool reset_to_defaults()
{
    return save(config_persistence::make_default_settings());
}

const char* device_name()
{
    return g_settings.device_name[0] != '\0' ? g_settings.device_name.data()
                                             : config_persistence::kDefaultDeviceName;
}

} // namespace config_manager
