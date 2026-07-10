/**
 * @file cache.h
 * @brief D-cache maintenance-by-VA primitives.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Two operations for keeping a *caller-managed cacheable* buffer coherent
 * with a DMA-capable device by hand. Buffers from #dma_alloc don't need
 * either of these — they're mapped Normal Non-cacheable, so nothing is
 * ever cached there to clean or invalidate in the first place. These exist
 * for the alternative strategy: keep a buffer cacheable (faster CPU-side
 * access) and explicitly flush around each transfer instead.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D12.2 (Cache maintenance)
 *   - Arm Architecture Reference Manual for A-profile, §D17.2.32 (CTR_EL0)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once
#include <stdint.h>

/**
 * @brief Clean (write back) the D-cache for `[addr, addr + size)` to the
 *        Point of Coherency, without discarding the cached copy.
 *
 * Call before a DMA-capable device reads a cacheable buffer the CPU has
 * written, so the device sees the CPU's writes rather than stale memory.
 *
 * Rounds the range out to whole cache lines, so a partial-line @p addr or
 * end is safe — the extra bytes are harmlessly re-cleaned, never skipped.
 *
 * @param addr Start of the range (any alignment).
 * @param size Length in bytes (any alignment).
 */
void cache_clean(const void* addr, uint64_t size);

/**
 * @brief Invalidate the D-cache for `[addr, addr + size)` from the Point of
 *        Coherency, discarding any cached copy without writing it back.
 *
 * Call before the CPU reads a cacheable buffer a DMA-capable device has
 * just written, so the read fetches the device's data instead of a stale
 * cached copy.
 *
 * @warning Rounds the range out to whole cache lines. If @p addr or
 *          @p addr + @p size falls mid-line, any *dirty* CPU-written data
 *          sharing that line is discarded, not written back first. Safe for
 *          a buffer that occupies whole cache-line-aligned pages on its
 *          own; unsafe for a sub-cache-line slice of a struct something
 *          else is concurrently writing.
 *
 * @param addr Start of the range (any alignment).
 * @param size Length in bytes (any alignment).
 */
void cache_invalidate(const void* addr, uint64_t size);
