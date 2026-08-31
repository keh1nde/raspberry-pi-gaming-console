/**
 * @file classallocator.h
 * @brief Compatibility shim for Circle's circle/classallocator.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle's class allocator is a fixed-size object pool with an
 * optional spinlock, so allocation/deallocation are guaranteed O(1) and
 * safe to call from IRQ context — the concern that actually matters is
 * an interrupt handler calling `new`/`delete` while a general heap
 * allocator's internal state is being mutated on the main path. This
 * port's xHCI driver is polling-only for now (see project history: RP1's
 * interrupts are dynamic PCIe MSI-X vectors with no routing driver here
 * yet), so nothing allocates from IRQ context — the property a pool
 * exists to guarantee doesn't matter yet. Rather than build a real pool
 * allocator for no current benefit, this shim makes `DECLARE_CLASS_ALLOCATOR`
 * a no-op (falls through to the global `operator new`/`delete` already in
 * `src/kernel/crt/new.cpp`) and `IMPLEMENT_CLASS_ALLOCATOR`/
 * `INIT_(PROTECTED_)CLASS_ALLOCATOR` empty. Revisit once real interrupt-
 * driven xHCI work makes the IRQ-safety property load-bearing.
 *
 * `IRQ_LEVEL` (Circle's execution-level tag for the protected-allocator
 * variant) is defined here as a placeholder — its value is never read,
 * since `InitProtectedAllocator` below ignores its argument entirely.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_classallocator_h
#define _circle_classallocator_h

#define IRQ_LEVEL 0

// Falls through to the global operator new/delete (src/kernel/crt/new.cpp)
// instead of declaring per-class pool-backed ones — see file comment.
#define DECLARE_CLASS_ALLOCATOR					\
	public:								\
		static void InitAllocator (unsigned nReservedObjects) {}	\
		static void InitProtectedAllocator (unsigned nReservedObjects,\
						    unsigned nTargetLevel) {}

// Nothing to define out-of-line — DECLARE_CLASS_ALLOCATOR above doesn't
// declare class-specific operator new/delete, so there's nothing here to
// match. Expands to nothing at the call site (real Circle's own macro
// invocation carries no trailing semicolon either).
#define IMPLEMENT_CLASS_ALLOCATOR(class)

#define INIT_CLASS_ALLOCATOR(class, objects) \
	class::InitAllocator (objects)
#define INIT_PROTECTED_CLASS_ALLOCATOR(class, objects, level) \
	class::InitProtectedAllocator (objects, level)

#endif
