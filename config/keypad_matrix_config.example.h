#pragma once

// Copy this file to config/keypad_matrix_config.h and fill in the Pico GPIO connected
// to each front-panel ribbon pin you want to monitor during keypad bring-up.
//
// Use -1 for any line that is not wired yet.
// The defaults below assume active-low monitoring with pull-ups enabled.

inline constexpr int kKeypadPanelPin5Gpio = 6;
inline constexpr int kKeypadPanelPin6Gpio = 7;
inline constexpr int kKeypadPanelPin7Gpio = 8;
inline constexpr int kKeypadPanelPin8Gpio = 9;
inline constexpr int kKeypadPanelPin9Gpio = 10;
inline constexpr int kKeypadPanelPin10Gpio = 11;
inline constexpr int kKeypadPanelPin11Gpio = 12;
inline constexpr int kKeypadPanelPin14Gpio = 20;
inline constexpr int kKeypadPanelPin15Gpio = 13;
inline constexpr int kKeypadPanelPin16Gpio = 14;
inline constexpr int kKeypadPanelPin17Gpio = 15;
inline constexpr int kKeypadPanelPin18Gpio = 16;
inline constexpr int kKeypadPanelPin19Gpio = 19;
inline constexpr int kKeypadPanelPin20Gpio = 17;
inline constexpr int kKeypadPanelPin21Gpio = 18;
inline constexpr int kKeypadPanelPin22Gpio = 22;
