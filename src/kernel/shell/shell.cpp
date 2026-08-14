/**
 * @file shell.cpp
 * @brief Interactive UART shell sitting directly on the FatFs API.
 *
 * Part of kehinde-kernel: a bare-metal AArch64 operating system for the
 * Raspberry Pi 3 Model B (Cortex-A53) and Pi 5 (Cortex-A76).
 *
 * Read-parse-dispatch loop: print a prompt, read a line over UART, split
 * on whitespace, look the verb up in a small static table of handlers.
 * No fd table or open-handle layer — commands invoke `f_*` functions
 * directly; FatFs (`FF_FS_RPATH == 2`) owns cwd tracking and relative/
 * `.`/`..` path resolution internally, so the shell carries no path-
 * resolution state of its own.
 *
 * Limitations:
 *   - `write` always starts at offset 0 (overwrite). No append mode, and
 *     a write shorter than the existing file does not truncate it.
 *   - No quoting; arguments are split on plain spaces.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: MIT
 */

#include "kernel/shell.h"
#include "kernel/uart.h"
#include "kernel/timer.h"
#include "kernel/pmm.h"
#include "kernel/heap_alloc.h"
#include "kernel/dma.h"
#include "lib/fatfs/ff.h"

/** Maximum command line length, in bytes. */
#define MAX_LINE 256

/** Maximum number of argv slots after splitting on whitespace. */
#define MAX_ARGS 16

/** Maximum length of a path string the shell will construct internally. */
#define MAX_PATH 512

// ====== String helpers (no libc) ======

/** @brief Length of null-terminated @p s. */
static uint64_t slen(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

/** @brief Equality test for two null-terminated strings. */
static bool seq(const char* a, const char* b) {
    uint64_t i = 0;
    while (a[i] && b[i] && a[i] == b[i]) i++;
    return a[i] == b[i];
}

/**
 * @brief Concatenate argv[@p start ..@p argc) into @p dst with single-space
 *        separators. Used by `write` to allow multi-word values.
 */
static void join_args(char* dst, char** argv, int start, int argc) {
    uint64_t pos = 0;
    for (int i = start; i < argc; i++) {
        if (i > start) dst[pos++] = ' ';
        const char* s = argv[i];
        uint64_t j = 0;
        while (s[j]) dst[pos++] = s[j++];
    }
    dst[pos] = '\0';
}

// ====== I/O helpers ======

/**
 * @brief Read one line of UART input into @p buf, with local echo.
 *
 * Handles CR/LF (terminates the line), backspace/DEL (deletes one byte
 * with `\b \b` redraw), and printable ASCII (≥ 32) only. Other control
 * characters are silently discarded.
 *
 * @param buf Destination buffer.
 * @param max Buffer capacity, including the null terminator.
 * @return Number of bytes stored in @p buf (excluding terminator).
 */
static int read_line(char* buf, int max) {
    int i = 0;
    while (i < max - 1) {
        char c = uart_getc();
        if (c == '\r' || c == '\n') {
            uart_puts("\r\n");
            break;
        }
        if ((c == 127 || c == '\b') && i > 0) {
            i--;
            uart_puts("\b \b");
            continue;
        }
        if (c >= 32) {
            buf[i++] = c;
            uart_putc(c);
        }
    }
    buf[i] = '\0';
    return i;
}

/**
 * @brief In-place tokenize @p line on whitespace; populate @p argv.
 *
 * Overwrites each separator byte with `\0`. Caller's @p line must be
 * writable.
 *
 * @return Number of tokens written (0 to @p max_argc).
 */
static int parse_args(char* line, char** argv, int max_argc) {
    int argc = 0;
    char* p = line;
    while (*p && argc < max_argc) {
        while (*p == ' ') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ') p++;
        if (*p) *p++ = '\0';
    }
    return argc;
}

// ====== Commands ======

/** @brief Print the kernel banner used by `kernel` and at shell start-up. */
static void print_banner() {
    uart_puts("kehinde-kernel v1.1.1\r\n");
    uart_puts("Authored by Kehinde Adeoso\r\n");
    uart_puts("Finished May 13, 2026. Updated June 2, 2026\r\n");
    uart_puts("Type 'help' for a list of commands.\r\n");
}

/** @brief `help` — list the available commands. */
static void cmd_help() {
    uart_puts("Commands:\r\n");
    uart_puts("  ls [path]              list directory\r\n");
    uart_puts("  pwd                    print working directory\r\n");
    uart_puts("  cd <path>              change directory\r\n");
    uart_puts("  mkdir <name>           create directory\r\n");
    uart_puts("  touch <name>           create empty file\r\n");
    uart_puts("  write <file> <text>    write text to file\r\n");
    uart_puts("  cat <file>             print file contents\r\n");
    uart_puts("  rm <name>              remove file or empty directory\r\n");
    uart_puts("  clear                  clear the screen\r\n");
    uart_puts("  kernel                 show kernel info\r\n");
    uart_puts("  uptime                 print the uptime of the kernel\r\n");
    uart_puts("  meminfo                print PMM/heap capacity-planning figures\r\n");
    uart_puts("  shutdown               halt the system\r\n");
    uart_puts("  help                   show this message\r\n");
}

/** @brief `clear` — emit the ANSI "erase screen + cursor home" sequence. */
static void cmd_clear() {
    uart_puts("\033[2J\033[H");
}

/** @brief `shutdown` — mask all exceptions and park the core in `wfi`. */
static void cmd_shutdown() {
    uart_puts("Shutting down kehinde-kernel. Ciao!\r\n");
    asm volatile("msr daifset, #0xf");
    for (;;) asm volatile("wfi");
}

/** @brief `pwd` — print the current working directory. */
static void cmd_pwd() {
    char cwd[MAX_PATH];
    if (f_getcwd(cwd, sizeof(cwd)) != FR_OK) { uart_puts("pwd: failed\r\n"); return; }
    uart_puts(cwd);
    uart_puts("\r\n");
}

/** @brief `ls [path]` — list a directory; append `/` to subdirectory entries.
 *  Defaults to the cwd (FatFs resolves `"."` internally). */
static void cmd_ls(int argc, char** argv) {
    const char* path = (argc < 2) ? "." : argv[1];

    DIR dir;
    if (f_opendir(&dir, path) != FR_OK) { uart_puts("ls: not found\r\n"); return; }

    FILINFO info;
    bool any = false;
    while (f_readdir(&dir, &info) == FR_OK && info.fname[0] != '\0') {
        uart_puts(info.fname);
        if (info.fattrib & AM_DIR) uart_puts("/");
        uart_puts("\r\n");
        any = true;
    }
    f_closedir(&dir);
    if (!any) uart_puts("(empty)\r\n");
}

/** @brief `cd <path>` — change the cwd. FatFs (FF_FS_RPATH == 2) tracks
 *  the cwd itself, including `.`/`..` resolution. */
static void cmd_cd(int argc, char** argv) {
    if (argc < 2) { uart_puts("cd: missing path\r\n"); return; }
    if (f_chdir(argv[1]) != FR_OK) { uart_puts("cd: not found or not a directory\r\n"); return; }
}

/** @brief `mkdir <name>` — create a directory. */
static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { uart_puts("mkdir: missing name\r\n"); return; }
    if (f_mkdir(argv[1]) != FR_OK) {
        uart_puts("mkdir: failed (name exists or invalid)\r\n");
    }
}

