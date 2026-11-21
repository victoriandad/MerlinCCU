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
using config_persistence::LegacyConfigSlotV3;
using config_persistence::LegacyConfigSlotV4;
using config_persistence::LegacyConfigSlotV5;

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

/// @brief Validates the slot at `offset`, in any stored layout version.
/// @details When `out_settings` is not null, a valid slot is also decoded
/// (and migrated if it is a legacy layout) straight into it. Validating
/// without decoding lets both slots be compared with no RuntimeConfig copy
/// on the core-0 stack (#164).
ConfigCandidate read_candidate(uint32_t offset, RuntimeConfig* out_settings)
{
    const ConfigSlot* slot = flash_slot(offset);
    if (config_persistence::validate_slot(*slot))
    {
        if (out_settings != nullptr)
        {
            *out_settings = slot->settings;
        }
        return {true, slot->sequence};
    }

    const auto* legacy_slot_v5 = reinterpret_cast<const LegacyConfigSlotV5*>(slot);
    if (config_persistence::validate_legacy_slot_v5(*legacy_slot_v5))
    {
        if (out_settings != nullptr)
        {
            *out_settings = config_persistence::migrate_legacy_settings_v5(legacy_slot_v5->settings);
        }
        return {true, legacy_slot_v5->sequence};
    }

    const auto* legacy_slot_v4 = reinterpret_cast<const LegacyConfigSlotV4*>(slot);
    if (config_persistence::validate_legacy_slot_v4(*legacy_slot_v4))
    {
        if (out_settings != nullptr)
        {
            *out_settings = config_persistence::migrate_legacy_settings_v4(legacy_slot_v4->settings);
        }
        return {true, legacy_slot_v4->sequence};
    }

    const auto* legacy_slot_v3 = reinterpret_cast<const LegacyConfigSlotV3*>(slot);
    if (config_persistence::validate_legacy_slot_v3(*legacy_slot_v3))
    {
        if (out_settings != nullptr)
        {
            *out_settings = config_persistence::migrate_legacy_settings_v3(legacy_slot_v3->settings);
        }
        return {true, legacy_slot_v3->sequence};
    }

    const auto* legacy_slot_v2 = reinterpret_cast<const LegacyConfigSlotV2*>(slot);
    if (config_persistence::validate_legacy_slot_v2(*legacy_slot_v2))
    {
        if (out_settings != nullptr)
        {
            *out_settings = config_persistence::migrate_legacy_settings_v2(legacy_slot_v2->settings);
        }
        return {true, legacy_slot_v2->sequence};
    }

    const auto* legacy_slot = reinterpret_cast<const LegacyConfigSlotV1*>(slot);
    if (config_persistence::validate_legacy_slot_v1(*legacy_slot))
    {
        if (out_settings != nullptr)
        {
            *out_settings = config_persistence::migrate_legacy_settings(legacy_slot->settings);
        }
        return {true, legacy_slot->sequence};
    }

    return {false, 0U};
}

/// @brief Loads the newest valid config slot from flash into g_settings.
bool load_from_flash()
{
    const ConfigCandidate slot0 = read_candidate(kSlot0Offset, nullptr);
    const ConfigCandidate slot1 = read_candidate(kSlot1Offset, nullptr);
    std::printf("Config flash scan: slot0=%s slot1=%s\n", slot0.valid ? "valid" : "invalid",
                slot1.valid ? "valid" : "invalid");

    const ConfigCandidate* chosen = config_persistence::choose_newest_candidate(slot0, slot1);
    if (chosen == nullptr)
    {
        return false;
    }

    (void)read_candidate(chosen == &slot0 ? kSlot0Offset : kSlot1Offset, &g_settings);
    g_sequence = chosen->sequence;
    std::printf("Config loaded from flash sequence %lu\n",
                static_cast<unsigned long>(chosen->sequence));
    return true;
}

/// @brief Writes one complete slot to flash.
bool write_slot(uint32_t offset, const RuntimeConfig& settings, uint32_t sequence)
{
    // The slot is built in the static sector buffer rather than as a stack
    // local: a ConfigSlot is about 1.5 KB (#164).
    union FlashSector
    {
        ConfigSlot slot;
        uint8_t bytes[FLASH_SECTOR_SIZE];
    };
    alignas(4) static FlashSector sector = {};
    std::memset(sector.bytes, 0xFF, sizeof(sector.bytes));

    ConfigSlot& slot = sector.slot;
    slot.magic = config_persistence::kConfigMagic;
    slot.version = config_persistence::kConfigVersion;
    slot.reserved = 0U;
    slot.sequence = sequence;
    slot.payload_size = sizeof(RuntimeConfig);
    slot.settings = settings;
    slot.crc32 = config_persistence::crc32(reinterpret_cast<const uint8_t*>(&slot.settings),
                                           sizeof(RuntimeConfig));

    std::printf("Config save: writing sequence %lu to flash offset 0x%08lX\n",
                static_cast<unsigned long>(sequence), static_cast<unsigned long>(offset));

    const uint32_t interrupts = save_and_disable_interrupts();
    flash_range_erase(offset, FLASH_SECTOR_SIZE);
    flash_range_program(offset, sector.bytes, sizeof(sector.bytes));
    restore_interrupts(interrupts);

    const ConfigSlot* stored = flash_slot(offset);
    const bool valid = config_persistence::validate_slot(*stored) && stored->sequence == sequence;
    std::printf("Config save: verify %s for sequence %lu\n", valid ? "ok" : "failed",
                static_cast<unsigned long>(sequence));
    return valid;
}

/// @brief Replaces the settings with factory defaults and writes them to flash.
/// @details Kept out of line so its RuntimeConfig temporary and the one in
/// read_candidate() are never on the stack together (#164). Safe to write
/// synchronously: init() runs before wifi_manager::init(), so there is no
/// network stack yet for a flash write to disrupt.
[[gnu::noinline]] void install_defaults()
{
    std::printf("Config flash empty or invalid; writing defaults\n");
    g_settings = config_persistence::make_default_settings();
    g_sequence = 0;
    (void)save(g_settings);
    (void)flush_pending_save();
}

/// @brief Returns the next flash slot offset for a save operation.
uint32_t next_save_slot_offset()
{
    return (g_sequence % 2U) == 0U ? kSlot1Offset : kSlot0Offset;
}

} // namespace

void init()
{
    if (load_from_flash())
    {
        if (g_settings.weather_source == WeatherSource::MetNorway)
        {
            g_settings.weather_source = WeatherSource::OpenMeteo;
        }
        if (config_persistence::should_auto_enable_home_assistant(g_settings))
        {
            g_settings.home_assistant_enabled = true;
            std::printf("Config load: repaired HA enable flag from saved credentials\n");
        }
        return;
    }

    install_defaults();
}

const RuntimeConfig& settings()
{
    return g_settings;
}

bool save(const RuntimeConfig& settings)
{
    // Sanitised in place: sanitize_settings() would add a ~1.5 KB copy to
    // the core-0 stack (#164).
    if (&settings != &g_settings)
    {
        g_settings = settings;
    }
    config_persistence::sanitize_settings_in_place(g_settings);
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

const char* device_name()
{
    return g_settings.device_name[0] != '\0' ? g_settings.device_name.data()
                                             : config_persistence::kDefaultDeviceName;
}

} // namespace config_manager
