#pragma once

// Copy this file to keypad_matrix_config.h and fill in the Pico GPIO connected
// to each front-panel ribbon pin you want to monitor during keypad bring-up.
//
// Use -1 for any line that is not wired yet.
// The defaults below assume active-low monitoring with pull-ups enabled.

inline constexpr int KEYPAD_PANEL_PIN_5_GPIO  = 6;
inline constexpr int KEYPAD_PANEL_PIN_6_GPIO  = 7;
inline constexpr int KEYPAD_PANEL_PIN_7_GPIO  = 8;
inline constexpr int KEYPAD_PANEL_PIN_8_GPIO  = 9;
inline constexpr int KEYPAD_PANEL_PIN_9_GPIO  = 10;
inline constexpr int KEYPAD_PANEL_PIN_10_GPIO = 11;
inline constexpr int KEYPAD_PANEL_PIN_11_GPIO = 12;
inline constexpr int KEYPAD_PANEL_PIN_14_GPIO = 20;
inline constexpr int KEYPAD_PANEL_PIN_15_GPIO = 13;
inline constexpr int KEYPAD_PANEL_PIN_16_GPIO = 14;
inline constexpr int KEYPAD_PANEL_PIN_17_GPIO = 15;
inline constexpr int KEYPAD_PANEL_PIN_18_GPIO = 16;
inline constexpr int KEYPAD_PANEL_PIN_19_GPIO = 19;
inline constexpr int KEYPAD_PANEL_PIN_20_GPIO = 17;
inline constexpr int KEYPAD_PANEL_PIN_21_GPIO = 18;
