/**
 * @file mmu.cpp
 * @brief AArch64 stage-1 MMU bring-up and page-table management.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Configures a TTBR0-only, 4 KiB-granule, 39 or 40-bit-VA translation regime
 * with a three-level walk (L1 → L2 → L3). The kernel runs identity-mapped:
 * every VA equals its PA across the regions we care about
 * (low RAM, kernel + bitmap, free RAM, BCM2835 peripherals, ARM local
 * peripherals). MAIR slots: Attr0 = Normal Inner/Outer WB cacheable,
 * Attr1 = Device-nGnRnE.
 *
 * Mnemonic for register bring-up order:
 *   - MAIR: *who* are the memory types (define the vocabulary)
 *   - TCR:  *what* rules does the walker follow (define the grammar)
 *   - TTBR0: *where* is the top-level table (hand over the map)
 *   - SCTLR: *go* (flip M=1 and start translating)
 *
 * Barrier glossary (used pervasively below):
 *   - `dsb ish`   — wait for memory accesses to complete, inner-shareable.
 *   - `dsb ishst` — same but only orders stores.
 *   - `tlbi vmalle1` — invalidate every TLB entry in this regime.
 *   - `tlbi vaae1is, va` — invalidate one VA across all ASIDs, broadcast.
 *   - `isb` — flush the instruction pipeline so subsequent fetches see the
 *     new translation state.
 *
 * Page-table-edit sequence: `dsb ishst` (writes visible) → `tlbi vaae1is`
 * per page (kill stale translations) → `dsb ish` (let the invalidate
 * propagate) → `isb` (refetch). Used inside #map and #unmap.
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D5 (VMSAv8-64)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdint.h>

#include "pmm.h"
#include "uart.h"
#include "mmu.h"

/** Top-level (L1) translation table. Written by mmu_init (board-specific);
 *  read by map, translate, and unmap (shared). External linkage so the
 *  board-specific TU can assign it after allocating the frame. */
uint64_t* l1_table;


void map(const uint64_t va, const uint64_t phys, const uint64_t size, uint64_t flags) {
	uint64_t num_pages = size / PAGE_SIZE;

	// Page-table-edit barrier sequence (see file header).
	asm volatile("dsb ishst" ::: "memory");

	for (int i = 0; i < num_pages; i++) {
		uint64_t current_va = va + i * PAGE_SIZE;
		uint64_t current_phys = phys + i * PAGE_SIZE;

		// VA[38:30] = L1 index, VA[29:21] = L2 index, VA[20:12] = L3 index.
		uint64_t L1_INDEX = (current_va >> 30) & 0x1FF;
		uint64_t L2_INDEX = (current_va >> 21) & 0x1FF;

		uint64_t* l2_table = _get_or_alloc_table(l1_table, L1_INDEX);
		if (!l2_table) {
			return; // TODO: real error path.
		}

		uint64_t* l3_table = _get_or_alloc_table(l2_table, L2_INDEX);
		if (!l3_table) {
			return; // TODO: real error path.
		}

		uint64_t L3_INDEX = (current_va >> 12) & 0x1FF;
		uint64_t PAGE_VALID_BITS = 0b11;

		l3_table[L3_INDEX] = current_phys | flags | PAGE_VALID_BITS;
	}

	// Invalidate stale TLB entries for every page just written.
	for (int i = 0; i < num_pages; i++) {
		uint64_t va_page = (va + i * PAGE_SIZE) >> 12;
		asm volatile("tlbi vaae1is, %0" :: "r"(va_page) : "memory");
	}
	asm volatile("dsb ish" ::: "memory");
	asm volatile("isb" ::: "memory");
}

