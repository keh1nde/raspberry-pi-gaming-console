/**
 * @file xhcisharedmemallocator.cpp
 * @brief CXHCISharedMemAllocator, backed by this kernel's own dma_alloc.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Not a port of Circle's own implementation — deliberately excluded from
 * vendoring from the start (see `lib/README.md`). Circle's version
 * sub-allocates a fixed pre-reserved region with a hand-rolled free list
 * that tracks each block's alignment (up to 64 bytes) and "boundary"
 * requirement (must not straddle a 4K or 64K address boundary — a real
 * xHCI spec constraint, from `xhcisharedmemallocator.h`'s own shared
 * memory requirements table).
 *
 * This kernel's `dma_alloc` (`kernel/dma.h`) already rounds every request
 * up to whole 4 KiB pages and returns a page-aligned, physically-
 * contiguous buffer. Every object in that requirements table is at most
 * 2048 bytes, and the largest (a single scratchpad buffer) is defined by
 * the xHCI spec itself as exactly one page — so a page-aligned block that
 * size or smaller can never cross a 4K or 64K boundary; 4096 divides
 * evenly into both. That means `Allocate`'s `nAlign`/`nBoundary`
 * parameters are satisfied automatically by page alignment alone — no
 * free-list, no boundary bookkeeping, no risk of getting that math wrong
 * on hardware that's expensive to debug.
 *
 * The vendored `xhcisharedmemallocator.h` this implements fixes this
 * class's private members (`m_nMemStart`/`m_nMemEnd`/`m_pFreeList`) — its
 * constructor's `nMemStart`/`nMemEnd` arguments (computed by the vendored
 * `xhcidevice.cpp` from `CMemorySystem::GetCoherentPage`, a fixed
 * pre-reserved range Circle's own allocator would sub-allocate from) are
 * accepted and stored but never used, since `Allocate` calls `dma_alloc`
 * fresh per request instead of sub-allocating from any fixed range.
 * `m_pFreeList` is repurposed: its declared type (`TXHCIBlockHeader *`) is
 * kept so the vendored header doesn't need editing, but every store/load
 * through it in this file casts to `SAllocationRecord *` instead — a
 * small intrusive-list node (kernel-heap-allocated via plain `new`, never
 * placed inside a DMA buffer, so it never disturbs the page alignment the
 * whole approach above depends on) tracking `{pointer, size}` per live
 * allocation, since `Free`/`GetFreeSpace` need the size back and
 * `dma_free` requires it explicitly (unlike Circle's own `Free`, which
 * recovers it from its block header).
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <circle/usb/xhcisharedmemallocator.h>

#include "kernel/dma.h"

namespace {

struct SAllocationRecord {
	void *pBlock;
	size_t nSize;
	SAllocationRecord *pNext;
};

inline SAllocationRecord *&RecordsHead(TXHCIBlockHeader *&pFreeList) {
	return reinterpret_cast<SAllocationRecord *&>(pFreeList);
}

} // namespace

CXHCISharedMemAllocator::CXHCISharedMemAllocator(uintptr nMemStart, uintptr nMemEnd)
:	m_nMemStart(nMemStart),
	m_nMemEnd(nMemEnd),
	m_pFreeList(nullptr) {
}

CXHCISharedMemAllocator::~CXHCISharedMemAllocator(void) {
	// Defensive cleanup only — every real caller frees each block itself
	// before the allocator is torn down. Anything still tracked here is a
	// caller bug (a leaked DMA buffer), not expected steady-state.
	SAllocationRecord *pRecord = RecordsHead(m_pFreeList);
	while (pRecord != nullptr) {
		SAllocationRecord *pNext = pRecord->pNext;
		dma_free(pRecord->pBlock, pRecord->nSize);
		delete pRecord;
		pRecord = pNext;
	}
	m_pFreeList = nullptr;
}

size_t CXHCISharedMemAllocator::GetFreeSpace(void) const {
	// Only ever used for one diagnostic log line (xhcidevice.cpp) — an
	// approximation against the DMA window's total capacity is enough,
	// not a precise accounting the way Circle's own free-list gives.
	size_t nUsed = 0;
	for (SAllocationRecord *pRecord = RecordsHead(const_cast<TXHCIBlockHeader *&>(m_pFreeList));
	     pRecord != nullptr; pRecord = pRecord->pNext) {
		nUsed += pRecord->nSize;
	}
	return nUsed > DMA_MAX_SIZE ? 0 : DMA_MAX_SIZE - nUsed;
}

void *CXHCISharedMemAllocator::Allocate(size_t nSize, size_t nAlign, size_t nBoundary) {
	// nAlign/nBoundary are unused deliberately — see file comment: every
	// dma_alloc result is page-aligned, which already satisfies both for
	// every object this driver ever requests.
	(void) nAlign;
	(void) nBoundary;

	void *pBlock = dma_alloc(nSize);
	if (pBlock == nullptr) {
		return nullptr;
	}

	SAllocationRecord *pRecord = new SAllocationRecord{pBlock, nSize, RecordsHead(m_pFreeList)};
	RecordsHead(m_pFreeList) = pRecord;

	return pBlock;
}

void CXHCISharedMemAllocator::Free(void *pBlock) {
	SAllocationRecord **ppRecord = &RecordsHead(m_pFreeList);
	while (*ppRecord != nullptr) {
		if ((*ppRecord)->pBlock == pBlock) {
			SAllocationRecord *pRecord = *ppRecord;
			*ppRecord = pRecord->pNext;
			dma_free(pRecord->pBlock, pRecord->nSize);
			delete pRecord;
			return;
		}
		ppRecord = &(*ppRecord)->pNext;
	}
	// Freeing a pointer this allocator never handed out is a caller bug;
	// nothing to do here since there's no size to recover.
}
