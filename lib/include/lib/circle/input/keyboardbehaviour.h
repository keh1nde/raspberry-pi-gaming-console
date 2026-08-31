/**
 * @file keyboardbehaviour.h
 * @brief Compatibility shim for Circle's circle/input/keyboardbehaviour.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * From-scratch minimal design (not a port — nothing outside `usbkeyboard.h`/
 * `.cpp` touches this class, checked via grep before writing this), scoped
 * down from real Circle's `CKeyboardBehaviour` in three ways, agreed before
 * writing any of this:
 *
 *  - No auto-repeat. Real Circle drives repeat through a periodic timer
 *    callback (`TKernelTimerHandle`/`CTimer::StartKernelTimer`) — this
 *    port's `CTimer` shim only wraps `MsDelay`/`GetTicks`, no callback
 *    machinery. One physical press = one key event; revisit if it turns
 *    out to matter (backspace-and-hold being the obvious case).
 *  - `RegisterSelectConsoleHandler`/`RegisterShutdownHandler` are accepted
 *    (matching the interface `usbkeyboard.cpp` calls) but the handlers are
 *    never invoked — no multi-console concept or shutdown semantics exist
 *    in this kernel.
 *  - CapsLock/NumLock/ScrollLock only toggle the LED-status bits
 *    `GetLEDStatus()` reports (so the physical LEDs light up correctly);
 *    CapsLock does not invert Shift for letters the way a real keyboard
 *    does. `CKeyMap::Translate` has no lock-state input at all — it's a
 *    pure function of (physical code, modifiers).
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_input_keyboardbehaviour_h
#define _circle_input_keyboardbehaviour_h

#include <circle/input/keymap.h>
#include <circle/types.h>

enum TModifierKey
{
	ModifierKeyLeftCtrl = 0x80,
	ModifierKeyLeftShift,
	ModifierKeyAlt,
	ModifierKeyLeftWin,
	ModifierKeyRightCtrl,
	ModifierKeyRightShift,
	ModifierKeyAltGr,
	ModifierKeyRightWin,
	ModifierKeyUnknown
};

#define KEYB_LED_NUM_LOCK    (1 << 0)
#define KEYB_LED_CAPS_LOCK   (1 << 1)
#define KEYB_LED_SCROLL_LOCK (1 << 2)

typedef void TKeyPressedHandler (const char *pString);
typedef void TSelectConsoleHandler (unsigned nConsole);
typedef void TShutdownHandler (void);

class CKeyboardBehaviour
{
public:
	CKeyboardBehaviour (void)
	:	m_pKeyPressedHandler (nullptr),
		m_pSelectConsoleHandler (nullptr),
		m_pShutdownHandler (nullptr),
		m_ucModifiers (0),
		m_ucLockStatus (0)
	{
	}

	~CKeyboardBehaviour (void) {}

	void RegisterKeyPressedHandler (TKeyPressedHandler *pKeyPressedHandler)
	{
		m_pKeyPressedHandler = pKeyPressedHandler;
	}

	// Stored, never invoked — see file comment.
	void RegisterSelectConsoleHandler (TSelectConsoleHandler *pSelectConsoleHandler)
	{
		m_pSelectConsoleHandler = pSelectConsoleHandler;
	}

	void RegisterShutdownHandler (TShutdownHandler *pShutdownHandler)
	{
		m_pShutdownHandler = pShutdownHandler;
	}

	void KeyPressed (u8 ucKeyCode)
	{
		if (ucKeyCode >= 0x80 && ucKeyCode <= ModifierKeyRightWin)
		{
			m_ucModifiers |= (u8) (1 << (ucKeyCode - 0x80));
			return;
		}

		// Lock keys: LED toggle only, no shift-inversion — see file
		// comment. Usage IDs per the USB HID Keyboard/Keypad page.
		switch (ucKeyCode)
		{
		case 0x39: m_ucLockStatus ^= KEYB_LED_CAPS_LOCK;   return;
		case 0x53: m_ucLockStatus ^= KEYB_LED_NUM_LOCK;    return;
		case 0x47: m_ucLockStatus ^= KEYB_LED_SCROLL_LOCK; return;
		default: break;
		}

		GenerateKeyEvent (ucKeyCode);
	}

	void KeyReleased (u8 ucKeyCode)
	{
		if (ucKeyCode >= 0x80 && ucKeyCode <= ModifierKeyRightWin)
		{
			m_ucModifiers &= (u8) ~(1 << (ucKeyCode - 0x80));
		}
		// No repeat state to cancel — see file comment.
	}

	u8 GetLEDStatus (void) const
	{
		return m_ucLockStatus;
	}

private:
	void GenerateKeyEvent (u8 ucKeyCode)
	{
		if (m_pKeyPressedHandler == nullptr)
		{
			return;
		}

		const u16 nKeyCode = m_KeyMap.Translate (ucKeyCode, m_ucModifiers);
		if (nKeyCode == KeyNone)
		{
			return;
		}

		char Buffer[2];
		const char *pString = m_KeyMap.GetString (nKeyCode, m_ucModifiers, Buffer);
		if (pString != nullptr)
		{
			m_pKeyPressedHandler (pString);
		}
	}

private:
	TKeyPressedHandler    *m_pKeyPressedHandler;
	TSelectConsoleHandler *m_pSelectConsoleHandler;
	TShutdownHandler      *m_pShutdownHandler;

	u8 m_ucModifiers;
	u8 m_ucLockStatus;

	CKeyMap m_KeyMap;
};

#endif
