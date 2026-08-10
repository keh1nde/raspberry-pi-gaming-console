/**
 * @file sd.cpp
 * @brief SD card driver implementation — SDHCI register-level init, read,
 *  and write logic for BCM2712's native SDHCI controller, the one actually
 *  wired to this board's physical microSD card slot.
 *
 * This driver originally targeted RP1's own SDIO0 block, which is real,
 * spec-compliant silicon but — as discovered via a persistent "CMD line
 * conflict" signature on every command, including CMD0 — simply isn't
 * connected to the card slot on this board at all. See
 * p-docs/claude-notes/bcm2712-sdhci-migration-notes.md for the full story
 * and sources; the protocol-level logic below (command issuance, Present
 * State polling, interrupt status handling) is unchanged from that
 * RP1-targeted, hardware-verified-up-to-CMD8 version — only the register
 * base and a small BCM2712-specific bring-up sequence (pinctrl pull-ups,
 * the vendor "cfg" register) are new.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
*/

#include "kernel/sd.h"
#include "kernel/board.h"
#include "kernel/uart.h"
#include <stdint.h>
#include <math.h>
#include "kernel/timer.h"

/** Base clock frequency for the SD clock, in MHz, read from the
 *  CAPABILITIES register's Base Clock Frequency field during sdio_init().
 *  Zero until initialization reads it. */
uint64_t base_clock = 0;

/** Card Identification (CMD2) and Relative Card Address (CMD3), captured
 *  once by sdio_init() and needed by every addressed command afterward
 *  (CMD7 to select the card, any future ACMD's CMD55 prefix, etc). See
 *  _sdio_finish_identification(). */
uint32_t cid[4] = {0};
uint16_t rca = 0;

/** Card Type. 0 == SDSC card; 1 == SDHC/SDXC card */
uint32_t ccs = 0;

/** Card Detect (PRESENT_STATE_SDIO0 bit 16), latched by _sdio_clock_init()
 *  each time it runs. Purely a hardware presence signal — distinct from
 *  "identification succeeded" (see ccs/cid/rca) — kept as its own global
 *  so a future disk_status()/error path can report "no card" separately
 *  from "card present but not initialized" without re-reading the
 *  register. 0 == Card not inserted; 1 == Card inserted */
uint32_t card_inserted = 0;

/**
 * @brief Waits for Clock Control's Internal Clock Stable bit (bit 1) to
 *  assert, per the 150ms timeout the Host Controller spec itself defines
 *  for this wait (§3.2.3, Figure 3-5 step (5)) — not an arbitrary bound.
 *  Needed twice during clock setup (Figure 3-3: once after Internal
 *  Clock Enable, again after PLL Enable on Version 4.10+ controllers),
 *  hence its own helper.
 *
 * @return true once stable; false (after logging the live register
 *  value) if 150ms elapses first.
 */
bool _sdio_wait_clock_stable() {
 const uint64_t start = get_ticks();
 while (((mmio_read16(CLOCK_CONTROL_SDIO0) >> 1) & 1) == 0) {
  if (ticks_elapsed_ms(start, 150)) {
   uart_puts("[SDIO] error: Internal Clock Stable never asserted after"
             " 150ms. CLOCK_CONTROL: ");
   uart_put_hex(mmio_read16(CLOCK_CONTROL_SDIO0));
   uart_puts("\r\n");
   return false;
  }
 }
 return true;
}

/**
 * @brief One-time BCM2712-specific bring-up: enables internal pull-ups on
 *  EMMC_CMD/DAT0-3 via BCM2712's own pinctrl block.
 *
 * A real prerequisite, not polish: without a pull-up, the CMD line has no
 * defined idle-high state and can float/sag low the instant nothing is
 * actively driving it — the leading hypothesis for the spec-documented
 * "CMD line conflict" signature (Host Controller spec §2.2.18: driving
 * high but reading back low) hit on every command while this driver was
 * still targeting RP1's SDIO0, whose own pins have entirely separate pull
 * config. See board.h's PINCTRL_EMMC_PULL_REG comment and
 * p-docs/claude-notes/bcm2712-sdhci-migration-notes.md for the full
 * per-pin bit-offset derivation. Read-modify-write, since EMMC_CLK/
 * EMMC_DS share this register and must be left alone.
 */
void _sdio_pinctrl_init() {
 uint32_t pull_reg = mmio_read(PINCTRL_EMMC_PULL_REG);
 pull_reg = (pull_reg & ~PINCTRL_EMMC_CMD_DAT_MASK) | PINCTRL_EMMC_CMD_DAT_PULLUP;
 mmio_write(PINCTRL_EMMC_PULL_REG, pull_reg);
}

/**
 * @brief Selects SD (not eMMC) pin timing in BCM2712's vendor "cfg"
 *  register block.
 *
 * Per Linux's sdhci_bcm2712_set_clock() (drivers/mmc/host/sdhci-brcmstb.c),
 * this must be written every time the SD clock is (re)configured,
 * alongside the standard Clock Control sequence — an SDHCI-spec-external
 * requirement layered underneath it, the BCM2712 equivalent of what RP1's
 * clock-generator bring-up used to be for the (now unused) RP1-targeted
 * path. This driver only ever configures the clock once, at the fixed
 * identification-phase rate, so one call from _sdio_clock_init() covers
 * it. Read-modify-write, since this register's other bits are reserved
 * for features (UHS/HS-eMMC/CQE) this driver doesn't implement.
 */
void _sdio_cfg_select_sd_pin_timing() {
 uint32_t pin_sel = mmio_read(SDIO_CFG_SD_PIN_SEL);
 pin_sel = (pin_sel & ~SDIO_CFG_SD_PIN_SEL_MASK) | SDIO_CFG_SD_PIN_SEL_SD;
 mmio_write(SDIO_CFG_SD_PIN_SEL, pin_sel);
}

/**
 * @brief Clock initialization helper for sdio_init().
 *
 * Helper that initializes the clock during SDIO initialization.
 * It is used in sdio_init() to allow a retry in the event of a
 * CMD8 response failure.
 *
 * @return True if the clock is successfully initialized. False otherwise.
 */
