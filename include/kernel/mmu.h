/**
 * @file mmu.h
 * @brief AArch64 stage-1 MMU — public API and descriptor flag constants.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Configures a TTBR0-only, 4 KiB-granule, 39-bit-VA, three-level (L1→L2→L3)
 * translation regime. MAIR slots: Attr0 = Normal Inner/Outer WB cacheable,
 * Attr1 = Device-nGnRnE, Attr2 = Normal Inner/Outer Non-cacheable (DMA
 * buffers). The kernel runs identity-mapped after #mmu_init.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D5 (VMSAv8-64)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>


/**
 * @brief L3 (page) descriptor templates for the two memory types we use.
 *
 * Each value bakes in the AttrIndx field (MAIR slot), AP (read/write at EL1),
 * SH (inner shareable), and AF (access flag). The terminal `0b11` (valid +
 * page) bits are added at descriptor-write time in #map.
 */
enum {
	/** Normal Inner/Outer WB cacheable, EL1 RW, inner shareable, AF=1. */
	PTE_NORMAL_RW = 0x703,
	/** Device-nGnRnE, EL1 RW, outer shareable, AF=1. */
	PTE_DEVICE_RW = 0x707,
	/** Normal Inner/Outer Non-cacheable, EL1 RW, outer shareable, AF=1.
	 *  Used by #dma_alloc: nothing is ever cached here, so a CPU write is
	 *  visible to a DMA-capable device (and vice versa) with no explicit
	 *  cache-maintenance call needed. */
	PTE_NORMAL_NC_RW = 0x60B,
};


/**
 * @brief Bring up the MMU and switch the kernel onto virtual addressing.
 *
 * Programs MAIR_EL1 and TCR_EL1, allocates and zeros the L1 table, installs
 * the identity map (low RAM, kernel + bitmap, free RAM, BCM2835 peripherals,
 * ARM local peripherals), writes TTBR0_EL1, then flips SCTLR_EL1.M to enable
 * translation. Also enables instruction and data caches.
 *
 * Requires #pmm_init to have run first (table frames come from the PMM).
 */
/** Top-level (L1) translation table. Defined in mmu.cpp; assigned by the
 *  board-specific mmu_init before any map/translate/unmap call. */
extern uint64_t* l1_table;

void mmu_init();


/**
 * @brief Install identity-style mappings for a contiguous VA range.
 *
 * Walks (and on-demand allocates) the L1→L2→L3 tables for each 4 KiB page
 * in `[va, va + size)` and writes the terminal L3 descriptor as
 * `phys | flags | 0b11`. Emits the page-table-edit barrier sequence
 * (`dsb ishst` → per-page `tlbi vaae1is` → `dsb ish` → `isb`) so the change
 * is visible everywhere before returning.
 *
 * @param va    Page-aligned virtual base address.
 * @param phys  Page-aligned physical base address.
 * @param size  Length in bytes; must be a multiple of `PAGE_SIZE`.
 * @param flags L3 descriptor flag template (see #PTE_NORMAL_RW, #PTE_DEVICE_RW).
 */
void map(uint64_t va, uint64_t phys, uint64_t size, uint64_t flags);

/**
 * @brief Tear down mappings for a contiguous VA range.
 *
 * Walks each 4 KiB page in `[va, va + size)` to L3 and zeros the descriptor,
 * then issues the same batched TLB-invalidate sequence as #map. Does not
 * reclaim intermediate L2 or L3 tables. If any L1 or L2 descriptor is
 * invalid mid-range, the function early-returns without invalidating any
 * already-cleared pages — partial unmap leaves stale TLB entries.
 *
 * @param va   Page-aligned virtual base address.
 * @param size Length in bytes; must be a multiple of `PAGE_SIZE`.
 */
void unmap(uint64_t va, uint64_t size);

/**
 * @brief Resolve or create the next-level table referenced by @p table[@p index].
 *
 * If the descriptor is already valid (low bit set), returns the existing
 * next-level table pointer (masking off the descriptor's flag bits).
 * Otherwise allocates a fresh frame via #alloc_frame, zeros it, installs it
 * as a table descriptor (`0b11` low bits), and returns it.
 *
 * @param table Pointer to the current-level table (512 entries).
 * @param index Entry index within @p table (0–511).
 * @return Pointer to the next-level table, or `nullptr` if frame allocation
 *         failed.
 */
uint64_t* _get_or_alloc_table(uint64_t* table, uint64_t index);

/**
 * @brief Detects if a given page @p table is empty by scanning all 512 pages.
 *
 * Read-only. Walks page table's entries to determine if page table points.
 * to empty tables.
 *
 * @param table The table being checked
 * @return True if the table's 512 entries are all empty, or false otherwise.
 */
bool _table_is_empty(const uint64_t* table);

/**
 * @brief Software page-table walk for @p virt.
 *
 * Read-only: never allocates tables. Walks L1→L2→L3 and returns the
 * translated physical address with the original page offset preserved.
 *
 * @param virt Virtual address to translate.
 * @return Physical address on success; `~0ULL` if any descriptor along the
 *         walk is invalid (the sentinel is chosen so that PA `0` — a real,
 *         mapped low-RAM address — is not aliased).
 */
uint64_t translate(uint64_t virt);
