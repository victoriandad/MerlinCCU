#include "alert_ordering.h"

#include "test_framework.h"

namespace
{

/// @brief Builds an ActiveAlert with only the fields alert_ordering cares about set.
ActiveAlert make_alert(AlertSeverity severity, uint32_t sequence)
{
    ActiveAlert alert = {};
    alert.severity = severity;
    alert.sequence = sequence;
    return alert;
}

} // namespace

HOST_TEST(sort_display_indices_orders_newest_first)
{
    std::array<ActiveAlert, kActiveAlertCapacity> alerts = {};
    alerts[0] = make_alert(AlertSeverity::Message, 10U);
    alerts[1] = make_alert(AlertSeverity::Warning, 30U);
    alerts[2] = make_alert(AlertSeverity::Alert, 20U);

    std::array<uint8_t, kActiveAlertCapacity> indices = {};
    alert_ordering::sort_display_indices(alerts, 3U, indices);

    // Sequence 30 (index 1) is newest, then 20 (index 2), then 10 (index 0).
    EXPECT_EQ(indices[0], 1U);
    EXPECT_EQ(indices[1], 2U);
    EXPECT_EQ(indices[2], 0U);
}

HOST_TEST(sort_display_indices_ignores_entries_past_alert_count)
{
    std::array<ActiveAlert, kActiveAlertCapacity> alerts = {};
    alerts[0] = make_alert(AlertSeverity::Message, 5U);
    // A stale slot beyond alert_count with a huge sequence must not leak into
    // the sorted output -- it belongs to an alert that has already been
    // cleared and should be treated as absent.
    alerts[1] = make_alert(AlertSeverity::Alert, 999U);

    std::array<uint8_t, kActiveAlertCapacity> indices = {};
    alert_ordering::sort_display_indices(alerts, 1U, indices);

    EXPECT_EQ(indices[0], 0U);
}

HOST_TEST(summarize_reports_none_and_unsuppressed_when_no_alerts_active)
{
    std::array<ActiveAlert, kActiveAlertCapacity> alerts = {};
    const alert_ordering::AnnunciationSummary summary =
        alert_ordering::summarize(alerts, 0U, 0U);

    EXPECT_TRUE(summary.highest_severity == AlertSeverity::None);
    EXPECT_EQ(summary.newest_sequence, 0U);
    EXPECT_FALSE(summary.suppressed);
}

HOST_TEST(summarize_picks_highest_severity_regardless_of_recency)
{
    std::array<ActiveAlert, kActiveAlertCapacity> alerts = {};
    // The most severe alert arrived first; a milder one arrived later. The
    // lamp should still reflect the worst active condition, not just the
    // newest one.
    alerts[0] = make_alert(AlertSeverity::Alert, 1U);
    alerts[1] = make_alert(AlertSeverity::Message, 2U);

    const alert_ordering::AnnunciationSummary summary =
        alert_ordering::summarize(alerts, 2U, 0U);

    EXPECT_TRUE(summary.highest_severity == AlertSeverity::Alert);
    EXPECT_EQ(summary.newest_sequence, 2U);
}

HOST_TEST(summarize_suppresses_only_when_no_alert_is_newer_than_acknowledged)
{
    std::array<ActiveAlert, kActiveAlertCapacity> alerts = {};
    alerts[0] = make_alert(AlertSeverity::Warning, 5U);
    alerts[1] = make_alert(AlertSeverity::Alert, 8U);

    const alert_ordering::AnnunciationSummary acknowledged_everything =
        alert_ordering::summarize(alerts, 2U, 8U);
    EXPECT_TRUE(acknowledged_everything.suppressed);

    // A newer alert (sequence 9) than the last acknowledged one (8) must
    // re-annunciate even though older alerts are still active and were
    // already seen.
    alerts[1].sequence = 9U;
    const alert_ordering::AnnunciationSummary new_alert_arrived =
        alert_ordering::summarize(alerts, 2U, 8U);
    EXPECT_FALSE(new_alert_arrived.suppressed);
}