bool _sdio_clock_init() {
 // Software Reset For All — spec-mandated first step of Host Controller
 // initialization (§2.2.17, Table 2-23: "During its initialization, the
 // Host Driver shall set this bit to 1 to reset the Host Controller.").
 // Safe to run before card detection: the spec explicitly excludes the
 // card detection circuit from this reset's effect. Software Reset is an
 // 8-bit register (0x02F) sharing Timeout Control's 4-byte-aligned word;
 // the bit is RWAC (self-clearing) — the Host Controller clears it once
 // the reset completes and the Capabilities registers are valid again.
 // Without this, every register this driver touches starts from whatever
 // state the Pi 5's own boot firmware left the controller in after using
 // this exact SDIO block to load kernel8.img off the boot partition —
 // not the clean power-on-reset state the rest of this init sequence
 // assumes.
 mmio_write8(SOFTWARE_RESET_SDIO0, 1U); // Software Reset For All.

 const uint64_t reset_start = get_ticks();
 while ((mmio_read8(SOFTWARE_RESET_SDIO0) & 1U) != 0) {
  if (ticks_elapsed_ms(reset_start, 100)) {
   uart_puts("[SDIO] error: Software Reset For All never cleared after"
             " 100ms.\r\n");
   return false;
  }
 }

 // One-time BCM2712-specific bring-up: internal pull-ups on EMMC_CMD/
 // DAT0-3 (pinctrl) and SD-vs-eMMC pin timing select ("cfg" register) —
 // see _sdio_pinctrl_init()/_sdio_cfg_select_sd_pin_timing()'s docstrings.
 // Neither has anything to do with card detection or the SDHCI IP itself,
 // so both can safely run before card detection, same as the RP1
 // clock-generator bring-up they replace used to.
 _sdio_pinctrl_init();
 _sdio_cfg_select_sd_pin_timing();

 // Detect Card
 card_inserted = (mmio_read(PRESENT_STATE_SDIO0) >> 16) & 1;
 if (card_inserted == 0) {
  uart_puts("[SDIO] error: Card Inserted bit reads 0; no card detected"
            " (or card-detect logic is wrong for this controller).\r\n");
  return false; // No card inserted.
 }

 // Read capabilities register for SD card details.
 const uint32_t capabilities = mmio_read(CAPABILITIES1_SDIO0);

 // Turn on SD Bus Power before anything else touches the bus — per Host
 // Controller spec §3.3 (Figure 3-6, "SD Bus Power Control Sequence"),
 // this is its own distinct procedure, separate from card detection and
 // clock setup. Table 1-10 explains why skipping it produces exactly a
 // silent hang rather than an error: SDCLK is held low whenever SD Bus
 // Power is 0, *regardless* of SD Clock Enable/Internal Clock Stable —
 // so the internal oscillator can lock just fine while the bus itself
 // never sees a clock edge, and every command silently never gets
 // clocked out. 3.3V (bits [3:1] = 111b) matches the 2.7-3.6V window
 // this driver's ACMD41 calls already assume; Power Control is an
 // 8-bit register (§2.2.12), not 16 or 32.
 mmio_write8(POWER_CONTROL_SDIO0, 0x0F); // SD Bus Voltage Select = 3.3V, SD Bus Power = 1.

 // Unmask every Normal/Error Interrupt Status bit so the Normal/Error
 // Interrupt Status registers actually latch events at all. Per Host
 // Controller spec §2.2.20/§2.2.21 ("Setting to 1 enables Interrupt
 // Status"), a status bit stays hard-masked — reads 0 forever, even
 // though the underlying event genuinely occurred on the bus — until its
 // Status Enable bit is set here. This is a different register from
 // Signal Enable (§2.2.22/23, 0x038/0x03A), which only controls whether
 // an IRQ is raised to the GIC; since this driver polls the status
 // registers directly instead of using interrupts, Signal Enable is
 // deliberately left at its reset value of 0. Without this, every
 // _send_command()/_send_data_command() wait loop below spins forever —
 // Command Complete, Transfer Complete, and every error bit are all
 // gated by this same enable, so a real error would hang exactly like a
 // silent success instead of being reported.
 mmio_write16(NORMAL_INTERRUPT_STATUS_ENABLE_SDIO0, 0xFFFF);
 mmio_write16(ERROR_INTERRUPT_STATUS_ENABLE_SDIO0, 0xFFFF);

 // Read version, base clock, and clock multiplier. base_clock_mhz stays
 // in the raw MHz units CAPABILITIES itself uses (needed as-is for the
 // version_val compatibility check below); base_clock is stored in Hz,
 // since the SDCLK divisor math further down expects Hz.
 const uint16_t version = mmio_read16(HOST_CONTROLLER_VERSION_SDIO0);
 const uint16_t version_val = (version & 0xFF00) >> 8;
 uint32_t base_clock_mhz = (capabilities >> 8) & 0xFF;

 // base clock values greater than 0b11xxxxxx are not compatible
 // with host controller versions below 3.00. in the case this occurs:
 if (version_val < 0x02 && base_clock_mhz > 0x3F)
  uart_puts("[SDIO] warning: BCF may not be supported,"
            " or sdio_init implementation is incorrect.");

 // BCM2712's native SDHCI controller is fed by clk_emmc2, a plain fixed
 // 200MHz clock (bcm2712.dtsi) — unlike RP1's own SDIO0/SDIO1, which
 // always report 0 here due to an explicit SDHCI_QUIRK_CAP_CLOCK_BASE_
 // BROKEN pdata quirk declared only against RP1's variant in Linux's
 // sdhci-of-dwcmshc.c. sdhci-brcmstb.c declares no equivalent quirk for
 // BCM2712, so Capabilities should self-report 200 correctly in practice.
 // This fallback is kept anyway — both because the spec itself
 // anticipates a Base Clock Frequency of 0 (§3.6, Figure 3-3, step (1):
 // "the Host System shall provide this information... by another
 // method") and because which path this controller actually takes
 // hasn't yet been confirmed on real hardware.
 if (base_clock_mhz == 0) {
  uart_puts("[SDIO] Base Clock Frequency read as 0; using BCM2712's known"
            " fixed 200MHz clk_emmc2 clock.\r\n");
  base_clock_mhz = 200;
 }

 base_clock = static_cast<uint64_t>(base_clock_mhz) * 1000000ULL;

 // Write in divisor to SDCLK/RCLK register field. Clock Control is a
 // 16-bit register (Timeout Control/Software Reset share its 4-byte-
 // aligned word) — must use the 16-bit accessor, not a 32-bit one.
 //
 // SDCLK = base_clock / (2 * divisor) in divided-clock mode, and the
 // identification-phase clock must not exceed 400kHz (SD Physical
 // Layer spec §4.2), so divisor has to round UP from the exact ratio —
 // rounding down could leave SDCLK slightly over 400kHz. base_clock and
 // target_clock are both integers, so plain division truncates instead
 // of rounding; ceil() on an already-truncated integer result is a
 // no-op, hence the explicit integer ceiling-division form instead.
 const uint64_t target_clock = 2 * 400000;
 const uint64_t divisor = (base_clock + target_clock - 1) / target_clock;
 uint16_t init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value = (init_value & ~0xFF00U) | ((divisor & 0xFFU) << 8);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 // Write in Clock Generator Select
 init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value &= ~(1U << 5);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 // Enable Clock (bit 0, Internal Clock Enable) — starts the internal
 // oscillator only. Neither this bit nor Internal Clock Stable (bit 1)
 // control whether SDCLK actually reaches the bus — that's SD Clock
 // Enable (bit 2), set further down (Host Controller spec §3.2.2,
 // Figure 3-4), a separate, later step easy to miss because Figure 3-3
 // (Internal Clock Setup) and Figure 3-4 (SD Clock Supply) are two
 // distinct procedures.
 init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value |= (1U);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 if (!_sdio_wait_clock_stable()) return false;

 // Set PLL Enable (bit 3) — required from Host Controller Version 4.10
 // onward (§3.2.1, Figure 3-3, steps (5)-(6)); this controller reports
 // 4.20. Internal Clock Stable must be re-checked afterward — enabling
 // the PLL drops it back to 0 before it re-asserts.
 init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value |= (1U << 3);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 if (!_sdio_wait_clock_stable()) return false;

 // Set SD Clock Enable (bit 2) — the actual "start supplying SDCLK to
 // the card" step (§3.2.2, Figure 3-4), independent of everything above.
 init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value |= (1U << 2);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 return true;
}

