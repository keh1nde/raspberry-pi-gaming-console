/**
 * @file board.h
 * @brief MMIO base addresses and register definitions for the Raspberry Pi 5.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Constants for all MMIO, including for the BCM2712 SoC and RP1 south bridge:
 * the PCIe-windowed peripheral base, GIC-400 distributor/CPU interface,
 * RP1 UART, GPIO, UART clocks, and SDIO MMIO registers.
 *
 * Note: Most variables have docstrings to eliminate the need to
 * revisit documentation. All variables will recieve documentation at
 * a later date.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */


#include <stdint.h>
#pragma once

/**
	 * RP1 PCIe BAR0 as seen from the AP (Application Processor).
	 *
	 * The RP1 south bridge sits behind the BCM2712 PCIe root complex. This
	 * address is the CPU-side window into RP1 peripheral space and is only
	 * valid after the PCIe link is up. Setting `pciex4_reset=0` in config.txt
	 * tells firmware to leave PCIe configured so bare-metal code can skip
	 * root-complex initialization entirely.
	 *
	 * All RP1 peripheral addresses are computed as:
	 *   PERIPHERAL_BASE + (rp1_internal_offset - 0x40000000)
	 *
	 * Source: Derived from the firmware-logged RP1_UART address:
	 *  0x1C00030000 − 0x30000 (RP1's UART offset) = 0x1C00000000.
	 *
	 * The previous value was retrieved from a /proc/iomem harvest
	 * (2026-06-04) on a Pi 5 Model B Rev 1.1 running Raspberry Pi OS 64-bit,
	 * but removed since that value reflects Linux's PCIe BAR remapping, not the
	 * firmware default.
	 *
	 * Development Note: The previous PERIPHERAL_BASE was 0x1F00000000
	 */
	constexpr uint64_t PERIPHERAL_BASE = 0x1C00000000;

	/**
	 * GIC-400 block base address on BCM2712.
	 *
	 * Derived from the device-tree node `interrupt-controller@7fff9000` inside
	 * `soc@107c000000`. The SoC node maps its internal base 0x7c000000 to CPU
	 * physical 0x107c000000 (offset +0x10000000000); applying that offset to
	 * the DT-internal address 0x7fff8000 yields 0x107FFF8000.
	 *
	 * GICD, GICC, GICH, and GICV are at standard GIC-400 offsets from this
	 * base (ARM IHI0048). Source: Device Tree `reg` tuples; confirmed 2026-06-04.
	 */
	constexpr uint64_t LOCAL_PERIPHERAL_BASE = 0x107FFF8000;

	/** First physical address claimed by Raspberry Pi MMIO peripherals. The PMM
	*  manages frames up to but not including this address. */
	constexpr uint64_t PHYS_MEM_END = 0x3F800000;

	/**
	 * GIC-400 Distributor (GICD) — CPU physical 0x107FFF9000.
	 *
	 * The Distributor is the global interrupt-routing hub shared by all
	 * cores. It controls per-interrupt enable/disable (GICD_ISENABLER),
	 * priority (GICD_IPRIORITYR), target-core routing (GICD_ITARGETSR),
	 * and trigger configuration (GICD_ICFGR). Initialised once by core 0
	 * during interrupt controller bring-up.
	 *
	 * Offset +0x1000 from LOCAL_PERIPHERAL_BASE per GIC-400 memory map
	 * (ARM IHI0048). Source: Device Tree reg first tuple (0x7fff9000 + SoC offset).
	 */
	constexpr uint64_t GICD_BASE = LOCAL_PERIPHERAL_BASE + 0x1000;

	/**
	 * GIC-400 CPU Interface (GICC) — CPU physical 0x107FFFA000.
	 *
	 * Each core has its own CPU Interface view at this address. A core
	 * reads GICC_IAR to acknowledge the highest-priority pending interrupt
	 * (which also dequeues it) and writes GICC_EOIR to signal End-Of-
	 * Interrupt. GICC_PMR sets the priority mask; GICC_CTLR enables the
	 * interface. Must be initialized on every core that will handle IRQs.
	 *
	 * Offset +0x2000 from LOCAL_PERIPHERAL_BASE per GIC-400 memory map
	 * (ARM IHI0048). Source: DT reg second tuple (0x7fffa000 + SoC offset).
	 */
	constexpr uint64_t GICC_BASE = LOCAL_PERIPHERAL_BASE + 0x2000;

	/**
	 * GIC-400 Hypervisor Control (GICH) — CPU physical 0x107FFFC000.
	 * GIC-400 Virtual CPU Interface (GICV) — CPU physical 0x107FFFE000.
	 *
	 * Used only when a hypervisor at EL2 virtualizes interrupt delivery to
	 * guest OSes at EL1. Not initialized in this kernel (EL1 only, no
	 * virtualization). Documented here so the addresses are not re-derived
	 * if EL2 work starts in a later phase.
	 *
	 * Source: DT reg third/fourth tuples; ARM IHI 0069H.b §12.
	 */
	constexpr uint64_t GICH_BASE = LOCAL_PERIPHERAL_BASE + 0x4000;
	constexpr uint64_t GICV_BASE = LOCAL_PERIPHERAL_BASE + 0x6000;

	// RP1 GPIO — IO_BANK0 (function select) and PADS_BANK0 (pad control).
	// Addresses derived via: PERIPHERAL_BASE + (rp1_internal_offset - 0x40000000).
	// Source: RP1 Peripherals datasheet §3 (GPIO).
	constexpr uint64_t IO_BANK0_BASE  = PERIPHERAL_BASE + (0x400d0000 - 0x40000000);
	constexpr uint64_t PADS_BANK_BASE = PERIPHERAL_BASE + (0x400f0000 - 0x40000000);

	constexpr uint64_t GPIO14_CTRL = IO_BANK0_BASE + 0x074;
	constexpr uint64_t GPIO15_CTRL = IO_BANK0_BASE + 0x07C;

	constexpr uint64_t GPIO14_PAD = PADS_BANK_BASE + 0x3C;
	constexpr uint64_t GPIO15_PAD = PADS_BANK_BASE + 0x40;

	/**
	 * RP1 PL011 UART0 base — CPU physical 0xCF00030000.
	 *
	 * Still a PL011 (same IP as the Pi 3); all register offsets (DR, FR,
	 * IBRD, FBRD, LCRH, CR, IMSC, ICR, MIS, etc.) are unchanged from the
	 * Pi 3 driver — only this base address differs. The uart_pi5 driver
	 * should use PERIPHERAL_BASE + 0x30000 as its base.
	 *
	 * GPIO muxing for UART0 on Pi 5 goes through RP1 GPIO (not BCM GPIO),
	 * so the Pi 3 GPPUD/GPPUDCLK pull-up sequence does not apply here.
	 *
	 * Source: RP1 Peripherals datasheet §3.2 and /proc/iomem
	 * (`1f00030000.serial`) harvest 2026-06-04.
	 */
	constexpr uint64_t UART0_BASE  = PERIPHERAL_BASE + 0x30000;

	constexpr uint64_t UART0_DR     = UART0_BASE + 0x00;
	constexpr uint64_t UART0_RSRECR = UART0_BASE + 0x04;
	constexpr uint64_t UART0_FR     = UART0_BASE + 0x18;
	constexpr uint64_t UART0_ILPR   = UART0_BASE + 0x20;
	constexpr uint64_t UART0_IBRD   = UART0_BASE + 0x24;
	constexpr uint64_t UART0_FBRD   = UART0_BASE + 0x28;
	constexpr uint64_t UART0_LCRH   = UART0_BASE + 0x2C;
	constexpr uint64_t UART0_CR     = UART0_BASE + 0x30;
	constexpr uint64_t UART0_IFLS   = UART0_BASE + 0x34;
	constexpr uint64_t UART0_IMSC   = UART0_BASE + 0x38;
	constexpr uint64_t UART0_RIS    = UART0_BASE + 0x3C;
	constexpr uint64_t UART0_MIS    = UART0_BASE + 0x40;
	constexpr uint64_t UART0_ICR    = UART0_BASE + 0x44;
	constexpr uint64_t UART0_DMACR  = UART0_BASE + 0x48;
	constexpr uint64_t UART0_ITCR   = UART0_BASE + 0x80;
	constexpr uint64_t UART0_ITIP   = UART0_BASE + 0x84;
	constexpr uint64_t UART0_ITOP   = UART0_BASE + 0x88;
	constexpr uint64_t UART0_TDR    = UART0_BASE + 0x8C;

	/**
	 * RP1 UART clock control register — CPU physical 0x1C00018054.
	 *
	 * Controls the source and enable state of `clk_uart`, the reference clock
	 * fed to the PL011 baud-rate generator. Firmware does not initialize this
	 * clock before handoff; `uart_init` must configure it before the UART can
	 * transmit.
	 *
	 * Relevant fields (derived from `linux/drivers/clk/clk-rp1.c`, rpi-6.1.y):
	 *   - Bits [3:0]  SRC     — glitchless source mux. SRC=1 (AUX_SEL) routes
	 *                           the auxiliary path selected by AUXSRC.
	 *   - Bits [9:5]  AUXSRC  — auxiliary source select. AUXSRC=2 = xosc (50 MHz
	 *                           crystal oscillator).
	 *   - Bit  11     ENABLE  — gates the clock output. Must be set after source
	 *                           and divisor are configured.
	 *
	 * Write `(2 << 5) | (1 << 11) | (1 << 0)` to select xosc and enable.
	 */
	constexpr uint64_t CLK_UART_CTRL    = PERIPHERAL_BASE + (0x40018000 - 0x40000000) + 0x00054;

	/**
	 * RP1 UART clock integer divisor — CPU physical 0x1C00018058.
	 *
	 * Divides the selected clock source before it reaches the PL011. Writing 1
	 * passes the source through without division, yielding 50 MHz from xosc.
	 * Must be written before ENABLE is set in CLK_UART_CTRL.
	 *
	 * Source: `linux/drivers/clk/clk-rp1.c` (rpi-6.1.y), `rp1_clock_set_rate`.
	 */
	constexpr uint64_t CLK_UART_DIV_INT = PERIPHERAL_BASE + (0x40018000 - 0x40000000) + 0x00058;

	/**
	 * RP1 UART clock source select status — CPU physical 0x1C00018060.
	 *
	 * Read-only register reflecting which source is currently active after any
	 * glitch-free mux transition. No write is needed during normal init.
	 *
	 * Source: `linux/drivers/clk/clk-rp1.c` (rpi-6.1.y).
	 */
	constexpr uint64_t CLK_UART_SEL     = PERIPHERAL_BASE + (0x40018000 - 0x40000000) + 0x00060;

	/**
	 * RP1 SYS_RIO base — CPU physical 0x1C000E0000.
	 *
	 * SYS_RIO (Synchronous Registered I/O) is RP1's fast parallel GPIO block.
	 * It exposes a 28-bit wide register interface for bulk GPIO output and
	 * input operations, bypassing the per-pin FUNCSEL path of IO_BANK0. Each
	 * register is available at four address aliases that perform different
	 * operations on write (see atomic alias constants below).
	 *
	 * Source: RP1 Peripherals datasheet §3.3 (SYS_RIO).
	 */
	constexpr uint64_t SYS_RIO_BASE     = PERIPHERAL_BASE + (0x400e0000 - 0x40000000);

	/**
	 * RIO_OUT  (+0x00) — output data register. A set bit drives the
	 *                     corresponding GPIO pin high when OE is also set.
	 * RIO_OE   (+0x04) — output enable register. Set a bit to configure the
	 *                     corresponding GPIO as an output.
	 * RIO_NOSYNC_IN (+0x08) — raw input register, not synchronized to the
	 *                     system clock. Use only for inputs known to be stable;
	 *                     metastability risk on asynchronous signals.
	 * RIO_SYNC_IN   (+0x0C) — synchronized input register. Passes the input
	 *                     through a flip-flop stage before sampling, eliminating
	 *                     metastability for asynchronous signals.
	 */
	constexpr uint64_t RIO_OUT          = SYS_RIO_BASE + 0x00;
	constexpr uint64_t RIO_OE           = SYS_RIO_BASE + 0x04;
	constexpr uint64_t RIO_NOSYNC_IN    = SYS_RIO_BASE + 0x08;
	constexpr uint64_t RIO_SYNC_IN      = SYS_RIO_BASE + 0x0C;

	/**
	 * RP1 atomic access aliases for SYS_RIO (and other RP1 APB peripherals).
	 *
	 * RP1 maps each APB peripheral block at four consecutive 4 KiB windows.
	 * A write to the window at offset +0x0000 is a normal read/write. Writes
	 * to the other windows perform register-level atomic operations without
	 * requiring a read-modify-write sequence:
	 *
	 *   NORMAL_RW   (+0x0000) — standard read/write.
	 *   ATOMIC_XOR  (+0x1000) — written bits are XORed into the register.
	 *   ATOMIC_SET  (+0x2000) — written bits are ORed into the register (set).
	 *   ATOMIC_CLEAR(+0x4000) — written bits are cleared from the register.
	 *
	 * Usage: add the alias offset to the register address before writing.
	 * Example — atomically set bit 14 of RIO_OE:
	 *   `mmio_write(RIO_OE + ATOMIC_SET, 1 << 14);`
	 *
	 * Source: RP1 Peripherals datasheet §2.4 (Atomic Register Access).
	 */
	constexpr uint64_t NORMAL_RW        = 0x0000;
	constexpr uint64_t ATOMIC_XOR       = 0x1000;
	constexpr uint64_t ATOMIC_SET       = 0x2000;
	constexpr uint64_t ATOMIC_CLEAR     = 0x4000;



	/*
	 * SDIO Addresses and MMIO offsets.
	 *
	 * Note that this list does not contain the UHS-II register addresses as
	 * the PI 5 does not support this feature. For more information please
	 * refer to the SD Host Controller Simplified Specification, V4.20 from
	 * page 45 onward.
	 */
	// TODO: Write docstring when complete.

	/**
	 * SDIO0_BASE — CPU physical 0x1000fff000.
	 *
	 * Points at BCM2712's own native SDHCI controller (`sdio1: mmc@fff000`,
	 * `compatible = "brcm,bcm2712-sdhci", "brcm,sdhci-brcmstb"` in
	 * bcm2712.dtsi) — the controller actually wired to this board's
	 * physical microSD card slot, per the retail board DTS's own comment
	 * on its `&sdio1` node: "SDIO1 is used to drive the SD card".
	 *
	 * This was NOT the original target: every register offset below was
	 * first built and hardware-verified against RP1's own `SDIO0` block
	 * (real, functional silicon at PERIPHERAL_BASE + 0x180000) before
	 * discovering — via a persistent, spec-matching "CMD line conflict"
	 * signature on every command, including CMD0 — that RP1's SDIO0/SDIO1
	 * are simply not connected to this board's card slot at all. Full
	 * derivation, sources, and the address-translation formula below:
	 * p-docs/claude-notes/bcm2712-sdhci-migration-notes.md.
	 *
	 * Address translation is different from RP1's PCIe-BAR-window scheme
	 * (PERIPHERAL_BASE + rp1-internal-offset): bcm2712.dtsi's root
	 * simple-bus node declares `ranges = <0x0 0x10 0x0 0x80000000>`, so any
	 * child address X in [0, 0x80000000) maps to real physical
	 * `0x1000000000 + X`. Cross-checked against this project's own
	 * already-hardware-confirmed LOCAL_PERIPHERAL_BASE (GIC-400): the
	 * devicetree's own `gicv2@7fff9000` node translates via this exact
	 * formula to 0x107FFF9000, which matches GICD_BASE
	 * (LOCAL_PERIPHERAL_BASE + 0x1000) exactly — same mechanism,
	 * independently verified against a peripheral already proven correct
	 * on real hardware, not just re-derived from the same devicetree
	 * source in isolation.
	 *
	 * `sdio1`'s `reg` property is two windows: `<0x00fff000 0x260>` (the
	 * standard SDHCI "host" registers every offset below still targets —
	 * same spec, same offsets, only the base changed) and
	 * `<0x00fff400 0x200>` (a small BCM2712-specific "cfg" extension block
	 * — see SDHCI_CFG_BASE below).
	 */
	constexpr uint64_t SDIO0_BASE = 0x1000fff000;

	/** RP1's second SDIO instance (`rp1_mmc1`) — like RP1's SDIO0, real
	 *  silicon but not wired to this board's card slot. Unused by this
	 *  driver; kept only so the register-offset constants below that are
	 *  derived from it still compile if anything ever needs to probe RP1's
	 *  side directly. */
	constexpr uint64_t SDIO1_BASE = PERIPHERAL_BASE + (0x40184000 - 0x40000000);

	/**
	 * BCM2712 "cfg" vendor register block — CPU physical 0x1000fff400.
	 * The second `reg` window on the `sdio1` devicetree node (see
	 * SDIO0_BASE above), separate from the standard SDHCI "host" registers.
	 */
	constexpr uint64_t SDHCI_CFG_BASE = SDIO0_BASE + 0x400;

	/**
	 * SD vs eMMC pin-timing select — CPU physical 0x1000fff444.
	 *
	 * Per Linux's sdhci_bcm2712_set_clock() (drivers/mmc/host/sdhci-
	 * brcmstb.c), this must be written every time the SD clock is
	 * (re)configured, alongside the standard Clock Control sequence. This
	 * driver never does eMMC/HS-eMMC timings, so it's always written as SD
	 * (bit 1), never MMC (bit 0).
	 */
	constexpr uint64_t SDIO_CFG_SD_PIN_SEL      = SDHCI_CFG_BASE + 0x44;
	constexpr uint32_t SDIO_CFG_SD_PIN_SEL_MMC  = (1U << 0);
	constexpr uint32_t SDIO_CFG_SD_PIN_SEL_SD   = (1U << 1);
	constexpr uint32_t SDIO_CFG_SD_PIN_SEL_MASK = 0x3U;

	/**
	 * BCM2712 main pinctrl block — CPU physical 0x107d504100.
	 *
	 * `pinctrl@7d504100`, `compatible = "brcm,bcm2712c0-pinctrl"` — the
	 * retail board's C0 silicon stepping, confirmed directly from its own
	 * devicetree node (pinctrl-brcmstb-bcm2712.c defines two incompatible
	 * register tables for C0 vs D0; using the wrong one would silently
	 * target the wrong pins entirely). Holds the pull-up/down
	 * configuration for `emmc_cmd`/`emmc_dat0-3` — a subsystem the
	 * RP1-targeted driver never had to touch, since RP1's own pins have
	 * entirely separate pull config. Without a pull-up here, the CMD line
	 * has no defined idle-high state and can float/sag low the instant
	 * nothing is actively driving it — the leading hypothesis for the
	 * "drove CMD high, read back low" CMD-line-conflict signature every
	 * RP1-targeted command hit; see the migration notes for the full
	 * per-pin bit-offset derivation.
	 */
	constexpr uint64_t PINCTRL_BASE = 0x107d504100;

	/** EMMC_CMD/DAT0-3 pull-config register — CPU physical 0x107d50412c.
	 *  All five pins pack into this one 32-bit register, 2 bits each
	 *  (00=none, 01=pull-down, 10=pull-up); EMMC_CLK and EMMC_DS share the
	 *  register but are deliberately left alone (CLK is host-driven, DS is
	 *  an eMMC-only HS400 signal irrelevant to plain SD cards) — hence the
	 *  read-modify-write mask below instead of a blind overwrite. */
	constexpr uint64_t PINCTRL_EMMC_PULL_REG       = PINCTRL_BASE + 0x2C;
	constexpr uint32_t PINCTRL_EMMC_CMD_DAT_MASK   = 0xFF0CU; // CMD/DAT0-3's five 2-bit fields (bits 2-3,8-9,10-11,12-13,14-15).
	constexpr uint32_t PINCTRL_EMMC_CMD_DAT_PULLUP = 0xAA08U; // Same five fields, each set to 10b (pull-up).

	/**
	* SDMA System Address (default) or 32-bit Block Count (only if Host
	* Version 4 Enable is set in Host Control 2) — the same physical register
	* serves either purpose depending on that mode bit, never both at once.
	* In its default (SDMA) meaning, holds the system memory address an SDMA
	* transfer reads from or writes to.
	*/
	constexpr uint64_t BLOCK_COUNT_LOW  = 0x000;
	constexpr uint64_t BLOCK_COUNT_HIGH = 0x002;
	constexpr uint64_t BLOCK_COUNT_LOW_SDIO0  = SDIO0_BASE + BLOCK_COUNT_LOW;
	constexpr uint64_t BLOCK_COUNT_LOW_SDIO1  = SDIO1_BASE + BLOCK_COUNT_LOW;
	constexpr uint64_t BLOCK_COUNT_HIGH_SDIO0 = SDIO0_BASE + BLOCK_COUNT_HIGH;
	constexpr uint64_t BLOCK_COUNT_HIGH_SDIO1 = SDIO1_BASE + BLOCK_COUNT_HIGH;

	/** Byte size of a single data block for the upcoming transfer. Unused bits
	*  are reserved. */
	constexpr uint64_t BLOCK_SIZE = 0x004;
	constexpr uint64_t BLOCK_SIZE_SDIO0 = SDIO0_BASE + BLOCK_SIZE;
	constexpr uint64_t BLOCK_SIZE_SDIO1 = SDIO1_BASE + BLOCK_SIZE;

	/** Number of blocks to move for the current multiple-block transfer;
	*  decrements automatically as each block completes. */
	constexpr uint64_t BLOCK_COUNT = 0x006;
	constexpr uint64_t BLOCK_COUNT_SDIO0 = SDIO0_BASE + BLOCK_COUNT;
	constexpr uint64_t BLOCK_COUNT_SDIO1 = SDIO1_BASE + BLOCK_COUNT;

	/** Holds the argument sent alongside the next issued SD command. */
	constexpr uint64_t ARGUMENT_LOW  = 0x008;
	constexpr uint64_t ARGUMENT_HIGH = 0x00A;
	constexpr uint64_t ARGUMENT_LOW_SDIO0  = SDIO0_BASE + ARGUMENT_LOW;
	constexpr uint64_t ARGUMENT_LOW_SDIO1  = SDIO1_BASE + ARGUMENT_LOW;
	constexpr uint64_t ARGUMENT_HIGH_SDIO0 = SDIO0_BASE + ARGUMENT_HIGH;
	constexpr uint64_t ARGUMENT_HIGH_SDIO1 = SDIO1_BASE + ARGUMENT_HIGH;

	/** Configures how the data phase of the next command behaves — transfer
	*  direction, single vs. multi-block, DMA use, and automatic stop-command
	*  behavior. Some bits are reserved. */
	constexpr uint64_t TRANSFER_MODE = 0x00C;
	constexpr uint64_t TRANSFER_MODE_SDIO0 = SDIO0_BASE + TRANSFER_MODE;
	constexpr uint64_t TRANSFER_MODE_SDIO1 = SDIO1_BASE + TRANSFER_MODE;

	/** Holds the command index and response-format bits for the next command;
	*  writing this register's upper byte is what actually triggers the
	*  controller to issue it onto the bus. Some bits are reserved. */
	constexpr uint64_t COMMAND = 0x00E;
	constexpr uint64_t COMMAND_SDIO0 = SDIO0_BASE + COMMAND;
	constexpr uint64_t COMMAND_SDIO1 = SDIO1_BASE + COMMAND;

	/** Card response storage. Content and how it's packed across these
	*  registers depends on the response type of the command that was issued
	*  (short vs. long response). */
	constexpr uint64_t RESPONSE0 = 0x010;
	constexpr uint64_t RESPONSE1 = 0x012;
	constexpr uint64_t RESPONSE2 = 0x014;
	constexpr uint64_t RESPONSE3 = 0x016;
	constexpr uint64_t RESPONSE4 = 0x018;
	constexpr uint64_t RESPONSE5 = 0x01A;
	constexpr uint64_t RESPONSE6 = 0x01C;
	constexpr uint64_t RESPONSE7 = 0x01E;
	constexpr uint64_t RESPONSE0_SDIO0 = SDIO0_BASE + RESPONSE0;
	constexpr uint64_t RESPONSE0_SDIO1 = SDIO1_BASE + RESPONSE0;
	constexpr uint64_t RESPONSE1_SDIO0 = SDIO0_BASE + RESPONSE1;
	constexpr uint64_t RESPONSE1_SDIO1 = SDIO1_BASE + RESPONSE1;
	constexpr uint64_t RESPONSE2_SDIO0 = SDIO0_BASE + RESPONSE2;
	constexpr uint64_t RESPONSE2_SDIO1 = SDIO1_BASE + RESPONSE2;
	constexpr uint64_t RESPONSE3_SDIO0 = SDIO0_BASE + RESPONSE3;
	constexpr uint64_t RESPONSE3_SDIO1 = SDIO1_BASE + RESPONSE3;
	constexpr uint64_t RESPONSE4_SDIO0 = SDIO0_BASE + RESPONSE4;
	constexpr uint64_t RESPONSE4_SDIO1 = SDIO1_BASE + RESPONSE4;
	constexpr uint64_t RESPONSE5_SDIO0 = SDIO0_BASE + RESPONSE5;
	constexpr uint64_t RESPONSE5_SDIO1 = SDIO1_BASE + RESPONSE5;
	constexpr uint64_t RESPONSE6_SDIO0 = SDIO0_BASE + RESPONSE6;
	constexpr uint64_t RESPONSE6_SDIO1 = SDIO1_BASE + RESPONSE6;
	constexpr uint64_t RESPONSE7_SDIO0 = SDIO0_BASE + RESPONSE7;
	constexpr uint64_t RESPONSE7_SDIO1 = SDIO1_BASE + RESPONSE7;

	/** Data port used to shuttle block data to/from the controller's internal
	*  buffer one access at a time during non-DMA (PIO) transfers. */
	constexpr uint64_t BUFFER_DATA_0 = 0x020;
	constexpr uint64_t BUFFER_DATA_1 = 0x022;
	constexpr uint64_t BUFFER_DATA_0_SDIO0 = SDIO0_BASE + BUFFER_DATA_0;
	constexpr uint64_t BUFFER_DATA_0_SDIO1 = SDIO1_BASE + BUFFER_DATA_0;
	constexpr uint64_t BUFFER_DATA_1_SDIO0 = SDIO0_BASE + BUFFER_DATA_1;
	constexpr uint64_t BUFFER_DATA_1_SDIO1 = SDIO1_BASE + BUFFER_DATA_1;

	/** Read-only live snapshot of controller/bus state — card presence, line
	*  levels, and whether a command or data transfer can currently be
	*  issued. Some bits are reserved. */
	constexpr uint64_t PRESENT_STATE = 0x024;
	constexpr uint64_t PRESENT_STATE_SDIO0 = SDIO0_BASE + PRESENT_STATE;
	constexpr uint64_t PRESENT_STATE_SDIO1 = SDIO1_BASE + PRESENT_STATE;

	/** General transfer configuration — bus width, speed mode, and DMA mode
	*  selection. */
	constexpr uint64_t HOST_CONTROL_1 = 0x028;
	constexpr uint64_t HOST_CONTROL_1_SDIO0 = SDIO0_BASE + HOST_CONTROL_1;
	constexpr uint64_t HOST_CONTROL_1_SDIO1 = SDIO1_BASE + HOST_CONTROL_1;

	/** Controls whether, and at what voltage, the controller supplies power to
	*  the card. Some bits are reserved. */
	constexpr uint64_t POWER_CONTROL = 0x029;
	constexpr uint64_t POWER_CONTROL_SDIO0 = SDIO0_BASE + POWER_CONTROL;
	constexpr uint64_t POWER_CONTROL_SDIO1 = SDIO1_BASE + POWER_CONTROL;

	/** Controls stopping/continuing a transfer at the gap between blocks. Some
	*  bits are reserved. */
	constexpr uint64_t BLOCK_GAP_CONTROL = 0x02A;
	constexpr uint64_t BLOCK_GAP_CONTROL_SDIO0 = SDIO0_BASE + BLOCK_GAP_CONTROL;
	constexpr uint64_t BLOCK_GAP_CONTROL_SDIO1 = SDIO1_BASE + BLOCK_GAP_CONTROL;

	/** Enables the controller to wake the host system on card-related events
	*  (insertion, removal, card interrupt). Some bits are reserved. */
	constexpr uint64_t WAKEUP_CONTROL = 0x02B;
	constexpr uint64_t WAKEUP_CONTROL_SDIO0 = SDIO0_BASE + WAKEUP_CONTROL;
	constexpr uint64_t WAKEUP_CONTROL_SDIO1 = SDIO1_BASE + WAKEUP_CONTROL;

	/** Selects and enables the SD clock supplied to the card, and reports
	*  whether that clock has stabilized. */
	constexpr uint64_t CLOCK_CONTROL = 0x02C;
	constexpr uint64_t CLOCK_CONTROL_SDIO0 = SDIO0_BASE + CLOCK_CONTROL;
	constexpr uint64_t CLOCK_CONTROL_SDIO1 = SDIO1_BASE + CLOCK_CONTROL;

	/** Sets how long the controller waits before flagging a data-line timeout.
	*  Some bits are reserved. */
	constexpr uint64_t TIMEOUT_CONTROL = 0x02E;
	constexpr uint64_t TIMEOUT_CONTROL_SDIO0 = SDIO0_BASE + TIMEOUT_CONTROL;
	constexpr uint64_t TIMEOUT_CONTROL_SDIO1 = SDIO1_BASE + TIMEOUT_CONTROL;

	/** Triggers a reset of part or all of the controller's internal state;
	*  the written bit(s) self-clear once the reset completes. Some bits are
	*  reserved. */
	constexpr uint64_t SOFTWARE_RESET = 0x02F;
	constexpr uint64_t SOFTWARE_RESET_SDIO0 = SDIO0_BASE + SOFTWARE_RESET;
	constexpr uint64_t SOFTWARE_RESET_SDIO1 = SDIO1_BASE + SOFTWARE_RESET;

	/** Latched status bits for routine events (command complete, transfer
	*  complete, card insertion/removal, buffer ready, etc.); write 1 to a bit
	*  to clear it. */
	constexpr uint64_t NORMAL_INTERRUPT_STATUS = 0x030;
	constexpr uint64_t NORMAL_INTERRUPT_STATUS_SDIO0 = SDIO0_BASE + NORMAL_INTERRUPT_STATUS;
	constexpr uint64_t NORMAL_INTERRUPT_STATUS_SDIO1 = SDIO1_BASE + NORMAL_INTERRUPT_STATUS;

	/** Latched status bits for error conditions encountered during a command
	*  or data transfer; write 1 to a bit to clear it. */
	constexpr uint64_t ERROR_INTERRUPT_STATUS = 0x032;
	constexpr uint64_t ERROR_INTERRUPT_STATUS_SDIO0 = SDIO0_BASE + ERROR_INTERRUPT_STATUS;
	constexpr uint64_t ERROR_INTERRUPT_STATUS_SDIO1 = SDIO1_BASE + ERROR_INTERRUPT_STATUS;

	/** Masks which Normal Interrupt Status bits are allowed to latch at all;
	*  independent of whether they also raise the interrupt line. */
	constexpr uint64_t NORMAL_INTERRUPT_STATUS_ENABLE = 0x034;
	constexpr uint64_t NORMAL_INTERRUPT_STATUS_ENABLE_SDIO0 = SDIO0_BASE + NORMAL_INTERRUPT_STATUS_ENABLE;
	constexpr uint64_t NORMAL_INTERRUPT_STATUS_ENABLE_SDIO1 = SDIO1_BASE + NORMAL_INTERRUPT_STATUS_ENABLE;

	/** Masks which Error Interrupt Status bits are allowed to latch at all. */
	constexpr uint64_t ERROR_INTERRUPT_STATUS_ENABLE = 0x036;
	constexpr uint64_t ERROR_INTERRUPT_STATUS_ENABLE_SDIO0 = SDIO0_BASE + ERROR_INTERRUPT_STATUS_ENABLE;
	constexpr uint64_t ERROR_INTERRUPT_STATUS_ENABLE_SDIO1 = SDIO1_BASE + ERROR_INTERRUPT_STATUS_ENABLE;

	/** Of the status bits already allowed to latch, masks which ones are also
	*  allowed to actually raise the controller's interrupt line. */
	constexpr uint64_t NORMAL_INTERRUPT_SIGNAL_ENABLE = 0x038;
	constexpr uint64_t NORMAL_INTERRUPT_SIGNAL_ENABLE_SDIO0 = SDIO0_BASE + NORMAL_INTERRUPT_SIGNAL_ENABLE;
	constexpr uint64_t NORMAL_INTERRUPT_SIGNAL_ENABLE_SDIO1 = SDIO1_BASE + NORMAL_INTERRUPT_SIGNAL_ENABLE;

	/** Same idea as Normal Interrupt Signal Enable, for error status bits. */
	constexpr uint64_t ERROR_INTERRUPT_SIGNAL_ENABLE = 0x03A;
	constexpr uint64_t ERROR_INTERRUPT_SIGNAL_ENABLE_SDIO0 = SDIO0_BASE + ERROR_INTERRUPT_SIGNAL_ENABLE;
	constexpr uint64_t ERROR_INTERRUPT_SIGNAL_ENABLE_SDIO1 = SDIO1_BASE + ERROR_INTERRUPT_SIGNAL_ENABLE;

	/** Reports what went wrong with an automatically-issued stop command
	*  (Auto CMD12/CMD23); only meaningful once the Auto CMD Error bit is set
	*  in Error Interrupt Status. Some bits are reserved. */
	constexpr uint64_t AUTO_CMD_ERROR_STATUS = 0x03C;
	constexpr uint64_t AUTO_CMD_ERROR_STATUS_SDIO0 = SDIO0_BASE + AUTO_CMD_ERROR_STATUS;
	constexpr uint64_t AUTO_CMD_ERROR_STATUS_SDIO1 = SDIO1_BASE + AUTO_CMD_ERROR_STATUS;

	/** Controller-wide mode configuration — UHS mode select, 1.8V signaling,
	*  tuning control, and the Host Version 4 Enable bit that changes the
	*  meaning of several other registers in this block. */
	constexpr uint64_t HOST_CONTROL_2 = 0x03E;
	constexpr uint64_t HOST_CONTROL_2_SDIO0 = SDIO0_BASE + HOST_CONTROL_2;
	constexpr uint64_t HOST_CONTROL_2_SDIO1 = SDIO1_BASE + HOST_CONTROL_2;

	/** Read-only, hardware-fixed description of what this controller instance
	*  actually supports — voltages, speed modes, DMA types, max block
	*  length, base clock frequency, and similar. Some bits are reserved. */
	constexpr uint64_t CAPABILITIES1 = 0x040;
	constexpr uint64_t CAPABILITIES2 = 0x042;
	constexpr uint64_t CAPABILITIES3 = 0x044;
	constexpr uint64_t CAPABILITIES4 = 0x046;
	constexpr uint64_t CAPABILITIES1_SDIO0 = SDIO0_BASE + CAPABILITIES1;
	constexpr uint64_t CAPABILITIES1_SDIO1 = SDIO1_BASE + CAPABILITIES1;
	constexpr uint64_t CAPABILITIES2_SDIO0 = SDIO0_BASE + CAPABILITIES2;
	constexpr uint64_t CAPABILITIES2_SDIO1 = SDIO1_BASE + CAPABILITIES2;
	constexpr uint64_t CAPABILITIES3_SDIO0 = SDIO0_BASE + CAPABILITIES3;
	constexpr uint64_t CAPABILITIES3_SDIO1 = SDIO1_BASE + CAPABILITIES3;
	constexpr uint64_t CAPABILITIES4_SDIO0 = SDIO0_BASE + CAPABILITIES4;
	constexpr uint64_t CAPABILITIES4_SDIO1 = SDIO1_BASE + CAPABILITIES4;

	/** Read-only, reports the maximum current the host system can supply per
	*  voltage rail; only meaningful if the corresponding voltage's support is
	*  indicated in Capabilities. */
	constexpr uint64_t MAX_CAPABILITIES1 = 0x048;
	constexpr uint64_t MAX_CAPABILITIES2 = 0x04A;
	constexpr uint64_t MAX_CAPABILITIES1_SDIO0 = SDIO0_BASE + MAX_CAPABILITIES1;
	constexpr uint64_t MAX_CAPABILITIES1_SDIO1 = SDIO1_BASE + MAX_CAPABILITIES1;
	constexpr uint64_t MAX_CAPABILITIES2_SDIO0 = SDIO0_BASE + MAX_CAPABILITIES2;
	constexpr uint64_t MAX_CAPABILITIES2_SDIO1 = SDIO1_BASE + MAX_CAPABILITIES2;

	/** Continuation of Maximum Current Capabilities; mostly reserved today. */
	constexpr uint64_t RES_MAX_CAPABILITIES1 = 0x04C;
	constexpr uint64_t RES_MAX_CAPABILITIES2 = 0x04E;
	constexpr uint64_t RES_MAX_CAPABILITIES1_SDIO0 = SDIO0_BASE + RES_MAX_CAPABILITIES1;
	constexpr uint64_t RES_MAX_CAPABILITIES1_SDIO1 = SDIO1_BASE + RES_MAX_CAPABILITIES1;
	constexpr uint64_t RES_MAX_CAPABILITIES2_SDIO0 = SDIO0_BASE + RES_MAX_CAPABILITIES2;
	constexpr uint64_t RES_MAX_CAPABILITIES2_SDIO1 = SDIO1_BASE + RES_MAX_CAPABILITIES2;

	/** Write-only test hook: artificially sets bits in Auto CMD Error Status,
	*  for exercising error-handling paths without a real error occurring. */
	constexpr uint64_t FORCE_AUTO_CMD_EVENT = 0x050;
	constexpr uint64_t FORCE_AUTO_CMD_EVENT_SDIO0 = SDIO0_BASE + FORCE_AUTO_CMD_EVENT;
	constexpr uint64_t FORCE_AUTO_CMD_EVENT_SDIO1 = SDIO1_BASE + FORCE_AUTO_CMD_EVENT;

	/** Same idea as Force Event for Auto CMD Error Status, but for Error
	*  Interrupt Status bits instead. */
	constexpr uint64_t FORCE_ERROR_INTERRUPT_EVENT = 0x052;
	constexpr uint64_t FORCE_ERROR_INTERRUPT_EVENT_SDIO0 = SDIO0_BASE + FORCE_ERROR_INTERRUPT_EVENT;
	constexpr uint64_t FORCE_ERROR_INTERRUPT_EVENT_SDIO1 = SDIO1_BASE + FORCE_ERROR_INTERRUPT_EVENT;

	/** Reports why/where an ADMA (descriptor-driven DMA) transfer failed; used
	*  together with the ADMA System Address register to locate the failing
	*  descriptor. Some bits are reserved. */
	constexpr uint64_t ADMA_ERROR_STATUS = 0x054;
	constexpr uint64_t ADMA_ERROR_STATUS_SDIO0 = SDIO0_BASE + ADMA_ERROR_STATUS;
	constexpr uint64_t ADMA_ERROR_STATUS_SDIO1 = SDIO1_BASE + ADMA_ERROR_STATUS;

	/** Physical address of the descriptor table ADMA2/ADMA3 reads from for a
	*  descriptor-driven DMA transfer; one 64-bit register held as two 32-bit
	*  halves. */
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_LOW  = 0x058;
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_HIGH = 0x05C;
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_LOW_SDIO0  = SDIO0_BASE + ADMA_SYSTEM_ADDRESS_LOW;
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_LOW_SDIO1  = SDIO1_BASE + ADMA_SYSTEM_ADDRESS_LOW;
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_HIGH_SDIO0 = SDIO0_BASE + ADMA_SYSTEM_ADDRESS_HIGH;
	constexpr uint64_t ADMA_SYSTEM_ADDRESS_HIGH_SDIO1 = SDIO1_BASE + ADMA_SYSTEM_ADDRESS_HIGH;

	/** Per-speed-mode preset clock-divisor/driver-strength values; used
	*  automatically instead of Host Driver-computed settings when Preset
	*  Value Enable is set in Host Control 2. One register per bus speed mode
	*  (initialization, default speed, high speed, then each UHS-I mode). */
	constexpr uint64_t PRESET_VALUE_1 = 0x060;
	constexpr uint64_t PRESET_VALUE_2 = 0x062;
	constexpr uint64_t PRESET_VALUE_3 = 0x064;
	constexpr uint64_t PRESET_VALUE_4 = 0x066;
	constexpr uint64_t PRESET_VALUE_5 = 0x068;
	constexpr uint64_t PRESET_VALUE_6 = 0x06A;
	constexpr uint64_t PRESET_VALUE_7 = 0x06C;
	constexpr uint64_t PRESET_VALUE_8 = 0x06E;
	constexpr uint64_t PRESET_VALUE_1_SDIO0 = SDIO0_BASE + PRESET_VALUE_1;
	constexpr uint64_t PRESET_VALUE_1_SDIO1 = SDIO1_BASE + PRESET_VALUE_1;
	constexpr uint64_t PRESET_VALUE_2_SDIO0 = SDIO0_BASE + PRESET_VALUE_2;
	constexpr uint64_t PRESET_VALUE_2_SDIO1 = SDIO1_BASE + PRESET_VALUE_2;
	constexpr uint64_t PRESET_VALUE_3_SDIO0 = SDIO0_BASE + PRESET_VALUE_3;
	constexpr uint64_t PRESET_VALUE_3_SDIO1 = SDIO1_BASE + PRESET_VALUE_3;
	constexpr uint64_t PRESET_VALUE_4_SDIO0 = SDIO0_BASE + PRESET_VALUE_4;
	constexpr uint64_t PRESET_VALUE_4_SDIO1 = SDIO1_BASE + PRESET_VALUE_4;
	constexpr uint64_t PRESET_VALUE_5_SDIO0 = SDIO0_BASE + PRESET_VALUE_5;
	constexpr uint64_t PRESET_VALUE_5_SDIO1 = SDIO1_BASE + PRESET_VALUE_5;
	constexpr uint64_t PRESET_VALUE_6_SDIO0 = SDIO0_BASE + PRESET_VALUE_6;
	constexpr uint64_t PRESET_VALUE_6_SDIO1 = SDIO1_BASE + PRESET_VALUE_6;
	constexpr uint64_t PRESET_VALUE_7_SDIO0 = SDIO0_BASE + PRESET_VALUE_7;
	constexpr uint64_t PRESET_VALUE_7_SDIO1 = SDIO1_BASE + PRESET_VALUE_7;
	constexpr uint64_t PRESET_VALUE_8_SDIO0 = SDIO0_BASE + PRESET_VALUE_8;
	constexpr uint64_t PRESET_VALUE_8_SDIO1 = SDIO1_BASE + PRESET_VALUE_8;

	/** Physical address of the Integrated Descriptor ADMA3 reads to start a
	*  transfer — writing this register is what triggers ADMA3, the same role
	*  the Command register plays for a normal command. One 64-bit register
	*  held as two 32-bit halves; only relevant when DMA Select in Host
	*  Control 1 selects ADMA3. */
	constexpr uint64_t ADMA3_ID_ADDRESS_LOW  = 0x078;
	constexpr uint64_t ADMA3_ID_ADDRESS_HIGH = 0x07C;
	constexpr uint64_t ADMA3_ID_ADDRESS_LOW_SDIO0  = SDIO0_BASE + ADMA3_ID_ADDRESS_LOW;
	constexpr uint64_t ADMA3_ID_ADDRESS_LOW_SDIO1  = SDIO1_BASE + ADMA3_ID_ADDRESS_LOW;
	constexpr uint64_t ADMA3_ID_ADDRESS_HIGH_SDIO0 = SDIO0_BASE + ADMA3_ID_ADDRESS_HIGH;
	constexpr uint64_t ADMA3_ID_ADDRESS_HIGH_SDIO1 = SDIO1_BASE + ADMA3_ID_ADDRESS_HIGH;


