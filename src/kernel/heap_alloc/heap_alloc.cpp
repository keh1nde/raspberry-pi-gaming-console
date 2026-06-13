/**
 * @file heap_alloc.cpp
 * @brief Bump heap allocator with lazy page-backed VA mapping.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * The heap reserves a fixed virtual range and grows on demand: each
 * #kmalloc that advances `bump_ptr` past `heap_end` pulls fresh physical
 * frames from the PMM and maps them in. Allocation-only (no `kfree`);
 * memory is reclaimed at reboot.
 *
 * Invariants:
 *   - `bump_ptr` is always 16-byte aligned.
 *   - Every VA in `[HEAP_BASE, bump_ptr)` is backed by a real physical frame.
 *   - `heap_end` is page-aligned and always `>= bump_ptr`.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "heap_alloc.h"

void kheap_init() {
	bump_ptr = HEAP_BASE;
	heap_end = HEAP_BASE; // No frames mapped yet.
}

void *kmalloc(const uint64_t size) {
	// Round to the 16-byte alignment guarantee.
	const uint64_t aligned = (size + 15) & ~15ULL;
	const uint64_t required_end = bump_ptr + aligned;

	if (required_end > HEAP_BASE + HEAP_MAX_SIZE) {
		panic("kmalloc: heap region exhausted");
	}

	// Lazy-map physical frames until the requested span is backed.
	while (heap_end < required_end) {
		uint64_t pa = alloc_frame();
		if (pa == 0) panic("kmalloc: out of physical frames");

		map(heap_end, pa, PAGE_SIZE, PTE_NORMAL_RW);
		heap_end += PAGE_SIZE;
	}

	uint64_t result = bump_ptr;
	bump_ptr += aligned;

	return reinterpret_cast<void*>(result);
}

[[noreturn]] void panic(const char* msg) {
	uart_puts(msg);
	for (;;) asm volatile("wfe");
}
