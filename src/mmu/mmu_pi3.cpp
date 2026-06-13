/**
 * @file mmu_pi3.cpp
 * @brief Pi 3 MMU initialization — identity map for BCM2837 memory layout.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Contains mmu_init for the Pi 3. Shared MMU functions (map, translate,
 * unmap) live in mmu.cpp and are compiled for all boards.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */


#include <stdint.h>
#include "pmm.h"
#include "uart.h"
#include "mmu.h"
#include "board.h"


void mmu_init() {
	// MAIR_EL1: define the memory-attribute vocabulary the walker will
	// reference by index from L3 descriptors.
	uint64_t mair = 0;
	mair |= (0xFFULL << (0 * 8)); // Attr0: Normal Inner/Outer WB cacheable.
	mair |= (0x00ULL << (1 * 8)); // Attr1: Device-nGnRnE.
	mair |= (0x04ULL << (2 * 8)); // Attr2: reserved for future use.
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
	| (1ULL << 32);  // IPS: 36-bit PA. Adequate for the Pi 3 SoC.
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
	asm volatile("dsb ish" ::: "memory");
	asm volatile("tlbi vmalle1" ::: "memory");
	asm volatile("dsb ish" ::: "memory");
	asm volatile("isb" ::: "memory");

	asm volatile("msr TTBR0_EL1, %0" :: "r"(page_table_address));

	// Identity-map the five regions the kernel needs post-MMU:
	//   1. Low RAM (stack region, grows down from 0x80000).
	//   2. Kernel + bitmap (kernel image up to phys_mem_start).
	//   3. Free RAM (the region the PMM hands out — page tables live here).
	//   4. BCM2835 peripheral block.
	//   5. ARM local peripherals (different L1 entry than the rest).
	map(0x00000000, 0x00000000, 0x00080000, PTE_NORMAL_RW);
	map(0x00080000, 0x00080000, phys_mem_start - 0x80000, PTE_NORMAL_RW);
	map(phys_mem_start, phys_mem_start, PHYS_MEM_END - phys_mem_start, PTE_NORMAL_RW);
	map(PERIPHERAL_BASE, PERIPHERAL_BASE, 0x01000000, PTE_DEVICE_RW);
	map(LOCAL_PERIPHERAL_BASE, LOCAL_PERIPHERAL_BASE, 0x00001000, PTE_DEVICE_RW);


	// SCTLR_EL1: flip M (MMU on), C (data cache on), I (instruction
	// cache on). After the ISB, every fetch and load/store is translated.
	uint64_t sctlr;
	asm volatile ("mrs %0, SCTLR_EL1" : "=r"(sctlr));
	sctlr |=
		(1ULL << 0)   // M: MMU enable.
	| (1ULL << 2)   // C: data cache enable.
	| (1ULL << 12); // I: instruction cache enable.
	asm volatile("msr SCTLR_EL1, %0" :: "r"(sctlr));

	asm volatile("isb" ::: "memory");
}