#pragma once

#include <algorithm>
#include <cstdint>

/// @brief Growing, jittered delays between reconnect attempts (#155).
/// @details Each failure doubles the wait from `initial_ms` up to `max_ms`, so
/// an outage that lasts hours costs a handful of attempts an hour rather than
/// one every few seconds. Each delay is then cut by up to a quarter at random,
/// so several CCUs that lost the same router or broker at the same moment do
/// not all come back in step. A success resets the wait to `initial_ms`.
/// Pure and header-only so the host tests can drive it with fixed random
/// values.
class RetryBackoff
{
  public:
    /// @param initial_ms Wait after the first failure.
    /// @param max_ms Longest wait; the doubling stops here.
    constexpr RetryBackoff(uint32_t initial_ms, uint32_t max_ms)
        : initial_ms_(initial_ms), max_ms_(std::max(initial_ms, max_ms)), base_ms_(initial_ms)
    {
    }

    /// @brief Records a failure and returns how long to wait before retrying.
    /// @param random_value Any 32-bit random number; it picks the jitter.
    /// @return A delay between three quarters of the current base and the base.
    uint32_t next_delay_ms(uint32_t random_value)
    {
        const uint32_t base = base_ms_;
        const uint32_t jitter_span = base / kJitterDivisor;
        const uint32_t delay = base - (random_value % (jitter_span + 1U));

        base_ms_ = (base >= max_ms_ / 2U) ? max_ms_ : base * 2U;
        if (failures_ < UINT32_MAX)
        {
            ++failures_;
        }
        return delay;
    }

    /// @brief Forgets past failures after a successful connection.
    void reset()
    {
        base_ms_ = initial_ms_;
        failures_ = 0U;
    }

    /// @brief Failures since the last reset.
    [[nodiscard]] uint32_t failures() const
    {
        return failures_;
    }

  private:
    /// Jitter takes up to 1/kJitterDivisor off each delay.
    static constexpr uint32_t kJitterDivisor = 4U;

    uint32_t initial_ms_;
    uint32_t max_ms_;
    uint32_t base_ms_;
    uint32_t failures_ = 0U;
};
