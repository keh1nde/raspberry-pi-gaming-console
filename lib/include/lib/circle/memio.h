/**
 * @file memio.h
 * @brief Compatibility shim for Circle's circle/memio.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle memio.h is a larger set of MMIO helpers (with
 * PeripheralEntry/PeripheralExit barrier bracketing for some SoCs). This
 * kernel already has the same class of primitive — `mmio_read`/
 * `mmio_write` and byte/halfword-narrowed variants, `kernel/io.h` — so
 * this shim just renames onto those instead of reimplementing MMIO access.
 * Covers only the widths actually called anywhere in the vendored tree
 * (checked via grep before writing this: `read8`, `read32`, `write32` —
 * all from `xhcimmiospace.cpp`); add more as the compiler asks for them.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_memio_h
#define _circle_memio_h

#include "kernel/io.h"
#include <circle/types.h>

inline u8 read8 (uintptr nAddress)
{
	return mmio_read8 ((uint64_t) nAddress);
}

inline u16 read16 (uintptr nAddress)
{
	return mmio_read16 ((uint64_t) nAddress);
}

inline u32 read32 (uintptr nAddress)
{
	return mmio_read ((uint64_t) nAddress);
}

inline void write32 (uintptr nAddress, u32 nValue)
{
	mmio_write ((uint64_t) nAddress, nValue);
}

#endif
