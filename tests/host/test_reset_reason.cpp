#include "reset_reason.h"

#include <cstring>

#include "test_framework.h"

using reset_reason::Signals;

HOST_TEST(reset_reason_fault_wins_over_the_watchdog_reset_it_caused)
{
    Signals signals = {};
    signals.cpu_fault = true;
    signals.watchdog_reset = true;
    signals.power_on = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::CpuFault);

    signals.stack_overflow = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::StackOverflow);
}

HOST_TEST(reset_reason_update_and_rollback_win_over_the_watchdog)
{
    Signals signals = {};
    signals.watchdog_timeout = true;
    signals.watchdog_reset = true;
    signals.after_update = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Update);

    signals.rolled_back = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Rollback);
}

HOST_TEST(reset_reason_watchdog_timeout_is_a_hang_and_a_plain_reboot_a_restart)
{
    Signals signals = {};
    signals.power_on = true;
    signals.watchdog_reset = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Restart);

    signals.watchdog_timeout = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Watchdog);
}

HOST_TEST(reset_reason_powman_flags_in_order)
{
    Signals signals = {};
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Unknown);
    signals.power_on = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::PowerOn);
    signals.run_pin = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::ResetPin);
    signals.brownout = true;
    EXPECT_TRUE(reset_reason::classify(signals) == ResetReason::Brownout);
}

HOST_TEST(reset_reason_text_slug_and_unexpected)
{
    EXPECT_EQ(std::strcmp(reset_reason::text(ResetReason::Watchdog), "Watchdog"), 0);
    EXPECT_EQ(std::strcmp(reset_reason::text(ResetReason::Unknown), "-"), 0);
    EXPECT_EQ(std::strcmp(reset_reason::slug(ResetReason::StackOverflow), "stack_overflow"), 0);
    EXPECT_TRUE(std::strstr(reset_reason::slug_options_json(), "\"cpu_fault\"") != nullptr);

    EXPECT_TRUE(reset_reason::unexpected(ResetReason::Watchdog));
    EXPECT_TRUE(reset_reason::unexpected(ResetReason::Brownout));
    EXPECT_FALSE(reset_reason::unexpected(ResetReason::Update));
    EXPECT_FALSE(reset_reason::unexpected(ResetReason::PowerOn));
}
