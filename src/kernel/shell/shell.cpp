/**
 * @file shell.cpp
 * @brief Interactive UART shell sitting directly on the ramfs API.
 *
 * Part of kehinde-kernel: a bare-metal AArch64 operating system for the
 * Raspberry Pi 3 Model B (Cortex-A53) and Pi 5 (Cortex-A76).
 *
 * Read-parse-dispatch loop: print a prompt, read a line over UART, split
 * on whitespace, look the verb up in a small static table of handlers.
 * No fd table or open-handle layer — commands invoke `fs_*` functions
 * directly with inode numbers.
 *
 * State is two file-static fields:
 *   - `cwd_ino` — inode number of the current directory (used for FS ops).
 *   - `cwd_path` — string form of the cwd (used for `pwd` display and
 *     resolving relative paths).
 *
 * Limitations:
 *   - `cd ..` is unsupported (inodes carry no parent pointer). Use an
 *     absolute path to back out.
 *   - `write` always starts at offset 0 (overwrite). No append mode.
 *   - No quoting; arguments are split on plain spaces.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: MIT
 */

#include "shell.h"
#include "filesystem.h"
#include "uart.h"
#include "timer.h"

/** Maximum command line length, in bytes. */
#define MAX_LINE 256

/** Maximum number of argv slots after splitting on whitespace. */
#define MAX_ARGS 16

/** Maximum length of a path string the shell will construct internally. */
#define MAX_PATH 512

/** Inode of the current working directory. */
static uint64_t cwd_ino = ROOT_INO;

/** String form of the cwd. Updated by #cmd_cd. */
static char cwd_path[MAX_PATH] = "/";

// ====== String helpers (no libc) ======

/** @brief Length of null-terminated @p s. */
static uint64_t slen(const char* s) {
    uint64_t n = 0;
    while (s[n]) n++;
    return n;
}

