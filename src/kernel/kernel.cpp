/**
 * @file kernel.cpp
 * @brief Kernel entry point — initializes subsystems and hands off to the shell.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Called from `src/boot.S` after the boot stub has dropped to EL1, set up
 * the stack, zeroed BSS, installed the vector table, and unmasked IRQs.
 * Initialization order is load-bearing:
 *   1. `mmu_init` (which internally runs `pmm_init`) — switches the kernel
 *      onto virtual addressing.
 *   2. `kheap_init` — depends on the MMU for lazy mapping.
 *   3. `fs_init` — depends on the heap.
 *   4. `interrupt_init` — programs the IRQ controller; vector table is
 *      already in place via boot.S.
 *   5. `shell_run` — does not return.
 *
 * The timer is intentionally not started here: its IRQ handler emits an
 * uptime message that would interleave with the shell prompt. Re-enable
 * `timer_init` in non-interactive demos.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <stddef.h>
#include <stdint-gcc.h>
#include <stdint.h>

#include "kernel/heap_alloc.h"
#include "kernel/uart.h"
#include "kernel/interrupts.h"
#include "kernel/mmu.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/filesystem.h"
#include "kernel/shell.h"
#include "kernel/spinlock.h"
#include "kernel/dma.h"
#include "kernel/cache.h"

extern "C" int64_t psci_cpu_on(uint64_t mpidr, uint64_t entry, uint64_t context_id);

extern "C" void secondary_entry();

extern "C" void secondary_main() {
	uint64_t core_id;
	asm volatile("mrs %0, TPIDR_EL1" : "=r"(core_id));

	uart_puts("Core ");
	uart_put_uint(core_id);
	uart_puts(" has been turned on.\r\n");

	for (;;) {
		asm volatile("wfe");
	}
}

/**
 * @brief One-shot sanity check for the DMA/cache primitives (barrier.h,
 * cache.h, dma.h), run once from kernel_main before the shell starts.
 *
 * Nothing in the kernel calls dma_alloc/cache_clean/cache_invalidate yet —
 * there's no driver to exercise them — so this exists purely to catch a
 * broken primitive (bad address math, wrong cache-line size, a mapping that
 * doesn't tear down) before Phase 2 builds a real DMA-capable driver on top
 * of them. Not a substitute for testing against an actual device: there's
 * no second observer here to prove cross-agent (CPU vs. device) visibility,
 * only that these functions don't fault and don't corrupt data.
 */
static void dma_cache_smoke_test() {
	uart_puts("--- DMA/cache smoke test ---\r\n");

	// Two pages, not one: exercises alloc_frames' contiguity guarantee and
	// map()'s multi-page path, not just the trivial single-frame case.
	void* buf = dma_alloc(2 * PAGE_SIZE);
	if (!buf) {
		uart_puts("FAIL: dma_alloc returned nullptr\r\n");
		return;
	}
	uart_puts("dma_alloc(2 pages) -> VA ");
	uart_put_hex(reinterpret_cast<uint64_t>(buf));
	uart_puts("\r\n");

	// Write a pattern across both pages through the VA and read it back —
	// confirms the Non-cacheable mapping still behaves like ordinary memory.
	volatile auto* bytes = static_cast<volatile uint8_t*>(buf);
	for (uint64_t i = 0; i < 2 * PAGE_SIZE; i++) {
		bytes[i] = static_cast<uint8_t>(i & 0xFF);
	}
	bool readback_ok = true;
	for (uint64_t i = 0; i < 2 * PAGE_SIZE; i++) {
		if (bytes[i] != static_cast<uint8_t>(i & 0xFF)) { readback_ok = false; break; }
	}
	uart_puts(readback_ok ? "PASS: readback matches pattern\r\n" : "FAIL: readback mismatch\r\n");

	// translate() the first byte of each page: the two physical addresses
	// must be exactly one page apart, proving alloc_frames returned a truly
	// contiguous run and map() laid the two pages out correctly.
	const uint64_t phys0 = translate(reinterpret_cast<uint64_t>(buf));
	const uint64_t phys1 = translate(reinterpret_cast<uint64_t>(buf) + PAGE_SIZE);
	uart_puts("phys page 0: "); uart_put_hex(phys0); uart_puts("\r\n");
	uart_puts("phys page 1: "); uart_put_hex(phys1); uart_puts("\r\n");
	uart_puts((phys1 == phys0 + PAGE_SIZE) ? "PASS: physically contiguous\r\n" : "FAIL: not contiguous\r\n");

	// cache_clean/cache_invalidate on dma_alloc's own buffer would prove
	// nothing (it's Non-cacheable by construction, so both are no-ops
	// there) — exercise them on an ordinary, cacheable kmalloc buffer
	// instead, the strategy they actually exist for.
	auto* cbuf = static_cast<uint8_t*>(kmalloc(256));
	for (int i = 0; i < 256; i++) cbuf[i] = static_cast<uint8_t>(i);
	cache_clean(cbuf, 256);
	cache_invalidate(cbuf, 256);
	bool cache_ok = true;
	for (int i = 0; i < 256; i++) {
		if (cbuf[i] != static_cast<uint8_t>(i)) { cache_ok = false; break; }
	}
	uart_puts(cache_ok ? "PASS: cache_clean/invalidate round-trip intact\r\n" : "FAIL: cache round-trip corrupted data\r\n");

	// Free and confirm the mapping is really gone.
	dma_free(buf, 2 * PAGE_SIZE);
	const uint64_t phys_after_free = translate(reinterpret_cast<uint64_t>(buf));
	uart_puts((phys_after_free == ~0ULL) ? "PASS: dma_free tore down the mapping\r\n" : "FAIL: mapping still resolves after dma_free\r\n");

	uart_puts("--- DMA/cache smoke test done ---\r\n");
}

/**
 * @brief C++ entry point. Called from `_start` in `boot.S`.
 *
 * The three register-passed parameters are AArch32-era boot ABI artifacts
 * (machine ID and ATAGs pointer); they are unused under the AArch64 boot
 * flow and explicitly silenced.
 *
 * @param r0    Unused. Bootloader-passed.
 * @param r1    Unused. Bootloader-passed.
 * @param atags Unused. Bootloader-passed.
 */
extern "C" void kernel_main(uint32_t r0, uint32_t r1, uint32_t atags)
{
	(void) r0;
	(void) r1;
	(void) atags;

	uart_init();
	uart_puts("UART successfully initialized.\r\n");

	mmu_init();
	uart_puts("Page Frame Allocator and MMU initialized.\r\n");

	kheap_init();
	uart_puts("Heap allocator initialized.\r\n");

	fs_init();
	uart_puts("fs_init done.\r\n");

	timer_init();
	interrupt_init();

	// Activate extra CPU cores

	for (int i = 1; i < 4; i++) {
		uint64_t stack_top = reinterpret_cast<uint64_t>(kmalloc(4096)) + 4096;
		psci_cpu_on(
			i,
			reinterpret_cast<uint64_t>(&secondary_entry),
			stack_top
			);
	}

	dma_cache_smoke_test();

	shell_run();
}