/** Address for the Host Controller Version Register.
 */

constexpr uint64_t HOST_CONTROLLER_VERSION_REGISTER = 0xFE;
constexpr uint64_t HOST_CONTROLLER_VERSION_SDIO0 = SDIO0_BASE + HOST_CONTROLLER_VERSION_REGISTER;
constexpr uint64_t HOST_CONTROLLER_VERSION_SDIO1 = SDIO1_BASE + HOST_CONTROLLER_VERSION_REGISTER;

/** RP1's dedicated SDIO clock-generator IP (compatible "raspberrypi,rp1-sdio-clk"
 *  in Raspberry Pi's own Linux kernel source, drivers/clk/clk-rp1-sdio.c) —
 *  needed only for RP1's own SDIO0/SDIO1 blocks, which turned out not to be
 *  wired to this board's card slot at all (see SDIO0_BASE's comment).
 *  BCM2712's native SDHCI controller (the one actually in use) is fed by
 *  `clk_emmc2`, a plain fixed 200MHz clock with no equivalent
 *  bring-out-of-reset step — so this block is no longer part of the active
 *  driver. Left defined here for reference only; addresses translated the
 *  same way as the old SDIO0_BASE/SDIO1_BASE (RP1-internal offset minus
 *  0x40000000, plus PERIPHERAL_BASE); reg property confirmed against
 *  rp1.dtsi ("sdio_clk0@b0004" / "sdio_clk1@b4004"). */
constexpr uint64_t RP1_SDIO0_CLKGEN_BASE = PERIPHERAL_BASE + (0x400b0004 - 0x40000000);
constexpr uint64_t RP1_SDIO1_CLKGEN_BASE = PERIPHERAL_BASE + (0x400b4004 - 0x40000000);