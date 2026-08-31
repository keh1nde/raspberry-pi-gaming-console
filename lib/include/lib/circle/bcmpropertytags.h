/**
 * @file bcmpropertytags.h
 * @brief Compatibility shim for Circle's circle/bcmpropertytags.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * `xhcidevice.cpp` `#include`s this unconditionally, but every actual use
 * (checked via grep before writing this — all in the same file) is inside
 * an `#if RASPPI == 4` block: Pi 4's VL805-firmware-reload-after-PCIe-reset
 * dance, which doesn't apply to RP1's DWC3 controllers and compiles out
 * entirely under this port's `-DRASPPI=5` (see the Makefile rule's
 * comment). This shim only needs to exist and parse — a minimal stub
 * matching the call shapes seen in that dead branch, in case it's ever
 * revisited, not a real property-tag transport.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_bcmpropertytags_h
#define _circle_bcmpropertytags_h

#include <circle/types.h>

class CBcmPropertyTags
{
public:
	boolean GetTag (unsigned nTagId, void *pTag, unsigned nTagSize, unsigned nRequestParmSize = 0)
	{
		(void) nTagId;
		(void) pTag;
		(void) nTagSize;
		(void) nRequestParmSize;
		return FALSE;
	}
};

#endif
