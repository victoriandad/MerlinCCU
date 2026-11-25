#include "reset_reason.h"

namespace reset_reason
{

ResetReason classify(const Signals& signals)
{
    // A fault and an update both restart through the watchdog, so they are
    // checked before it; the POWMAN flags are checked last because a
    // watchdog reset of the cores does not clear them. A USB load leaves no
    // update record, only the boot type, but its ROM reboot can still carry
    // the old firmware's watchdog mark (#204).
    if (signals.cpu_fault)
    {
        return signals.stack_overflow ? ResetReason::StackOverflow : ResetReason::CpuFault;
    }
    if (signals.rolled_back)
    {
        return ResetReason::Rollback;
    }
    if (signals.after_update || signals.flash_update_boot)
    {
        return ResetReason::Update;
    }
    if (signals.watchdog_timeout)
    {
        return ResetReason::Watchdog;
    }
    if (signals.watchdog_reset)
    {
        return ResetReason::Restart;
    }
    if (signals.brownout)
    {
        return ResetReason::Brownout;
    }
    if (signals.run_pin)
    {
        return ResetReason::ResetPin;
    }
    if (signals.power_on)
    {
        return ResetReason::PowerOn;
    }
    return ResetReason::Unknown;
}

const char* text(ResetReason reason)
{
    switch (reason)
    {
    case ResetReason::PowerOn:
        return "Power on";
    case ResetReason::ResetPin:
        return "Reset pin";
    case ResetReason::Brownout:
        return "Brown-out";
    case ResetReason::Restart:
        return "Restart";
    case ResetReason::Update:
        return "Update";
    case ResetReason::Rollback:
        return "Rollback";
    case ResetReason::Watchdog:
        return "Watchdog";
    case ResetReason::CpuFault:
        return "CPU fault";
    case ResetReason::StackOverflow:
        return "Stack overflow";
    case ResetReason::Unknown:
        break;
    }
    return "-";
}

const char* slug(ResetReason reason)
{
    switch (reason)
    {
    case ResetReason::PowerOn:
        return "power_on";
    case ResetReason::ResetPin:
        return "reset_pin";
    case ResetReason::Brownout:
        return "brownout";
    case ResetReason::Restart:
        return "restart";
    case ResetReason::Update:
        return "update";
    case ResetReason::Rollback:
        return "rollback";
    case ResetReason::Watchdog:
        return "watchdog";
    case ResetReason::CpuFault:
        return "cpu_fault";
    case ResetReason::StackOverflow:
        return "stack_overflow";
    case ResetReason::Unknown:
        break;
    }
    return "unknown";
}

const char* slug_options_json()
{
    return "[\"unknown\",\"power_on\",\"reset_pin\",\"brownout\",\"restart\",\"update\","
           "\"rollback\",\"watchdog\",\"cpu_fault\",\"stack_overflow\"]";
}

bool unexpected(ResetReason reason)
{
    switch (reason)
    {
    case ResetReason::Brownout:
    case ResetReason::Watchdog:
    case ResetReason::CpuFault:
    case ResetReason::StackOverflow:
        return true;
    case ResetReason::Unknown:
    case ResetReason::PowerOn:
    case ResetReason::ResetPin:
    case ResetReason::Restart:
    case ResetReason::Update:
    case ResetReason::Rollback:
        return false;
    }
    return false;
}

} // namespace reset_reason
