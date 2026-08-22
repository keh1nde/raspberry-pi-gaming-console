/**
 * @file io.cpp
 * @brief PL011 UART driver implementation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Drives the BCM2835's PL011 UART at 115200 8N1 with FIFOs enabled. All
 * MMIO goes through the inline `mmio_read`/`mmio_write` helpers in
 * `<io.h>`. Public API is documented in the header; this file documents
 * register-level behavior.
 *
 * References:
 *   - ARM PrimeCell UART (PL011) Technical Reference Manual (ARM DDI 0183)
 *   - BCM2835 ARM Peripherals, §13 (UART) and §6 (GPIO)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <stddef.h>
#include <stdint.h>
#include "kernel/io.h"

#include "string.h"
#include "display/display.h"
#include "kernel/board.h"
#include "kernel/spinlock.h"
#include "display/render.h"

static spinlock uart_lock = {0};

void uart_init(){
	uart_lock.spin_lock = SPINLOCK_FREE;
	// Disable the UART before reconfiguring.
	mmio_write(UART0_CR, 0x00000000);

	// Set up the clock
	mmio_write(CLK_UART_DIV_INT, 1);
	mmio_write(CLK_UART_CTRL, (2 << 5) | (1 << 11) | (1 << 0));

	// Set FUNCSEL field to a4 to mux GPIO14 to UART0_TX and GPIO15 to UART0_RX
	mmio_write(GPIO14_CTRL, 4);
	mmio_write(GPIO15_CTRL, 4);

	// Enable Schmitt Trigger and set drive strength to 0x12
	mmio_write(GPIO14_PAD, 0x12);

	// Enable Schmitt Trigger, set drive strength to 0x12, enable input and disable output.
	mmio_write(GPIO15_PAD, 0xD2);

	// Clear all pending UART interrupts.
	mmio_write(UART0_ICR, 0x7FF);

	// Baud divisors for 115200 baud at 50 MHz (xosc)
	// BRD = 50,000,000 / (16 * 115,200) = 27.127
	// IBRD = 27, FBRD = round(0.127 * 64) = 8
	mmio_write(UART0_IBRD, 27);
	mmio_write(UART0_FBRD, 8);

	// LCRH: FIFOs enabled (FEN), 8-bit word length (WLEN=3).
	mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

	// Unmask the set of interrupts the kernel currently cares about.
	// UART RX IRQ (bit 4) is intentionally enabled but the IRQ controller
	// side (IRQ_EN2 bit 25) is currently disabled to avoid an IRQ storm —
	// see CLAUDE.md / known issues.
	mmio_write(UART0_IMSC, (1 << 1) | (1 << 4) | (1 << 5) | (1 << 6) |
						(1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));

	// Re-enable the UART with TX and RX.
	mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void uart_handle_irq() {
		if (
			(mmio_read(UART0_DR) & 8) |
			(mmio_read(UART0_DR) & 9) |
			(mmio_read(UART0_DR) & 10) |
			(mmio_read(UART0_DR) & 11)
		) return;

		uart_putc(mmio_read(UART0_DR) & 0xFF);
		mmio_write(UART0_ICR, 0x7FF);
}

void uart_putc(unsigned char c)
{
	uint64_t flags;
	spin_lock(uart_lock, flags);
	while (mmio_read(UART0_FR) & (1 << 5)) { }
	mmio_write(UART0_DR, c);
	spin_unlock(uart_lock, flags);
}

unsigned char uart_getc()
{
	uint64_t flags;
	spin_lock(uart_lock, flags);
	while (mmio_read(UART0_FR) & (1 << 4)) { }
	unsigned char c = mmio_read(UART0_DR);
	spin_unlock(uart_lock, flags);
	return c;
}

void uart_puts(const char* str)
{
	for (size_t i = 0; str[i] != '\0'; i ++)
		uart_putc(static_cast<unsigned char>(str[i]));
}

void print_helper(const uint64_t val) {
	uart_putc(val < 10 ? '0' + val : 'A' + val - 10);
}

void uart_put_hex(uint64_t val) {
	uint64_t value_buffer[20];
	uint64_t counter = 0;

	if (val == 0) {
		uart_putc('0');
		return;
	}

	// Decompose into base-16 digits, least-significant first.
	for (size_t i = 0; val != 0; i++) {
		value_buffer[i] = val % 16;
		counter++;

		val = val / 16;
	}

	uart_putc('0');
	uart_putc('x');
	// Emit digits in reverse (most-significant first).
	for (size_t i = 0; i != counter; i++) {
		print_helper(value_buffer[counter - 1 - i]);
	}
}

void uart_put_uint(uint64_t val) {
	uint64_t value_buffer[20];
	uint64_t counter = 0;

	if (val == 0) {
		uart_putc('0');
		return;
	}

	for (size_t i = 0; val != 0; i++) {
		value_buffer[i] = val % 10;
		counter++;

		val = val / 10;
	}

	for (size_t i = 0; i != counter; i++) {
		print_helper(value_buffer[counter - i - 1]);
	}
}

void _term_scroll() {
	// logical buffer scroll
	memmove(term_buffer, term_buffer + MAX_TERM_COLS, (term_rows - 1) * MAX_TERM_COLS);

	// blank new bottom logical row
	memset(term_buffer + (term_rows - 1) * MAX_TERM_COLS, ' ', term_cols);

	// scroll framebuffer pixels
	// one character-row is 8 scanlines tall
	// each scanline is framebuffer_pitch bytes
	memmove(reinterpret_cast<void*>(framebuffer_base),
		reinterpret_cast<void*>(framebuffer_base + 8 * framebuffer_pitch),
		(term_rows - 1) * 8 * framebuffer_pitch);

	// memset fills by byte, so filling in a multibyte word could
	// result in UB. Thus, loop each of the 8 scanlines and set
	// the color manually.
	for (uint32_t y = (term_rows-1)*8; y < (term_rows-1)*8 + 8; y++) {
		draw_horizontal_line(y, background_color);
	}
}

void term_clear() {
	memset(term_buffer, ' ', MAX_TERM_ROWS * MAX_TERM_COLS);
	fill_screen(background_color);
	cursor_row = 0;
	cursor_col = 0;

	uart_term_clear();
}

void uart_term_clear() {
	uart_puts("\033[2J\033[H");
}

void term_putc(char c) {
	draw_char(cursor_col, cursor_row, ' ', text_color, background_color);

	if (c == '\r' || c == '\n') {
		cursor_row++;
		cursor_col = 0;
		if (cursor_row >= term_rows) {
			_term_scroll();
			cursor_row = term_rows - 1;
		}

		draw_char(cursor_col, cursor_row, ' ', background_color, text_color);
		return;
	}

	if ((c == 127 || c == '\b') && cursor_col > 3) {
		cursor_col--;
		term_buffer[cursor_row * MAX_TERM_COLS + cursor_col] = ' ';

		draw_char(cursor_col, cursor_row, ' ', background_color, text_color);
		return;
	}

	term_buffer[cursor_row * MAX_TERM_COLS + cursor_col] = c;
	draw_char(cursor_col, cursor_row, c, text_color, background_color);
	cursor_col++;

	if (cursor_col >= term_cols) {
		cursor_col = 0;
		cursor_row++;
		if (cursor_row >= term_rows) {
			_term_scroll();
			cursor_row = term_rows - 1;
		}
	}

	draw_char(cursor_col, cursor_row, ' ', background_color, text_color);
}

void term_puts(const char* str) {
	for (size_t i = 0; str[i] != '\0'; i ++)
		term_putc(str[i]);
}

void io_puts(const char *str) {
	term_puts(str);
	uart_puts(str);
}

void io_putc(char c) {
	term_putc(c);
	uart_putc(c);
}
