/**
 * @file machineinfo.h
 * @brief Compatibility shim for Circle's circle/machineinfo.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Same story as `bcmpropertytags.h`: `xhcidevice.cpp` includes this
 * unconditionally, but its one actual use (`CMachineInfo::Get()->
 * GetMachineModel()`, checked via grep before writing this) is inside an
 * `#ifdef USE_XHCI_INTERNAL` block this port never defines — dead code for
 * RP1's DWC3 controllers. Minimal stub so it exists and parses, not a real
 * board-identification query.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_machineinfo_h
#define _circle_machineinfo_h

enum TMachineModel
{
	MachineModel4B,
	MachineModelUnknown
};

class CMachineInfo
{
public:
	static CMachineInfo *Get (void)
	{
		static CMachineInfo s_Instance;
		return &s_Instance;
	}

	TMachineModel GetMachineModel (void) const
	{
		return MachineModelUnknown;
	}
};

#endif
