/**
 * @file ptrlist.h
 * @brief Compatibility shim for Circle's circle/ptrlist.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Unlike most of this shim layer, CPtrList itself has no framework
 * coupling — it's a generic intrusive singly-linked list, same shape as
 * CNumberPool (which this port vendors verbatim instead of shimming).
 * Reimplemented directly here rather than vendored only because real
 * Circle keeps `TPtrListElement` opaque (defined in ptrlist.cpp, not the
 * header) — inlining everything into one header is simpler than carrying
 * a second vendored file for this little logic. Covers the full method
 * surface actually called anywhere in the vendored tree (checked via grep
 * before writing this: GetFirst/GetNext/GetPtr/InsertAfter/Remove, all
 * from `usbhostcontroller.cpp`'s m_HubList) — real CPtrList also has
 * InsertBefore and Find, neither used here, so neither is implemented.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_ptrlist_h
#define _circle_ptrlist_h

struct TPtrListElement
{
	void            *pPtr;
	TPtrListElement *pNext;
};

class CPtrList
{
public:
	CPtrList (void) : m_pFirst (nullptr) {}

	~CPtrList (void)
	{
		while (m_pFirst)
		{
			TPtrListElement *pNext = m_pFirst->pNext;
			delete m_pFirst;
			m_pFirst = pNext;
		}
	}

	TPtrListElement *GetFirst (void) const
	{
		return m_pFirst;
	}

	TPtrListElement *GetNext (TPtrListElement *pElement) const
	{
		return pElement->pNext;
	}

	static void *GetPtr (TPtrListElement *pElement)
	{
		return pElement->pPtr;
	}

	void InsertAfter (TPtrListElement *pBefore, void *pPtr)
	{
		TPtrListElement *pNew = new TPtrListElement;
		pNew->pPtr = pPtr;

		if (pBefore == nullptr)
		{
			pNew->pNext = m_pFirst;
			m_pFirst = pNew;
		}
		else
		{
			pNew->pNext = pBefore->pNext;
			pBefore->pNext = pNew;
		}
	}

	void Remove (TPtrListElement *pElement)
	{
		if (m_pFirst == pElement)
		{
			m_pFirst = pElement->pNext;
		}
		else
		{
			TPtrListElement *pPrev = m_pFirst;
			while (pPrev != nullptr && pPrev->pNext != pElement)
			{
				pPrev = pPrev->pNext;
			}
			if (pPrev != nullptr)
			{
				pPrev->pNext = pElement->pNext;
			}
		}

		delete pElement;
	}

private:
	TPtrListElement *m_pFirst;
};

#endif
