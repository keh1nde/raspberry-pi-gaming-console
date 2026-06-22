/**
 * @file interrupts.h
 * @brief Exception and IRQ handler entry points + controller bring-up.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Declarations have C linkage because the vector table in
 * `src/interrupts/vector_table.S` calls into these handlers and the assembly
 * cannot tolerate C++ name mangling.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D1 (Exception model)
 *   - BCM2835 ARM Peripherals, §7 (Interrupts)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>

/**
 * @brief Synchronous-exception dispatcher.
 *
 * Called from the `sync_handler` stub in the vector table after general
 * register state has been saved. Decodes ESR_EL1 (EC, ISS, DFSC) and
 * dumps FAR_EL1 / ELR_EL1 over UART before halting. Intended as a
 * panic/diagnostic path; does not return for fault-class exceptions.
 */
extern "C" void handle_synchronous_interrupts();

/**
 * @brief IRQ dispatcher.
 *
 * Called from the `irq_handler` stub in the vector table after general
 * register state has been saved. Reads the per-core IRQ pending register
 * and dispatches to the appropriate sub-handler (currently: ARM Generic
 * Timer only).
 */
extern "C" void handle_interrupt_requests();

/**
 * @brief Program the BCM2835 IRQ controller and the ARM local timer source.
 *
 * Disables all peripheral IRQs, then enables ARM Timer (basic IRQ 0) on the
 * BCM2835 side and timer IRQ on the per-core ARM local controller. Must be
 * called after the vector table has been installed (boot.S handles that
 * via `VBAR_EL1`) and before IRQs are unmasked.
 */
extern "C" void interrupt_init();

/**
 * @brief Unexpected/fatal exception handler.
 *
 * Prints the decoded EC plus FAR_EL1/ELR_EL1 over UART, then halts. Used
 * for the synchronous default case and as the shared diagnostic for any
 * vector table entry that should never legitimately fire.
 */
extern "C" [[noreturn]] void handle_unexpected(uint64_t ec, uint64_t far, uint64_t elr);

/**
 * @brief SError (asynchronous abort) handler.
 *
 * SError syndrome is not guaranteed to carry a valid FAR_EL1, and the
 * faulting instruction may already be gone by the time it is reported, so
 * this never attempts to resume — it logs ESR_EL1/ELR_EL1 and halts.
 */
extern "C" [[noreturn]] void handle_serror();

/**
 * @brief Diagnostic handler for an unexpected FIQ.
 *
 * The GIC-400 only routes Group 0 interrupts to FIQ, and nothing is
 * currently configured as Group 0, so this should never fire. ESR_EL1 is
 * not valid for FIQ (it is not a synchronous trap), so only ELR_EL1 is
 * logged before halting.
 */
extern "C" [[noreturn]] void handle_unexpected_fiq();