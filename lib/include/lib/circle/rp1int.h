/**
 * @file rp1int.h
 * @brief Compatibility shim for Circle's circle/rp1int.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle rp1int.h enumerates every RP1-internal interrupt source's
 * index (used as a PCIe MSI-X vector index — see the project's own
 * research into RP1's interrupt architecture: each RP1 peripheral gets a
 * dedicated MSI-X vector via `pci_alloc_irq_vectors(..., PCI_IRQ_MSIX)`,
 * not a fixed GIC SPI). Only `RP1_IRQ_USBHOST0_0`/`RP1_IRQ_USBHOST1_0` are
 * referenced anywhere in the vendored tree (checked via grep before
 * writing this) — as the argument to `CInterruptSystem::ConnectIRQ`/
 * `DisconnectIRQ`, which this port's shim (`circle/interrupt.h`) makes a
 * no-op, since this driver is polling-only for now. Their actual values
 * are placeholders (0/1) rather than the real RP1-internal indices — they
 * don't matter functionally today, but revisit and get the real values
 * (from RP1's own interrupt enum, not yet looked up) if this port ever
 * moves to real interrupt-driven xHCI.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_rp1int_h
#define _circle_rp1int_h

#define RP1_IRQ_USBHOST0_0 0
#define RP1_IRQ_USBHOST1_0 1

#endif
