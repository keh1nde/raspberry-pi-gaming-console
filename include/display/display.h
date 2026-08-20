/**
 * @file display.h
 * @brief VideoCore mailbox display driver — public API and framebuffer state.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 * Built on the kehinde-kernel project, originally MIT-licensed.
 *
 * Negotiates a framebuffer with VideoCore over the mailbox property
 * interface (channel 8) and exposes direct pixel-write primitives on top of
 * it. The framebuffer's base address, size, pitch, and dimensions are
 * runtime-negotiated (see #set_resolution) and stored here as file-scope
 * globals — every display.cpp function and the shell both read them
 * directly rather than threading a state struct through every call.
 *
 * References:
 *   - Mailbox property interface (tag format, request/response codes):
 *     https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface
 *   - Linux reference implementation of this same negotiation:
 *     drivers/video/fbdev/bcm2708_fb.c, raspberrypi/linux
 *   - Known Pi 5 firmware bug affecting #set_resolution's depth tag
 *     (worked around via config.txt, not code):
 *     https://github.com/raspberrypi/firmware/issues/1904
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once
#include <stdint.h>

inline uint32_t framebuffer_base = 0;
inline uint32_t framebuffer_size = 0;
inline uint32_t framebuffer_pitch = 0;
inline uint32_t framebuffer_width = 0;
inline uint32_t framebuffer_height = 0;

/**
 * @brief Smoke-test the mailbox transport by requesting the firmware
 *        revision.
 *
 * Doesn't negotiate a framebuffer — just confirms channel-8 request/response
 * round-tripping works before anything else in this file trusts it. Safe to
 * call before a display is connected.
 */
void display_init();

/**
 * @brief Query and print VideoCore's current physical display size.
 *
 * Read-only — doesn't allocate or touch the framebuffer, just the "get
 * physical size" tag. Useful as a standalone check independent of
 * #set_resolution's own diagnostics.
 */
void get_resolution();

/**
 * @brief Negotiate a new physical/virtual resolution and framebuffer with
 *        VideoCore, mapping the result into the kernel's address space.
 *
 * Bundles release buffer → set physical size → set depth → set virtual
 * size → set pixel order → set virtual offset → allocate buffer → get
 * pitch into a single request (tag order follows bcm2708_fb.c's own
 * sequence — see this file's references — not any response-content
 * dependency we've confirmed ourselves). Every echoed value is trusted over
 * what was requested: VideoCore can silently clamp or reject dimensions the
 * connected display doesn't support (e.g. a height beyond the panel's
 * native resolution), so #framebuffer_width / #framebuffer_height and
 * friends are always populated from the response, with a diagnostic print
 * if that differs from @p width / @p height.
 *
 * Unmaps the previous framebuffer (if any) before mapping the new one,
 * since VideoCore is not guaranteed to hand back the same base address on
 * a repeat call — confirmed to actually happen in practice, once the depth
 * bug referenced above was worked around.
 *
 * @param width  Requested physical width in pixels. May be silently
 *               clamped by VideoCore.
 * @param height Requested physical height in pixels. May be silently
 *               clamped by VideoCore.
 */
void set_resolution(int width, int height);