/** @brief Copy null-terminated @p src into @p dst including the terminator. */
static void scpy(char* dst, const char* src) {
    uint64_t i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
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

// ====== Path resolution ======

/**
 * @brief Resolve @p arg (absolute or single-component relative) to an inode.
 *
 * Absolute paths go straight to #fs_lookup. Bare names are prefixed with
 * `cwd_path + "/"` and then looked up. Does NOT handle `..` or `.`.
 *
 * @return Inode number, or #INVALID_INO if the path doesn't resolve.
 */
static uint64_t resolve_path(const char* arg) {
    if (arg[0] == '/') return fs_lookup(arg);
    char full[MAX_PATH];
    uint64_t clen = slen(cwd_path);
    scpy(full, cwd_path);
    if (cwd_path[clen - 1] != '/') { full[clen] = '/'; full[clen + 1] = '\0'; clen++; }
    scpy(full + clen, arg);
    return fs_lookup(full);
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
    uart_puts(cwd_path);
    uart_puts("\r\n");
}

/** @brief `ls [path]` — list a directory; append `/` to subdirectory entries. */
static void cmd_ls(int argc, char** argv) {
    uint64_t dir_ino;
    if (argc < 2) {
        dir_ino = cwd_ino;
    } else {
        dir_ino = resolve_path(argv[1]);
        if (dir_ino == INVALID_INO) { uart_puts("ls: not found\r\n"); return; }
    }

    dirent ent;
    uint64_t i = 0;
    bool any = false;
    while (fs_readdir(dir_ino, i, &ent) == 1) {
        uart_puts(ent.name);
        uint8_t type = 0;
        fs_stat(ent.inode_id, nullptr, &type);
        if (type == 2) uart_puts("/");
        uart_puts("\r\n");
        i++;
        any = true;
    }
    if (!any) uart_puts("(empty)\r\n");
}

/** @brief `cd <path>` — change the cwd. Refuses non-directories. */
static void cmd_cd(int argc, char** argv) {
    if (argc < 2) { uart_puts("cd: missing path\r\n"); return; }
    const char* target = argv[1];

    uint64_t new_ino = resolve_path(target);
    if (new_ino == INVALID_INO) { uart_puts("cd: not found\r\n"); return; }

    uint8_t type = 0;
    fs_stat(new_ino, nullptr, &type);
    if (type != 2) { uart_puts("cd: not a directory\r\n"); return; }

    cwd_ino = new_ino;
    if (target[0] == '/') {
        scpy(cwd_path, target);
    } else {
        uint64_t clen = slen(cwd_path);
        if (cwd_path[clen - 1] != '/') { cwd_path[clen] = '/'; cwd_path[clen + 1] = '\0'; clen++; }
        scpy(cwd_path + clen, target);
    }
}

/** @brief `mkdir <name>` — create a directory in the cwd. */
static void cmd_mkdir(int argc, char** argv) {
    if (argc < 2) { uart_puts("mkdir: missing name\r\n"); return; }
    if (argv[1][0] == '/') { uart_puts("mkdir: use relative names\r\n"); return; }
    if (fs_create(cwd_ino, argv[1], 2) == INVALID_INO) {
        uart_puts("mkdir: failed (name exists or invalid)\r\n");
    }
}

/** @brief `touch <name>` — create an empty file in the cwd. */
static void cmd_touch(int argc, char** argv) {
    if (argc < 2) { uart_puts("touch: missing name\r\n"); return; }
    if (argv[1][0] == '/') { uart_puts("touch: use relative names\r\n"); return; }
    if (fs_create(cwd_ino, argv[1], 1) == INVALID_INO) {
        uart_puts("touch: failed (name exists or invalid)\r\n");
    }
}

/** @brief `cat <file>` — stream the file's contents to UART. */
static void cmd_cat(int argc, char** argv) {
    if (argc < 2) { uart_puts("cat: missing file\r\n"); return; }
    uint64_t ino = resolve_path(argv[1]);
    if (ino == INVALID_INO) { uart_puts("cat: not found\r\n"); return; }

    char buf[128];
    int64_t n;
    uint64_t offset = 0;
    while ((n = fs_read(ino, offset, sizeof(buf) - 1, buf)) > 0) {
        buf[n] = '\0';
        uart_puts(buf);
        offset += (uint64_t)n;
    }
    if (n < 0) uart_puts("cat: read error\r\n");
    else uart_puts("\r\n");
}

/** @brief `write <file> <text...>` — overwrite the file with the joined text. */
static void cmd_write(int argc, char** argv) {
    if (argc < 3) { uart_puts("write: usage: write <file> <text>\r\n"); return; }
    uint64_t ino = resolve_path(argv[1]);
    if (ino == INVALID_INO) { uart_puts("write: not found\r\n"); return; }

    char text[MAX_LINE];
    join_args(text, argv, 2, argc);
    uint64_t len = slen(text);
    if (fs_write(ino, 0, len, text) < 0) uart_puts("write: failed\r\n");
}

/** @brief `rm <name>` — unlink a file or empty directory in the cwd. */
static void cmd_rm(int argc, char** argv) {
    if (argc < 2) { uart_puts("rm: missing name\r\n"); return; }
    if (argv[1][0] == '/') { uart_puts("rm: use relative names\r\n"); return; }
    if (fs_unlink(cwd_ino, argv[1]) < 0) {
        uart_puts("rm: failed (not found or non-empty directory)\r\n");
    }
}

/** @brief `uptime` — print the uptime of the kernel in seconds. */
static void cmd_timer() {
    shell_print_time();
    uart_puts("\n");
}

// ====== Shell entry point ======

void shell_run() {
    char line[MAX_LINE];
    char* argv[MAX_ARGS];

    uart_puts("\r\n");
    print_banner();

    while (1) {
        uart_puts(cwd_path);
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
        else {
            uart_puts(argv[0]);
            uart_puts(": command not found\r\n");
        }
    }
}