/** Outcome of issuing a single command via _send_command(), or a data
 *  transaction via _send_data_command(). Mirrors the error bits the host
 *  controller can report in ERROR_INTERRUPT_STATUS — Timeout/CrcError/
 *  IndexError for a command-phase failure (bits 0/1/3), DataTimeoutError/
 *  DataCrcError/DataEndBitError for a data-phase failure during the block
 *  loop (bits 4/5/6) — distinct bit positions, so distinct variants. */
enum class SDCommandStatus : uint8_t {
 Success,
 Timeout,
 CrcError,
 IndexError,
 NotExpected,
 DataTimeoutError,
 DataCrcError,
 DataEndBitError,
};

/**
 * @brief Issues Software Reset For CMD Line and waits for it to self-clear.
 *
 * Per Host Controller spec §2.2.9 (Present State register, the Version
 * 4.10+ note on Command Inhibit (CMD)): once a command hits a response
 * error, Command Inhibit (CMD) latches at 1 specifically to stop a *next*
 * command from overwriting that error status before the Host Driver has
 * seen it — it does not clear on its own, and does not clear just because
 * a later command's own issuance completes cleanly. Software Reset For
 * CMD Line (§2.2.17, bit 1) is the only thing that clears it.
 *
 * Called from _send_command()'s error path so a command-level error
 * (CRC/Index/Timeout) never leaves Command Inhibit (CMD) stuck, silently
 * hanging whatever command gets issued next — including CMD0: "Command
 * CRC Error" is raised both for a real response CRC failure *and* for a
 * CMD-line drive conflict the Host Controller detects independently of
 * any response, so it can fire even for a no-response command like CMD0.
 *
 * @return true once cleared; false (after logging the live register
 *  value) if it never clears within 100ms.
 */
bool _sdio_reset_cmd_line() {
 mmio_write8(SOFTWARE_RESET_SDIO0, 1U << 1); // Software Reset For CMD Line.

 const uint64_t start = get_ticks();
 while ((mmio_read8(SOFTWARE_RESET_SDIO0) & (1U << 1)) != 0) {
  if (ticks_elapsed_ms(start, 100)) {
   uart_puts("[SDIO] error: Software Reset For CMD Line never cleared"
             " after 100ms. SOFTWARE_RESET: ");
   uart_put_hex(mmio_read8(SOFTWARE_RESET_SDIO0));
   uart_puts("\r\n");
   return false;
  }
 }
 return true;
}

/**
 * @brief Issues Software Reset For DAT Line and waits for it to self-clear.
 *
 * The data-phase counterpart to _sdio_reset_cmd_line() — Command Inhibit
 * (DAT) latches on a data-phase error (§2.2.17, bit 2 clears it) the same
 * way Command Inhibit (CMD) latches on a command-phase error. Called from
 * _send_data_command()'s data-phase error paths (Buffer Ready wait and
 * Transfer Complete wait) so a stuck DAT line can't silently hang whatever
 * data command gets issued next.
 *
 * @return true once cleared; false (after logging the live register
 *  value) if it never clears within 100ms.
 */
bool _sdio_reset_dat_line() {
 mmio_write8(SOFTWARE_RESET_SDIO0, 1U << 2); // Software Reset For DAT Line.

 const uint64_t start = get_ticks();
 while ((mmio_read8(SOFTWARE_RESET_SDIO0) & (1U << 2)) != 0) {
  if (ticks_elapsed_ms(start, 100)) {
   uart_puts("[SDIO] error: Software Reset For DAT Line never cleared"
             " after 100ms. SOFTWARE_RESET: ");
   uart_put_hex(mmio_read8(SOFTWARE_RESET_SDIO0));
   uart_puts("\r\n");
   return false;
  }
 }
 return true;
}

