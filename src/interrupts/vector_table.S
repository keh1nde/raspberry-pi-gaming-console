// @file vector_table.S
// @brief AArch64 exception vector table + register save/restore stubs.
//
// Part of raspberry-pi-gaming-console, a retro gaming console OS.
// Built on the kehinde-kernel project, originally MIT-licensed.
//
// The AArch64 vector table is 16 entries laid out at 128-byte intervals,
// the whole table 2 KiB aligned. Entries are grouped into four bands:
//   - Current EL with SP_EL0  (we don't use this — bands self-loop)
//   - Current EL with SP_ELx  (active; sync + IRQ wired up)
//   - Lower EL using AArch64  (no userspace yet)
//   - Lower EL using AArch32  (we never run AArch32)
//
// Each used entry branches to a small stub below that saves the general
// register file plus ELR_EL1/SPSR_EL1 onto the kernel stack, calls into a
// C++ handler with C linkage, restores the state, and ERETs back to the
// faulting instruction (or the next one, depending on the exception class).
//
// Stack frame written by `save_registers`:
//   [SP + 0x000 .. 0x0FF]  x0–x29 (30 GP regs, 240 bytes)
//   [SP + 0x0F0 .. 0x0FF]  x30, xzr (LR + pad)
//   [SP + 0x100 .. 0x10F]  ELR_EL1, SPSR_EL1
//   Total: 272 bytes.
//
// References:
//   - Arm Architecture Reference Manual for A-profile, §D1.10 (Vector tables)
//
// @author Kehinde Adeoso
// SPDX-License-Identifier: GPL-3.0-only

.section ".text"
.balign 2048
.global vector_table


// Save x0–x30 plus ELR_EL1/SPSR_EL1 to a 272-byte stack frame.
.macro save_registers
    sub SP, SP, #272
    stp x0, x1, [SP, #0]
    stp x2, x3, [SP, #16]
    stp x4, x5, [SP, #32]
    stp x6, x7, [SP, #48]
    stp x8, x9, [SP, #64]
    stp x10, x11, [SP, #80]
    stp x12, x13, [SP, #96]
    stp x14, x15, [SP, #112]
    stp x16, x17, [SP, #128]
    stp x18, x19, [SP, #144]
    stp x20, x21, [SP, #160]
    stp x22, x23, [SP, #176]
    stp x24, x25, [SP, #192]
    stp x26, x27, [SP, #208]
    stp x28, x29, [SP, #224]
    stp x30, xzr, [SP, #240]
    mrs x0, ELR_EL1
    mrs x1, SPSR_EL1
    stp x0, x1, [SP, #256]
.endm

// Reverse of save_registers — restore ELR_EL1/SPSR_EL1 first so ERET sees
// the right state, then unwind the general registers.
.macro load_registers
    ldp x0, x1, [SP, #256]
    msr ELR_EL1, x0
    msr SPSR_EL1, x1
    ldp x30, xzr, [SP, #240]
    ldp x28, x29, [SP, #224]
    ldp x26, x27, [SP, #208]
    ldp x24, x25, [SP, #192]
    ldp x22, x23, [SP, #176]
    ldp x20, x21, [SP, #160]
    ldp x18, x19, [SP, #144]
    ldp x16, x17, [SP, #128]
    ldp x14, x15, [SP, #112]
    ldp x12, x13, [SP, #96]
    ldp x10, x11, [SP, #80]
    ldp x8, x9, [SP, #64]
    ldp x6, x7, [SP, #48]
    ldp x4, x5, [SP, #32]
    ldp x2, x3, [SP, #16]
    ldp x0, x1, [SP, #0]
    add SP, SP, #272
.endm


// ====== Vector table: 16 entries, 128-byte aligned. ======

vector_table:

// Current EL with SP0 — unused; self-loop on entry.
.balign 128
    b .                             // Synchronous
.balign 128
    b .                             // IRQ
.balign 128
    b .                             // FIQ
.balign 128
    b .                             // SError

// Current EL with SPx — the kernel runs here.
.balign 128
    b sync_handler                  // Synchronous
.balign 128
    b irq_handler                   // IRQ
.balign 128
    b .                             // FIQ
.balign 128
    b .                             // SError

// Lower EL using AArch64 — no userspace yet.
.balign 128
    b .                             // Synchronous
.balign 128
    b .                             // IRQ
.balign 128
    b .                             // FIQ
.balign 128
    b .                             // SError

// Lower EL using AArch32 — we never run AArch32.
.balign 128
    b .                             // Synchronous
.balign 128
    b .                             // IRQ
.balign 128
    b .                             // FIQ
.balign 128
    b .                             // SError


// ====== Handler bodies. Placed outside the table so each table slot
//        stays under the 128-byte budget. ======

sync_handler:
    save_registers
    bl handle_synchronous_interrupts
    load_registers
    eret

irq_handler:
    save_registers
    bl handle_interrupt_requests
    load_registers
    eret
