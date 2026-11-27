#pragma once

#include <cstddef>
#include <new>
#include <type_traits>

/// @brief One block of RAM the screen savers share for their state.
/// @details Only the running screen saver needs its state, and each one is
/// started (init) before it is drawn, so they take turns in one block sized
/// for the largest, Life's two grids, rather than each keeping its own.
namespace screensaver_arena
{

/// @brief Size of the block: Life's two 2,520-byte grids.
inline constexpr size_t kBytes = 5040U;
/// @brief Alignment the block gives its occupant.
inline constexpr size_t kAlign = 8U;

/// @brief The block itself.
void* storage();

/// @brief Starts a screen saver's state `T` in the block, value-initialised.
/// Call from its init(); the previous occupant's state is gone.
template <typename T> T& start()
{
    static_assert(sizeof(T) <= kBytes, "screen saver state must fit the shared block");
    static_assert(alignof(T) <= kAlign, "screen saver state needs more alignment");
    static_assert(std::is_trivially_destructible_v<T>, "the block never destroys its occupant");
    return *::new (storage()) T{};
}

/// @brief The state `T` the running screen saver started.
template <typename T> T& get()
{
    return *std::launder(static_cast<T*>(storage()));
}

} // namespace screensaver_arena
