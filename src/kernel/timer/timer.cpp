/**
 * @file timer.cpp
 * @brief ARM Generic Timer driver implementation (CNTP, EL0 physical timer).
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Programs the EL0 physical timer (CNTP) to fire roughly every 100 ms
 * (10 Hz). The IRQ handler in `interrupts.cpp` reloads `CNTP_TVAL_EL0`
 * after each tick, so the period is approximate — drift accumulates from
 * the handler's own latency.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D11 (Generic Timer)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "kernel/timer.h"
#include <stdint.h>
#include "kernel/io.h"
#include "kernel/spinlock.h"

/** Tick counter; advanced by #increment_time from the IRQ. */
static volatile uint64_t time;

/** Latched timer frequency in Hz (read from CNTFRQ_EL0 at init). */
static volatile uint64_t freq;

static spinlock timer_lock = {0};

void timer_init() {
	timer_lock.spin_lock = SPINLOCK_FREE;
	asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
	asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(freq / 10));
	asm volatile("msr CNTP_CTL_EL0, %0" :: "r"(1));
}

void increment_time() {
	uint64_t flags;
	spin_lock(timer_lock, flags);
	time += 1;
	spin_unlock(timer_lock, flags);
}

uint64_t get_time() {
	uint64_t flags;
	spin_lock(timer_lock, flags);
	uint64_t t = time;
	spin_unlock(timer_lock, flags);
	return t;
}


uint64_t get_freq() {
	return freq;
}


void delay_ms(uint64_t N) {
	uint64_t init_count;
	asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(init_count));
	uint64_t target_ticks = (init_count / 1000) * N;

	uint64_t d_time;
	asm volatile("mrs %0, CNTPCT_EL0" : "=r"(d_time));
	while ((d_time - init_count) <= target_ticks) { // NOLINT
		asm volatile("mrs %0, CNTPCT_EL0" : "=r"(d_time));
	}
}

uint64_t get_ticks() {
	uint64_t ticks;
	asm volatile("mrs %0, CNTPCT_EL0" : "=r"(ticks));
	return ticks;
}

bool ticks_elapsed_ms(uint64_t start_ticks, uint64_t ms) {
	uint64_t freq;
	asm volatile("mrs %0, CNTFRQ_EL0" : "=r"(freq));
	uint64_t target_ticks = (freq / 1000) * ms;
	return (get_ticks() - start_ticks) >= target_ticks;
}

void print_time() {
	uart_puts("\rUptime: ");
	uart_put_uint(get_time());
}

void shell_print_time() {
	uart_puts("\rUptime: ");
	uart_put_uint(get_time());
	uart_puts("\r\n");
}