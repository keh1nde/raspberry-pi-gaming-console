/**
 * @file heap_alloc.h
 * @brief Kernel heap — bump allocator with lazy-mapped backing pages.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Reserves the virtual range `[HEAP_BASE, HEAP_BASE + HEAP_MAX_SIZE)`
 * (1 MiB at the 4 GiB mark) by convention; physical frames are pulled from
 * the PMM and mapped only as the bump pointer crosses page boundaries.
 * 16-byte aligned. No `kfree`: memory is freed at reboot. This region is the
 * intended substrate for a future slab/free-list allocator.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef RASPBERRY_PI_OPERATING_SYSTEM_HEAP_ALLOC_H
#define RASPBERRY_PI_OPERATING_SYSTEM_HEAP_ALLOC_H

#include <stdint.h>
#include <stdint.h>


#include "pmm.h"
#include "mmu.h"
#include "uart.h"


/** Virtual base address of the kernel heap (4 GiB mark). */
constexpr uint64_t HEAP_BASE = 0x100000000;

/** Maximum heap region size, in bytes (1 MiB). */
constexpr uint64_t HEAP_MAX_SIZE = 0x100000;

/** Allocation alignment guarantee. Every #kmalloc result is a multiple of this. */
constexpr uint64_t HEAP_ALIGN = 16;

/**
 * Bump pointer — next byte to allocate. Always 16-byte aligned and lies in
 * `[HEAP_BASE, heap_end]`. Declared `inline` so multiple translation units
 * including this header don't cause a multiple-definition link error.
 */
inline uint64_t bump_ptr;

/**
 * End of currently-mapped heap region. Always page-aligned. Every VA in
 * `[HEAP_BASE, bump_ptr)` is backed by a real physical frame.
 */
inline uint64_t heap_end;

/**
 * @brief Initialize the heap. Sets `bump_ptr` and `heap_end` to `HEAP_BASE`.
 *
 * Must be called once at boot after #mmu_init.
 */
void kheap_init();

/**
 * @brief Allocate @p size bytes of zero-uninitialized kernel memory.
 *
 * Rounds @p size up to a 16-byte multiple, then lazy-maps fresh physical
 * frames into the heap VA window until the requested region is backed.
 * Calls #panic on out-of-memory (either VA region exhausted or PMM empty).
 *
 * @param size Requested size in bytes.
 * @return 16-byte-aligned pointer to the allocated region. Never `nullptr`
 *         (failure path goes through #panic).
 */
void* kmalloc(uint64_t size);

/**
 * @brief Print @p msg over UART and halt the CPU in a `wfe` loop.
 *
 * Used as the fatal-error path for unrecoverable conditions (OOM, heap
 * cap exceeded). Does not return.
 *
 * @param msg Null-terminated message to emit before halting.
 */
[[noreturn]] void panic(const char* msg);

#endif //RASPBERRY_PI_OPERATING_SYSTEM_HEAP_ALLOC_H
