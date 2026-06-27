/**
 * @file shell.h
 * @brief Interactive UART shell — entry point.
 *
 * Part of kehinde-kernel: a bare-metal AArch64 operating system for the
 * Raspberry Pi 3 Model B (Cortex-A53) and Pi 5 (Cortex-A76).
 *
 * The shell sits directly on the ramfs API (no fd table or open-handle
 * layer) and exposes a small command set for poking at the filesystem
 * (`ls`, `pwd`, `cd`, `mkdir`, `touch`, `write`, `cat`, `rm`) plus a few
 * housekeeping commands (`help`, `clear`, `kernel`, `shutdown`). See
 * `src/shell/shell.cpp` for the implementation.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: MIT
 */

#ifndef SHELL_H
#define SHELL_H

/**
 * @brief Enter the shell's read–parse–dispatch loop. Does not return.
 *
 * Prints a banner once, then repeatedly: prints the prompt (`<cwd>$ `),
 * reads a line over UART, splits on whitespace into argv, and dispatches
 * to the matching command handler.
 */
void shell_run();

#endif
