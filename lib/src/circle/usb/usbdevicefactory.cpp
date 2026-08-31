/**
 * @file usbdevicefactory.cpp
 * @brief CUSBDeviceFactory, scoped to keyboard + Xbox One only.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Not a port of Circle's own implementation — deliberately excluded from
 * vendoring from the start (see `lib/README.md`). Circle's real version
 * matches every device it ships a driver for (every gamepad vendor, mice,
 * mass storage, audio, ...); this one only knows about the two devices
 * this port actually supports.
 *
 * Matching mirrors the calling convention in the vendored `usbdevice.cpp`
 * (unmodified) exactly, since that file's `#include`s and call sites are
 * fixed: a device's first interface is tried against a `"ven<vid>-<pid>"`
 * string, then `"dev<vid>-<pid>-<release>"`, and only if both miss does it
 * fall back to an interface-signature string, `"int<class>-<subclass>-
 * <protocol>"`. The Xbox One controller is matched by vendor/device ID
 * (it's a specific product); the keyboard is matched by interface
 * signature `"int3-1-1"` (USB HID class 3, boot subclass 1, keyboard
 * protocol 1 — any compliant boot-protocol keyboard, not vendor-specific).
 *
 * `pName` is always this function's to free (checked against the vendored
 * `usbdevice.cpp` call sites: none of them delete the `CString` they pass
 * in) — every return path below deletes it exactly once.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <circle/usb/usbdevicefactory.h>

#include <circle/usb/usbgamepadxboxone.h>
#include <circle/usb/usbkeyboard.h>

#include <stdio.h>

namespace {

// Microsoft's USB vendor ID, and every wired-USB-protocol Xbox One-family
// product ID this driver's command sequences actually target (checked
// against Linux's xpad.c, the same reference usbgamepadxboxone.cpp itself
// cites) — not just the specific unit on hand, since supporting the whole
// family costs nothing extra here.
constexpr u16 VENDOR_ID_MICROSOFT = 0x045E;

// FindDeviceID's signature is fixed by the vendored header (no table-size
// parameter to pass instead — it walks until a zero-vendor-ID entry), so
// this needs an explicit {0, 0} terminator, not just a plain-sized array.
constexpr TUSBDeviceID XboxOneDeviceTable[] = {
	{VENDOR_ID_MICROSOFT, 0x02D1}, // Xbox One pad (original, 2013)
	{VENDOR_ID_MICROSOFT, 0x02DD}, // Xbox One pad (2015 firmware)
	{VENDOR_ID_MICROSOFT, 0x02E3}, // Xbox One Elite
	{VENDOR_ID_MICROSOFT, 0x02EA}, // Xbox One S
	{VENDOR_ID_MICROSOFT, 0x0B00}, // Xbox One Elite 2
	{VENDOR_ID_MICROSOFT, 0x0B12}, // Xbox Series S|X
	{0, 0},
};

} // namespace

boolean CUSBDeviceFactory::FindDeviceID(CString *pName, const TUSBDeviceID *pIDTable) {
	unsigned nVendorID, nDeviceID;
	if (sscanf((const char *) *pName, "ven%x-%x", &nVendorID, &nDeviceID) != 2) {
		return FALSE;
	}

	for (const TUSBDeviceID *pEntry = pIDTable; pEntry->usVendorID != 0; pEntry++) {
		if (pEntry->usVendorID == nVendorID && pEntry->usDeviceID == nDeviceID) {
			return TRUE;
		}
	}

	return FALSE;
}

CUSBFunction *CUSBDeviceFactory::GetGenericHIDDevice(CUSBFunction *pParent) {
	// Not implemented — this port only matches the keyboard by its exact
	// boot-protocol interface signature (see GetDevice below), not a
	// generic "any HID device" fallback (mice, unclassified HID, etc.
	// aren't in scope).
	(void) pParent;
	return nullptr;
}

CUSBFunction *CUSBDeviceFactory::GetDevice(CUSBFunction *pParent, CString *pName) {
	CUSBFunction *pResult = nullptr;

	if (FindDeviceID(pName, XboxOneDeviceTable)) {
		pResult = new CUSBGamePadXboxOneDevice(pParent);
	} else if (pName->Compare("int3-1-1") == 0) {
		pResult = new CUSBKeyboardDevice(pParent);
	}

	delete pName;

	return pResult;
}
