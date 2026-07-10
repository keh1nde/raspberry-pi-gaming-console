// @file boot.S
// @brief Kernel entry point — parks secondary cores, drops to EL1, jumps to C++.
//
// Part of raspberry-pi-gaming-console, a retro gaming console OS.
// Built on the kehinde-kernel project, originally MIT-licensed.
//
// The Raspberry Pi firmware loads `kernel8.img` at physical 0x80000 and
// drops every core into `_start` in EL2. Sequence:
//   1. Read MPIDR_EL1 — if not core 0, halt in `wfe`.
//   2. Configure CNTHCTL_EL2 (allow EL1 timer access), CNTVOFF_EL2,
//      HCR_EL2 (AArch64 at EL1).
//   3. ERET to EL1 with DAIF masked, SP_EL1 initialized to 0x80000
//      (stack grows down).
//   4. Zero `[__bss_start, __bss_end)` 16 bytes at a time.
//   5. Install `vector_table` into VBAR_EL1.
//   6. Unmask IRQs (DAIFClr #2 — clears I-bit only; F/A/D remain masked).
//   7. Branch to `kernel_main`.
//
// References:
//   - Arm Architecture Reference Manual for A-profile, §D1 (Exception model)
//   - Arm Power State Coordination Interface - Platform Design Document, Sections
//     §3 (Assumptions and Recommendations), §4 (PSCI Use Cases and Use Case Requirements)
//     and §5 (Functions), particularly §5.1 and §5.6 (Function Prototypes and CPU_ON)
//   - ARM SMC Calling Convention (DEN0028)
//   - Trusted Firmware-A Documentation
//   - Arm A-profile Architecture Registers
//      - MPIDR, Multiprocessor Affinity Register
//      - TPIDR_EL1, EL1 Software Thread ID Register
//
// @author Kehinde Adeoso
// SPDX-License-Identifier: GPL-3.0-only

.section ".text.boot"

.global _start
.extern vector_table

_start:
    // On the Pi 5, secondary-core parking is handled by the Power State
    // Coordination Interface (PSCI), so the Pi 3-style MPIDR core check is
    // unnecessary. When multicore support lands, the other three cores will
    // be activated here.

    mrs x0, MPIDR_EL1
    and x0, x0, #0xFF
    cbnz x0, halt

    b level_init

level_init:
    // Check if we are already in EL1, if so, skip to el1_entry
    mrs x0, CurrentEL
    lsr x0, x0, #2
    cmp x0, #2
    bne el1_entry

    // Allow EL1 to read and program the physical timer registers.
    mov     x4, #0x3
    msr     CNTHCTL_EL2, x4

    // Pin virtual time equal to physical time.
    msr     CNTVOFF_EL2, xzr

    // HCR_EL2.RW = 1: EL1 will run in AArch64.
    mov     x4, #(1 << 31)
    msr     HCR_EL2, x4

    // SPSR_EL2: target EL1h with DAIF masked (0x3C0 | 0x5 = 0x3C5).
    mov     x4, #0x3C5
    msr     SPSR_EL2, x4

    // ELR_EL2: address the ERET will jump to.
    adr     x4, el1_entry
    msr     ELR_EL2, x4

    eret

el1_entry:
    // We explicitly set the current stack pointer to SP_EL1
    // to avoid double exceptions on early-boot.
    msr SPSel, #1

    // Clear OS Lock so external debuggers can read CPU state (ELR_EL1, ESR_EL1, etc.)
    // via the EDSCR/EDITR interface. Set by hardware at reset; must be cleared by software.
    msr OSLAR_EL1, xzr
    isb

    // Disable the MMU to avoid premature address translation.
    mrs x0, SCTLR_EL1
    bic x0, x0, #1
    msr SCTLR_EL1, x0
    isb

    // SP_EL1 grows downward from 0x80000 (where the kernel was loaded).
    ldr     x1, =_start
    mov     sp, x1

    // Zero the BSS region [__bss_start, __bss_end), 16 bytes at a time.
    ldr     x1, =__bss_start
    ldr     x2, =__bss_end
    b       2f

1:
    stp     xzr, xzr, [x1], #16

2:
    cmp     x1, x2
    blo     1b

    // Install the interrupt vector table.
    ldr     x0, =vector_table
    msr     VBAR_EL1, x0

    // Unmask IRQs (DAIF I-bit).
    msr     DAIFClr, #2

    // Hand off to the C++ kernel entry.
    bl      kernel_main


halt:
    wfe
    b       halt

.global psci_cpu_on
psci_cpu_on:
    mov x3, x2 // Move context_id to x3 from default x2
    mov x2, x1 // Move entry_address from x1 to x2
    mov x1, x0 // Move MPIDR from x0 to x1

    ldr x0, =0xC4000003 // set x0 to FUNCTION_ID

    // Call SMCCC
    smc #0

    // Return to C++
    ret

.global secondary_entry
secondary_entry:
    // Set up Stack Pointer
    mov sp, x0

    // Install the interrupt vector table.
    ldr     x0, =vector_table
    msr     VBAR_EL1, x0

    // Unmask IRQs (DAIF I-bit).
    msr     DAIFClr, #2

    // Set up CPU's Thread ID
    mrs x0, MPIDR_EL1 // Set x0 to MPIDR_EL1
    and x0, x0, #0xFF // Get Aff0
    msr TPIDR_EL1, x0 // Write Aff0 to TPIDR_EL1

    // Branch to secondary_main
    bl secondary_main

    // As a safety net, halt if execution
    // continues past the last branch
    b halt

