/**
 * @file sysconfig.h
 * @brief Compatibility shim for Circle's circle/sysconfig.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle sysconfig.h is a large, framework-wide tunable-options file
 * (heap sizes, screen/network/gadget-mode options, etc.) — none of it
 * applies here. The only reason anything in our vendored tree includes it
 * is xhciconfig.h's `#if RASPPI == 4` check — but RASPPI itself is defined
 * via the Makefile (`-DRASPPI=5`, scoped to `build/lib/circle/%.o`), not
 * here. An earlier version of this file left RASPPI undefined on the
 * (correct-as-far-as-it-went, but incomplete) reasoning that an undefined
 * macro in `#if RASPPI == 4` evaluates as 0, landing on the same branch as
 * a real Pi 5 would. That held for every `== 4` check encountered at the
 * time, but broke silently for the `#if RASPPI >= 4` / `<= 3` branches
 * found throughout usbdevice.cpp/usbendpoint.cpp/usbhostcontroller.cpp/
 * usbstandardhub.cpp once the build actually reached them — those pick the
 * old Pi 1-3 DWHCI-era code path when RASPPI==0. Fixed by actually
 * defining RASPPI=5 in the Makefile instead of relying on that coincidence
 * — see the Makefile rule's own comment for the full story.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_sysconfig_h
#define _circle_sysconfig_h

#endif
