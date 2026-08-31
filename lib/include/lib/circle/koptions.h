/**
 * @file koptions.h
 * @brief Compatibility shim for Circle's circle/koptions.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CKernelOptions parses `cmdline.txt` into a large set of
 * runtime-configurable options. This kernel has no command-line/options
 * file mechanism at all, and the only method actually called anywhere in
 * the vendored tree (checked via grep before writing this) is
 * `GetUSBPowerDelay()`, used as a hub port power-on settle delay in
 * `usbstandardhub.cpp`/`xhciroothub.cpp`. Returns a fixed 100 ms — matching
 * the settle delay every other hardcoded wait in this same vendored code
 * already uses for the same "give the device time to settle" purpose
 * (`xhcirootport.cpp`'s post-reset delay, `xhciusbdevice.cpp`'s post-
 * address-set delay), not an independently chosen number.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_koptions_h
#define _circle_koptions_h

class CKernelOptions
{
public:
	static CKernelOptions *Get (void)
	{
		static CKernelOptions s_Instance;
		return &s_Instance;
	}

	unsigned GetUSBPowerDelay (void) const
	{
		return 100;
	}
};

#endif
