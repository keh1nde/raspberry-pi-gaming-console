/**
 * @file diskio.c
 * @brief FatFs low-level disk I/O port — the glue FatFs calls into.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * FatFs's own porting doc ("How to Port", elm-chan.org/fsw/ff/doc/appnote.html)
 * specifies these five functions and their exact return-code contract
 * (RES_OK/RES_ERROR/RES_WRPRT/RES_NOTRDY/RES_PARERR); their signatures are
 * declared in `lib/include/lib/fatfs/diskio.h`, part of the vendored FatFs
 * distribution and not to be edited. Only one physical drive exists on this
 * board (the microSD card FatFs mounts, FF_VOLUMES == 1), so pdrv is always
 * 0 — no multi-device dispatch like FatFs's own diskio.c example ships
 * with. Not vendored FatFs source itself, hence living under src/kernel/
 * (kernel-specific glue, like libc_retarget.cpp) rather than lib/.
 *
 * Stub bodies only for now: the SD driver these will call into doesn't
 * exist yet. Each function fails safe (STA_NOINIT / RES_NOTRDY) until
 * wired up one at a time.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#include "lib/fatfs/ff.h"
#include "lib/fatfs/diskio.h"
#include "sd/sd.h"

#define DEV_SD 0

static DSTATUS init_status = STA_NOINIT;

DSTATUS disk_status(BYTE pdrv) {
	if (pdrv != DEV_SD) return STA_NOINIT;

	// Only card removal should force re-initialization. The previous
	// `else if (card_inserted == 1) init_status = STA_NOINIT;` branch
	// unconditionally clobbered a successful disk_initialize() back to
	// STA_NOINIT on every single status check as long as a card was
	// present — which is effectively always, once mounted — forcing
	// FatFs to silently re-run sdio_init() from inside f_open()/f_write(),
	// reassigning a fresh RCA and reselecting the card mid-operation.
	// Found on real hardware 2026-08-10: the SD self-test's f_write()
	// failed immediately after a second, spurious full re-init sequence
	// appeared in the boot log right after a successful mount.
	if (card_inserted == 0)
		init_status = STA_NOINIT | STA_NODISK;

	return init_status;
}

DSTATUS disk_initialize(BYTE pdrv) {
	if (pdrv != DEV_SD) return STA_NOINIT;

	const bool init_ok = sdio_init();

	if (init_ok)
		init_status = 0;

	else if (card_inserted == 0)
		init_status = STA_NOINIT | STA_NODISK;

	else
		init_status = STA_NOINIT;

	return init_status;
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != DEV_SD) return RES_PARERR;
	if (!sdio_read(sector, count, buff)) return RES_ERROR;
	return RES_OK;
}

#if FF_FS_READONLY == 0
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
	if (pdrv != DEV_SD) return RES_PARERR;
	if (!sdio_write(sector, count, buff)) return RES_ERROR;
	return RES_OK;
}
#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
	if (pdrv != DEV_SD) return RES_PARERR;

	// Only CTRL_SYNC is handled here.
	if (cmd != CTRL_SYNC) return RES_PARERR;
	return RES_OK; // Writes completed in disk_write.
}