/** @brief `touch <name>` — create an empty file. Fails if it already exists. */
static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { uart_puts("touch: missing name\r\n"); return; }
    FIL fil;
    if (f_open(&fil, argv[1], FA_WRITE | FA_CREATE_NEW) != FR_OK) {
        uart_puts("touch: failed (name exists or invalid)\r\n");
        return;
    }
    f_close(&fil);
}

/** @brief `cat <file>` — stream the file's contents to UART. */
static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { uart_puts("cat: missing file\r\n"); return; }

    FIL fil;
    if (f_open(&fil, argv[1], FA_READ) != FR_OK) { uart_puts("cat: not found\r\n"); return; }

    char buf[128];
    UINT br;
    FRESULT res;
    while ((res = f_read(&fil, buf, sizeof(buf) - 1, &br)) == FR_OK && br > 0) {
        buf[br] = '\0';
        uart_puts(buf);
    }
    f_close(&fil);
    if (res != FR_OK) uart_puts("cat: read error\r\n");
    else uart_puts("\r\n");
}

/** @brief `write <file> <text...>` — overwrite the file with the joined text,
 *  starting at offset 0. The file must already exist (see `touch`). */
static void cmd_write(int argc, char** argv) {
    if (argc < 3) { uart_puts("write: usage: write <file> <text>\r\n"); return; }

    FIL fil;
    if (f_open(&fil, argv[1], FA_WRITE) != FR_OK) { uart_puts("write: not found\r\n"); return; }

    char text[MAX_LINE];
    join_args(text, argv, 2, argc);
    UINT len = static_cast<UINT>(slen(text));
    UINT written = 0;
    const FRESULT res = f_write(&fil, text, len, &written);
    f_close(&fil);
    if (res != FR_OK || written != len) uart_puts("write: failed\r\n");
}

/** @brief `rm <name>` — unlink a file or empty directory. */
static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { uart_puts("rm: missing name\r\n"); return; }
    if (f_unlink(argv[1]) != FR_OK) {
        uart_puts("rm: failed (not found or non-empty directory)\r\n");
    }
}

/** @brief `uptime` — print the uptime of the kernel in seconds. */
static void cmd_timer() {
    shell_print_time();
    uart_puts("\n");
}

/** @brief Print @p bytes rounded down to whole MiB, no fractional part. */
static void put_mib(uint64_t bytes) {
    uart_put_uint(bytes / (1024 * 1024));
    uart_puts(" MiB");
}

