/**
 * @file sd.h
 * @brief SD card driver interface — SD/ACMD command codes and card init state.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <stdbool.h>
#include <stdint-gcc.h>

#ifdef __cplusplus

/** SDHCI response types, mapped to the COMMAND register's Response Type
 *  Select, Command Index Check Enable, and Command CRC Check Enable bits
 *  (Host Controller spec Table 2-11). Passed to sd.cpp's internal command
 *  primitive so it can build the correct COMMAND register value for
 *  whichever command is being issued. */
enum class SDResponseType : uint8_t {
	/** No response expected (e.g. CMD0). */
	None,

	/** Normal response — Card Status (e.g. CMD9, CMD13). */
	R1,

	/** Normal response with busy signal held on DAT[0] until the card
	 *  finishes the operation (e.g. CMD7, CMD12). */
	R1b,

	/** Long (136-bit) response — CID or CSD register content (e.g.
	 *  CMD2, CMD9). */
	R2,

	/** OCR register response for memory, no index/CRC check (e.g.
	 *  ACMD41). */
	R3,

	/** OCR register response for I/O, no index/CRC check (CMD5). Same
	 *  COMMAND register configuration as R3, kept as a separate name for
	 *  clarity at call sites (SD Host Controller spec Table 2-11 groups
	 *  R3/R4 together). */
	R4,

	/** Published RCA response (CMD3). */
	R6,

	/** Card Interface Condition response (CMD8). */
	R7,
};

/** SD command indices, written into the COMMAND register's index bits
 *  (board.h). ACMDs (application-specific commands) must be preceded by
 *  issuing CMD55 — the card only interprets the following command as the
 *  ACMD variant if CMD55 immediately precedes it.
 *
 *  NOTE: Included commands are a subset of the full SD command set.
 *  new commands should be added when needed.
 *
 *  TODO: Formalize command docstrings.
 */
enum class SDCommand : uint8_t {
	/** GO_IDLE_STATE — no response. Broadcast reset; puts the card into
	 *  Idle State.
	 *  Arguments: None (stuff bits). */
	CMD0 = 0,

	/** ALL_SEND_CID — R2. Broadcast; every unaddressed card responds with
	 *  its 136-bit CID.
	 *  Arguments: None (stuff bits). */
	CMD2 = 2,

	/** SEND_RELATIVE_ADDR — R6. Card publishes its own RCA and moves to
	 *  Standby State. Every addressed command from here on needs this
	 *  RCA.
	 *  Arguments: None (stuff bits). */
	CMD3 = 3,

	/** IO_SEND_OP_COND ("Get SDIO OCR") — R4. Not part of the Physical
	 *  Layer spec's memory-card-only flow (Figure 4-2) — it's step (5)
	 *  of the SD Host Controller spec's broader init sequence (§3.6,
	 *  Figure 3-9), used to probe whether the inserted card has an SDIO
	 *  (I/O) function before falling through to the memory-only ACMD41
	 *  path. Combo Cards (memory + I/O) support it; plain memory cards
	 *  don't respond at all, which is itself the expected/valid outcome
	 *  for this driver's target hardware. Step (7)'s follow-up call (real
	 *  voltage window + S18R) only happens if step (5)/(6) got a genuine
	 *  OCR back — unreachable for this driver's plain-memory-card target.
	 *  Arguments (SDIO Card Spec §3.2, Figure 3-4): bits[31:25] stuff bits
	 *  (0); bit[24] = S18R (Switching to 1.8V Request); bits[23:0] = I/O
	 *  OCR (voltage window — supported VDD range). Same layout as
	 *  ACMD41's S18R/OCR fields. The probe call (step 5) sends this as
	 *  all zero; a real follow-up call (step 7, if ever needed) would set
	 *  bit[24] and the actual VDD range in bits[23:0]. */
	CMD5 = 5,

	/** SWITCH_FUNC — R1. Negotiates high-speed/UHS bus modes beyond
	 *  Default Speed.
	 *  Arguments: bit[31] = Mode (0 = check function, 1 = switch
	 *  function); bits[3:0]/[7:4]/[11:8]/[15:12] = requested setting for
	 *  function groups 1-4 (Access Mode / Command System / Drive
	 *  Strength / Power Limit); bits[19:16]/[23:20] reserved (set to 0
	 *  or Fh); bits[30:24] reserved (set to 0). */
	CMD6 = 6,

	/** SELECT/DESELECT_CARD — R1b. Addressed; moves the card from
	 *  Standby into Transfer State.
	 *  Arguments: bits[31:16] = RCA of the card to select; 0 deselects
	 *  all cards. */
	CMD7 = 7,

