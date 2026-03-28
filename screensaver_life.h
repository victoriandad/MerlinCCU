#pragma once

#include <cstdint>

struct LifeFrameStats {
    int64_t sim_us;
    int64_t draw_us;
    int64_t present_us;
};

namespace screensaver_life {

void init();
void step_and_render(uint8_t* fb, LifeFrameStats& stats);

}  // namespace screensaver_life
