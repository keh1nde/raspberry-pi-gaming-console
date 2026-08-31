/**
 * @file memory.h
 * @brief Compatibility shim for Circle's circle/memory.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CMemorySystem is a full heap/page-allocator subsystem.
 * `MEM_KERNEL_END` (used in `assert()` sanity checks confirming a DMA
 * buffer pointer lands past the kernel image, not core logic) maps to the
 * linker-provided `__kernel_end` symbol (`kernel/pmm.h`, the same one the
 * physical memory manager places its frame bitmap after).
 *
 * `CMemorySystem::GetCoherentPage`/`COHERENT_SLOT_XHCI_START`/
 * `COHERENT_SLOT_XHCI_END` are the other symbols referenced (checked via
 * grep before writing this) — only in `xhcidevice.cpp`'s
 * `CXHCISharedMemAllocator` constructor call, to compute a fixed
 * pre-reserved memory range Circle's own allocator sub-allocates from.
 * This port's `CXHCISharedMemAllocator` (`lib/src/circle/usb/
 * xhcisharedmemallocator.cpp`, ours, not vendored) doesn't work that way —
 * it calls `dma_alloc` fresh per request instead (see that file's own
 * comment for why that's sufficient), so the constructor arguments these
 * produce are accepted but never used. Placeholder values are enough.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_memory_h
#define _circle_memory_h

#include "kernel/pmm.h"
#include <circle/types.h>

#define MEM_KERNEL_END ((uintptr) __kernel_end)

#define COHERENT_SLOT_XHCI_START 0
#define COHERENT_SLOT_XHCI_END   1

class CMemorySystem
{
public:
	static uintptr GetCoherentPage (unsigned nSlot)
	{
		(void) nSlot;
		return 0;
	}
};

#endif