	/** SEND_IF_COND — R7. Sends the host's supported voltage range and a
	 *  check pattern; the card echoes it back if it supports the
	 *  interface condition. Distinguishes v2.00+ cards from legacy ones,
	 *  which ignore this command entirely.
	 *  Arguments: bits[11:8] = supply voltage (VHS); bits[7:0] = check
	 *  pattern, echoed back verbatim in the response; bits[31:12]
	 *  reserved (set to 0). */
	CMD8 = 8,

	/** SEND_CSD — R2. Addressed; fetches the card-specific data
	 *  register. Requires RCA, card must be in Standby State.
	 *  Arguments: bits[31:16] = RCA of addressed card. */
	CMD9 = 9,

	/** SEND_CID — R2. Addressed re-fetch of CID, post-identification.
	 *  Arguments: bits[31:16] = RCA of addressed card. */
	CMD10 = 10,

	/** VOLTAGE_SWITCH — R1. Only if S18A was set in the ACMD41 response;
	 *  begins the 1.8V UHS-I signaling switch sequence.
	 *  Arguments: None (all bits reserved, set to 0). */
	CMD11 = 11,

	/** STOP_TRANSMISSION — R1b. Terminates an in-progress multi-block
	 *  read/write.
	 *  Arguments: None (stuff bits). */
	CMD12 = 12,

	/** SEND_STATUS — R1. Addressed; polls the card's current
	 *  status/error flags.
	 *  Arguments: bits[31:16] = RCA of addressed card; bit[15] = Send
	 *  Task Status Register select (only meaningful with Command Queuing
	 *  enabled — otherwise a stuff bit); bits[14:0] stuff bits. */
	CMD13 = 13,

	/** SET_BLOCKLEN — R1. Sets block length for legacy (non-SDHC/SDXC)
	 *  transfers; a formality for SDHC/SDXC cards, which are fixed at
	 *  512 bytes.
	 *  Arguments: bits[31:0] = requested block length in bytes. */
	CMD16 = 16,

	/** READ_SINGLE_BLOCK — R1. Reads one block at the given address.
	 *  Arguments: bits[31:0] = address — byte address for SDSC (CCS=0),
	 *  block address for SDHC/SDXC (CCS=1). */
	CMD17 = 17,

	/** READ_MULTIPLE_BLOCK — R1. Streams multiple blocks starting at the
	 *  given address until stopped with CMD12.
	 *  Arguments: bits[31:0] = address, same convention as CMD17. */
	CMD18 = 18,

	/** WRITE_BLOCK — R1. Writes one block at the given address.
	 *  Arguments: bits[31:0] = address, same convention as CMD17. */
	CMD24 = 24,

	/** WRITE_MULTIPLE_BLOCK — R1. Streams multiple blocks of write data
	 *  until stopped with CMD12.
	 *  Arguments: bits[31:0] = address, same convention as CMD17. */
	CMD25 = 25,

	/** APP_CMD — R1. Must immediately precede any ACMD; tells the card
	 *  the next command is application-specific.
	 *  Arguments: bits[31:16] = RCA of addressed card. */
	CMD55 = 55,
};

/** SD application-specific command indices. Each must be preceded by
 *  issuing SDCommand::CMD55. */
enum class SDAppCommand : uint8_t {
	/** SET_BUS_WIDTH — R1. Switches the data lines between 1-bit and
	 *  4-bit mode.
	 *  Arguments: bits[1:0] = bus width select (00 = 1-bit, 10 = 4-bit);
	 *  bits[31:2] stuff bits. */
	ACMD6 = 6,

	/** SD_SEND_OP_COND — R3. Polled with the host's voltage window (and
	 *  HCS if high-capacity is supported) until the card's busy bit
	 *  clears. Response also carries CCS (SDHC/SDXC vs. SDSC) and S18A
	 *  (UHS-I voltage-switch support).
	 *  Arguments: bit[30] = HCS (host capacity support); bit[28] = XPC
	 *  (max-power request for SDXC/SDUC default speed); bit[24] = S18R
	 *  (request 1.8V signaling switch); bits[23:0] = VDD voltage window
	 *  (OCR); bits[31], [29], [27:25] reserved (set to 0). */
	ACMD41 = 41,
};

#endif // __cplusplus


extern uint64_t base_clock;
extern uint32_t card_inserted;

#ifdef __cplusplus
extern "C" {
#endif

bool sdio_init();

bool sdio_read(uint32_t lba, uint32_t count, uint8_t* buf);

bool sdio_write(uint32_t lba, uint32_t count, const uint8_t* buf);

#ifdef __cplusplus
}
#endif
