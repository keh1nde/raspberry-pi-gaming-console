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
#include <stdint.h>

#include "kernel/heap_alloc.h"
#include "kernel/uart.h"
#include "kernel/interrupts.h"
#include "kernel/mmu.h"
#include "kernel/pmm.h"
#include "kernel/timer.h"
#include "kernel/filesystem.h"
#include "kernel/shell.h"

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

	shell_run();
}
