/**
 * @file cache.cpp
 * @brief D-cache maintenance-by-VA primitives.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * `dc cvac`/`dc ivac` operate by virtual address and are unrestricted at
 * EL1 (SCTLR_EL1.UCI only gates EL0 use, which this kernel never grants).
 * Cache line size comes from CTR_EL0.DminLine rather than being hardcoded,
 * since it's an implementation-defined field.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D12.2 (Cache maintenance)
 *   - Arm Architecture Reference Manual for A-profile, §D17.2.32 (CTR_EL0)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "kernel/cache.h"
#include "kernel/barrier.h"

/** Read the D-cache line size from CTR_EL0.DminLine, in bytes. */
static uint64_t cache_line_size() {
	uint64_t ctr;
	asm volatile("mrs %0, CTR_EL0" : "=r"(ctr));
	const uint64_t dminline = (ctr >> 16) & 0xF; // CTR_EL0[19:16].
	return 4ULL << dminline; // Line length in words (2^DminLine) x 4 bytes.
}

void cache_clean(const void* addr, const uint64_t size) {
	const uint64_t line = cache_line_size();
	const uint64_t start = reinterpret_cast<uint64_t>(addr) & ~(line - 1);
	const uint64_t end = (reinterpret_cast<uint64_t>(addr) + size + line - 1) & ~(line - 1);

	for (uint64_t va = start; va < end; va += line) {
		asm volatile("dc cvac, %0" :: "r"(va) : "memory");
	}
	dsb_sy();
}

void cache_invalidate(const void* addr, const uint64_t size) {
	const uint64_t line = cache_line_size();
	const uint64_t start = reinterpret_cast<uint64_t>(addr) & ~(line - 1);
	const uint64_t end = (reinterpret_cast<uint64_t>(addr) + size + line - 1) & ~(line - 1);

	for (uint64_t va = start; va < end; va += line) {
		asm volatile("dc ivac, %0" :: "r"(va) : "memory");
	}
	dsb_sy();
}
