/**
 * @file debug.cpp
 * @brief DebugHexDump, backed by this kernel's own printf.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * `circle/debug.h` is vendored unmodified (declaration only, no matching
 * .cpp upstream in what this port pulled in) — genuinely called from
 * several `Error()`/`DumpStatus()` diagnostic paths across the vendored
 * tree (confirmed via the linker, not grep — an earlier "nothing calls
 * this" check during shim-writing was wrong). `DebugStackTrace` isn't
 * implemented since nothing calls it (same check, this time linker-
 * confirmed clean).
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include <circle/debug.h>

#include <stdio.h>

void DebugHexDump(const void *pStart, unsigned nBytes, const char *pSource, unsigned nFlags) {
	const unsigned char *pData = (const unsigned char *) pStart;

	if (nFlags & DEBUG_HEXDUMP_HEADER) {
		printf("%s: hex dump of %u bytes at %p\r\n", pSource ? pSource : "?", nBytes, pStart);
	}

	for (unsigned nOffset = 0; nOffset < nBytes; nOffset += 16) {
		if (nFlags & DEBUG_HEXDUMP_ADDRESS) {
			printf("%p: ", pData + nOffset);
		} else {
			printf("%04X: ", nOffset);
		}

		const unsigned nLineBytes = (nBytes - nOffset < 16) ? (nBytes - nOffset) : 16;

		for (unsigned i = 0; i < 16; i++) {
			if (i < nLineBytes) {
				printf("%02X ", pData[nOffset + i]);
			} else {
				printf("   ");
			}
		}

		if (nFlags & DEBUG_HEXDUMP_ASCII) {
			printf(" ");
			for (unsigned i = 0; i < nLineBytes; i++) {
				const unsigned char c = pData[nOffset + i];
				printf("%c", (c >= 0x20 && c < 0x7F) ? c : '.');
			}
		}

		printf("\r\n");
	}
}
