#pragma once

#include "pico/stdlib.h"

/// @brief Describes one panel mounting and timing setup.
/// @details The firmware works with two coordinate systems:
/// - UI space: portrait-oriented coordinates used by drawing code.
/// - Native panel space: the electrical scan order expected by the display.
struct PanelConfig
{
    /// @brief Native panel pixel width in electrical scan order.
    int fb_width;
    /// @brief Native panel pixel height in electrical scan order.
    int fb_height;
    /// @brief Logical portrait UI width used by drawing helpers.
    int ui_width;
    /// @brief Logical portrait UI height used by drawing helpers.
    int ui_height;
    /// @brief Row offset applied when mapping UI rows to panel rows.
    int native_row_offset;
    /// @brief Blank pixels emitted before active video on each line.
    int h_pre_blank;
    /// @brief Blank pixels emitted after active video on each line.
    int h_post_blank;
    /// @brief Number of lines with vertical sync asserted.
    int v_sync_pulse;
    /// @brief Blank lines before active video.
    int v_pre_blank;
    /// @brief Blank lines after active video.
    int v_post_blank;
    /// @brief PIO state machine clock divider for stable panel timing.
    float clkdiv;
};

/// @brief GPIO2..GPIO5 = VID,VCLK,HS,VS.
inline constexpr uint kPinBase = 2;

/// @brief Panel timing and geometry for the current EL320 portrait mounting.
/// @details These values are the empirically stable timing neighborhood used
/// during bring-up. Divider sweeping is done around this region.
inline constexpr PanelConfig kPanel = {
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
    .clkdiv = 2.51F,
};

/// @brief Frequently used geometry aliases derived from `kPanel`.
inline constexpr int kFbWidth = kPanel.fb_width;
inline constexpr int kFbHeight = kPanel.fb_height;
inline constexpr int kUiWidth = kPanel.ui_width;
inline constexpr int kUiHeight = kPanel.ui_height;
inline constexpr int kUiStride = (kUiWidth + 7) / 8;
inline constexpr int kUiFbSize = kUiStride * kUiHeight;

/// @brief Packed raster sizing derived from the current panel timing model.
inline constexpr int kPixelsPerLine = kPanel.h_pre_blank + kFbWidth + kPanel.h_post_blank;
inline constexpr int kStepsPerLine = kPixelsPerLine * 2;
inline constexpr int kStepsPerWord = 8;
inline constexpr int kWordsPerLine = kStepsPerLine / kStepsPerWord;
inline constexpr int kTotalLines =
    kPanel.v_sync_pulse + kPanel.v_pre_blank + kFbHeight + kPanel.v_post_blank;
inline constexpr int kRasterWords = kWordsPerLine * kTotalLines;

static_assert((kStepsPerLine % kStepsPerWord) == 0, "Line packing must be exact");
