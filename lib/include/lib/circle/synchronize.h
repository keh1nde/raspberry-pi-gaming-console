/**
 * @file synchronize.h
 * @brief Compatibility shim for Circle's circle/synchronize.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Maps Circle's barrier/cache-maintenance free functions onto primitives
 * this kernel already has (`kernel/barrier.h`, `kernel/cache.h`) instead of
 * reimplementing them. `DataMemBarrier` (Circle's weaker ordering-only
 * barrier) maps to `dsb_ish` rather than a true `dmb` — this kernel's own
 * barrier.h deliberately has no `dmb` wrapper yet ("no current call site
 * needs a barrier weaker than dsb", per its own file comment), and a
 * stronger `dsb` is always a safe substitute for a `dmb` requirement, just
 * potentially more conservative. `DataSyncBarrier` (Circle's full
 * completion barrier) maps to `dsb_sy`. `CleanAndInvalidateDataCacheRange`
 * is `cache_clean` + `cache_invalidate` back to back — both already round
 * the affected range out to whole cache lines internally using the real,
 * runtime-read `CTR_EL0.DminLine` (see `cache.cpp`), so this shim doesn't
 * need to duplicate that logic, just sequence the two calls.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_synchronize_h
#define _circle_synchronize_h

#include "kernel/barrier.h"
#include "kernel/cache.h"
#include <circle/memory.h>	// MEM_KERNEL_END — real Circle presumably pulls
				// this in transitively somewhere our simplified
				// shim headers don't; xhciendpoint.cpp uses it
				// (a DMA-buffer sanity assert) without including
				// circle/memory.h directly, and already includes
				// this header, so it lands here instead of
				// editing that vendored file to add the include.
#include <circle/types.h>

inline void DataMemBarrier (void)
{
	dsb_ish ();
}

inline void DataSyncBarrier (void)
{
	dsb_sy ();
}

inline void CleanAndInvalidateDataCacheRange (uintptr nAddress, size_t nLength)
{
	cache_clean ((const void *) nAddress, nLength);
	cache_invalidate ((const void *) nAddress, nLength);
}

#endif