/**
 * @brief `meminfo` — print PMM/heap capacity-planning figures.
 *
 * Development-only diagnostic: physical memory layout, live frame
 * usage (scanned from the PMM bitmap), and each fixed-size arena's
 * base/cap. Exists so sizing a new arena's `*_MAX_SIZE` (see
 * `sbrk-capacity-planning` writeup) doesn't require hand-deriving these
 * numbers via `nm` and the `pmm_init` bitmap math each time.
 */
static void cmd_meminfo() {
    uart_puts("--- Physical memory (kernel/pmm.h, board.h) ---\r\n");
    uart_puts("PHYS_MEM_END:    "); uart_put_hex(PHYS_MEM_END); uart_puts("\r\n");
    uart_puts("phys_mem_start:  "); uart_put_hex(phys_mem_start); uart_puts("\r\n");
    uart_puts("__kernel_end:    "); uart_put_hex(reinterpret_cast<uint64_t>(__kernel_end)); uart_puts("\r\n");

    // pmm_init marks the bits past total_frames in the bitmap's final word as
    // "used" so alloc_frame can't hand them out — subtract that padding back
    // out so this reports real frame usage, not usage-plus-padding.
    const uint64_t words = (total_frames + 63) / 64;
    const uint64_t padding_bits = words * 64 - total_frames;
    uint64_t raw_set_bits = 0;
    for (uint64_t i = 0; i < words; i++) raw_set_bits += __builtin_popcountll(bitmap[i]);
    const uint64_t used_frames = raw_set_bits - padding_bits;
    const uint64_t free_frames = total_frames - used_frames;

    uart_puts("total_frames:    "); uart_put_uint(total_frames);
    uart_puts(" ("); put_mib(total_frames * PAGE_SIZE); uart_puts(")\r\n");
    uart_puts("used_frames:     "); uart_put_uint(used_frames);
    uart_puts(" ("); put_mib(used_frames * PAGE_SIZE); uart_puts(")\r\n");
    uart_puts("free_frames:     "); uart_put_uint(free_frames);
    uart_puts(" ("); put_mib(free_frames * PAGE_SIZE); uart_puts(")\r\n");

    uart_puts("--- kmalloc heap (kernel/heap_alloc.h) ---\r\n");
    uart_puts("HEAP_BASE:       "); uart_put_hex(HEAP_BASE); uart_puts("\r\n");
    uart_puts("HEAP_MAX_SIZE:   "); uart_put_hex(HEAP_MAX_SIZE);
    uart_puts(" ("); put_mib(HEAP_MAX_SIZE); uart_puts(")\r\n");
    uart_puts("bump_ptr:        "); uart_put_hex(bump_ptr); uart_puts("\r\n");
    uart_puts("heap_end:        "); uart_put_hex(heap_end); uart_puts("\r\n");

    uart_puts("--- DMA arena (kernel/dma.h) ---\r\n");
    uart_puts("DMA_BASE:        "); uart_put_hex(DMA_BASE); uart_puts("\r\n");
    uart_puts("DMA_MAX_SIZE:    "); uart_put_hex(DMA_MAX_SIZE);
    uart_puts(" ("); put_mib(DMA_MAX_SIZE); uart_puts(")\r\n");
}

// ====== Shell entry point ======

void shell_run() {
    char line[MAX_LINE];
    char* argv[MAX_ARGS];

    uart_puts("\r\n");
    print_banner();

    while (1) {
        char cwd[MAX_PATH];
        if (f_getcwd(cwd, sizeof(cwd)) == FR_OK) uart_puts(cwd);
        uart_puts("$ ");

        if (read_line(line, MAX_LINE) == 0) continue;

        int argc = parse_args(line, argv, MAX_ARGS);
        if (argc == 0) continue;

        if      (seq(argv[0], "help"))   cmd_help();
        else if (seq(argv[0], "ls"))     cmd_ls(argc, argv);
        else if (seq(argv[0], "pwd"))    cmd_pwd();
        else if (seq(argv[0], "cd"))     cmd_cd(argc, argv);
        else if (seq(argv[0], "mkdir"))  cmd_mkdir(argc, argv);
        else if (seq(argv[0], "touch"))  cmd_touch(argc, argv);
        else if (seq(argv[0], "cat"))    cmd_cat(argc, argv);
        else if (seq(argv[0], "write"))  cmd_write(argc, argv);
        else if (seq(argv[0], "rm"))     cmd_rm(argc, argv);
        else if (seq(argv[0], "clear"))    cmd_clear();
        else if (seq(argv[0], "kernel"))   print_banner();
        else if (seq(argv[0], "shutdown")) cmd_shutdown();
        else if (seq(argv[0], "uptime")) cmd_timer();
        else if (seq(argv[0], "meminfo")) cmd_meminfo();
        else {
            uart_puts(argv[0]);
            uart_puts(": command not found\r\n");
        }
    }
}