/**
 * @brief Issues a single SD/ACMD command and waits for its completion.
 *
 * Shared low-level primitive for every command the driver issues: writes
 * the argument, builds and writes the COMMAND register for the given
 * response type, waits for Command Complete or an error, and (for
 * commands that carry a response) reads it back from RESPONSE0. Callers
 * pack `argument` themselves — see the per-command bit-field notes on
 * SDCommand/SDAppCommand in sd.h.
 *
 * Only covers commands with no data phase (Data Present Select left at
 * 0); CMD6/CMD17/CMD18/CMD24/CMD25 will need that wired in separately
 * once sdio_read()/sdio_write() are underway. Callers of ACMDs are
 * responsible for issuing CMD55 first — this does not do it for you.
 *
 * @param cmd Command index to issue.
 * @param argument Pre-packed 32-bit argument word.
 * @param response_type Expected response type; determines which COMMAND
 *  register check bits get set.
 * @param response_out If non-null and a response is expected, receives
 *  RESPONSE0's content on success. Left untouched on failure.
 * @return Success, or the specific error the host controller reported.
 */
SDCommandStatus _send_command(SDCommand cmd, uint32_t argument,
 SDResponseType response_type, uint32_t* response_out) {
 // Wait until the CMD line is free before issuing another command.
 while ((mmio_read(PRESENT_STATE_SDIO0) & 1) == 1) {}

 // Argument is documented as one 32-bit register based at ARGUMENT_LOW;
 // ARGUMENT_HIGH is a separate 16-bit-aligned half, not a 32-bit-wide
 // register of its own, so both halves need the 16-bit accessor.
 mmio_write16(ARGUMENT_LOW_SDIO0, argument & 0xFFFF);
 mmio_write16(ARGUMENT_HIGH_SDIO0, (argument >> 16) & 0xFFFF);

 uint16_t command_val = (static_cast<uint16_t>(cmd) & 0x3F) << 8;
 switch (response_type) {
  case SDResponseType::None:
   break; // Response Type Select = 00, no index/CRC check.
  case SDResponseType::R2:
   command_val |= (1 << 3) | 0x1; // CRC check; Response Type Select = 01.
   break;
  case SDResponseType::R3:
  case SDResponseType::R4:
   command_val |= 0x2; // Response Type Select = 10, no index/CRC check.
   break;
  case SDResponseType::R1:
  case SDResponseType::R6:
  case SDResponseType::R7:
   command_val |= (1 << 4) | (1 << 3) | 0x2; // Index + CRC check; Response Type Select = 10.
   break;
  case SDResponseType::R1b:
   command_val |= (1 << 4) | (1 << 3) | 0x3; // Index + CRC check; Response Type Select = 11.
   break;
 }

 mmio_write16(COMMAND_SDIO0, command_val);

 // Wait for Command Complete or an error interrupt.
 uint16_t normal_status;
 uint16_t error_status;
 do {
  normal_status = mmio_read16(NORMAL_INTERRUPT_STATUS_SDIO0);
  error_status = mmio_read16(ERROR_INTERRUPT_STATUS_SDIO0);
 } while ((normal_status & 1) == 0 && error_status == 0);

 if (error_status != 0) {
  mmio_write16(ERROR_INTERRUPT_STATUS_SDIO0, error_status); // RW1C: clear reported errors.
  _sdio_reset_cmd_line(); // Clear the Command Inhibit (CMD) latch this error left behind.

  // Raw diagnostic dump — the coarse SDCommandStatus below only reports
  // the first matching bit, which loses information when the controller
  // sets more than one simultaneously (e.g. Command CRC Error alongside
  // Command Timeout Error, per spec Table 2-26's CMD-line-conflict case).
  uart_puts("[SDIO] command error: cmd=");
  uart_put_hex(static_cast<uint16_t>(cmd));
  uart_puts(" ERROR_INTERRUPT_STATUS=");
  uart_put_hex(error_status);
  uart_puts("\r\n");

  if (error_status & (1 << 1)) return SDCommandStatus::CrcError;
  if (error_status & (1 << 3)) return SDCommandStatus::IndexError;
  return SDCommandStatus::Timeout;
 }

 mmio_write16(NORMAL_INTERRUPT_STATUS_SDIO0, 1); // RW1C: clear Command Complete.

 if (response_out != nullptr && response_type != SDResponseType::None)
  *response_out = mmio_read(RESPONSE0_SDIO0);

 return SDCommandStatus::Success;
}

/**
 * @brief Issues a single ACMD (application-specific command) and waits
 *  for its completion.
 *
 * Overload for the SDAppCommand space, so call sites don't need to cast
 * across command types to reuse the shared primitive above — SDCommand
 * and SDAppCommand stay distinct types everywhere except this one spot,
 * where it's safe because both are just an underlying command index.
 * Does not issue CMD55 itself; callers are still responsible for that
 * prefix (see SDAppCommand's docs in sd.h).
 *
 * @param cmd Application-specific command index to issue.
 * @param argument Pre-packed 32-bit argument word.
 * @param response_type Expected response type; determines which COMMAND
 *  register check bits get set.
 * @param response_out If non-null and a response is expected, receives
 *  RESPONSE0's content on success. Left untouched on failure.
 * @return Success, or the specific error the host controller reported.
*/
SDCommandStatus _send_command(SDAppCommand cmd, uint32_t argument,
 SDResponseType response_type, uint32_t* response_out) {
 uint32_t response = 0;
 const SDCommandStatus in_response_type  = _send_command(SDCommand::CMD55,
  0x00000000, SDResponseType::R1, &response);

 switch (in_response_type) {
  case SDCommandStatus::CrcError   : return in_response_type;
  case SDCommandStatus::IndexError : return in_response_type;
  case SDCommandStatus::Timeout    : return in_response_type;
  default: break;
 }

 if (const uint32_t app_cmd = (response >> 5) & 1; app_cmd == 0) return SDCommandStatus::NotExpected;

 return _send_command(static_cast<SDCommand>(cmd), argument, response_type, response_out);
}

