/**
 * @file scheduler.h
 * @brief Compatibility shim for Circle's circle/sched/scheduler.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * This kernel has no scheduler (deliberately deferred — see project
 * history: cores 1-3 park in `wfe`, no dynamic work yet). The only call
 * anywhere in the vendored tree (checked via grep before writing this) is
 * `CScheduler::Get()->Yield()` in `xhciendpoint.cpp`'s busy-wait loop —
 * genuinely a no-op here, since there is no other task to yield to; core 0
 * just keeps polling.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_sched_scheduler_h
#define _circle_sched_scheduler_h

class CScheduler
{
public:
	static CScheduler *Get (void)
	{
		static CScheduler s_Instance;
		return &s_Instance;
	}

	void Yield (void) {}
};

#endif
