/**
 * @file pmm.h
 * @brief Physical Memory Manager — public API and shared constants.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * The PMM is a flat-bitmap frame allocator covering the physical range
 * `[__kernel_end, PHYS_MEM_END)`, in 4 KiB frames. The bitmap itself is
 * placed at the page-aligned `__kernel_end`; the first allocatable frame
 * sits at the next page boundary past the bitmap.
 *
 * Bit convention: bit 0 of `bitmap[0]` represents the frame at
 * `phys_mem_start`; `0` = free, `1` = used.
 *
 * Not thread-safe. The MMU module depends on this layer for backing
 * page-table memory; #pmm_init must run before #mmu_init.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef RASPBERRY_PI_OPERATING_SYSTEM_PMM_H
#define RASPBERRY_PI_OPERATING_SYSTEM_PMM_H

#include <stdint.h>
#include "kernel/io.h"
#include "kernel/board.h"


/** Frame and page size in bytes (4 KiB). */
constexpr uint64_t PAGE_SIZE = 4096;


extern "C" {
	/** Physical address of the first allocatable frame (one past the bitmap). */
	extern uint64_t phys_mem_start;

	/** Number of frames the bitmap tracks. Set by #pmm_init. */
	extern uint64_t total_frames;

	/** Linker-provided symbol marking the byte past the kernel image. */
	extern "C" uint8_t __kernel_end[];

	/** Base pointer of the allocation bitmap. Placed at page-aligned
	 *  `__kernel_end`. One bit per frame; words are 64 bits wide. */
	extern "C" uint64_t* bitmap;
}

/**
 * @brief Round @p value up to the next multiple of @p align.
 *
 * @p align must be a power of two; behavior is undefined otherwise.
 *
 * @param value Unsigned value to round.
 * @param align Power-of-two alignment, in the same units as @p value.
 * @return Smallest multiple of @p align greater than or equal to @p value.
 */
static inline uint64_t round_up(const uint64_t value, const uint64_t align) {
	return (value + align - 1) & ~(align - 1);
}

/**
 * @brief Initialize the bitmap and compute the managed-region geometry.
 *
 * Places the bitmap at the page-aligned `__kernel_end`, derives
 * `phys_mem_start` and `total_frames`, zeroes the bitmap (mark all free),
 * then marks any trailing bits past `total_frames` as used so they cannot
 * be returned by #alloc_frame. Must be called once at boot before any
 * #alloc_frame / #free_frame call.
 */
void pmm_init();

/**
 * @brief Mark the frame backing @p addr as free.
 *
 * @param addr Physical base address of the frame. Should be page-aligned and
 *             within `[phys_mem_start, PHYS_MEM_END)`. The current
 *             implementation does *not* validate these preconditions —
 *             passing a bad address can corrupt the bitmap.
 */
void free_frame(uint64_t addr);

/**
 * @brief Allocate one 4 KiB physical frame.
 *
 * Linear word-scan over the bitmap; finds the first word containing a free
 * bit, locates it with `__builtin_ctzll`, sets it, and returns the
 * corresponding physical address.
 *
 * @return Physical base address of the allocated frame, or `0` if no frame
 *         is available (out of memory).
 */
uint64_t alloc_frame();

/**
 * @brief Allocate @p count physically-contiguous 4 KiB frames.
 *
 * Unlike #alloc_frame, this scans bit-by-bit (not word-at-a-time) looking
 * for a run of @p count consecutive free bits, since a physically
 * contiguous run is the point — a DMA-capable device follows physical
 * addresses, not the CPU's page tables, so a multi-page DMA buffer can't be
 * assembled from scattered frames the way a multi-page heap allocation can.
 *
 * @param count Number of consecutive 4 KiB frames needed. `0` is invalid.
 * @return Physical base address of the first frame in the run, or `0` if no
 *         run of that length is free (including fragmented-but-sufficient
 *         total free memory — this does not compact or retry).
 */
uint64_t alloc_frames(uint64_t count);

#endif //RASPBERRY_PI_OPERATING_SYSTEM_PMM_H