SDCommandStatus _send_data_command(SDCommand cmd, uint32_t argument,
    SDResponseType response_type, uint16_t block_count, bool is_read,
    uint8_t* buf, uint32_t* response_out) {
 // Wait until both the CMD and DAT lines are free — must keep waiting if
 // *either* is still inhibited, not only while both are simultaneously set.
 while (((mmio_read(PRESENT_STATE_SDIO0) & 1) == 1)
  || (((mmio_read(PRESENT_STATE_SDIO0) >> 1) & 1) == 1)) {}

 // Set block size = 0x200 (512 bytes) and block count to block_count.
 // Block Size and Block Count are two independent 16-bit registers
 // sharing one 4-byte-aligned word (same pattern as Clock Control) —
 // need the 16-bit accessor for both.
 uint16_t block_size = mmio_read16(BLOCK_SIZE_SDIO0);
 block_size = (block_size & ~0xFFFU) | 0x200; // Transfer Block Size, bits [11:0].
 mmio_write16(BLOCK_SIZE_SDIO0, block_size);
 mmio_write16(BLOCK_COUNT_SDIO0, block_count);


 // Argument is documented as one 32-bit register based at ARGUMENT_LOW;
 // ARGUMENT_HIGH is a separate 16-bit-aligned half, not a 32-bit-wide
 // register of its own, so both halves need the 16-bit accessor.
 mmio_write16(ARGUMENT_LOW_SDIO0, argument & 0xFFFF);
 mmio_write16(ARGUMENT_HIGH_SDIO0, (argument >> 16) & 0xFFFF);


 // Transfer Mode is a 16-bit register (Command shares its 4-byte-aligned
 // word) — must use the 16-bit accessor, not a 32-bit one.
 uint16_t transfer_mode = mmio_read16(TRANSFER_MODE_SDIO0);

 // Data Transfer Direction
 // 1 == SD-to-Host; 0 == Host-to-SD
 if (is_read) transfer_mode |= (1 << 4);
 else transfer_mode &= ~(1 << 4);

 // 1 == Multi-Block Enable; 0 == Single-Block Enable
 if (block_count > 1) transfer_mode |= (1 << 5);
 else transfer_mode &= ~(1 << 5);

 // Auto CMD Enable, bits [3:2]: 01b == Auto CMD12 Enable; 00b == disabled.
 transfer_mode &= ~(3 << 2);
 if (block_count > 1) transfer_mode |= (1 << 2);

 transfer_mode |= (1 << 1); // Block Count Enable
 transfer_mode &= ~1U; // DMA Enable = 0 (PIO in use).

 mmio_write16(TRANSFER_MODE_SDIO0, transfer_mode);

 // Set bit 5, Data Present Select
 uint16_t command_val = (static_cast<uint16_t>(cmd) & 0x3F) << 8;
 switch (response_type) {
   case SDResponseType::None:
   break; // Response Type Select = 00, no index/CRC check.
  case SDResponseType::R2:
   command_val |= (1 << 3) | 0x1; // CRC check; Response Type Select = 01.
   break;
  case SDResponseType::R3:
  case SDResponseType::R4:
   command_val |= 0x2; // Response Type Select = 10, no index/CRC check.
   break;
  case SDResponseType::R1:
  case SDResponseType::R6:
  case SDResponseType::R7:
   command_val |= (1 << 4) | (1 << 3) | 0x2; // Index + CRC check; Response Type Select = 10.
   break;
  case SDResponseType::R1b:
   command_val |= (1 << 4) | (1 << 3) | 0x3; // Index + CRC check; Response Type Select = 11.
   break;
 }

 command_val |= (1 << 5); // Data Present Select = 1
 mmio_write16(COMMAND_SDIO0, command_val);

 // Wait for Command Complete or an error interrupt.
 uint16_t normal_status;
 uint16_t error_status;
 do {
  normal_status = mmio_read16(NORMAL_INTERRUPT_STATUS_SDIO0);
  error_status = mmio_read16(ERROR_INTERRUPT_STATUS_SDIO0);
 } while ((normal_status & 1) == 0 && error_status == 0);

 if (error_status != 0) {
  mmio_write16(ERROR_INTERRUPT_STATUS_SDIO0, error_status); // RW1C: clear reported errors.
  _sdio_reset_cmd_line(); // Clear the Command Inhibit (CMD) latch this error left behind.
  if (error_status & (1 << 1)) return SDCommandStatus::CrcError;
  if (error_status & (1 << 3)) return SDCommandStatus::IndexError;
  return SDCommandStatus::Timeout;
 }

 mmio_write16(NORMAL_INTERRUPT_STATUS_SDIO0, 1); // RW1C: clear Command Complete.

 if (response_out != nullptr && response_type != SDResponseType::None)
  *response_out = mmio_read(RESPONSE0_SDIO0);

 const uint32_t total_words = static_cast<uint32_t>(block_count) * 128;
 const uint16_t ready_bit = is_read ? (1 << 5) : (1 << 4); // Buffer Read/Write Ready.

 for (uint32_t i = 0; i < total_words; i++) {
  if (i % 128 == 0) {
   // Start of a new block: wait for Buffer Read/Write Ready, or an error.
   do {
    normal_status = mmio_read16(NORMAL_INTERRUPT_STATUS_SDIO0);
    error_status = mmio_read16(ERROR_INTERRUPT_STATUS_SDIO0);
   } while ((normal_status & ready_bit) == 0 && error_status == 0);

   if (error_status != 0) {
    mmio_write16(ERROR_INTERRUPT_STATUS_SDIO0, error_status); // RW1C: clear reported errors.
    _sdio_reset_dat_line(); // Clear the Command Inhibit (DAT) latch this error left behind.
    if (error_status & (1 << 5)) return SDCommandStatus::DataCrcError;
    if (error_status & (1 << 6)) return SDCommandStatus::DataEndBitError;
    return SDCommandStatus::DataTimeoutError;
   }

   mmio_write16(NORMAL_INTERRUPT_STATUS_SDIO0, ready_bit); // RW1C: clear only this bit.
  }

  const uint32_t offset = i * 4;

  if (is_read) {
   const uint32_t data = mmio_read(BUFFER_DATA_0_SDIO0);

   buf[offset + 0] = data & 0xFF; // bits [7:0]
   buf[offset + 1] = (data >> 8) & 0xFF; // bits [15:8]
   buf[offset + 2] = (data >> 16) & 0xFF; // bits [23:16]
   buf[offset + 3] = (data >> 24) & 0xFF; // bits [31:24]
  } else {
   const uint32_t data = buf[offset + 0]
   | static_cast<uint32_t>(buf[offset + 1]) << 8
   | static_cast<uint32_t>(buf[offset + 2]) << 16
   | static_cast<uint32_t>(buf[offset + 3]) << 24;

   mmio_write(BUFFER_DATA_0_SDIO0, data);
  }
 }

 // Wait for the whole transaction to finish (Transfer Complete), or an error.
 do {
  normal_status = mmio_read16(NORMAL_INTERRUPT_STATUS_SDIO0);
  error_status = mmio_read16(ERROR_INTERRUPT_STATUS_SDIO0);
 } while ((normal_status & (1 << 1)) == 0 && error_status == 0);

 if (error_status != 0) {
  mmio_write16(ERROR_INTERRUPT_STATUS_SDIO0, error_status); // RW1C: clear reported errors.
  _sdio_reset_dat_line(); // Clear the Command Inhibit (DAT) latch this error left behind.
  if (error_status & (1 << 5)) return SDCommandStatus::DataCrcError;
  if (error_status & (1 << 6)) return SDCommandStatus::DataEndBitError;
  return SDCommandStatus::DataTimeoutError;
 }

 mmio_write16(NORMAL_INTERRUPT_STATUS_SDIO0, 1 << 1); // RW1C: clear Transfer Complete.

 return SDCommandStatus::Success;
}

