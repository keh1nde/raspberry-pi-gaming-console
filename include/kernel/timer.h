/**
 * @file timer.h
 * @brief ARM Generic Timer (CNTP, EL0 physical timer) — public API.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Drives the per-core physical timer at a fixed 10 Hz tick. Tick count is
 * incremented from inside the IRQ handler (see
 * `src/interrupts/interrupts.cpp`). On the Pi 3, the timer IRQ is routed
 * through the ARM local peripheral block at `0x40000000`, not through the
 * legacy BCM2835 IRQ controller.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D11 (Generic Timer)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>

/**
 * @brief Latch the timer frequency, program the next compare value, enable CNTP.
 *
 * Reads `CNTFRQ_EL0` to discover the timer frequency, loads
 * `freq / 10` into `CNTP_TVAL_EL0` (yielding ~10 Hz ticks), and enables the
 * physical timer by setting `CNTP_CTL_EL0.ENABLE`.
 */
void timer_init();

/** @brief Advance the in-kernel tick counter by one. Called from the IRQ. */
void increment_time();

/** @brief Return the current tick count (10 Hz, monotonic since boot). */
uint64_t get_time();

/** @brief Return the latched timer frequency (Hz). Set by #timer_init. */
uint64_t get_freq();

/** @brief Delays the execution of a method until a certain amount of milliseconds has elapsed.*/
void delay_ms(uint64_t N);

/** @brief Read the raw hardware tick count (CNTPCT_EL0). Free-running,
 *  independent of timer_init()/the software 10Hz tick — the same counter
 *  #delay_ms is built on. Pair with #ticks_elapsed_ms to bound a polling
 *  loop by wall-clock time instead of a fixed sleep. */
uint64_t get_ticks();

/** @brief True once at least `ms` milliseconds have elapsed since
 *  `start_ticks` (a value previously returned by #get_ticks). */
bool ticks_elapsed_ms(uint64_t start_ticks, uint64_t ms);

/** @brief Emit `"Uptime: <ticks>"` over UART, preceded by a carriage return. */
void print_time();

/** @brief Emit "Uptime: <ticks>" over UART without a carriage return. Used with the shell.*/
void shell_print_time();
