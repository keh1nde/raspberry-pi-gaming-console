/**
 * @file display.cpp
 * @brief VideoCore mailbox display driver — mailbox transport and
 *        framebuffer negotiation.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Every function here builds a tag-based request buffer, hands it to
 * #_send_command over mailbox channel 8, and reads the response back out of
 * the same buffer in place. VideoCore overwrites request words with
 * response values as each tag is processed). Tag IDs, ordering, and
 * response layouts follow the Linux bcm2708_fb driver's own negotiation
 * sequence where one exists, since it's the closest thing to a
 * ground-truth reference for this interface on this platform (see
 * references below). Pixel-write primitives built on top of the
 * framebuffer this file negotiates live in render.cpp, not here.
 *
 * References:
 *   - Mailbox property interface (tag format, request/response codes):
 *     https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 *   - Linux reference implementation of this same negotiation:
 *     drivers/video/fbdev/bcm2708_fb.c, raspberrypi/linux
 *   - Known Pi 5 firmware bug (still open) where #set_resolution's depth tag
 *     echoes "accepted" while the framebuffer stays 16bpp regardless —
 *     worked around via config.txt (framebuffer_depth=32,
 *     framebuffer_ignore_alpha=1, hdmi_force_hotplug=1), not in this code:
 *     https://github.com/raspberrypi/firmware/issues/1904
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "display/display.h"
#include "kernel/barrier.h"
#include "kernel/board.h"
#include "kernel/dma.h"
#include "kernel/mmu.h"
#include "kernel/pmm.h"
#include "kernel/io.h"
#include "display/render.h"


/**
 * @brief Send a mailbox property-interface request and block until the
 *        matching response arrives.
 *
 * @p words must point to a buffer already populated with a full tag-based
 * request (see e.g. #set_resolution) and be DMA-visible — the buffer's
 * physical address (via #translate) is what actually gets written to the
 * mailbox, not the virtual pointer. On return, @p words holds the response
 * in place: VideoCore overwrites each tag's request fields with its
 * response values as it processes them.
 *
 * Uses `dsb_sy()` rather than `dsb_ish()` on both sides of the transaction
 * — VideoCore sits outside the inner-shareable domain, so a full-system
 * barrier is required for the request to be visible to it, and for its
 * response to be visible back to the CPU.
 */
void _send_command(uint32_t *words) {
	// Prevent memory ordering from this point forward
	// Done as Mailbox becomes a new memory observer
	dsb_sy();

	// Wait for !ARM_MS_FULL before writing
	uint32_t fifo_status;
	do {
		fifo_status = mmio_read(MAIL1_STA);
	} while (fifo_status & ARM_MS_FULL);

	// Write request to Mailbox
	const uint64_t buf_virt = reinterpret_cast<uint64_t>(words);
	uint64_t buf_phys = translate(buf_virt);
	mmio_write(MAIL1_WRT, buf_phys | MBOX_CHANNEL_PROPERTY);


	// Check request FIFO status to confirm a response,
	// then check last four bits of result against
	// current channel to confirm we have the correct
	// response before continuing
	do {
		do {
			fifo_status = mmio_read(MAIL0_STA);
		} while (fifo_status & ARM_MS_EMPTY);

	} while ((mmio_read(MAIL0_RD) & MBOX_CHANNEL_MASK)
		!= MBOX_CHANNEL_PROPERTY);

	dsb_sy();
}


void display_init() {
	// Note: Will allocate a 4KiB buffer anyway
	void* buf = dma_alloc(32);

	if (!buf) {
		uart_puts("[display]: Failed to initialize. dma_alloc failed.");
		return;
	}

	const auto words = static_cast<uint32_t *>(buf);

	words[0] = 28; // Total size in bytes
	words[1] = 0; // MBOX_REQUEST_CODE; Define message as request

	words[2] = 0x00000001; // MBOX_TAG_GET_FIRMWARE_REVISION; Tag identifier
	words[3] = 4; // Value-buffer size in bytes; u32 = 4
	words[4] = 0; // Request/Response indicator: 0 in (no request payload),
	// 0x80000004 out (response_flag | value-buffer size)

	words[5] = 0; // Placeholder; Will store firmware revision version on response
	words[6] = MBOX_TAG_LAST; // Terminates tag list

	_send_command(words);

	if (words[1] != MBOX_RESPONSE_SUCCESS) {
		uart_puts("[display]: initialization failed, retry later.");
		return;
	}

	uart_puts("[display]: initialized successfully. ");
	uart_puts("Current Mailbox version is: ");
	uart_put_uint(words[5]);
	uart_puts("\r\n");

	dma_free(buf, 32);
}

