/**
 * @file logger.h
 * @brief Compatibility shim for Circle's circle/logger.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CLogger is a target-device-backed ring-buffer logger with
 * event notification/panic-handler hooks and its own CSpinLock-guarded
 * queue. None of that machinery is exercised anywhere in the vendored USB
 * tree (checked via grep before writing this) — every call site is either
 * `CLogger::Get()->Write(source, severity, format, ...)` or `->WriteV(...)`
 * with the va_list already split out. This shim skips straight to this
 * kernel's own unified `printf` (already fans out to UART + screen, see
 * `kernel/io.h`) instead of replicating a ring buffer + target-device
 * abstraction nothing here reads back from.
 *
 * `LogWrite(severity, format, ...)` (used a couple of places in
 * usbdevice.cpp with no source argument) isn't part of Circle's current
 * logger.h at all — checked directly, it's not there. Rather than guess at
 * a stale/removed upstream API, this shim just defines it as a thin
 * wrapper over Write() with a fixed "usb" source tag, which matches the
 * call sites' actual intent.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_logger_h
#define _circle_logger_h

#include <circle/types.h>

#include <stdarg.h>
#include <stdio.h>

enum TLogSeverity
{
	LogPanic,
	LogError,
	LogWarning,
	LogNotice,
	LogDebug
};

class CLogger
{
public:
	static CLogger *Get (void)
	{
		static CLogger s_Instance;
		return &s_Instance;
	}

	void Write (const char *pSource, TLogSeverity Severity, const char *pMessage, ...)
	{
		va_list Args;
		va_start (Args, pMessage);
		WriteV (pSource, Severity, pMessage, Args);
		va_end (Args);
	}

	void WriteV (const char *pSource, TLogSeverity Severity, const char *pMessage, va_list Args)
	{
		printf ("[%s] %s: ", SeverityTag (Severity), pSource ? pSource : "?");
		vprintf (pMessage, Args);
		printf ("\r\n");
	}

private:
	static const char *SeverityTag (TLogSeverity Severity)
	{
		switch (Severity)
		{
		case LogPanic:   return "PANIC";
		case LogError:   return "ERROR";
		case LogWarning: return "WARN";
		case LogNotice:  return "NOTE";
		case LogDebug:   return "DEBUG";
		default:         return "?";
		}
	}
};

inline void LogWrite (TLogSeverity Severity, const char *pMessage, ...)
{
	va_list Args;
	va_start (Args, pMessage);
	CLogger::Get ()->WriteV ("usb", Severity, pMessage, Args);
	va_end (Args);
}

#endif