/**
 * @brief Issues CMD2 (ALL_SEND_CID) and reads back the full CID content.
 *
 * CMD2's R2 response doesn't fit in the single register word every other
 * response type in this driver uses — its 120 bits of content span all
 * four response registers (SD Host Controller spec Table 2-13: on-wire
 * bits [127:8] land in the response register's bits [119:0]).
 * _send_command() only reads the first word, so this reads the other
 * three directly once the command completes.
 *
 * board.h's RESPONSE0 through RESPONSE7 constants are individually
 * 16-bit-spaced (matching the RP1's own register map), but mmio_read()
 * always performs a 32-bit access, so each *pair* forms one logical
 * 32-bit response word — the four words actually start at RESPONSE0,
 * RESPONSE2, RESPONSE4, and RESPONSE6.
 *
 * CMD9 (SEND_CSD) is also R2 and would need the same technique if it's
 * ever added.
 *
 * @param cid_out Receives the four 32-bit response words, least
 *  significant first: cid_out[0] = RESPONSE0 (on-wire bits [39:8]),
 *  cid_out[1] = RESPONSE2, cid_out[2] = RESPONSE4, cid_out[3] = RESPONSE6
 *  (only this word's low 24 bits are meaningful — its top byte falls
 *  outside the response's defined [119:0] range). Left untouched on
 *  failure. Field-level parsing (MID/OID/PNM/etc, Physical Layer spec
 *  Table 5-2) isn't done here — this just captures the raw content.
 * @return Success, or the specific error the host controller reported.
*/
SDCommandStatus _sdio_read_cid(uint32_t cid_out[4]) {
 const SDCommandStatus status = _send_command(SDCommand::CMD2, 0, SDResponseType::R2, &cid_out[0]);
 if (status != SDCommandStatus::Success) return status;

 cid_out[1] = mmio_read(RESPONSE2_SDIO0);
 cid_out[2] = mmio_read(RESPONSE4_SDIO0);
 cid_out[3] = mmio_read(RESPONSE6_SDIO0);

 return SDCommandStatus::Success;
}

/**
 * @brief Finishes identification: reads CID (CMD2) and RCA (CMD3) into
 *  the driver's persistent state (the cid/rca globals above).
 *
 * Common tail shared by every successful path through sdio_init() once
 * the card has reported ready via ACMD41 — SDSC, SDHC/SDXC, and post
 * voltage-switch all end here identically.
 *
 * @return true on success; false if either command fails.
*/
bool _sdio_finish_identification() {
 if (_sdio_read_cid(cid) != SDCommandStatus::Success) return false;

 uint32_t response = 0;
 if (_send_command(SDCommand::CMD3, 0x00000000,
  SDResponseType::R6, &response) != SDCommandStatus::Success)
  return false;

 rca = (response >> 16) & 0xFFFF;

 // Select newly identified card
 if (_send_command(SDCommand::CMD7, static_cast<uint32_t>(rca) << 16,
  SDResponseType::R1b, &response) != SDCommandStatus::Success) return false;

 // Set Block Length to 512 bytes
 if (_send_command(SDCommand::CMD16, 512,
  SDResponseType::R1, &response) != SDCommandStatus::Success) return false;

 return true;
}

/**
 * @brief Issues CMD0, then CMD8, validating CMD8's response if the card
 *  sends one back. Retries once (redoing clock setup too) on a CRC or
 *  Index error, per the standing "identification first stage" TODO.
 *
 * @param[out] f8 Set to 0 if CMD8 timed out (legacy/non-SD card — the
 *  caller should continue with HCS forced to 0 per Figure 4-2). Left
 *  unmodified (starts at 1) for every other outcome.
 * @return false if the card is unusable — CMD8 responded but with a
 *  mismatched voltage/check pattern (Figure 4-2: "Unusable Card", not a
 *  retry case), or a CRC/Index error persisted after the retry. true
 *  otherwise, including the legacy/no-response case.
 */
bool _sdio_identify_stage1(int* f8) {
 for (int attempt = 0; attempt < 2; ++attempt) {
  // Set SD card to idle.
  _send_command(SDCommand::CMD0, 0, SDResponseType::None, nullptr);

  // Send CMD8 to support high capacity SD card.
  // Full argument corresponds to VHS = 2.7-3.6V (via 0b0001)
  // and a check pattern of 0xAA.
  uint32_t response = 0;
  const SDCommandStatus cmd8_status = _send_command(SDCommand::CMD8, 0x000001AA, SDResponseType::R7, &response);

  // If there is no response for CMD8, then set F8 to 0.
  // The card is either v2.00 or lower, or isn't an SD card.
  if (cmd8_status == SDCommandStatus::Timeout) {
   *f8 = 0;
   return true;
  }

  // A CRC/Index error means the response can't be trusted — retry the
  // whole stage (including the clock) once before giving up.
  if (cmd8_status == SDCommandStatus::CrcError || cmd8_status == SDCommandStatus::IndexError) {
   if (!_sdio_clock_init()) return false;
   continue;
  }


  // Voltage and check pattern must match.
  // Mismatched voltage/check pattern indicates an unusable card.
  // See SD Assoc Physical Layer Spec, Figure 4-2. CMD8's R7 response packs
  // the voltage-accepted echo into bits [11:8] and the check-pattern echo
  // into bits [7:0] — the shifts below were previously off by 8 bits
  // (>>16/>>8, as if voltage lived at [19:16]), a latent bug never
  // reachable until this session's controller retarget, since CMD8 never
  // got a real Command Complete before (see the migration notes' "CMD
  // line conflict" section). Caught on real hardware 2026-08-10: a
  // correctly-echoing response (0x1AA, matching the 0x1AA sent) was
  // misread as voltage_accepted=0/echoed_pattern=0x1 and rejected as an
  // "Unusable Card".
  const uint32_t voltage_accepted = (response >> 8) & 0xF;
  const uint32_t echoed_pattern = response & 0xFF;

  return voltage_accepted == 0b0001 && echoed_pattern == 0xAA;
 }

 return false; // CRC/Index error persisted after the retry.
}

