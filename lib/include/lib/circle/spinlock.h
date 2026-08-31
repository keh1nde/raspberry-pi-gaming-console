/**
 * @file spinlock.h
 * @brief Compatibility shim for Circle's circle/spinlock.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CSpinLock takes a target IRQ/FIQ level in its constructor and
 * is a thin wrapper over its own architecture-specific exclusive-access
 * primitives. This shim is just an adapter over this kernel's own
 * `spinlock`/`spin_lock`/`spin_unlock` (`kernel/spinlock.h`) — the same
 * IRQ-save, blocking, WFE-parked design already used everywhere else in
 * this kernel, not a separate lock implementation. `Acquire`/`Release`
 * take no arguments in Circle's real API (unlike this kernel's own
 * `spin_lock`/`spin_unlock`, which thread a DAIF-flags out-param between
 * paired calls) — this class just holds that flags state as a member
 * between the two calls instead, same shape as `SpinLockGuard`
 * (`kernel/spinlock.h`) already does for RAII-style locking.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_spinlock_h
#define _circle_spinlock_h

#include "kernel/spinlock.h"

class CSpinLock
{
public:
	// Real Circle's constructor takes an optional target IRQ/FIQ level;
	// nothing in the vendored tree passes one (checked via grep), so it's
	// accepted and ignored rather than modeled.
	CSpinLock (int nTargetLevel = 0)
	:	m_Lock {SPINLOCK_FREE},
		m_nFlags (0)
	{
		(void) nTargetLevel;
	}

	void Acquire (void)
	{
		spin_lock (m_Lock, m_nFlags);
	}

	void Release (void)
	{
		spin_unlock (m_Lock, m_nFlags);
	}

private:
	spinlock m_Lock;
	uint64_t m_nFlags;
};

#endif
