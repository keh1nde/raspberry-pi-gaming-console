/**
 * @file macros.h
 * @brief Compatibility shim for Circle's circle/macros.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Not vendored (see lib/README.md's "compatibility shim" section), but
 * only because keeping every shim file visibly ours is simpler than
 * tracking which small headers were copied verbatim vs written — the
 * content below is otherwise identical in effect to upstream's, since
 * these are plain, self-contained GCC attribute macros with no dependency
 * on the rest of Circle's framework.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_macros_h
#define _circle_macros_h

#define PACKED		__attribute__ ((packed))
#define MAXALIGN	__attribute__ ((aligned))
#define ALIGN(n)	__attribute__ ((aligned (n)))
#define NORETURN	__attribute__ ((noreturn))
#define NOOPT		__attribute__ ((optimize (0)))
#define STDOPT		__attribute__ ((optimize (2)))
#define MAXOPT		__attribute__ ((optimize (3)))
#define WEAK		__attribute__ ((weak))

#define likely(exp)	__builtin_expect (!!(exp), 1)
#define unlikely(exp)	__builtin_expect (!!(exp), 0)

#define BIT(n)		(1UL << (n))

#define IS_POWEROF_2(num) ((num) != 0 && (((num) & ((num) - 1)) == 0))

// Compile-time assertion. Used unqualified by Circle's own vendored xhci.h
// and by circle/types.h's boolean size check, but never actually defined
// anywhere in Circle's own headers (checked directly) — likely from a
// header this port doesn't vendor. Unambiguous in shape from every call
// site (a plain compile-time boolean check), so defined here in terms of
// C++11 static_assert rather than guessed at from an unknown upstream
// source.
#define ASSERT_STATIC(cond) static_assert (cond, #cond)

// Big-endian byte swap (16-bit constants only, matches Circle's own scope).
#define BE(value)	((((value) & 0xFF00) >> 8) | (((value) & 0x00FF) << 8))

#endif
