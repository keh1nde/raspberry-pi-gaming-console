/**
 * @file string.h
 * @brief Compatibility shim for Circle's circle/string.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CString hand-rolls its own number/float formatting (ntoa/
 * lltoa/ftoa) because a from-scratch bare-metal build can't assume a real
 * libc is present. This kernel already has newlib linked and a working
 * `vsnprintf` (proven throughout the printf/stdio migration — see
 * `printf-stdio-migration` project history), so this shim just grows a
 * heap buffer (via the now-available `operator new`/`malloc`) and formats
 * into it with the real, full `vsnprintf` — strictly more capable than
 * Circle's own "small subset of printf(3)", and far less code. Covers
 * exactly the method surface actually called anywhere in the vendored USB
 * tree (checked via grep before writing this): constructors, the
 * `const char*` conversion operator, `operator=(const char*)`, GetLength,
 * Append, Compare, Format. Real CString also has move semantics, Find,
 * TrimRight, Replace, FormatV, and char-append overloads — none of it
 * exercised here, so none of it is replicated.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_string_h
#define _circle_string_h

#include <circle/types.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

class CString
{
public:
	CString (void) : m_pBuffer (nullptr), m_nSize (0)
	{
		EnsureCapacity (1);
		m_pBuffer[0] = '\0';
	}

	CString (const char *pString) : m_pBuffer (nullptr), m_nSize (0)
	{
		*this = pString;
	}

	CString (const CString &rString) : m_pBuffer (nullptr), m_nSize (0)
	{
		*this = (const char *) rString;
	}

	~CString (void)
	{
		free (m_pBuffer);
	}

	operator const char *(void) const
	{
		return m_pBuffer ? m_pBuffer : "";
	}

	const char *c_str (void) const
	{
		return m_pBuffer ? m_pBuffer : "";
	}

	const char *operator = (const char *pString)
	{
		size_t nLen = pString ? strlen (pString) + 1 : 1;
		EnsureCapacity (nLen);
		if (pString)
		{
			memcpy (m_pBuffer, pString, nLen);
		}
		else
		{
			m_pBuffer[0] = '\0';
		}
		return m_pBuffer;
	}

	size_t GetLength (void) const
	{
		return m_pBuffer ? strlen (m_pBuffer) : 0;
	}

	void Append (const char *pString)
	{
		if (!pString)
		{
			return;
		}
		size_t nCurLen = GetLength ();
		size_t nAddLen = strlen (pString);
		EnsureCapacity (nCurLen + nAddLen + 1);
		memcpy (m_pBuffer + nCurLen, pString, nAddLen + 1);
	}

	int Compare (const char *pString) const
	{
		return strcmp (m_pBuffer ? m_pBuffer : "", pString);
	}

	void Format (const char *pFormat, ...)
	{
		va_list Args;
		va_start (Args, pFormat);

		va_list Args2;
		va_copy (Args2, Args);
		int nNeeded = vsnprintf (nullptr, 0, pFormat, Args2);
		va_end (Args2);

		if (nNeeded >= 0)
		{
			EnsureCapacity ((size_t) nNeeded + 1);
			vsnprintf (m_pBuffer, (size_t) nNeeded + 1, pFormat, Args);
		}

		va_end (Args);
	}

private:
	void EnsureCapacity (size_t nSize)
	{
		if (nSize > m_nSize)
		{
			m_pBuffer = (char *) realloc (m_pBuffer, nSize);
			m_nSize = nSize;
		}
	}

	char   *m_pBuffer;
	size_t  m_nSize;
};

#endif
