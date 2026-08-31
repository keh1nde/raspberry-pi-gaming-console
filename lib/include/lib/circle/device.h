/**
 * @file device.h
 * @brief Compatibility shim for Circle's circle/device.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CDevice is a much bigger interface (Read/Write/Seek/GetSize/
 * IOCtl for block/char devices, plus a CPtrList of removed-device
 * handlers) — none of that is exercised anywhere in the vendored USB/
 * xHCI/HID tree (checked via grep before writing this: only CUSBFunction
 * inherits CDevice). Two members *are* actually called: RemoveDevice, and
 * — missed on the first grep pass, since it was filtered out as noise
 * from an unrelated file, then caught by a real build error —
 * SetProperty (four call sites in usbdevice.cpp, tagging each USB
 * function with its vendor/product/serial-number/interface-name string).
 * Nothing calls GetProperty, so only the write side is implemented. This
 * shim covers exactly that load-bearing subset rather than replicating
 * the rest unused, per lib/README.md's "compatibility shim, not a port of
 * Circle's own primitives" approach. Revisit if a later class starts
 * calling the removed-handler/IO methods or GetProperty.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_device_h
#define _circle_device_h

#include <circle/string.h>
#include <circle/types.h>

class CDevice		// Base class for all devices — minimal shim, see file comment.
{
public:
	enum TProperty
	{
		PropertyVendor,
		PropertyProduct,
		PropertySerialNumber,
		PropertyFunction,

		PropertyUnknown
	};

public:
	CDevice (CDevice *pParent = nullptr) {}

	virtual ~CDevice (void) {}

	/// Default matches Circle's own base behavior (removal always
	/// succeeds unless a subclass overrides this to do real teardown).
	virtual boolean RemoveDevice (void) { return TRUE; }

	void SetProperty (TProperty Property, const char *pString)
	{
		if (Property < PropertyUnknown)
		{
			m_Property[Property] = pString;
		}
	}

private:
	CString m_Property[PropertyUnknown];
};

#endif
