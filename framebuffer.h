#pragma once

#include <cstdint>

namespace framebuffer {

/// @brief Returns the currently visible UI framebuffer.
uint8_t* front();

/// @brief Returns the UI framebuffer that should be drawn into next.
uint8_t* back();

/// @brief Swaps the front and back UI framebuffer pointers.
void swap();

/// @brief Fills the whole UI framebuffer with black or white.
void clear(uint8_t* fb, bool on);

/// @brief Sets one pixel in portrait UI coordinates.
void set_pixel(uint8_t* fb, int x, int y, bool on);

/// @brief Reads one pixel from portrait UI coordinates.
bool get_pixel(const uint8_t* fb, int x, int y);

/// @brief Draws a horizontal line in UI space.
void draw_hline(uint8_t* fb, int x0, int x1, int y, bool on);

/// @brief Draws a vertical line in UI space.
void draw_vline(uint8_t* fb, int x, int y0, int y1, bool on);

/// @brief Draws an outline rectangle in UI space.
void draw_rect(uint8_t* fb, int x, int y, int w, int h, bool on);

/// @brief Draws a filled rectangle in UI space.
void fill_rect(uint8_t* fb, int x, int y, int w, int h, bool on);

/// @brief Draws a simple diagonal test line.
void draw_diag(uint8_t* fb, bool on);

/// @brief Draws a general line using Bresenham's algorithm.
void draw_line(uint8_t* fb, int x0, int y0, int x1, int y1, bool on);

/// @brief Draws one 5x7 character at the requested scale.
void draw_char(uint8_t* fb, int x, int y, char c, bool on, int scale = 1);

/// @brief Draws a null-terminated text string in UI space.
void draw_text(uint8_t* fb, int x, int y, const char* s, bool on, int scale = 1, int spacing = 1);

}  // namespace framebuffer
