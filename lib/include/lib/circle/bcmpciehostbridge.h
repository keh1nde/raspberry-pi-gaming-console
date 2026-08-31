/**
 * @file bcmpciehostbridge.h
 * @brief Compatibility shim for Circle's circle/bcmpciehostbridge.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CBcmPCIeHostBridge is a full ported Linux PCIe root-complex
 * driver (link training, bridge software-init, MSI setup, BAR/outbound
 * window programming — see Circle's own file header, itself ported from
 * drivers/pci/controller/pcie-brcmstb.c). None of that is reachable from
 * our build: every call into it in the vendored xhcidevice.{h,cpp} — the
 * constructor, `Initialize()`, `EnableDevice()` — lives inside
 * `#if RASPPI == 4 && !defined (USE_XHCI_INTERNAL)` blocks, and RASPPI is
 * never defined in this port (see xhciconfig.h/sysconfig.h shim comments),
 * so those branches compile out entirely. Confirmed by reading
 * xhcidevice.cpp directly before writing this shim, not assumed.
 *
 * The one call that *does* survive preprocessing is xhci.h's
 * `XHCI_TO_DMA` macro: `CBcmPCIeHostBridge::GetDMAAddress (PCIE_BUS_XHCI)`,
 * used unconditionally (guarded only by `#ifdef USE_XHCI_INTERNAL`, which
 * we also never define). That's real and load-bearing — RP1 genuinely
 * sits behind BCM2712's PCIe root complex, and DMA buffer addresses handed
 * to the xHCI/DWC3 controller need translating into RP1's view of host
 * memory. Confirmed via bcm2712.dtsi's own `pcie1` node (the one RP1 hangs
 * off): `dma-ranges = <0x03000000 0x10 0x00000000  0x00 0x00000000
 * 0x10 0x00000000>` — a flat +0x1000000000 (64 GiB) offset covering all
 * of a Pi 5's actual RAM, not a true 1:1 identity map. Still DTS-derived,
 * not yet hardware-confirmed the way this project's other addresses are —
 * verify against real DMA behavior once we're actually driving the
 * controller.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_bcmpciehostbridge_h
#define _circle_bcmpciehostbridge_h

#include <circle/types.h>

// Only one PCIe-attached device matters for this port; a single index is
// enough (real Circle indexes by bus, since it also supports Pi 4's
// separate onboard/external xHCI buses).
#define PCIE_BUS_XHCI 0

class CBcmPCIeHostBridge
{
public:
	/// RP1's fixed devicetree-derived DMA offset — see file comment.
	static u64 GetDMAAddress (unsigned nBus)
	{
		(void) nBus;
		return 0x1000000000ULL;
	}
};

#endif