void get_resolution() {
	void* buf = dma_alloc(32);

	if (!buf) {
		uart_puts("[display]: Operation failed. buffer allocation failed.");
		return;
	}

	const auto words = static_cast<uint32_t *>(buf);

	words[0] = 32; // Total size in bytes
	words[1] = 0; // MBOX_REQUEST_CODE; Define message as request
	words[2] = 0x00040003; // Tag ID; Get physical size
	words[3] = 8; // Value-buffer size in bytes; u32 * 2
	words[4] = 0; // Request/Response indicator: 0 in (no request payload),
	// 0x80000000 out (response_flag | value-buffer size)

	words[5] = 0; // Placeholder; Will store width, VideoCore overwrites
	words[6] = 0; // Placeholder; Will store height, VideoCore overwrites
	words[7] = MBOX_TAG_LAST; // Terminates tag list

	_send_command(words);

	if (words[1] != MBOX_RESPONSE_SUCCESS) {
		uart_puts("[display]: operation failed, retry later.");
		return;
	}

	const uint32_t width = words[5];
	const uint32_t height = words[6];
	uart_puts("[display]: current resolution is: ");
	uart_put_uint(width);
	uart_puts("x");
	uart_put_uint(height);
	uart_puts("\r\n");

	dma_free(buf, 32);
}