/**
 * @brief Repeatedly issues ACMD41 with the given argument until the card
 *  reports ready (Busy bit, response bit 31, set to 1).
 *
 * Shared by every ACMD41 handshake this driver performs — the F8=0
 * path, the F8=1 path, and the S18R=0 retry after a failed voltage
 * switch are all this same loop with a different fixed argument.
 *
 * Bounded by the spec's 1-second wall-clock limit (Physical Layer spec
 * §4.2.3: "card initialization shall be completed within 1 second from
 * the first ACMD41").
 *
 * @param ocr_argument Argument to send with every ACMD41 in the loop —
 *  HCS/XPC/S18R/voltage window, see SDAppCommand::ACMD41 in sd.h.
 * @param response_out Receives the final (ready) response on success.
 *  Must not be null.
 * @return true once the card reports ready; false on a CRC/Index/Timeout
 *  error partway through, or if 1 second elapses without the card
 *  reporting ready.
 */
bool _sdio_acmd41_poll(uint32_t ocr_argument, uint32_t* response_out) {
 const uint64_t start_ticks = get_ticks();
 SDCommandStatus status;
 do {
  if (ticks_elapsed_ms(start_ticks, 1000)) return false; // Card is unusable.

  status = _send_command(SDAppCommand::ACMD41, ocr_argument, SDResponseType::R3, response_out);
  if (status == SDCommandStatus::CrcError || status == SDCommandStatus::IndexError ||
   status == SDCommandStatus::Timeout) {
   return false;
  }
 } while (((*response_out >> 31) & 1) == 0);

 return true;
}

bool _sdio_voltage_switch(const uint32_t s18a) {
 // Signal Voltage Switch is handled via a different procedure
 if (s18a == 0) return true;

 SDCommandStatus response = _send_command(SDCommand::CMD11,
  0x00000000, SDResponseType::R1, nullptr);

 // If an error occurs, we stop providing power to the card and
 // retry initialization with S18R = 0.
 if (response != SDCommandStatus::Success) return false;

 // Disable Clock. Clock Control and Host Control 2 are both 16-bit
 // registers — see the matching note in _sdio_clock_init().
 uint16_t init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value &= ~(1U);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 uint32_t dat_level = (mmio_read(PRESENT_STATE_SDIO0) >> 20) & 0xF;
 if (dat_level != 0b0000) return false;

 uint16_t signal_enable = mmio_read16(HOST_CONTROL_2_SDIO0);
 signal_enable &= ~(1 << 3);
 signal_enable |= (1 << 3);
 mmio_write16(HOST_CONTROL_2_SDIO0, signal_enable);

 // Per-spec, wait 5ms
 delay_ms(5);

 signal_enable = mmio_read16(HOST_CONTROL_2_SDIO0);

 // 1.8V signal enable has been cleared by the Host Controller
 // return failure.
 if (((signal_enable >> 3) & 1) != 1) return false;

 // Enable Clock
 init_value = mmio_read16(CLOCK_CONTROL_SDIO0);
 init_value |= (1U);
 mmio_write16(CLOCK_CONTROL_SDIO0, init_value);

 // Per-spec, wait 1ms.
 delay_ms(1);

 dat_level = (mmio_read(PRESENT_STATE_SDIO0) >> 20) & 0xF;

 if (dat_level != 0b1111) return false; // Voltage change failed

 return true; // Voltage change successful

}

bool sdio_init() {
 _sdio_clock_init();

 // Card Identification
 int F8 = 1;

 if (!_sdio_identify_stage1(&F8)) return false;

 // The CMD5 init path is only used to detect SDIO enabled cards before
 // entering the memory path. This driver is not meant to be compatible
 // with such cards, so we will skip this step.

 // Get OCR w/ Voltage Window = 0. Per the Physical Layer spec (p.45,
 // §4.2.3.1, rule (2)): "the inquiry ACMD41... shall ignore the other
 // field (bit 31-24)" when the voltage window is zero, so HCS doesn't
 // matter yet — this single inquiry call covers both the F8=0 and F8=1
 // cases below.
 uint32_t response = 0;
 const SDCommandStatus sd_response = _send_command(SDAppCommand::ACMD41,
  0x01000000, SDResponseType::R3, &response);

 // Check OCR
 if (sd_response == SDCommandStatus::Timeout) {
  return false; // Message not sent, not an SD Card.
 }

 if (sd_response == SDCommandStatus::CrcError || sd_response == SDCommandStatus::IndexError) {
  return false; // Card is unusable.
 }

 if (F8 == 0) {
  // Initialize with S18R=0 (legacy/pre-2.00 cards never offer 1.8V
  // switching). OCR = 0xFF8000 (2.7-3.6V).
  if (!_sdio_acmd41_poll(0x40FF8000, &response)) return false; // Card is unusable.

  uart_puts("[sdio_init]: The currently inserted SD card is of the SDSC Ver1.01 or Ver1.10 variant.\r\n");
  ccs = 0;
  return _sdio_finish_identification();
 }

 // === Begin F8=1 case ===

 // Initialize with HCS=1, S18R=1, and XPC. OCR = 0xFF8000 (2.7-3.6V).
 if (!_sdio_acmd41_poll(0x50FF8000, &response)) return false; // Card is unusable.

 // CCS (bit 30) and S18A (bit 24) are only valid once Busy (bit 31,
 // already confirmed by _sdio_acmd41_poll) is set to 1.
 if (((response >> 30) & 1) == 0) {
  ccs = 0;
  uart_puts("[sdio_init]: The currently inserted SD card is of the SDSC Ver2.00 or Ver3.00 variant.\r\n");

  return _sdio_finish_identification();
 }

 uart_puts("[sdio_init]: The currently inserted SD card is of the "
           "SDHC/SDXC variant. Undergoing a Signal Voltage Switch Procedure.\r\n");

 const uint32_t s18a = (response >> 24) & 1;

 if (!_sdio_voltage_switch(s18a)) {
  uart_puts("[sdio_init]: Voltage switch failed; retrying identification with S18R=0.\r\n");

  // Per Host Controller spec step (12) of the Signal Voltage Switch
  // Procedure: retry the ACMD41 handshake with S18R=0 (i.e. accept
  // 3.3V signaling) instead of failing the card outright.
  if (!_sdio_acmd41_poll(0x40FF8000, &response)) return false; // Card is unusable.

  // S18R was 0 this time, so the card never offers S18A=1 again — no
  // second voltage-switch attempt; proceed at the current signal level.
 }

 uart_puts("[sdio_init]: Signal Voltage Switch Procedure complete.\r\n");

 ccs = 1;
 return _sdio_finish_identification();
}