uint64_t* _get_or_alloc_table(uint64_t* table, const uint64_t index) {
	uint64_t* entry = &table[index];
	if (*entry & 1) {
		// Already-valid descriptor: strip flag bits, return the pointer.
		uint64_t* child_phys = reinterpret_cast<uint64_t*>(*entry & 0x0000FFFFFFFFF000);
		return child_phys;
	}
	uint64_t* child = reinterpret_cast<uint64_t*>(alloc_frame());
	if (!child) return nullptr;

	for (int i = 0; i < 512; i++) {
		child[i] = 0;
	}

	// Write a table descriptor (`0b11`): valid bit + table-type bit.
	table[index] = reinterpret_cast<uint64_t>(child) | 0b11;
	return child;
}

uint64_t translate(uint64_t virt) {
	uint64_t L1_INDEX = (virt >> 30) & 0x1FF;
	uint64_t L2_INDEX = (virt >> 21) & 0x1FF;
	uint64_t L3_INDEX = (virt >> 12) & 0x1FF;
	const uint64_t ADDR_MASK = 0x0000FFFFFFFFF000ULL;

	uint64_t l1_desc = l1_table[L1_INDEX];
	if ((l1_desc & 0b11) != 0b11) return ~0ULL;
	uint64_t* l2_table = reinterpret_cast<uint64_t *>(l1_desc & ADDR_MASK);

	uint64_t l2_desc = l2_table[L2_INDEX];
	if ((l2_desc & 0b11) != 0b11) return ~0ULL;
	uint64_t* l3_table = reinterpret_cast<uint64_t *>(l2_desc & ADDR_MASK);

	uint64_t l3_desc = l3_table[L3_INDEX];
	if ((l3_desc & 0b11) != 0b11) return ~0ULL;

	// PA = (L3 frame address) | (low 12 bits of the original VA).
	return (l3_desc & ADDR_MASK) | (virt & 0xFFFULL);
}

void unmap(uint64_t va, uint64_t size) {
	uint64_t num_pages = size / PAGE_SIZE;
	asm volatile("dsb ishst" ::: "memory");

	for (int i = 0; i < num_pages; i++) {
		uint64_t current_va = va + i * PAGE_SIZE;

		uint64_t L1_INDEX = (current_va >> 30) & 0x1FF;
		uint64_t L2_INDEX = (current_va >> 21) & 0x1FF;
		uint64_t L3_INDEX = (current_va >> 12) & 0x1FF;
		const uint64_t ADDR_MASK = 0x0000FFFFFFFFF000ULL;

		uint64_t l1_desc = l1_table[L1_INDEX];
		if ((l1_desc & 0b11) != 0b11) return; // Early-return: see caveat in header.
		uint64_t* l2_table = reinterpret_cast<uint64_t *>(l1_desc & ADDR_MASK);

		uint64_t l2_desc = l2_table[L2_INDEX];
		if ((l2_desc & 0b11) != 0b11) return;
		uint64_t* l3_table = reinterpret_cast<uint64_t *>(l2_desc & ADDR_MASK);

		l3_table[L3_INDEX] = 0;

		// Clear L3 table if empty.
		if (_table_is_empty(l3_table)) {
			// Zero out table descriptors before freeing frame to ensure
			// the walker doesn't follow a stale frame.

			l3_table[L3_INDEX] = 0ULL;
			free_frame(reinterpret_cast<uint64_t>(l3_table));
		}

		// Clear L2 table if empty.
		if (_table_is_empty(l2_table)) {
			l2_table[L2_INDEX] = 0ULL;
			free_frame(reinterpret_cast<uint64_t>(l2_table));
		}
	}

	// Invalidate stale TLB entries for every page cleared.
	for (int i = 0; i < num_pages; i++) {
		uint64_t va_page = (va + i * PAGE_SIZE) >> 12;
		asm volatile("tlbi vaae1is, %0" :: "r"(va_page) : "memory");
	}

	asm volatile("dsb ish" ::: "memory");
	asm volatile("isb" ::: "memory");
}

bool _table_is_empty(const uint64_t* table) {
	for (int i = 0; i < 512; i++) {
		if (table[i] != 0) return false;
	}
	return true;
}