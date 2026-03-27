#pragma once

#include "pico/stdlib.h"

/// @brief Describes one panel mounting and timing setup.
/// @details The firmware works with two coordinate systems:
/// - UI space: portrait-oriented coordinates used by drawing code.
/// - Native panel space: the electrical scan order expected by the display.
struct PanelConfig {
    int fb_width;
    int fb_height;
    int ui_width;
    int ui_height;
    int native_row_offset;
    int h_pre_blank;
    int h_post_blank;
    int v_sync_pulse;
    int v_pre_blank;
    int v_post_blank;
    float clkdiv;
};

/// @brief GPIO2..GPIO5 = VID,VCLK,HS,VS.
inline constexpr uint PIN_BASE = 2;

/// @brief Panel timing and geometry for the current EL320 portrait mounting.
inline constexpr PanelConfig PANEL = {
    .fb_width = 320,
    .fb_height = 256,
    .ui_width = 252,
    .ui_height = 320,
    .native_row_offset = -4,
    .h_pre_blank = 32,
    .h_post_blank = 32,
    .v_sync_pulse = 2,
    .v_pre_blank = 2,
    .v_post_blank = 2,
    .clkdiv = 2.51f,
};

inline constexpr int FB_WIDTH  = PANEL.fb_width;
inline constexpr int FB_HEIGHT = PANEL.fb_height;
inline constexpr int UI_WIDTH  = PANEL.ui_width;
inline constexpr int UI_HEIGHT = PANEL.ui_height;
inline constexpr int UI_STRIDE = (UI_WIDTH + 7) / 8;
inline constexpr int UI_FB_SIZE = UI_STRIDE * UI_HEIGHT;

inline constexpr int PIXELS_PER_LINE = PANEL.h_pre_blank + FB_WIDTH + PANEL.h_post_blank;
inline constexpr int STEPS_PER_LINE  = PIXELS_PER_LINE * 2;
inline constexpr int STEPS_PER_WORD  = 8;
inline constexpr int WORDS_PER_LINE  = STEPS_PER_LINE / STEPS_PER_WORD;
inline constexpr int TOTAL_LINES     = PANEL.v_sync_pulse + PANEL.v_pre_blank + FB_HEIGHT + PANEL.v_post_blank;
inline constexpr int RASTER_WORDS    = WORDS_PER_LINE * TOTAL_LINES;

static_assert((STEPS_PER_LINE % STEPS_PER_WORD) == 0, "Line packing must be exact");
