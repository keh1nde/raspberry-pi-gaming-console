/**
 * @file uart.cpp
 * @brief PL011 UART driver implementation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Drives the BCM2835's PL011 UART at 115200 8N1 with FIFOs enabled. All
 * MMIO goes through the inline `mmio_read`/`mmio_write` helpers in
 * `<uart.h>`. Public API is documented in the header; this file documents
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
#include "uart.h"
#include "board.h"

/** PL011 UART and GPIO register addresses are available in board.h*/

void uart_init()
{
	// Disable the UART before reconfiguring.
	mmio_write(UART0_CR, 0x00000000);

	// Disable pull-up/down on GPIO 14/15 (UART TX/RX), per the BCM2835
	// GPIO pull-state programming sequence (write GPPUD, delay, latch via
	// GPPUDCLK0, delay, clear GPPUDCLK0).
	mmio_write(GPPUD, 0x00000000);
	delay(150);

	mmio_write(GPPUDCLK0, (1 << 14) | (1 << 15));
	delay(150);

	mmio_write(GPPUDCLK0, 0x00000000);

	// Clear all pending UART interrupts.
	mmio_write(UART0_ICR, 0x7FF);

	// Baud divisors for 115200 baud assuming a 3 MHz UART clock under QEMU
	// (IBRD=1, FBRD=40). Real hardware programs different divisors via
	// firmware; values stable for development under QEMU.
	mmio_write(UART0_IBRD, 1);
	mmio_write(UART0_FBRD, 40);

	// LCRH: FIFOs enabled (FEN), 8-bit word length (WLEN=3).
	mmio_write(UART0_LCRH, (1 << 4) | (1 << 5) | (1 << 6));

	// Unmask the set of interrupts the kernel currently cares about.
	// UART RX IRQ (bit 4) is intentionally enabled but the IRQ controller
	// side (IRQ_EN2 bit 25) is currently disabled to avoid an IRQ storm —
	// see CLAUDE.md / known issues.
	mmio_write(UART0_IMSC, (1 << 4) | (1 << 6) |
						(1 << 7) | (1 << 8) | (1 << 9) | (1 << 10));

	// Re-enable the UART with TX and RX.
	mmio_write(UART0_CR, (1 << 0) | (1 << 8) | (1 << 9));
}

void uart_handle_irq() {
	mmio_write(UART0_ICR, 0x7FF);
}

void uart_putc(unsigned char c)
{
	// Spin while TXFF (TX FIFO full).
	while (mmio_read(UART0_FR) & (1 << 5)) { }
	mmio_write(UART0_DR, c);
}

unsigned char uart_getc()
{
	// Spin while RXFE (RX FIFO empty).
	while ( mmio_read(UART0_FR) & (1 << 4) ) { }
	return mmio_read(UART0_DR);
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
