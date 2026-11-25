#pragma once

#include <cstdint>

/// @brief Why the unit last started, as shown on Status > Overview and sent
/// to Home Assistant (#155).
enum class ResetReason : uint8_t
{
    /// Not read yet, or no recognised cause.
    Unknown = 0,
    /// Power applied.
    PowerOn,
    /// The RUN pin was pulled low: a reset button or a debugger.
    ResetPin,
    /// The supply dipped below the brown-out threshold.
    Brownout,
    /// The firmware restarted itself on purpose, e.g. to apply settings.
    Restart,
    /// First start of newly loaded firmware, over Wi-Fi or USB.
    Update,
    /// An uploaded image failed its trial and the previous one was restored.
    Rollback,
    /// The main loop stopped feeding the watchdog: a hang.
    Watchdog,
    /// A CPU fault other than a stack overflow.
    CpuFault,
    /// The core-0 stack limit fault.
    StackOverflow,
};

namespace reset_reason
{

/// @brief Hardware and boot-record facts read once at start-up.
/// @details Gathered by the firmware so the classification below stays pure
/// and host-tested. More than one can be set: a fault restarts through the
/// watchdog, and the power-on flag can outlive later watchdog resets, so
/// classify() checks them in order of how specific they are.
struct Signals
{
    /// stack_guard recorded a fault before the last restart.
    bool cpu_fault;
    /// That fault was the stack limit.
    bool stack_overflow;
    /// ota_manager saw an update record for the other slot.
    bool rolled_back;
    /// ota_manager saw an update record for this slot.
    bool after_update;
    /// The boot ROM started this image as a flash update, as after a USB
    /// (picotool) load (#204).
    bool flash_update_boot;
    /// The armed watchdog's timer ran out: watchdog_enable() had marked
    /// scratch 4 and the reset reason is the timer, not a forced trigger.
    bool watchdog_timeout;
    /// Any watchdog reset, including watchdog_reboot() (watchdog_caused_reboot()).
    bool watchdog_reset;
    /// POWMAN chip-reset flags.
    bool brownout;
    bool run_pin;
    bool power_on;
};

/// @brief Picks the most specific reason from the start-up signals.
ResetReason classify(const Signals& signals);

/// @brief Mixed-case text for the Status page, e.g. "Watchdog".
const char* text(ResetReason reason);

/// @brief Lower-case slug for the HA sensor, e.g. "watchdog".
const char* slug(ResetReason reason);

/// @brief JSON list of every slug, for the HA enum sensor's options.
const char* slug_options_json();

/// @brief True for a restart nobody asked for: a hang, a fault or a brown-out.
bool unexpected(ResetReason reason);

} // namespace reset_reason
