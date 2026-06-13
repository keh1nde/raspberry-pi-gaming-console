/**
 * @file board.h
 * @brief Board-specific MMIO base addresses and CPU identifiers, selected at compile time.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Board-specific constants (MMIO base addresses, CPU identifiers)
 * selected at compile time via BOARD_PI3 or BOARD_PI5 preprocessor define.
 * Add a new #elif here when porting to a new target.
 *
 * Note: GIC-400 variables have docustrings to eliminate the need to
 * revisit ARM documentation. All variables will recieve documentation at
 * a later date.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifdef BOARD_PI3b
	constexpr uint64_t PERIPHERAL_BASE = 0x3F000000; // For Broadcom devices
	constexpr uint64_t LOCAL_PERIPHERAL_BASE = 0x40000000; // For ARM devices

	/** First physical address claimed by Raspberry Pi MMIO peripherals. The PMM
	*  manages frames up to but not including this address. */
	constexpr uint64_t PHYS_MEM_END = 0x3F800000;

	// The following are Timer, IRQ and UART MMIO addresses for the Pi 3b.
	enum {
		// Timer Addresses
		TIMER_BASE = (PERIPHERAL_BASE + 0x00003000),

		TIMER_CS = (TIMER_BASE + 0x0),
		TIMER_CLO = (TIMER_BASE + 0x4),
		TIMER_CHI = (TIMER_BASE + 0x8),

		TIMER_C0 = (TIMER_BASE + 0xC),
		TIMER_C1 = (TIMER_BASE + 0x10),
		TIMER_C2 = (TIMER_BASE + 0x14),
		TIMER_C3 = (TIMER_BASE + 0x18),

		// IRQ Addresses
		IRQ_BASE = (PERIPHERAL_BASE + 0x0000B000),
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

		// ARM Local Addresses
		IRQ_ARM_BASE = LOCAL_PERIPHERAL_BASE,
		IRQ_EN_C0 = 0x40000040,
		IRQ_C0_SOURCE = 0x40000060,

		// UART Addresses
		GPIO_BASE = 0x3F200000,

		GPPUD = (GPIO_BASE + 0x94),
		GPPUDCLK0 = (GPIO_BASE + 0x98),

		UART0_BASE = 0x3F201000, // raspi2 & 3; 0x20201000 on raspi1.

		UART0_DR     = (UART0_BASE + 0x00),
		UART0_RSRECR = (UART0_BASE + 0x04),
		UART0_FR     = (UART0_BASE + 0x18),
		UART0_ILPR   = (UART0_BASE + 0x20),
		UART0_IBRD   = (UART0_BASE + 0x24),
		UART0_FBRD   = (UART0_BASE + 0x28),
		UART0_LCRH   = (UART0_BASE + 0x2C),
		UART0_CR     = (UART0_BASE + 0x30),
		UART0_IFLS   = (UART0_BASE + 0x34),
		UART0_IMSC   = (UART0_BASE + 0x38),
		UART0_RIS    = (UART0_BASE + 0x3C),
		UART0_MIS    = (UART0_BASE + 0x40),
		UART0_ICR    = (UART0_BASE + 0x44),
		UART0_DMACR  = (UART0_BASE + 0x48),
		UART0_ITCR   = (UART0_BASE + 0x80),
		UART0_ITIP   = (UART0_BASE + 0x84),
		UART0_ITOP   = (UART0_BASE + 0x88),
		UART0_TDR    = (UART0_BASE + 0x8C),
	};

#elif defined(BOARD_PI5)
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


#else
	#error "No board defined. Pass -DBOARD_PI3 or -DBOARD_PI5."
#endif


