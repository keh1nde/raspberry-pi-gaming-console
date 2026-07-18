/**
 * @file interrupts.cpp
 * @brief Synchronous-exception decoder and GIC-400 IRQ controller bring-up.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * The vector table in `vector_table.S` calls into the two `extern "C"`
 * entry points defined here. Synchronous exceptions are treated as fatal
 * diagnostics: decode ESR_EL1 (EC, ISS, DFSC), dump FAR_EL1 / ELR_EL1,
 * and halt. IRQs currently service the per-core ARM Generic Timer only
 * (GIC-400 PPI 30); UART RX interrupt is deferred pending SPI ID lookup.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D1 (Exception model)
 *   - ARM Generic Interrupt Controller Architecture Specification, GICv2 (IHI0048B)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

/*
* EC reference (bits [31:26] of ESR_EL1):
*   0x00: unknown
*   0x01: trapped WFI/WFE
*   0x0E: illegal execution state
*   0x15: SVC from AArch64 (syscall)
*   0x20: instruction abort from lower EL
*   0x21: instruction abort from same EL
*   0x24: data abort from lower EL
*   0x25: data abort from same EL
*   0x2C: SP alignment fault
*   0x3C: BRK instruction (debug breakpoint)
*/

#include "kernel/interrupts.h"

#include <stdint.h>

#include "kernel/uart.h"
#include "kernel/timer.h"
#include "kernel/board.h"

/** GIC-400 and peripheral register addresses. */
enum : uint64_t {
	TIMER_BASE = 0x400ac000,

	GICD_CTLR        = GICD_BASE + 0x000,
	GICD_ISENABLER0  = GICD_BASE + 0x100,
	GICD_IPRIORITYR7 = GICD_BASE + 0x41C,

	GICC_CTLR = GICC_BASE + 0x000,
	GICC_PMR  = GICC_BASE + 0x004,
	GICC_IAR  = GICC_BASE + 0x00C,
	GICC_EOIR = GICC_BASE + 0x010,
};

extern "C" void interrupt_init() {
	// Disable all interrupts by writing 0 to bits 0 and 1
	mmio_write(GICD_CTLR, 0);

	// Personal Note:
	// GICD_IPRIORITY7 controls priority for interrupt IDs 28-31
	// Implement logic for setting and handling this register once
	// the need arises. For now, the register will remain unset.

	// Enable the physical timer
	mmio_write(GICD_ISENABLER0, mmio_read(GICD_ISENABLER0) | (1 << 30));

	// Re-enable distributor
	mmio_write(GICD_CTLR, 1);

	// Pass all priorities to CPU
	mmio_write(GICC_PMR, 0xFF);

	// Enable the CPU interface
	mmio_write(GICC_CTLR, 1);
}

extern "C" void handle_synchronous_interrupts() {
	// Snapshot the exception-state registers up front so any subsequent
	// UART activity does not perturb FAR_EL1 / ELR_EL1.
	uint64_t esr;
	uint64_t far;
	uint64_t elr;
	asm volatile("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile("mrs %0, FAR_EL1" : "=r"(far));
	asm volatile("mrs %0, ELR_EL1" : "=r"(elr));

	// Decode ESR_EL1: EC = exception class (bits [31:26]),
	// ISS = instruction-specific syndrome, DFSC = data-fault status code.
	uint64_t ec = (esr >> 26) & 0x3F;
	uint64_t iss = esr & 0x1FFFFFF;
	uint64_t dfsc = iss & 0x3F;

	switch (ec) {
		case 0x20:
			uart_puts("Instruction Abort from a lower EL.\r\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\r\n");
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_puts("\r\n");
			while (true) {}
		case 0x21:
			uart_puts("Instruction Abort from the same EL.\r\n");
			uart_puts("There is likely no VA to PA mapping, or you did it wrong.\r\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\r\n");
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_puts("\r\n");
			uart_put_hex(far);
			uart_puts("\r\n");
			while (true) {}
		case 0x24:
			uart_puts("Data Abort from a lower EL \r\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\r\n");
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_puts("\r\n");
			uart_put_hex(far);
			uart_puts("\r\n");
			while (true) {}
		case 0x25:
			uart_puts("Data Abort from the same EL \r\n");
			uart_puts("Kernel did a load/store to an unmapped or wrong-perm VA.\r\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\r\n");
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_puts("\r\n");
			while (true) {}
		case 0x0E:
			uart_puts("Illegal execution state \n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\r\n");
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_puts("\r\n");
			while (true) {}
		case 0x15:
			uart_puts("SVC from AArch64: System call. \n");
			break;
		default:
			handle_unexpected(ec, far, elr);
	}
}

extern "C" void handle_interrupt_requests() {
	uint64_t iar = mmio_read(GICC_IAR);
	uint64_t int_id = iar & 0x3FF;

	if (int_id == 30) {
		uint64_t freq = get_freq();
		asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(freq / 10));
		increment_time();
	}

	mmio_write(GICC_EOIR, iar);
}

extern "C" [[noreturn]] void handle_serror() {
	// FAR_EL1 is not guaranteed valid for SError, so it is deliberately not
	// read here. ESR_EL1 still carries the SError syndrome.
	uint64_t esr, elr;
	asm volatile("mrs %0, ESR_EL1" : "=r"(esr));
	asm volatile("mrs %0, ELR_EL1" : "=r"(elr));

	uart_puts("An SError (asynchronous abort) has occurred.\r\n");
	uart_puts("ESR_EL1: "); uart_put_hex(esr); uart_puts("\r\n");
	uart_puts("ELR_EL1: "); uart_put_hex(elr); uart_puts("\r\n");

	while (true) {
		asm volatile("wfi");
	}
}

extern "C" [[noreturn]] void handle_unexpected(uint64_t ec, uint64_t far, uint64_t elr) {
	uart_puts("An unexpected error has occurred: \r\n");
	uart_puts("EC: "); uart_put_hex(ec); uart_puts("\r\n");
	uart_puts(" FAR: "); uart_put_hex(far); uart_puts("\r\n");
	uart_puts(" ELR: "); uart_put_hex(elr); uart_puts("\r\n");

	while (true) {
		asm volatile("wfi");
	}
}

extern "C" [[noreturn]] void handle_unexpected_fiq() {
	// FIQ is not a synchronous trap, so ESR_EL1 is not updated for it and
	// carries no meaningful syndrome here — only ELR_EL1 is worth reading.
	uint64_t elr;
	asm volatile("mrs %0, ELR_EL1" : "=r"(elr));

	uart_puts("Unexpected FIQ: the GIC delivered a Group 0 interrupt but none are configured.\r\n");
	uart_puts("ELR_EL1: "); uart_put_hex(elr); uart_puts("\r\n");

	while (true) {
		asm volatile("wfi");
	}
}