/**
 * @file bcm2711.h
 * @brief Compatibility shim for Circle's circle/bcm2711.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle bcm2711.h is a large Pi-4-era SoC memory map. The only
 * symbols actually referenced anywhere in the vendored tree (checked via
 * grep before writing this) are `ARM_XHCI0_BASE`/`ARM_XHCI1_BASE` — xhcidevice.cpp's
 * `#else` (non-RASPPI-4) branch picks between them by device index. Both
 * now live in `kernel/board.h` (`XHCI0_BASE`/`XHCI1_BASE`) alongside every
 * other hardware-derived address in this project, with full derivation
 * commentary there — this shim just aliases Circle's naming onto them
 * rather than duplicate that documentation here.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_bcm2711_h
#define _circle_bcm2711_h

#include "kernel/board.h"

#define ARM_XHCI0_BASE XHCI0_BASE
#define ARM_XHCI1_BASE XHCI1_BASE

#endif