void set_resolution(const int width, const int height) {
	// 38 words * 4 bytes = 152 bytes
	// 2 header + 3 (release buffer, no payload) + 4 (depth)
	// + 5 (phys size) + 5 (virt size) + 4 (pixel order)
	// + 5 (virtual offset) + 5 (allocate buffer) + 4 (get pitch)
	// Note that dma_alloc will allocate a full page anyway.
	void* buf = dma_alloc(152);

	if (!buf) {
		uart_puts("[display]: Operation failed. buffer allocation failed.");
		return;
	}

	const auto words = static_cast<uint32_t *>(buf);


	// Known Pi 5 firmware bug (raspberrypi/firmware#1904, still open): this
	// tag's response can echo "accepted" while the framebuffer VideoCore
	// actually hands back stays 16bpp regardless — pitch/size will match a
	// 16bpp buffer even though this reads back as 32. Fixed on this board by
	// setting framebuffer_depth=32 + framebuffer_ignore_alpha=1 (+
	// hdmi_force_hotplug=1) in config.txt; if depth ever looks wrong again,
	// check config.txt before suspecting this code.
	constexpr uint32_t depth = 32;       // bits per pixel
	constexpr uint32_t pixel_order = 1;  // 1 = RGB, 0 = BGR

	words[0] = 152; // total size in bytes. 4 bytes * 38 taga
	words[1] = 0; // MBOX_REQUEST_CODE

	// Tag 0: release buffer. Unlike every other tag here, this one has no
	// payload at all (request or response) so it's just {id, 0, 0} with
	// nothing after it before the next tag. Forces VideoCore to drop
	// whatever buffer already exists (e.g. the one from boot) before the
	// rest of this request runs, instead of allocate buffer below
	// potentially just handing back a reference to it unchanged.
	words[2] = 0x00048001;
	words[3] = 0; // value-buffer size: no payload
	words[4] = 0; // request length: no payload

	// Tag 1: set physical size
	words[5] = 0x00048003;
	words[6] = 8; // value-buffer size; 2 * u32
	words[7] = 8; // request length -> becomes 0x80000008 on response
	words[8] = width;
	words[9] = height;

	// Tag 3: set depth
	words[10] = 0x00048005;
	words[11] = 4; // value-buffer size; 1 * u32
	words[12] = 4; // request length
	words[13] = depth;

	// Tag 2: set virtual size
	words[14] = 0x00048004;
	words[15] = 8;
	words[16] = 8;
	words[17] = width;
	words[18] = height;

	// Tag 4: set pixel order
	words[19] = 0x00048006;
	words[20] = 4;
	words[21] = 4;
	words[22] = pixel_order;

	// Tag 5: set virtual offset
	words[23] = 0x00048009;
	words[24] = 8; // value-buffer size; 2 * u32
	words[25] = 8; // request length -> becomes 0x80000008 on response
	words[26] = 0;
	words[27] = 0;

	// Tag 5: allocate buffer. Request is 1 word (alignment); response is
	// 2 words (base address, size) — value-buffer size below covers the
	// larger of the two, and the request-length indicator stays at the
	// actual (smaller) request size, same asymmetry as get_resolution's
	// get-only tags but mirrored: here the *response* is the bigger side.
	words[28] = 0x00040001;
	words[29] = 8; // value-buffer size; must fit the 2-word response
	words[30] = 4; // request length; only the alignment word is real input
	words[31] = PAGE_SIZE; // alignment request -> becomes base address on response
	words[32] = 0; // unused on request -> becomes size on response

	// Tag 6: get pitch
	words[33] = 0x00040008;
	words[34] = 4;
	words[35] = 0; // no request payload
	words[36] = 0; // becomes pitch (bytes/line) on response

	words[37] = MBOX_TAG_LAST;

	_send_command(words);

	if (words[1] != MBOX_RESPONSE_SUCCESS) {
		uart_puts("[display]: operation failed, retry later.");
		return;
	}

	uart_puts("[display]: Resolution changed to: ");
	uart_put_uint(words[8]);
	uart_puts("x");
	uart_put_uint(words[9]);
	uart_puts("\r\n");

	if (words[8] != width || words[9] != height) {
		uart_puts("This differs from the requested resolution of: ");
		uart_put_uint(width);
		uart_puts("x");
		uart_put_uint(height);
		uart_puts("\r\n");
	}

	uart_puts("The virtual resolution is: ");
	uart_put_uint(words[17]);
	uart_puts("x");
	uart_put_uint(words[18]);
	uart_puts("\r\n");


	uart_puts("[display]: depth: "); uart_put_uint(words[13]); uart_puts(" bpp\r\n");
	uart_puts("[display]: pixel order: "); uart_put_uint(words[22]); uart_puts(" (1=RGB, 0=BGR)\r\n");

	// words[31] is VideoCore's returned address, unmasked. Older 32-bit
	// Pi docs mask this (& 0x3FFFFFFF) to strip a cache-behavior selector
	// bit before treating it as a real physical address. Not yet
	// confirmed whether that convention applies on this platform. Verify
	// before feeding this into map().
	uart_puts("[display]: framebuffer base (raw, unmasked): "); uart_put_hex(words[31]); uart_puts("\r\n");
	uart_puts("[display]: framebuffer size: "); uart_put_uint(words[32]); uart_puts(" bytes\r\n");
	uart_puts("[display]: pitch: "); uart_put_uint(words[36]); uart_puts(" bytes/line\r\n");

	// Unmap the previous framebuffer before adopting the new one as its base
	// address isn't guaranteed to stay the same across calls (moved once the
	// Pi 5 depth-negotiation bug was worked around). Leaving the old
	// mapping in place would strand page-table entries for a buffer
	// VideoCore no longer considers valid. Guarded against the very
	// first call, where framebuffer_base is still its zero-initialized
	// default and there's nothing mapped yet to unmap.
	if (framebuffer_base != 0) {
		const uint64_t prev_page_count = (framebuffer_size + PAGE_SIZE - 1) / PAGE_SIZE;
		const uint64_t prev_aligned_size = prev_page_count * PAGE_SIZE;
		unmap(framebuffer_base, prev_aligned_size);
	}

	framebuffer_base = words[31];
	framebuffer_size = words[32];
	framebuffer_pitch = words[36];
	// Actual applied physical size (words[8]/[9]), not the requested
	// width/height parameters — same "trust the echo, not the ask"
	// reasoning as everywhere else in this function.
	framebuffer_width = words[17];
	framebuffer_height = words[18];

	// The calculated page count comes out to 1012.5. Normal integer division
	// will truncate the value, making it 1012 and keeping the remaining
	// 2KBs unmapped. Thus, we round up the bytes before mapping.
	const uint64_t fb_page_count = (framebuffer_size + PAGE_SIZE - 1) / PAGE_SIZE;
	const uint64_t fb_aligned_size = fb_page_count * PAGE_SIZE;
	map(framebuffer_base, framebuffer_base, fb_aligned_size, PTE_NORMAL_NC_RW);

	// For the terminal, set terminal's maximum rows/cols
	// based on the current resolution.
	term_cols = framebuffer_width / 8;
	term_rows = framebuffer_height / 8;

	dma_free(buf, 152);
}

