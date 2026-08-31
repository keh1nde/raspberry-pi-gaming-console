/**
 * @file interrupt.h
 * @brief Compatibility shim for Circle's circle/interrupt.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CInterruptSystem is a full GIC wrapper (IRQ/FIQ connect,
 * shared-vs-dedicated handler dispatch, per-core state). This port is
 * polling-only for now — RP1's interrupts are dynamic PCIe MSI-X vectors
 * with no routing/ITS driver in this kernel yet (see project history) —
 * so `ConnectIRQ`/`DisconnectIRQ` (the only members actually called
 * anywhere in the vendored tree, both in `xhcidevice.cpp`) are genuine
 * no-ops here: the handler is accepted and silently discarded rather than
 * ever wired to a real GIC SPI. Revisit once real interrupt-driven xHCI
 * work lands.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_interrupt_h
#define _circle_interrupt_h

typedef void TIRQHandler (void *pParam);

class CInterruptSystem
{
public:
	void ConnectIRQ (unsigned nIRQ, TIRQHandler *pHandler, void *pParam)
	{
		(void) nIRQ;
		(void) pHandler;
		(void) pParam;
	}

	void DisconnectIRQ (unsigned nIRQ)
	{
		(void) nIRQ;
	}
};

#endif
