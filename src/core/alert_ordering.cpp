#include "alert_ordering.h"

#include <algorithm>

namespace alert_ordering
{

void sort_display_indices(const std::array<ActiveAlert, kActiveAlertCapacity>& alerts,
                          uint8_t alert_count,
                          std::array<uint8_t, kActiveAlertCapacity>& out_indices)
{
    const uint8_t count = std::min(alert_count, static_cast<uint8_t>(alerts.size()));
    for (uint8_t i = 0U; i < count; ++i)
    {
        out_indices[i] = i;
    }

    for (uint8_t i = 0U; i < count; ++i)
    {
        for (uint8_t j = static_cast<uint8_t>(i + 1U); j < count; ++j)
        {
            if (alerts[out_indices[j]].sequence > alerts[out_indices[i]].sequence)
            {
                const uint8_t tmp = out_indices[i];
                out_indices[i] = out_indices[j];
                out_indices[j] = tmp;
            }
        }
    }
}

AnnunciationSummary summarize(const std::array<ActiveAlert, kActiveAlertCapacity>& alerts,
                              uint8_t alert_count, uint32_t acknowledged_sequence)
{
    AnnunciationSummary summary = {AlertSeverity::None, 0U, false};
    const uint8_t count = std::min(alert_count, static_cast<uint8_t>(alerts.size()));

    for (uint8_t i = 0U; i < count; ++i)
    {
        const AlertSeverity severity = alerts[i].severity;
        if (static_cast<uint8_t>(severity) > static_cast<uint8_t>(summary.highest_severity))
        {
            summary.highest_severity = severity;
        }
        summary.newest_sequence = std::max(summary.newest_sequence, alerts[i].sequence);
    }

    summary.suppressed = count > 0U && summary.newest_sequence <= acknowledged_sequence;
    return summary;
}

} // namespace alert_ordering
