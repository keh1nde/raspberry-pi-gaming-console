#pragma once

/**
 * @file crt.h
 * @brief C++ runtime startup support — global/static constructor invocation.
 *
 * A hosted C runtime's crt0 normally calls `__libc_init_array()` before
 * `main()`. This kernel supplies its own entry point
 * (`src/kernel/boot.S` -> `kernel_main`), so nothing walks `.init_array`
 * unless something here does it explicitly.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

/**
 * @brief Runs every global/static C++ object's constructor.
 *
 * Walks the `.init_array` section (populated by the compiler, bounded by
 * `__init_array_start`/`__init_array_end` from `linker.ld`) and calls each
 * function pointer in order.
 *
 * @warning Must be called exactly once, from `kernel_main`, before any code
 * that might touch a non-trivial global/static C++ object. Secondary cores
 * must NOT call this — constructors run once total, not once per core.
 */
extern "C" void crt_init();
