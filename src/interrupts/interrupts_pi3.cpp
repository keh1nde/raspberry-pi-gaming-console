/**
 * @file interrupts.cpp
 * @brief Synchronous-exception decoder and IRQ controller bring-up.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * The vector table in `vector_table.S` calls into the two `extern "C"`
 * entry points defined here. Synchronous exceptions are treated as fatal
 * diagnostics: decode ESR_EL1 (EC, ISS, DFSC), dump FAR_EL1 / ELR_EL1,
 * and halt. IRQs currently service the per-core ARM Generic Timer only;
 * UART RX is wired on the device side but masked at the IRQ controller
 * to avoid a storm (see CLAUDE.md known issues).
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D1 (Exception model)
 *   - BCM2835 ARM Peripherals, §7 (Interrupts)
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

#include "interrupts.h"

#include <stdint.h>

#include "uart.h"
#include "timer.h"

/** Peripheral and IRQ-controller register addresses. */
enum {
	TIMER_BASE = 0x3F003000,

	TIMER_CS = (TIMER_BASE + 0x0),
	TIMER_CLO = (TIMER_BASE + 0x4),
	TIMER_CHI = (TIMER_BASE + 0x8),

	TIMER_C0 = (TIMER_BASE + 0xC),
	TIMER_C1 = (TIMER_BASE + 0x10),
	TIMER_C2 = (TIMER_BASE + 0x14),
	TIMER_C3 = (TIMER_BASE + 0x18),

	IRQ_BASE = 0x3F00B000,
	IRQ_BASIC = (IRQ_BASE + 0x200),
	IRQ_P1 = (IRQ_BASE + 0x204),
	IRQ_P2 = (IRQ_BASE + 0x208),
	IRQ_FC = (IRQ_BASE + 0x20C), // The IRQ pending register
	IRQ_EN1 = (IRQ_BASE + 0x210),
	IRQ_EN2 = (IRQ_BASE + 0x214),
	IRQ_BEN = (IRQ_BASE + 0x218),
	IRQ_1DS = (IRQ_BASE + 0x21C),
	IRQ_2DS = (IRQ_BASE + 0x220),
	IRQ_BDS = (IRQ_BASE + 0x224),

	IRQ_ARM_BASE = 0x40000000,
	IRQ_EN_C0 = 0x40000040,
	IRQ_C0_SOURCE = 0x40000060,
};

extern "C" void handle_synchronous_interrupts() {
	// Snapshot the exception-state registers up front so any subsequent
	// UART activity does not perturb FAR_EL1 / ELR_EL1.
	uint64_t esr, far, elr;
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
			uart_puts("Instruction Abort from a lower EL.");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			while (true) {}
		case 0x21:
			uart_puts("Instruction Abort from the same EL.\n");
			uart_puts("There is likely no VA to PA mapping, or you did it wrong.\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_put_hex(far);
			while (true) {}
		case 0x24:
			uart_puts("Data Abort from a lower EL \n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			uart_put_hex(far);
			while (true) {}
		case 0x25:
			uart_puts("Data Abort from the same EL \n");
			uart_puts("Kernel did a load/store to an unmapped or wrong-perm VA.\n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			while (true) {}
		case 0x0E:
			uart_puts("Illegal execution state \n");
			uart_puts("FAR_EL1: ");
			uart_put_hex(far);
			uart_puts("\nDFSC: ");
			uart_put_hex(dfsc);
			while (true) {}
		case 0x15:
			uart_puts("SVC from AArch64: System call. \n");

	}
}

extern "C" void interrupt_init() {
	// Mask every peripheral IRQ at the BCM2835 controller before enabling
	// the specific lines the kernel wants. Cleaner than relying on reset
	// state and avoids spurious IRQs during bring-up.
	mmio_write(IRQ_1DS, 0xFFFFFFFF);
	mmio_write(IRQ_2DS, 0xFFFFFFFF);
	mmio_write(IRQ_BDS, 0xFFFFFFFF);

	// Enable ARM Timer (basic IRQ 0) on the BCM2835 side. UART RX (IRQ_EN2
	// bit 25) is intentionally left masked — see CLAUDE.md known issues.
	mmio_write(IRQ_BEN, 1 << 0);
	mmio_write(IRQ_EN_C0, (1 << 1)); // Per-core timer source on the ARM local block.

	// Enable UART interrupts
	mmio_write(IRQ_EN2, (1 << 25));
}

extern "C" void handle_interrupt_requests() {
	uint64_t irq_pend_status;
	irq_pend_status = mmio_read(IRQ_C0_SOURCE);

	// UART interrupt handling.
	// If UART bit is read, then handler is called from uart.cpp
	if (mmio_read(IRQ_P2) & (1 << 25) ) {
		uart_handle_irq();

	}

	// Per-core timer IRQ: reload CNTP_TVAL_EL0 for the next tick, then
	// advance and print the in-kernel clock.
	uint64_t freq = get_freq();
	if (irq_pend_status & (1 << 1)) {
		asm volatile("msr CNTP_TVAL_EL0, %0" :: "r"(freq / 10));
		increment_time();

		// The timer API is now in charge of printing time when called.
		// As a result the bottom has been temporarily removed.

		// print_time();
	}
}
