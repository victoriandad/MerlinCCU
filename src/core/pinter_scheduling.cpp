#include "pinter_scheduling.h"

namespace pinter_scheduling
{

uint8_t brew_dock_count(const std::array<PinterStatus, kPinterCount>& pinters)
{
    uint8_t count = 0U;
    for (const PinterStatus& pinter : pinters)
    {
        if (pinter.state == PinterState::Brewing)
        {
            ++count;
        }
    }
    return count;
}

bool uses_fridge(PinterState state)
{
    return state == PinterState::Conditioning || state == PinterState::Ready;
}

uint8_t fridge_count(const std::array<PinterStatus, kPinterCount>& pinters)
{
    uint8_t count = 0U;
    for (const PinterStatus& pinter : pinters)
    {
        if (uses_fridge(pinter.state))
        {
            ++count;
        }
    }
    return count;
}

bool has_pending_cold_crash(const PinterStatus& pinter)
{
    return pinter.state == PinterState::Brewing && pinter.planned_cold_crash_days > 0U &&
           !pinter.cold_crash_used;
}

bool can_start(uint8_t selected_brew_count, uint8_t dock_count)
{
    return selected_brew_count > 0U && dock_count < kPinterBrewDockCapacity;
}

bool can_enter_fridge(const PinterStatus& pinter, uint8_t fridge_count)
{
    if (pinter.state == PinterState::Brewing && has_pending_cold_crash(pinter))
    {
        return true;
    }
    if (pinter.state != PinterState::Brewing && pinter.state != PinterState::ColdCrash)
    {
        return false;
    }

    return fridge_count < kPinterFridgeCapacity;
}

bool primary_action_enabled(const PinterStatus& pinter, uint8_t selected_brew_count,
                            uint8_t dock_count, uint8_t fridge_count)
{
    switch (pinter.state)
    {
    case PinterState::Idle:
        return can_start(selected_brew_count, dock_count);
    case PinterState::Brewing:
        if (has_pending_cold_crash(pinter))
        {
            return true;
        }
        return can_enter_fridge(pinter, fridge_count);
    case PinterState::ColdCrash:
        return can_enter_fridge(pinter, fridge_count);
    case PinterState::Conditioning:
    case PinterState::Ready:
    case PinterState::Consumed:
        return true;
    }

    return false;
}

SummaryCounts summarize(const std::array<PinterStatus, kPinterCount>& pinters,
                        uint8_t selected_brew_count)
{
    SummaryCounts counts = {selected_brew_count, 0U, 0U, 0U};

    for (const PinterStatus& pinter : pinters)
    {
        switch (pinter.state)
        {
        case PinterState::Brewing:
        case PinterState::ColdCrash:
            ++counts.brewing;
            break;
        case PinterState::Conditioning:
            ++counts.conditioning;
            break;
        case PinterState::Ready:
            ++counts.ready;
            break;
        case PinterState::Idle:
        case PinterState::Consumed:
            break;
        }
    }

    return counts;
}

bool advance_non_idle(PinterStatus& pinter, uint32_t today_epoch_day, uint8_t fridge_count)
{
    switch (pinter.state)
    {
    case PinterState::Idle:
        return false;
    case PinterState::Brewing:
        if (has_pending_cold_crash(pinter))
        {
            pinter.state = PinterState::ColdCrash;
            pinter.cold_crash_start_day = today_epoch_day;
            pinter.cold_crash_used = true;
            return true;
        }
        if (!can_enter_fridge(pinter, fridge_count))
        {
            return false;
        }
        pinter.state = PinterState::Conditioning;
        pinter.conditioning_start_day = today_epoch_day;
        return true;
    case PinterState::ColdCrash:
        if (!can_enter_fridge(pinter, fridge_count))
        {
            return false;
        }
        pinter.state = PinterState::Conditioning;
        pinter.conditioning_start_day = today_epoch_day;
        return true;
    case PinterState::Conditioning:
        pinter.state = PinterState::Ready;
        pinter.ready_day = today_epoch_day;
        return true;
    case PinterState::Ready:
        pinter.state = PinterState::Consumed;
        return true;
    case PinterState::Consumed:
        pinter.state = PinterState::Idle;
        pinter.brew_start_day = 0U;
        pinter.cold_crash_start_day = 0U;
        pinter.conditioning_start_day = 0U;
        pinter.ready_day = 0U;
        pinter.planned_brewing_days = 0U;
        pinter.planned_cold_crash_days = 0U;
        pinter.planned_conditioning_days = 0U;
        pinter.cold_crash_used = false;
        return true;
    }

    return false;
}

bool reset(PinterStatus& pinter, uint8_t default_brew_index)
{
    if (pinter.state == PinterState::Idle && !pinter.cold_crash_used)
    {
        return false;
    }

    pinter.state = PinterState::Idle;
    pinter.brew_index = default_brew_index;
    pinter.brew_start_day = 0U;
    pinter.cold_crash_start_day = 0U;
    pinter.conditioning_start_day = 0U;
    pinter.ready_day = 0U;
    pinter.planned_brewing_days = 0U;
    pinter.planned_cold_crash_days = 0U;
    pinter.planned_conditioning_days = 0U;
    pinter.cold_crash_used = false;
    return true;
}

} // namespace pinter_scheduling
