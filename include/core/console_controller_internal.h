#pragma once

#include <cstddef>

#include "input.h"

namespace console_controller
{

/// @brief Low-level softkey-label helpers shared between console_controller.cpp
/// and the feature modules it delegates to (pinter_controller.cpp, ...).
/// @details Not part of the public console_controller.h API -- only the
/// controller's own implementation files should include this.
namespace console_controller_internal
{

/// @brief Formats one two-line softkey label with a square-bracket selection,
/// using controller-owned storage that stays valid until the next call for
/// the same key.
const char* build_selection_softkey_label(SoftKeyId key, const char* title, const char* selection);

/// @brief Returns the controller-owned scratch label buffer for one softkey,
/// for labels that need more than build_selection_softkey_label's two lines.
char* dynamic_softkey_label_buffer(SoftKeyId key, size_t& out_capacity);

/// @brief Copies a short label title and forces uppercase for consistent softkey headings.
void build_uppercase_title(const char* input, char* output, size_t output_size);

/// @brief Returns true when the current LTRS mode maps a button to a digit.
/// @details Shared between core keypad text entry and settings_controller.cpp's
/// screen-saver timeout scratchpad, both of which need the same digit mapping.
bool keypad_digit_value(ButtonId id, uint8_t* out_digit);

} // namespace console_controller_internal

} // namespace console_controller
