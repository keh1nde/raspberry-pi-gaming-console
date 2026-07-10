/**
 * @file dma.h
 * @brief DMA-coherent buffer allocation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Hands out physically-contiguous buffers mapped Normal Inner/Outer
 * Non-cacheable (#PTE_NORMAL_NC_RW / MAIR Attr2) — the "map it
 * non-cacheable" side of the two standard DMA-coherency strategies. Nothing
 * is ever cached in a #dma_alloc buffer, so CPU writes are visible to a
 * DMA-capable device (and device writes visible to the CPU) with no
 * #cache_clean/#cache_invalidate call needed; that pair exists for the
 * other strategy (a caller-managed cacheable buffer, flushed by hand).
 *
 * Physical contiguity matters here in a way it doesn't for #kmalloc: a
 * DMA-capable device follows physical addresses, not the CPU's page
 * tables, so a multi-page buffer can't be assembled from scattered frames.
 *
 * VA space in the DMA window is bump-allocated and never reclaimed on
 * #dma_free (same trade-off #kheap_init already makes for the kernel
 * heap) — only the underlying physical frames and virtual mapping are
 * released. Fine for a 1 MiB window used for a handful of driver buffers
 * across a boot session.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once
#include <stdint.h>

/** Virtual base of the DMA-coherent allocation window (5 GiB mark — distinct
 *  from the kernel heap's window at the 4 GiB mark). */
constexpr uint64_t DMA_BASE = 0x140000000;

/** Size of the DMA VA window, in bytes (1 MiB, matching the kernel heap). */
constexpr uint64_t DMA_MAX_SIZE = 0x100000;

/**
 * @brief Allocate a physically-contiguous, DMA-coherent buffer.
 *
 * Rounds @p size up to a whole number of 4 KiB pages, reserves that much
 * VA space in the DMA window, allocates a matching physically-contiguous
 * run of frames via #alloc_frames, and maps it #PTE_NORMAL_NC_RW.
 *
 * Unlike #kmalloc, this returns `nullptr` on failure rather than calling
 * #panic — a failed allocation here (VA window exhausted, or no
 * sufficiently long contiguous physical run free) is a condition a caller
 * may reasonably want to handle rather than treat as fatal.
 *
 * @param size Requested size in bytes; rounded up to a whole number of pages.
 * @return Virtual address of the buffer, or `nullptr` on failure.
 */
void* dma_alloc(uint64_t size);

/**
 * @brief Release a buffer obtained from #dma_alloc.
 *
 * Unmaps the VA range and returns its underlying physical frames to the
 * PMM. Does not reclaim the VA range itself (see file header).
 *
 * @param ptr  Virtual address returned by the matching #dma_alloc call.
 * @param size The same size passed to that #dma_alloc call.
 */
void dma_free(void* ptr, uint64_t size);
