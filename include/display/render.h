/**
 * @file render.h
 * @brief Framebuffer pixel-write primitives and the shell's text/terminal
 *        rendering state — see render.cpp.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * `term_buffer` is sized for the maximum supported resolution
 * (1920x1080 at 8x8 glyph cells: 240 cols x 135 rows), a fixed-capacity
 * allocation the kernel heap's lack of `kfree` makes deliberate rather
 * than dynamic. `MAX_TERM_ROWS`/`MAX_TERM_COLS` (that fixed stride) vs.
 * `term_rows`/`term_cols` (the live, negotiated-resolution grid size) is a
 * capacity-vs-active-usage split — the same shape as the physical memory
 * manager's total frame count vs. its used-frame count.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once




/**
 * @brief Write one pixel directly into the negotiated framebuffer.
 *
 * No bounds checking against the current width/height — same trust
 * contract as mmio_write; callers are responsible for staying in range.
 * @p color is a pre-packed pixel word matching the framebuffer's actual
 * negotiated pixel order (BGR on this hardware, not the RGB originally
 * requested — see set_resolution's diagnostic output).
 */
void put_pixel(uint32_t x, uint32_t y, uint32_t color);

/** @brief Pack 8-bit channels into a pixel word in the framebuffer's
 *  actual negotiated order (BGR, not RGB — same caveat as put_pixel). */
uint32_t pack_color(uint8_t r, uint8_t g, uint8_t b);

/** @brief Fill the entire negotiated framebuffer with one color. */
void fill_screen(uint32_t color);

/** @brief Draw a single-pixel-tall horizontal line across the full
 *  framebuffer width at row @p y — a straight line confirms pitch is
 *  correct; visible shear means it isn't. No bounds check on @p y. */
void draw_horizontal_line(uint32_t y, uint32_t color);


/**
 * @brief Buffer for terminal characters
 *
 * Currently statically sized for a default resolution of 1920x1080
 * with 8x8 glyph cells
 * 240 columns * 135 rows = 32400 bytes
 */
inline char term_buffer[32400];

// Hardcoded to align with maximum at 1920x1080
constexpr uint8_t MAX_TERM_ROWS = 135;
constexpr uint8_t MAX_TERM_COLS = 240;

inline uint8_t term_rows = MAX_TERM_ROWS;
inline uint8_t term_cols = MAX_TERM_COLS;

inline uint8_t cursor_row = 0;
inline uint8_t cursor_col = 0;

inline uint32_t text_color = pack_color(255, 255, 255); // white
inline uint32_t background_color = pack_color(0, 0, 0); // black

void draw_char(uint32_t col, uint32_t row, char c, uint32_t fg, uint32_t bg);