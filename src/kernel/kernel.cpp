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
 *   0. `crt_init` — runs C++ global/static constructors (.init_array).
 *      Must be first, before anything else, since generated code may
 *      assume constructors already ran.
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
#include "kernel/crt.h"

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

	crt_init();

	uart_init();
	uart_puts("UART successfully initialized.\r\n");
	uart_puts("Global/static constructors executed (.init_array).\r\n");

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

	shell_run();
}


