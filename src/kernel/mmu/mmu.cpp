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
 * Attr1 = Device-nGnRnE, Attr2 = Normal Inner/Outer Non-cacheable (used by
 * #dma_alloc for DMA-coherent buffers).
 *
 * References:
 *   - Arm Architecture Reference Manual for A-profile, §D5 (VMSAv8-64)
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <stdint.h>

#include "kernel/pmm.h"
#include "kernel/uart.h"
#include "kernel/mmu.h"
#include "kernel/barrier.h"
#include "kernel/board.h"
#include "kernel/spinlock.h"

/** Top-level (L1) translation table. Written by mmu_init; read by map,
 *  translate, and unmap. */
uint64_t* l1_table;

static spinlock mmu_lock = {0};


bool map(const uint64_t va, const uint64_t phys, const uint64_t size, uint64_t flags) {
	uint64_t num_pages = size / PAGE_SIZE;

	uint64_t irq_flags;
	spin_lock(mmu_lock, irq_flags);

	dsb_ishst();

	for (int i = 0; i < num_pages; i++) {
		uint64_t current_va = va + i * PAGE_SIZE;
		uint64_t current_phys = phys + i * PAGE_SIZE;

		uint64_t L1_INDEX = (current_va >> 30) & 0x1FF;
		uint64_t L2_INDEX = (current_va >> 21) & 0x1FF;

		uint64_t* l2_table = _get_or_alloc_table(l1_table, L1_INDEX);
		if (!l2_table) {
			_unmap_locked(va, i * PAGE_SIZE);
			spin_unlock(mmu_lock, irq_flags);
			return false;
		}

		uint64_t* l3_table = _get_or_alloc_table(l2_table, L2_INDEX);
		if (!l3_table) {
			_unmap_locked(va, i * PAGE_SIZE);
			spin_unlock(mmu_lock, irq_flags);
			return false;
		}

		uint64_t L3_INDEX = (current_va >> 12) & 0x1FF;
		uint64_t PAGE_VALID_BITS = 0b11;

		l3_table[L3_INDEX] = current_phys | flags | PAGE_VALID_BITS;
	}

	for (int i = 0; i < num_pages; i++) {
		uint64_t va_page = (va + i * PAGE_SIZE) >> 12;
		asm volatile("tlbi vaae1is, %0" :: "r"(va_page) : "memory");
	}
	dsb_ish();
	isb();

	spin_unlock(mmu_lock, irq_flags);
	return true;
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
	uint64_t irq_flags;
	spin_lock(mmu_lock, irq_flags);

	uint64_t L1_INDEX = (virt >> 30) & 0x1FF;
	uint64_t L2_INDEX = (virt >> 21) & 0x1FF;
	uint64_t L3_INDEX = (virt >> 12) & 0x1FF;
	const uint64_t ADDR_MASK = 0x0000FFFFFFFFF000ULL;

	uint64_t l1_desc = l1_table[L1_INDEX];
	if ((l1_desc & 0b11) != 0b11) { spin_unlock(mmu_lock, irq_flags); return ~0ULL; }
	uint64_t* l2_table = reinterpret_cast<uint64_t *>(l1_desc & ADDR_MASK);

	uint64_t l2_desc = l2_table[L2_INDEX];
	if ((l2_desc & 0b11) != 0b11) { spin_unlock(mmu_lock, irq_flags); return ~0ULL; }
	uint64_t* l3_table = reinterpret_cast<uint64_t *>(l2_desc & ADDR_MASK);

	uint64_t l3_desc = l3_table[L3_INDEX];
	if ((l3_desc & 0b11) != 0b11) { spin_unlock(mmu_lock, irq_flags); return ~0ULL; }

	uint64_t pa = (l3_desc & ADDR_MASK) | (virt & 0xFFFULL);
	spin_unlock(mmu_lock, irq_flags);
	return pa;
}

void unmap(uint64_t va, uint64_t size) {
	uint64_t irq_flags;
	spin_lock(mmu_lock, irq_flags);

	_unmap_locked(va, size);

	spin_unlock(mmu_lock, irq_flags);
}

void _unmap_locked(uint64_t va, uint64_t size) {
	uint64_t num_pages = size / PAGE_SIZE;

	dsb_ishst();

	for (int i = 0; i < num_pages; i++) {
		uint64_t current_va = va + i * PAGE_SIZE;

		uint64_t L1_INDEX = (current_va >> 30) & 0x1FF;
		uint64_t L2_INDEX = (current_va >> 21) & 0x1FF;
		uint64_t L3_INDEX = (current_va >> 12) & 0x1FF;
		const uint64_t ADDR_MASK = 0x0000FFFFFFFFF000ULL;

		uint64_t l1_desc = l1_table[L1_INDEX];
		if ((l1_desc & 0b11) != 0b11) { return; }
		uint64_t* l2_table = reinterpret_cast<uint64_t *>(l1_desc & ADDR_MASK);

		uint64_t l2_desc = l2_table[L2_INDEX];
		if ((l2_desc & 0b11) != 0b11) { return; }
		uint64_t* l3_table = reinterpret_cast<uint64_t *>(l2_desc & ADDR_MASK);

		l3_table[L3_INDEX] = 0;

		// Clear L3 table if empty.
		if (_table_is_empty(l3_table)) {
			// Clear the parent's descriptor first — otherwise l2_table[L2_INDEX]
			// is left as a stale valid entry pointing at a frame the PMM now
			// considers free, and a future alloc_frame() call can hand that
			// same frame out again while this dangling descriptor still
			// aliases it as a live table.
			l2_table[L2_INDEX] = 0ULL;
			free_frame(reinterpret_cast<uint64_t>(l3_table));
		}

		// Clear L2 table if empty.
		if (_table_is_empty(l2_table)) {
			l1_table[L1_INDEX] = 0ULL;
			free_frame(reinterpret_cast<uint64_t>(l2_table));
		}
	}

	// Invalidate stale TLB entries for every page cleared.
	for (int i = 0; i < num_pages; i++) {
		uint64_t va_page = (va + i * PAGE_SIZE) >> 12;
		asm volatile("tlbi vaae1is, %0" :: "r"(va_page) : "memory");
	}

	dsb_ish();
	isb();
}

bool _table_is_empty(const uint64_t* table) {
	for (int i = 0; i < 512; i++) {
		if (table[i] != 0) return false;
	}
	return true;
}

void mmu_init() {
	mmu_lock.spin_lock = SPINLOCK_FREE;

	// MAIR_EL1: define the memory-attribute vocabulary the walker will
	// reference by index from L3 descriptors.
	uint64_t mair = 0;
	mair |= (0xFFULL << (0 * 8)); // Attr0: Normal Inner/Outer WB cacheable.
	mair |= (0x00ULL << (1 * 8)); // Attr1: Device-nGnRnE.
	mair |= (0x44ULL << (2 * 8)); // Attr2: Normal Inner/Outer Non-cacheable.
	asm volatile("msr MAIR_EL1, %0" :: "r"(mair));

	// TCR_EL1: walker configuration for TTBR0.
	uint64_t tcr = 0;
	tcr |=
		(25ULL << 0)   // T0SZ: VA is 64 - 25 = 39 bits. Affords a 512 GiB region.
	| (1ULL << 8)    // IRGN0: inner-cacheable walks.
	| (1ULL << 10)   // ORGN0: outer-cacheable walks.
	| (3ULL << 12)   // SH0: inner-shareable.
	| (0ULL << 14)   // TG0: 4 KiB granule.
	| (1ULL << 23)   // EPD1: disable TTBR1 walks (we use TTBR0 only).
	| (2ULL << 32);  // IPS: 40-bit PA. Adequate for the Pi 5 SoC.
	asm volatile("msr TCR_EL1, %0" :: "r"(tcr));

	// Bring up the PMM and allocate the L1 table from it.
	pmm_init();
	uint64_t* page_table_address = reinterpret_cast<uint64_t*>(alloc_frame());
	if (page_table_address) {
		for (int i = 0; i < 512; i++) {
			page_table_address[i] = 0;
		}
	} else {
		uart_puts("mmu_init: out of memory\r\n");
		while (1) {}
	}

	l1_table = page_table_address;

	// Clear any speculative state in the walker, then install TTBR0.
	dsb_ish();
	asm volatile("tlbi vmalle1" ::: "memory");
	dsb_ish();
	isb();

	asm volatile("msr TTBR0_EL1, %0" :: "r"(page_table_address));

	// Identity-map the regions the kernel needs post-MMU:
	//   1. Low RAM (stack region, grows down from 0x80000).
	//   2. Kernel + bitmap (kernel image up to phys_mem_start).
	//   3. Free RAM (the region the PMM hands out — page tables live here).
	//   4. RP1 peripheral block (PCIe-BAR window).
	//   5. ARM local peripherals (GIC-400; different L1 entry than the rest).
	//   6. BCM2712's native SDHCI host+cfg registers (SDIO0_BASE).
	//   7. BCM2712's main pinctrl block (EMMC pull-up config).
	map(0x00000000, 0x00000000, 0x00080000, PTE_NORMAL_RW);
	map(0x00080000, 0x00080000, phys_mem_start - 0x80000, PTE_NORMAL_RW);
	map(phys_mem_start, phys_mem_start, PHYS_MEM_END - phys_mem_start, PTE_NORMAL_RW);
	map(PERIPHERAL_BASE, PERIPHERAL_BASE, 0x410000, PTE_DEVICE_RW);
	map(LOCAL_PERIPHERAL_BASE, LOCAL_PERIPHERAL_BASE, 0x8000, PTE_DEVICE_RW);

	// SDIO0_BASE (0x1000fff000) is already page-aligned; one page covers
	// both the "host" register window (0x000-0x260) and the "cfg" window
	// (0x400-0x600) the SD driver touches — both fit inside the first
	// 0x1000 bytes. Neither of the two mappings above cover this: it's
	// BCM2712-native address space, a different translation scheme
	// entirely from RP1's PCIe-BAR window (PERIPHERAL_BASE) and a
	// different block from the GIC-400's own BCM2712-native range
	// (LOCAL_PERIPHERAL_BASE) — see SDIO0_BASE's comment in board.h.
	map(SDIO0_BASE, SDIO0_BASE, PAGE_SIZE, PTE_DEVICE_RW);

	// PINCTRL_BASE (0x107d504100) is not page-aligned (0x100 into its
	// page) — round down to the containing page. One page still covers
	// the pull-config register this driver actually writes
	// (PINCTRL_EMMC_PULL_REG = PINCTRL_BASE + 0x2C = 0x107d50412c).
	constexpr uint64_t PINCTRL_PAGE_BASE = PINCTRL_BASE & ~(PAGE_SIZE - 1);
	map(PINCTRL_PAGE_BASE, PINCTRL_PAGE_BASE, PAGE_SIZE, PTE_DEVICE_RW);


	// SCTLR_EL1: flip M (MMU on), C (data cache on), I (instruction
	// cache on). After the ISB, every fetch and load/store is translated.
	uint64_t sctlr;
	asm volatile ("mrs %0, SCTLR_EL1" : "=r"(sctlr));
	sctlr |=
		(1ULL << 0)   // M: MMU enable.
	| (1ULL << 2)   // C: data cache enable.
	| (1ULL << 12); // I: instruction cache enable.
	asm volatile("msr SCTLR_EL1, %0" :: "r"(sctlr));

	isb();

	MMU_ACTIVE = true;
}