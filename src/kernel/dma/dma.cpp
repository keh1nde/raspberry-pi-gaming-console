/**
 * @file dma.cpp
 * @brief DMA-coherent buffer allocation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Locking note: `dma_lock` only ever guards the VA bump-pointer critical
 * section below — it is never held while calling #alloc_frames (PMM) or
 * #map (MMU, which itself nests into the PMM for page-table frames). It
 * therefore never participates in the FS → heap → MMU → PMM lock-ordering
 * chain documented for the rest of the kernel; it stands alone.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "kernel/dma.h"
#include "kernel/mmu.h"
#include "kernel/pmm.h"
#include "kernel/spinlock.h"

static uint64_t dma_bump_ptr = DMA_BASE;
static spinlock dma_lock = {0};

void* dma_alloc(const uint64_t size) {
	const uint64_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	const uint64_t aligned_size = page_count * PAGE_SIZE;

	uint64_t irq_flags;
	spin_lock(dma_lock, irq_flags);

	if (dma_bump_ptr + aligned_size > DMA_BASE + DMA_MAX_SIZE) {
		spin_unlock(dma_lock, irq_flags);
		return nullptr;
	}
	const uint64_t va = dma_bump_ptr;
	dma_bump_ptr += aligned_size;

	spin_unlock(dma_lock, irq_flags);

	const uint64_t phys = alloc_frames(page_count);
	if (phys == 0) return nullptr; // VA reservation is leaked; see file header.

	map(va, phys, aligned_size, PTE_NORMAL_NC_RW);

	return reinterpret_cast<void*>(va);
}

void dma_free(void* ptr, const uint64_t size) {
	const uint64_t va = reinterpret_cast<uint64_t>(ptr);
	const uint64_t page_count = (size + PAGE_SIZE - 1) / PAGE_SIZE;
	const uint64_t aligned_size = page_count * PAGE_SIZE;

	const uint64_t phys = translate(va);
	if (phys == ~0ULL) return; // Not a live mapping; nothing to free.

	unmap(va, aligned_size);

	for (uint64_t i = 0; i < page_count; i++) {
		free_frame(phys + i * PAGE_SIZE);
	}
}
