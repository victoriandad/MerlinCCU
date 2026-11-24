#include "retry_backoff.h"

#include "test_framework.h"

HOST_TEST(retry_backoff_doubles_up_to_the_cap)
{
    RetryBackoff backoff(5000U, 60000U);

    EXPECT_EQ(backoff.next_delay_ms(0U), 5000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 10000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 20000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 40000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 60000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 60000U);
    EXPECT_EQ(backoff.failures(), 6U);
}

HOST_TEST(retry_backoff_jitter_takes_at_most_a_quarter_off)
{
    RetryBackoff backoff(8000U, 8000U);

    // 8000 / 4 = 2000, so the random value picks 0..2000 to subtract.
    EXPECT_EQ(backoff.next_delay_ms(2000U), 6000U);
    EXPECT_EQ(backoff.next_delay_ms(2001U), 8000U);
    EXPECT_EQ(backoff.next_delay_ms(UINT32_MAX), 8000U - (UINT32_MAX % 2001U));
}

HOST_TEST(retry_backoff_reset_starts_again_from_the_initial_delay)
{
    RetryBackoff backoff(10000U, 300000U);
    (void)backoff.next_delay_ms(0U);
    (void)backoff.next_delay_ms(0U);

    backoff.reset();

    EXPECT_EQ(backoff.failures(), 0U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 10000U);
}

HOST_TEST(retry_backoff_cap_below_initial_uses_the_initial_delay)
{
    RetryBackoff backoff(10000U, 1000U);

    EXPECT_EQ(backoff.next_delay_ms(0U), 10000U);
    EXPECT_EQ(backoff.next_delay_ms(0U), 10000U);
}
