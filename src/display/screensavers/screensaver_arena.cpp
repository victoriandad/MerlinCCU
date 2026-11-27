#include "screensaver_arena.h"

#include <cstdint>

#include "psram.h"

namespace screensaver_arena
{

namespace
{

// In PSRAM on the Pimoroni, like Life's grids before it.
alignas(kAlign) MERLIN_PSRAM_BSS uint8_t g_block[kBytes];

} // namespace

void* storage()
{
    return g_block;
}

} // namespace screensaver_arena
