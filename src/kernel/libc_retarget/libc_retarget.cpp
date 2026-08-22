/**
 * @file libc_retarget.cpp
 * @brief newlib retarget layer — the stub functions newlib calls into.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Newlib has no idea how to talk to this kernel; it calls a fixed set of
 * `extern "C"` functions with fixed names (below) and expects the embedding
 * project to implement them — e.g. `malloc` calls `_sbrk`, `printf`
 * eventually calls `_write`. Nothing in this kernel calls these directly,
 * which is why there's no matching header: there's no other caller to
 * declare a contract for. Newlib finds them at link time by name alone.
 *
 * Signatures below are copied verbatim from the installed newlib headers
 * (`toolchain/newlib/aarch64-elf/include/sys/{unistd,stat,signal}.h`,
 * confirmed against this exact build via the preprocessor — some of these,
 * `_write`/`_read` in particular, resolve to different return types on
 * other newlib configurations). See the writeup for the
 * full design discussion behind each of these.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <stddef.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "../../../include/kernel/heap_alloc.h"
#include "../../../include/kernel/pmm.h"
#include "../../../include/kernel/spinlock.h"
#include "../../../include/kernel/mmu.h"
#include "../../../include/kernel/io.h"

static spinlock newlib_lock = {0};

// Tenative stubs.
constexpr uint64_t NEWLIB_HEAP_BASE = 0x180000000;
constexpr uint64_t NEWLIB_HEAP_MAX_SIZE = 0x38000000;
uint64_t curr_brk = NEWLIB_HEAP_BASE; // Similar to heap_end in the heap allocator
uint64_t CURR_NEWLIB_HEAP_END = NEWLIB_HEAP_BASE;

static int core_errno[4];

/**
 * @brief 
 * @return The errno
 */
extern "C" int* __errno(void) {
	uint64_t core_id;
	asm volatile ("mrs %0, TPIDR_EL1" : "=r"(core_id));

	return &core_errno[core_id];
}

/**
 * @brief Retract newlib heap break. A helper for @p _sbrk.
 *
 * Essentially the opposite of _sbrk. The logic is similar, but frees
 * heap memory. This is called as a helper in _sbrk when @p `incr` is less
 * than zero.
 *
 * @return True if the operation is successful. False otherwise.
 *
 * @param incr Bytes to reduce the break by.
 */
bool _free_sbrk(ptrdiff_t incr) {
	const uint64_t required_end = curr_brk + incr;

	if (required_end < NEWLIB_HEAP_BASE) {
		return false;
	}

	while (CURR_NEWLIB_HEAP_END > round_up(required_end, PAGE_SIZE)) {
		uint64_t pa = translate(CURR_NEWLIB_HEAP_END - PAGE_SIZE);
		unmap(CURR_NEWLIB_HEAP_END - PAGE_SIZE, PAGE_SIZE);
		free_frame(pa);

		CURR_NEWLIB_HEAP_END -= PAGE_SIZE;

	}

	curr_brk = required_end;
	return true;
}

/**
 * @brief Extend or query the newlib heap break; backs `malloc`/`free`.
 *
 * @p incr bytes are requested past the current break; returns the *old*
 * break (start of the newly-available region), or `(void*)-1` with `errno`
 * set to `ENOMEM` on failure. `incr == 0` just queries the current break.
 *
 * @param incr Bytes to extend the break by (may be negative or zero).
 * @return Previous break address, or `(void*)-1` on failure.
 */
extern "C" void* _sbrk(ptrdiff_t incr) {
	uint64_t flags;
	spin_lock(newlib_lock, flags);

	if (incr == 0) {
		spin_unlock(newlib_lock, flags);
		return reinterpret_cast<void *>(curr_brk);
	}

	if (incr < 0) {
		const uint64_t prev_brk = curr_brk;
		if (!_free_sbrk(incr)) {
			errno = ENOMEM;
			curr_brk = prev_brk;
			spin_unlock(newlib_lock, flags);
			return reinterpret_cast<void *>(-1);
		}
		spin_unlock(newlib_lock, flags);
		return reinterpret_cast<void *>(prev_brk);
	}

	// Check if we have enough space allocated
	const uint64_t required_end = curr_brk + incr;

	if (required_end > NEWLIB_HEAP_BASE + NEWLIB_HEAP_MAX_SIZE) {
		errno = ENOMEM;
		spin_unlock(newlib_lock, flags);
		return reinterpret_cast<void *>(-1);
	}

	while (CURR_NEWLIB_HEAP_END < required_end) {
		uint64_t pa = alloc_frame();
		if (pa == 0) {
			errno = ENOMEM;
			spin_unlock(newlib_lock, flags);
			return reinterpret_cast<void *>(-1);
		}
		bool map_result = map(CURR_NEWLIB_HEAP_END, pa, PAGE_SIZE, PTE_NORMAL_RW);

		if (!map_result) {
			errno = ENOMEM;
			free_frame(pa);
			spin_unlock(newlib_lock, flags);
			return reinterpret_cast<void *>(-1);
		}
		CURR_NEWLIB_HEAP_END += PAGE_SIZE;
	}

	uint64_t result = curr_brk;
	curr_brk += incr;

	spin_unlock(newlib_lock, flags);
	return reinterpret_cast<void *>(result);

}

