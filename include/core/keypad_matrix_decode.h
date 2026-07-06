#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "input.h"

/// @brief Pure keypad matrix closure-decode logic shared between the firmware and
/// host tests (see issue #14). Knows the ribbon-pin-pair-to-button map and how to
/// read a hit-mask snapshot; knows nothing about GPIOs, debounce, or timing, so it
/// can be exercised with synthetic data and no Pico hardware.
namespace keypad_matrix_decode
{

/// @brief One matrix closure definition mapping a logical button to its ribbon pin pair.
struct MatrixButtonConfig
{
    ButtonId id;
    uint8_t panel_pin_a;
    uint8_t panel_pin_b;
};

/// @brief Confirmed bench matrix map (see the keypad section of README.md).
inline constexpr std::array<MatrixButtonConfig, static_cast<size_t>(ButtonId::Count)>
    kMatrixButtons = {{
        {ButtonId::LeftTop, 7, 22},
        {ButtonId::LeftUpper, 8, 22},
        {ButtonId::LeftMiddle, 9, 22},
        {ButtonId::LeftLower, 10, 22},
        {ButtonId::LeftBottom, 11, 22},
        {ButtonId::RightTop, 7, 15},
        {ButtonId::RightUpper, 8, 15},
        {ButtonId::RightMiddle, 9, 15},
        {ButtonId::RightLower, 10, 15},
        {ButtonId::RightBottom, 11, 15},
        {ButtonId::Alert, 5, 20},
        {ButtonId::Test, 5, 17},
        {ButtonId::Brt, 5, 16},
        {ButtonId::Dim, 5, 15},
        {ButtonId::Ltrs, 6, 21},
        {ButtonId::BackStep, 6, 20},
        {ButtonId::CursorLeft, 6, 19},
        {ButtonId::CursorRight, 6, 18},
        {ButtonId::Slash, 6, 17},
        {ButtonId::Clr, 6, 16},
        {ButtonId::AlphaA, 7, 21},
        {ButtonId::AlphaB, 7, 20},
        {ButtonId::AlphaC, 7, 19},
        {ButtonId::AlphaD, 7, 18},
        {ButtonId::AlphaE, 7, 17},
        {ButtonId::AlphaF, 7, 16},
        {ButtonId::AlphaG, 8, 21},
        {ButtonId::AlphaH, 8, 20},
        {ButtonId::AlphaI, 8, 19},
        {ButtonId::AlphaJ, 8, 18},
        {ButtonId::AlphaK, 8, 17},
        {ButtonId::AlphaL, 8, 16},
        {ButtonId::AlphaM, 9, 21},
        {ButtonId::AlphaN, 9, 20},
        {ButtonId::AlphaO, 9, 19},
        {ButtonId::AlphaP, 9, 18},
        {ButtonId::AlphaQ, 9, 17},
        {ButtonId::AlphaR, 9, 16},
        {ButtonId::AlphaS, 10, 21},
        {ButtonId::AlphaT, 10, 20},
        {ButtonId::AlphaU, 10, 19},
        {ButtonId::AlphaV, 10, 18},
        {ButtonId::AlphaW, 10, 17},
        {ButtonId::AlphaX, 10, 16},
        {ButtonId::AlphaY, 11, 21},
        {ButtonId::AlphaZ, 11, 20},
        {ButtonId::TFunc, 11, 19},
        {ButtonId::Dot, 11, 18},
        {ButtonId::Zero, 11, 17},
        {ButtonId::Spc, 11, 16},
    }};

/// @brief Ribbon pin numbers observed by the keypad probe scan.
/// @details Index order matches the bit position used in a hit-mask snapshot
/// (`hits_by_drive[i]`'s bits are keyed to these same indices). input.cpp's
/// hardware-facing `kObservedLines` table must keep the same pin order --
/// input.cpp static_asserts that against this array.
inline constexpr std::array<uint8_t, kKeypadObservedLineCount> kObservedPanelPins = {
    5, 6, 7, 8, 9, 10, 11, 14, 15, 16, 17, 18, 19, 20, 21, 22};

/// @brief Returns the 16-bit hit-mask bit for one observed-line index.
constexpr uint16_t observed_line_hit_bit(size_t line_index)
{
    return static_cast<uint16_t>(1U << line_index);
}

/// @brief Returns the observed-line index for one ribbon pin.
/// @details Returns `kObservedPanelPins.size()` if the pin is not part of the
/// observed set.
constexpr size_t observed_line_index_for_panel_pin(uint8_t panel_pin)
{
    for (size_t i = 0; i < kObservedPanelPins.size(); ++i)
    {
        if (kObservedPanelPins[i] == panel_pin)
        {
            return i;
        }
    }

    return kObservedPanelPins.size();
}

/// @brief Returns whether a hit-mask snapshot shows a closure between two ribbon pins.
/// @details Pure decode logic: does not know whether either line's GPIO is wired --
/// callers that care about hardware presence apply that check separately.
inline bool hit_mask_shows_closure(
    const std::array<uint16_t, kKeypadObservedLineCount>& hits_by_drive, uint8_t panel_pin_a,
    uint8_t panel_pin_b)
{
    const size_t index_a = observed_line_index_for_panel_pin(panel_pin_a);
    const size_t index_b = observed_line_index_for_panel_pin(panel_pin_b);
    if (index_a >= kObservedPanelPins.size() || index_b >= kObservedPanelPins.size())
    {
        return false;
    }

    const uint16_t bit_a = observed_line_hit_bit(index_a);
    const uint16_t bit_b = observed_line_hit_bit(index_b);
    return ((hits_by_drive[index_a] & bit_b) != 0) || ((hits_by_drive[index_b] & bit_a) != 0);
}

} // namespace keypad_matrix_decode
