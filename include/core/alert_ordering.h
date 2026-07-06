#pragma once

#include <array>
#include <cstdint>

#include "console_model.h"

/// @brief Pure alert list/annunciation rules shared between the firmware and host tests.
/// @details Deliberately kept free of ConsoleState's other fields and any hardware
/// access so ordering and acknowledgement rules can be exercised without Pico
/// hardware (see issue #14).
namespace alert_ordering
{

/// @brief Sorts the first `alert_count` active alerts newest-to-oldest by sequence.
/// @details Ties keep their original relative order. Only the first `alert_count`
/// entries of `out_indices` are written; any trailing entries are left untouched.
void sort_display_indices(const std::array<ActiveAlert, kActiveAlertCapacity>& alerts,
                          uint8_t alert_count,
                          std::array<uint8_t, kActiveAlertCapacity>& out_indices);

/// @brief Highest severity and newest arrival among the active alerts, plus whether
/// annunciation should stay suppressed given the last-acknowledged sequence.
struct AnnunciationSummary
{
    AlertSeverity highest_severity;
    uint32_t newest_sequence;
    bool suppressed;
};

/// @brief Summarises annunciation state for the alert/test lamp update.
/// @details `suppressed` mirrors the CCU's ack rule: once the operator has opened
/// the alert list, the lamp stays quiet until an alert newer than
/// `acknowledged_sequence` arrives, even while older alerts remain active.
AnnunciationSummary summarize(const std::array<ActiveAlert, kActiveAlertCapacity>& alerts,
                              uint8_t alert_count, uint32_t acknowledged_sequence);

} // namespace alert_ordering