/**
 * @brief Write @p nbyte bytes from @p buf to file descriptor @p fd.
 *
 * Only fd 1 (stdout) and 2 (stderr) have anywhere to go right now — route
 * both through `io_putc`, which fans out to UART and the screen-side
 * terminal. This is the reason `printf`/`puts` reach the screen mirror at
 * all: newlib's stdio funnels every write through this stub. Any other fd
 * has no backing file yet (no VFS) — return -1 with `errno = EBADF`.
 *
 * @param fd    Target file descriptor.
 * @param buf   Bytes to write.
 * @param nbyte Number of bytes in @p buf.
 * @return Number of bytes written, or -1 on error.
 */
extern "C" int _write(int fd, const void* buf, size_t nbyte) {
	if (fd == 1 || fd == 2) {
		for (size_t i = 0; i < nbyte; i++) {
			io_putc(static_cast<const unsigned char*>(buf)[i]);
		}
		return static_cast<int>(nbyte);
	}
	errno = EBADF;
	return -1;
}

/**
 * @brief Read up to @p nbyte bytes from file descriptor @p fd into @p buf.
 *
 * No input source exists yet (e.g. USB HID, VFS) so there's nothing
 * real to read from any fd right now. Return 0 (EOF-shaped, not an error)
 * until an input driver is implemented
 *
 * @param fd    Source file descriptor.
 * @param buf   Destination buffer.
 * @param nbyte Maximum bytes to read.
 * @return Number of bytes read (0 for now), or -1 on error.
 */
extern "C" int _read(int fd, void* buf, size_t nbyte) {
	return 0;
}

/**
 * @brief Close file descriptor @p fildes.
 *
 * No real file descriptors exist yet (no VFS), so stub until then.
 *
 * @param fildes File descriptor to close.
 * @return 0 on success, -1 on error.
 */
extern "C" int _close(int fildes) {
	if (fildes == 1 || fildes == 2) {
		return 0;
	}
	return -1;
}

/**
 * @brief Reposition the file offset for @p fildes.
 *
 * No real file descriptors exist yet (no VFS), so stub until then.
 *
 * @param fildes File descriptor.
 * @param offset Offset, interpreted per @p whence.
 * @param whence `SEEK_SET`/`SEEK_CUR`/`SEEK_END`.
 * @return Resulting offset, or -1 on error.
 */
extern "C" off_t _lseek(int fildes, off_t offset, int whence) {
	errno = ESPIPE;
	return -1;
}

/**
 * @brief Get status information for file descriptor @p fd.
 *
 * No real file descriptors exist yet (no VFS), so we stub.
 *
 * Development Note:
 * Newlib's stdio internals may still probe this for fd 1/2 (stdout/stderr)
 * to help decide buffering behavior. It's worth checking whether reporting them
 * as a character device (matching what `_isatty` says) avoids surprises there.
 *
 * @param fd   File descriptor to stat.
 * @param sbuf Output — populated with status info on success.
 * @return 0 on success, -1 on error.
 */
extern "C" int _fstat(int fd, struct stat* sbuf) {
	if (fd == 1 || fd == 2) {
		sbuf->st_mode = S_IFCHR;
		return 0;
	}
	errno = EBADF;
	return -1;
}

/**
 * @brief Report whether file descriptor @p fildes is a terminal.
 *
 * Return 1 for fd 1/2 (stdout/stderr, backed by UART) so newlib's stdio
 * picks line-buffered mode for them instead of full-buffered. Everything
 * else: 0.
 *
 * @param fildes File descriptor to check.
 * @return 1 if @p fildes is a terminal, 0 otherwise.
 */
extern "C" int _isatty(int fildes) {
	if (fildes == 1 || fildes == 2) {
		return 1;
	}

	return 0;

}

/**
 * @brief Terminate — there's nothing to return to, so this must not return.
 *
 * Route to the existing `panic()` (`include/kernel/heap_alloc.h`) — prints
 * over UART and halts in a `wfe` loop.
 *
 * @param status Exit status (unused, as there no process model to report it to).
 */
extern "C" [[noreturn]] void _exit(int status) {
	panic("newlib: exit has been called\r\n");
}

/**
 * @brief Send signal @p sig to process @p pid.
 *
 * No process/signal model exists — always fail.
 *
 * @param pid Target process ID (unused).
 * @param sig Signal number (unused).
 * @return -1, with `errno = EINVAL`.
 */
extern "C" int _kill(pid_t pid, int sig) {
	errno = EINVAL;
	return -1;
}

/**
 * @brief Get the current process ID.
 *
 * No process model exists. Return a fixed, nonzero
 * value (e.g. 1); some libc internals treat a 0 or negative
 * pid as an error case.
 *
 * @return A fixed placeholder PID.
 */
extern "C" pid_t _getpid(void) {
	return 1;
}
