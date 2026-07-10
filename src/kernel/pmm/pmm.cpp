/**
 * @file pmm.cpp
 * @brief Physical Memory Manager — flat-bitmap frame allocator.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Bit convention: bit 0 of `bitmap[0]` represents the frame at
 * `phys_mem_start`; `0` = free, `1` = used. The bitmap itself is placed at
 * the page-aligned `__kernel_end` and tracks every 4 KiB frame in
 * `[phys_mem_start, PHYS_MEM_END)`. Not thread-safe.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "kernel/pmm.h"
#include "kernel/spinlock.h"

/** Allocation bitmap, located at the page-aligned `__kernel_end`. */
uint64_t* bitmap;

/** Physical address of frame 0. First page past the bitmap. */
uint64_t phys_mem_start = 0;

/** Number of frames the bitmap covers. */
uint64_t total_frames = 0;

static spinlock pmm_lock = {0};

void pmm_init() {
	pmm_lock.spin_lock = SPINLOCK_FREE;
	// Bitmap base: round __kernel_end up to a page boundary so it starts
	// at a clean address regardless of how the linker laid out .bss.
	const uint64_t bitmap_addr = round_up(reinterpret_cast<uint64_t>(__kernel_end), PAGE_SIZE);

	// Provisional frame count over the *full* managed range. The real
	// allocatable count (`total_frames`) is recomputed below once the
	// bitmap's own footprint has been carved out.
	const uint64_t managed_bytes = PHYS_MEM_END - bitmap_addr;
	const uint64_t max_frames = managed_bytes / PAGE_SIZE;

	// Bitmap size, rounded up to whole 64-bit words.
	const uint64_t bitmap_bytes = (max_frames + 63) / 64 * 8;

	bitmap = reinterpret_cast<uint64_t *>(bitmap_addr);

	// Shrink the allocatable region to start past the bitmap so the
	// allocator cannot hand out frames that overlap its own metadata.
	phys_mem_start = round_up(bitmap_addr + bitmap_bytes, PAGE_SIZE);

	total_frames = (PHYS_MEM_END - phys_mem_start) / PAGE_SIZE;

	// Mark all frames free.
	for (int i = 0; i < bitmap_bytes / 8; ++i) {
		bitmap[i] = 0;
	}

	// The final word may cover frames past total_frames; mark those bits
	// used so #alloc_frame's `__builtin_ctzll(~word)` cannot return them.
	const uint64_t last_word = total_frames / 64;

	if (const uint64_t valid_bits = total_frames % 64; valid_bits != 0) {
		const uint64_t mask = ~((1ULL << valid_bits) - 1);
		bitmap[last_word] |= mask;
	}

}

uint64_t alloc_frame() {
#ifdef TRACE
	static uint64_t count = 0;
#endif

	uint64_t flags;
	spin_lock(pmm_lock, flags);

	for (int i = 0; i < (total_frames + 63) / 64; ++i) {
		if (bitmap[i] != ~0ULL) {
			const uint64_t bit_index = __builtin_ctzll(~bitmap[i]);
			const uint64_t idx = i * 64 + bit_index;
			bitmap[i] |= (1ULL << bit_index);
			const uint64_t address = phys_mem_start + idx * PAGE_SIZE;

			spin_unlock(pmm_lock, flags);

#ifdef TRACE
			count++;
			uart_puts("alloc_frame #");
			uart_put_uint(count);
			uart_puts(" → ");
			uart_put_hex(address);
			uart_puts("\n");
#endif
			return address;
		}
	}

	spin_unlock(pmm_lock, flags);
	return 0; // Out of memory.
}

void free_frame(const uint64_t addr) {
	if (addr < phys_mem_start || addr >= PHYS_MEM_END) return;
	if (addr % PAGE_SIZE != 0) return;

	uint64_t frame_index = (addr - phys_mem_start) / PAGE_SIZE;
	if (frame_index >= total_frames) return;

	const uint64_t word_index = frame_index / 64;

	uint64_t flags;
	spin_lock(pmm_lock, flags);
	if (const uint64_t bit_index = frame_index % 64; (bitmap[word_index] & (1ULL << bit_index)) != 0) {
		bitmap[word_index] &= ~(1ULL << bit_index);
	}
	spin_unlock(pmm_lock, flags);
}
