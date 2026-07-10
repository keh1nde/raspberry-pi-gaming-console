/**
 * @file spinlock.h
 * @brief Spinlock implementation for multicore synchronization
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Declares an alias SPINLOCK_FREE, a spinlock in the form of a struct
 * and two methods spin_lock and spin_unlock.
 *
 * References:
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 *
 * enable_jtag_gpio=1
 */
#pragma once
#include <stdint-gcc.h>

#define SPINLOCK_FREE 0

inline bool MMU_ACTIVE = 0;

struct spinlock {
	volatile uint32_t spin_lock;
};

/**
 * @brief Acquire a spinlock, blocking until it is free.
 *
 * Saves the caller's DAIF into `flags` and masks all exceptions
 * (`DAIFSet #0xf`) so an IRQ on this core can't re-enter the same lock,
 * then spins on LDAXR/STLXR until the exclusive store-conditional succeeds.
 * While the lock reads held, WFE parks the core instead of hot-spinning;
 * the matching spin_unlock's release store wakes it via the exclusive
 * monitor.
 *
 * Only runs the LDAXR/STLXR/WFE sequence once #MMU_ACTIVE is set — on this
 * board LDAXR/STLXR fault if executed before the MMU is enabled, and no
 * multicore contention is possible in that window anyway (see #MMU_ACTIVE).
 *
 * @param lk    Lock to acquire.
 * @param flags Out-param: the caller's DAIF state at entry, restored by the
 *              matching spin_unlock.
 */
void spin_lock(spinlock& lk, uint64_t& flags);

/**
 * @brief Release a spinlock acquired via spin_lock.
 *
 * Issues a release store (STLR) clearing the lock word, followed by SEV to
 * wake any core parked in WFE inside spin_lock — both skipped before
 * #MMU_ACTIVE is set, matching spin_lock's early-boot fast path. Always
 * restores the caller's DAIF from `flags`, re-enabling whatever exceptions
 * were unmasked at the matching spin_lock call.
 *
 * @param lk    Lock to release. Must be currently held by this core.
 * @param flags DAIF state captured by the matching spin_lock call.
 */
void spin_unlock(spinlock& lk, uint64_t flags);