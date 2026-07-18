/**
 * @file crt.cpp
 * @brief C++ runtime startup support — see include/kernel/crt.h.
 *
 * Part of raspberry-pi-gaming-console.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "kernel/crt.h"

typedef void (*ctor_fn)();

/** Bounds of the `.init_array` section — defined by `linker.ld`, not by any
 * C++ translation unit; there's no array here, just two addresses. */
extern "C" ctor_fn __init_array_start[];
extern "C" ctor_fn __init_array_end[];

extern "C" void crt_init() {
	for (ctor_fn* ctor = __init_array_start; ctor != __init_array_end; ++ctor) {
		(*ctor)();
	}
}