/**
 * @brief Logs which data-phase failure a failed sdio_read()/sdio_write()
 *  call hit.
 *
 * _send_data_command() already distinguishes bus-level failures via
 * SDCommandStatus; this turns that into something more useful than a bare
 * `false` on the UART log.
 *
 * @param who Short tag identifying the caller, e.g. "sdio_read".
 * @param status The non-Success status returned by _send_data_command().
 */
void _sdio_report_data_error(const char* who, SDCommandStatus status) {
 uart_puts("[");
 uart_puts(who);
 uart_puts("]: ");
 switch (status) {
  case SDCommandStatus::Timeout:
   uart_puts("Command timed out.\r\n");
   break;
  case SDCommandStatus::CrcError:
   uart_puts("Command CRC error.\r\n");
   break;
  case SDCommandStatus::IndexError:
   uart_puts("Command index error.\r\n");
   break;
  case SDCommandStatus::DataTimeoutError:
   uart_puts("Data timeout.\r\n");
   break;
  case SDCommandStatus::DataCrcError:
   uart_puts("Data CRC error.\r\n");
   break;
  case SDCommandStatus::DataEndBitError:
   uart_puts("Data end bit error.\r\n");
   break;
  default:
   uart_puts("Unexpected command status.\r\n");
   break;
 }
}

/**
 * @brief Inspects an R1 Card Status word (CMD17/18/24/25's response) for
 *  card-level rejections that a Success from _send_data_command() can't
 *  see — the bus transaction can complete cleanly while the card itself
 *  refuses it (address past the end of the card, write-protected media,
 *  an internal ECC failure, etc). See Physical Layer spec Table 4-42.
 *
 * @param who Short tag identifying the caller, e.g. "sdio_read".
 * @param status Card Status word (R1 response) to inspect.
 * @return true if none of the checked error bits are set; false (after
 *  logging which one fired) otherwise.
 */
bool _sdio_check_card_status(const char* who, uint32_t status) {
 const char* problem = nullptr;

 if (status & (1U << 31)) problem = "address out of range.";
 else if (status & (1U << 30)) problem = "misaligned address.";
 else if (status & (1U << 29)) problem = "block length error.";
 else if (status & (1U << 26)) problem = "write-protect violation.";
 else if (status & (1U << 21)) problem = "card internal ECC failure.";
 else if (status & (1U << 20)) problem = "internal card controller error.";
 else if (status & (1U << 19)) problem = "general/unknown card error.";

 if (problem == nullptr) return true;

 uart_puts("[");
 uart_puts(who);
 uart_puts("]: Card Status error: ");
 uart_puts(problem);
 uart_puts("\r\n");
 return false;
}

bool sdio_read(const uint32_t lba, const uint32_t count, uint8_t* buf) {
 uint32_t response = 0;
 const uint32_t adj_lba = (ccs == 0) ? (lba * 512) : lba;

 // Single block
 if (count == 1) {
  const SDCommandStatus status = _send_data_command(SDCommand::CMD17, adj_lba,
   SDResponseType::R1, count, true, buf, &response);
  if (status != SDCommandStatus::Success) {
   _sdio_report_data_error("sdio_read", status);
   return false;
  }
  return _sdio_check_card_status("sdio_read", response);
 }

 // Multiple blocks
 if (count > 1) {
  const SDCommandStatus status = _send_data_command(SDCommand::CMD18, adj_lba,
   SDResponseType::R1, count, true, buf, &response);
  if (status != SDCommandStatus::Success) {
   _sdio_report_data_error("sdio_read", status);
   return false;
  }
  return _sdio_check_card_status("sdio_read", response);
 }

 uart_puts("[sdio_read]: Read failed. Unexpected Parameters.\r\n");
 return false; // fell through (count == 0).
}

bool sdio_write(const uint32_t lba, const uint32_t count, const uint8_t* buf) {
 uint32_t response = 0;
 const uint32_t adj_lba = (ccs == 0) ? (lba * 512) : lba;
 // _send_data_command()'s buf is shared with the read path (which writes
 // into it), so it can't be declared const there — safe here because the
 // is_read=false branch only ever reads from buf, never writes to it.
 uint8_t* const rw_buf = const_cast<uint8_t*>(buf); // TODO: Clarify this.

 // Single block
 if (count == 1) {
  const SDCommandStatus status = _send_data_command(SDCommand::CMD24, adj_lba,
   SDResponseType::R1, count, false, rw_buf, &response);
  if (status != SDCommandStatus::Success) {
   _sdio_report_data_error("sdio_write", status);
   return false;
  }
  return _sdio_check_card_status("sdio_write", response);
 }

 // Multiple blocks
 if (count > 1) {
  const SDCommandStatus status = _send_data_command(SDCommand::CMD25, adj_lba,
   SDResponseType::R1, count, false, rw_buf, &response);
  if (status != SDCommandStatus::Success) {
   _sdio_report_data_error("sdio_write", status);
   return false;
  }
  return _sdio_check_card_status("sdio_write", response);
 }

 uart_puts("[sdio_write]: Write failed. Unexpected Parameters.\r\n");
 return false; // fell through (count == 0).
}
