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
#include "stdlib.h"

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

void* operator new(const size_t size){
	void *ptr = (size == 0) ? malloc(1) : malloc(size);
	return ptr;
}

void* operator new[](const size_t size) {
	return operator new(size);
}

void operator delete(void* ptr) {
	if (ptr) free(ptr);
}

void operator delete[](void* ptr) {
	operator delete(ptr);
}

// Sized delete (C++14) — GCC calls this in preference to the unsized
// overload above whenever it knows the deleted object's exact type at the
// delete site, which is most places. The size itself isn't needed here;
// free() already knows how much to release from malloc's own bookkeeping.
void operator delete(void* ptr, size_t) {
	operator delete(ptr);
}

void operator delete[](void* ptr, size_t) {
	operator delete(ptr);
}

// Normally provided by libstdc++'s runtime for __cxa_atexit bookkeeping
// (registering global-object destructors against the shared object they
// belong to, for when it's unloaded). Meaningless here — this kernel never
// unloads and, per crt.h's own comment, never calls destructors at all —
// but the symbol still needs to exist for anything that references it.
void* __dso_handle = nullptr;