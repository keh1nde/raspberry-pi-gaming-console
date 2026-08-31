/**
 * @file types.h
 * @brief Compatibility shim for Circle's circle/types.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Not vendored from Circle (see lib/README.md's "compatibility shim"
 * section) — the real upstream file depends on ASSERT_STATIC, which is
 * never actually defined anywhere in Circle's own types.h/macros.h pair
 * (must come from a header outside what this port vendors), and redefines
 * size_t/ssize_t, which newlib already provides. This file supplies just
 * the typedef/macro names Circle's vendored USB/xHCI code actually spells
 * (u8/u16/u32/u64/s8/s16/s32/s64/boolean/TRUE/FALSE/uintptr), matching
 * Circle's own names exactly so the vendored files compile unmodified,
 * without those two problems. AArch64-only — Circle's AARCH==32 branch is
 * dropped, since this kernel never targets 32-bit.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_types_h
#define _circle_types_h

#include <stddef.h>	// size_t, ssize_t — from newlib, not redefined here

typedef unsigned char		u8;
typedef unsigned short		u16;
typedef unsigned int		u32;
typedef unsigned long		u64;

typedef signed char		s8;
typedef signed short		s16;
typedef signed int		s32;
typedef signed long		s64;

typedef long			intptr;
typedef unsigned long		uintptr;

#ifdef __cplusplus
typedef bool		boolean;
#define FALSE		false
#define TRUE		true
#else
typedef char		boolean;
#define FALSE		0
#define TRUE		1
#endif

#endif
