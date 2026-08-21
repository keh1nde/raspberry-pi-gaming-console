/**
 * @file io.h
 * @brief PL011 UART driver — public API for serial I/O.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Exposes the BCM2835's PL011 UART as a kernel-wide console. Boot logging,
 * panic messages, and the interactive shell all funnel through this module.
 * Inline `mmio_read`/`mmio_write` helpers (plus `mmio_read16`/`mmio_write16`
 * and `mmio_read8`/`mmio_write8` for registers narrower than 32 bits) double
 * as the generic MMIO accessors used by every other driver (timer, IRQ
 * controller, etc.).
 *
 * References:
 *   - ARM PrimeCell UART (PL011) Technical Reference Manual (ARM DDI 0183)
 *   - BCM2835 ARM Peripherals, §13 (UART)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdint.h>

/**
 * @brief Write a 32-bit value to a memory-mapped I/O register.
 *
 * The `volatile` cast forbids the compiler from coalescing or reordering this
 * store with adjacent memory accesses — required for any device register.
 *
 * @param reg  Physical address of the MMIO register.
 * @param data 32-bit value to store.
 */
static inline void mmio_write(uint64_t reg, uint32_t data)
{
	*reinterpret_cast<volatile uint32_t*>(reg) = data;
}

/**
 * @brief Read a 32-bit value from a memory-mapped I/O register.
 *
 * @param reg Physical address of the MMIO register.
 * @return The current 32-bit value at @p reg.
 */
static inline uint32_t mmio_read(uint64_t reg)
{
	return *reinterpret_cast<volatile uint32_t*>(reg);
}

/**
 * @brief Write a 16-bit value to a memory-mapped I/O register.
 *
 * Same `volatile`-access contract as #mmio_write, narrowed to a halfword —
 * for registers documented as 16-bit wide rather than folded into a 32-bit
 * access (e.g. SDHCI's Clock Control register).
 *
 * @param reg  Physical address of the MMIO register.
 * @param data 16-bit value to store.
 */
static inline void mmio_write16(uint64_t reg, uint16_t data)
{
	*reinterpret_cast<volatile uint16_t*>(reg) = data;
}

/**
 * @brief Read a 16-bit value from a memory-mapped I/O register.
 *
 * @param reg Physical address of the MMIO register.
 * @return The current 16-bit value at @p reg.
 */
static inline uint16_t mmio_read16(uint64_t reg)
{
	return *reinterpret_cast<volatile uint16_t*>(reg);
}

/**
 * @brief Write an 8-bit value to a memory-mapped I/O register.
 *
 * Same `volatile`-access contract as #mmio_write, narrowed to a byte — for
 * registers documented as 8-bit wide (e.g. SDHCI's Host Control 1, Power
 * Control, Block Gap Control, Wakeup Control, Timeout Control, and Software
 * Reset registers), so each can be addressed and accessed independently
 * instead of masked/shifted out of a wider read.
 *
 * @param reg  Physical address of the MMIO register.
 * @param data 8-bit value to store.
 */
static inline void mmio_write8(uint64_t reg, uint8_t data)
{
	*reinterpret_cast<volatile uint8_t*>(reg) = data;
}

/**
 * @brief Read an 8-bit value from a memory-mapped I/O register.
 *
 * @param reg Physical address of the MMIO register.
 * @return The current 8-bit value at @p reg.
 */
static inline uint8_t mmio_read8(uint64_t reg)
{
	return *reinterpret_cast<volatile uint8_t*>(reg);
}

/**
 * @brief Busy-wait spin loop, approximately @p count iterations long.
 *
 * Used during UART bring-up where the BCM2835 requires brief pauses between
 * GPIO pull-up state changes. Wall-clock duration is implementation-defined
 * (depends on CPU frequency and pipeline behavior); do not use for precise
 * timing.
 *
 * @param count Number of decrement iterations to perform.
 */
static inline void delay(int32_t count) {
	asm volatile("__delay_%=: subs %[count], %[count], #1; bne __delay_%=\n"
						: "=r"(count): [count]"0"(count) : "cc");
}

/**
 * @brief Initialize UART0 to 115200 baud, 8N1, no flow control, FIFO enabled.
 *
 * Disables the UART, clears GPIO 14/15 pull state, clears pending interrupts,
 * programs the integer/fractional baud divisors, configures line control,
 * unmasks the interrupt sources the kernel cares about, then re-enables the
 * UART for transmit and receive. Must be called before any other uart_* call.
 */
void uart_init();

/**
 * @brief Handle interrupts made by UART.
 *
 * Reads UART_MIS to confirm an RX interrupt is being emitted, processes the
 * incoming byte, then writes to UART0_ICR to clear the interrupt.
 */
void uart_handle_irq();


/**
 * @brief Transmit one byte over UART0, blocking until the TX FIFO has space.
 *
 * Polls UART0_FR.TXFF and busy-waits while the transmit FIFO is full. No
 * timeout; a wedged UART will hang the caller.
 *
 * @param c Byte to transmit. No newline translation is performed.
 */
void uart_putc(unsigned char c);

/**
 * @brief Receive one byte from UART0, blocking until a byte is available.
 *
 * Polls UART0_FR.RXFE and busy-waits while the receive FIFO is empty.
 *
 * @return The next received byte.
 */
unsigned char uart_getc();

/**
 * @brief Transmit a null-terminated C string over UART0.
 *
 * Calls #uart_putc for each character up to (but not including) the
 * terminating `'\0'`. No newline translation.
 *
 * @param str Null-terminated string. Must not be `nullptr`.
 */
void uart_puts(const char* str);

/**
 * @brief Print an unsigned 64-bit value in hexadecimal, prefixed with `0x`.
 *
 * Emits `0` for a zero value (no `0x` prefix in that case). Otherwise emits
 * `0x` followed by the value in uppercase hex with no leading zeros.
 *
 * @param val Value to print.
 */
void uart_put_hex(uint64_t val);

/**
 * @brief Print an unsigned 64-bit value in decimal with no leading zeros.
 *
 * @param val Value to print.
 */
void uart_put_uint(uint64_t val);

/**
 * @brief Print a single hex digit (0–15) as its ASCII character.
 *
 * Internal helper used by #uart_put_hex. Values 0–9 emit `'0'`–`'9'`; values
 * 10–15 emit `'A'`–`'F'`. Inputs outside 0–15 produce undefined output.
 *
 * @param val Digit value in the range [0, 15].
 */
void print_helper(uint64_t val);
