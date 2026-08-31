/**
 * @file timer.h
 * @brief Compatibility shim for Circle's circle/timer.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CTimer drives a scheduler-aware delay/callback/calendar-time
 * subsystem. None of that is exercised anywhere in the vendored USB tree
 * (checked via grep before writing this) — every call site is
 * `CTimer::Get()->MsDelay(...)`, `->GetTicks()`, or `->GetClockTicks()`.
 * This shim wraps `kernel/timer.h`'s existing primitives instead:
 * `delay_ms` for MsDelay, and `get_ticks()`/`get_freq()` (the free-running
 * `CNTPCT_EL0` counter and its real hardware frequency, independent of
 * this kernel's own 10 Hz software tick) for both tick-counter methods.
 *
 * `HZ` and `CLOCKHZ` are Circle's own naming for two different tick rates
 * (`GetTicks()` is meant to increment at `HZ` per second — conventionally
 * 100 in real Circle; `GetClockTicks()` at the raw hardware rate
 * `CLOCKHZ`). Rather than hardcode a Pi 5 crystal frequency we haven't
 * independently confirmed, `CLOCKHZ` is defined here as a live read of
 * `get_freq()` (the same `CNTFRQ_EL0` value this kernel's own timer.cpp
 * already relies on), and `GetTicks()` is computed by scaling the raw
 * counter down to `HZ`-per-second on every call — correct by construction
 * against whatever the real hardware frequency turns out to be, rather
 * than assuming Circle's conventional number is right for this board.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_timer_h
#define _circle_timer_h

#include "kernel/timer.h"
#include <circle/types.h>

#define HZ 100
#define CLOCKHZ (get_freq ())

class CTimer
{
public:
	static CTimer *Get (void)
	{
		static CTimer s_Instance;
		return &s_Instance;
	}

	void MsDelay (unsigned nMilliSeconds)
	{
		delay_ms (nMilliSeconds);
	}

	/// HZ (100)-per-second software tick, derived from the free-running
	/// hardware counter rather than this kernel's own 10 Hz OS tick — see
	/// file comment.
	unsigned GetTicks (void)
	{
		return (unsigned) (get_ticks () / (get_freq () / HZ));
	}

	/// Raw hardware-rate tick count (CLOCKHZ per second).
	unsigned GetClockTicks (void)
	{
		return (unsigned) get_ticks ();
	}
};

#endif
