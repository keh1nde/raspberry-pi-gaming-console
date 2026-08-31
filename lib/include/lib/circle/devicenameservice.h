/**
 * @file devicenameservice.h
 * @brief Compatibility shim for Circle's circle/devicenameservice.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CDeviceNameService is a registry mapping names like "ukbd1"/
 * "upad1" to device objects, primarily so other code can look devices up
 * by name later. Checked via grep before writing this: only `AddDevice`/
 * `RemoveDevice` are ever called anywhere in the vendored tree — nothing
 * calls `GetDevice`/`Find`, i.e. nothing ever reads the registry back. This
 * kernel also has no VFS/device-registry of its own to plug this into (see
 * `filesystem-architecture-vfs-ramfs` project history — decommissioned
 * outright). Genuine no-op shim; revisit if a later `GetDevice`-style
 * lookup shows up.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_devicenameservice_h
#define _circle_devicenameservice_h

#include <circle/device.h>
#include <circle/types.h>

class CDeviceNameService
{
public:
	static CDeviceNameService *Get (void)
	{
		static CDeviceNameService s_Instance;
		return &s_Instance;
	}

	void AddDevice (const char *pPrefix, unsigned nIndex, CDevice *pDevice, boolean bBlockDevice)
	{
		(void) pPrefix;
		(void) nIndex;
		(void) pDevice;
		(void) bBlockDevice;
	}

	void RemoveDevice (const char *pPrefix, unsigned nIndex, boolean bBlockDevice)
	{
		(void) pPrefix;
		(void) nIndex;
		(void) bBlockDevice;
	}
};

#endif
