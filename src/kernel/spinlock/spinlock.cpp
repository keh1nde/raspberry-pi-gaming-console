/**
 * @file spinlock.cpp
 * @brief Spinlock implementation for multicore synchronization
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Implements a spinlock, the synchronization primitive for the kernel.
 *
 * Uses a 32-bit word as the lock state: 0 = free (SPINLOCK_FREE),
 * 1 = held. Callers mask IRQs before acquiring so that an IRQ handler
 * on the same core cannot deadlock trying to re-enter the same lock.
 *
 * References:
 *	- Arm Architecture Reference Manual for A-profile (VMSAv8-64)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
*/

#include "../../../include/kernel/spinlock.h"
#include "kernel/spinlock.h"
#include <stdint-gcc.h>

void spin_lock(spinlock& lk, uint64_t& flags) {
	uint32_t val, result;

	// Save current DAIF then mask all exceptions so no IRQ can fire
	// between the save and the moment we own the lock.
	asm volatile("mrs %0, daif" : "=r"(flags));
	asm volatile("msr DAIFSet, #0xf");

	// Outer loop: retry the whole LDAXR/STLXR pair if the exclusive
	// monitor was broken by another core touching the cache line.
	if (!MMU_ACTIVE) return;
	do {
		// Inner loop: spin until the lock word reads 0 (free). WFE
		// parks the core instead of hot-spinning; a held lock's word
		// is already covered by our LDAXR's exclusive monitor, so the
		// unlocker's STLR/STLXR to that same address wakes us without
		// needing to re-issue LDAXR first.
		do {
			asm volatile("ldaxr %w0, [%1]"
				: "=&r"(val)
				: "r"(&lk.spin_lock)
				: "memory");
			if (val != 0) {
				asm volatile("wfe" ::: "memory");
			}
		} while (val != 0);

		// Attempt to claim the lock with a store-exclusive.
		// result == 0 means the store succeeded; 1 means the
		// exclusive monitor was broken and we must retry.
		asm volatile("stlxr %w0, %w2, [%1]"
			: "=&r"(result)
			: "r"(&lk.spin_lock), "r"(static_cast<uint32_t>(1))
			: "memory");
	} while (result != 0);
}

void spin_unlock(spinlock& lk, uint64_t flags) {
	if (MMU_ACTIVE) {
		// Release store: publish all preceding writes before clearing the lock.
		asm volatile("stlr %w0, [%1]"
			:
			: "r"(static_cast<uint32_t>(0)), "r"(&lk.spin_lock)
			: "memory");

		// Wake any core parked in WFE waiting on this lock.
		asm volatile("sev" ::: "memory");
	}

	// Restore the caller's DAIF (re-enables IRQs if they were on before).
	asm volatile("msr daif, %0" :: "r"(flags));
}
