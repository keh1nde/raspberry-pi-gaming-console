/**
 * @file util.h
 * @brief Compatibility shim for Circle's circle/util.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle util.h mostly just re-declares standard C string/mem
 * functions (memcpy/memset/strcmp/...) for its from-scratch bare-metal
 * builds — this kernel already has all of those via newlib's own
 * `<string.h>`, so this shim just pulls that in rather than re-declare
 * anything. `memcpy`/`memset` are the only two actually used anywhere in
 * the vendored tree (checked via grep before writing this).
 *
 * `DMA_BUFFER(type, name, count)` is, like `ASSERT_STATIC` and `LogWrite`
 * before it, used by the vendored tree but not actually defined anywhere
 * in Circle's current util.h/macros.h/sysconfig.h/bcm2711.h (checked
 * directly) — likely from a header this port doesn't vendor. Defined here
 * from its observed shape: a stack buffer for a DMA-visible transfer
 * (`usbgamepadxboxone.cpp`/`usbkeyboard.cpp`'s small fixed command
 * buffers, later passed through `CleanAndInvalidateDataCacheRange` deeper
 * in the transfer path — see `synchronize.h` shim). Cache-line-aligned
 * (64 bytes, Cortex-A76's typical D-cache line — this kernel's own
 * `cache_clean`/`cache_invalidate` read the real value from
 * `CTR_EL0.DminLine` at runtime rather than assume this, see `cache.cpp`;
 * 64 is just this macro's best compile-time guess) so the buffer doesn't
 * share a cache line with unrelated neighboring stack data — sharing one
 * would mean CleanAndInvalidateDataCacheRange's invalidate step could
 * silently discard a concurrent write to whatever else was on that line,
 * exactly the hazard this kernel's own cache-maintenance code already
 * documents.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_util_h
#define _circle_util_h

#include <string.h>

#define DMA_BUFFER(type, name, count) \
	type name[count] __attribute__ ((aligned (64)))

#endif
