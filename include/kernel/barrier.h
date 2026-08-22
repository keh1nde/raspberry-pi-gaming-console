/**
 * @file barrier.h
 * @brief Memory barrier and instruction-synchronization wrappers.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Thin inline wrappers over the AArch64 DSB/DMB/ISB instructions, covering
 * the specific barrier + domain combinations this kernel actually uses (no
 * `dmb` wrapper yet — every current call site needs the completion
 * guarantee only `dsb` gives, not just load/store ordering). `static
 * inline`, like the MMIO accessors in io.h, so each translation unit
 * gets its own trivially-inlined copy with no link-time multiple-definition
 * risk.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §B2.3 (Barriers)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

/**
 * @brief Data Synchronization Barrier, full system, all accesses.
 *
 * Waits for every prior memory access (of any kind) to complete before any
 * instruction after the barrier executes. The heaviest-weight barrier;
 * required after cache-maintenance operations (`dc cvac`/`dc ivac`), which
 * are only guaranteed complete once a `dsb sy` has retired.
 */
static inline void dsb_sy() {
	asm volatile("dsb sy" ::: "memory");
}

/**
 * @brief Data Synchronization Barrier, inner-shareable domain, all accesses.
 *
 * Same guarantee as #dsb_sy but scoped to the inner-shareable domain (this
 * core plus any others coherent with it) rather than the whole system —
 * cheaper when the effect only needs to be visible there, e.g. between a
 * page-table write and the TLB invalidate that targets it.
 */
static inline void dsb_ish() {
	asm volatile("dsb ish" ::: "memory");
}

/**
 * @brief Data Synchronization Barrier, inner-shareable domain, stores only.
 *
 * Like #dsb_ish but only orders prior stores (not loads) against
 * instructions after the barrier. Used before a TLB-invalidate sequence to
 * ensure the page-table write it targets is visible first.
 */
static inline void dsb_ishst() {
	asm volatile("dsb ishst" ::: "memory");
}

/**
 * @brief Instruction Synchronization Barrier.
 *
 * Flushes the instruction pipeline so every instruction fetched after this
 * point sees the effect of any preceding context-changing operation (TLB
 * invalidate, system-register write, etc.) rather than a stale pipelined
 * view.
 */
static inline void isb() {
	asm volatile("isb" ::: "memory");
}
