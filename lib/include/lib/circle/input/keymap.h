/**
 * @file keymap.h
 * @brief Compatibility shim for Circle's circle/input/keymap.h.
 *
 * Part of raspberry-pi-gaming-console, a retro gaming console OS.
 *
 * Real Circle CKeyMap is a multi-locale system: five modifier-combination
 * tables per physical key (normal/shift/alt/altshift/ctrl), a directory of
 * built-in default maps selected by locale string, and runtime table
 * editing (`ClearTable`/`SetEntry`). Nothing outside this port's own
 * `keyboardbehaviour.h` ever touches `CKeyMap` directly (checked via grep
 * before writing this — the vendored USB tree only calls
 * `CKeyboardBehaviour`'s methods), so this is a from-scratch minimal
 * design, not a port: one hardcoded US QWERTY layout, two tables
 * (normal/shift only — no separate Alt/AltGr symbol table, no locale
 * selection), and no runtime table editing.
 *
 * Usage-ID -> character mapping follows the USB HID Usage Tables' fixed
 * "Keyboard/Keypad Page" (0x07) assignment (0x04='a' ... 0x1D='z',
 * 0x1E='1' ... 0x27='0', etc.) — a standardized table, not something
 * chosen per-port.
 *
 * @author Kehinde Adeoso
 * @copyright 2026 Kehinde Adeoso. SPDX-License-Identifier: GPL-3.0-only
 */

#ifndef _circle_input_keymap_h
#define _circle_input_keymap_h

#include <circle/types.h>

#define PHY_MAX_CODE 127

// Modifier bitmasks, matching TModifierKey's bit order in
// keyboardbehaviour.h (ModifierKeyLeftCtrl=bit 0 ... ModifierKeyRightWin=
// bit 7) — defined here rather than there, since Translate() below needs
// them and this file is included first (matching real Circle's own
// keyboardbehaviour.h include order: keymap.h before its own enum/defines).
#define KEY_LCTRL_MASK   (1 << 0)
#define KEY_LSHIFT_MASK  (1 << 1)
#define KEY_ALT_MASK     (1 << 2)
#define KEY_LWIN_MASK    (1 << 3)
#define KEY_RCTRL_MASK   (1 << 4)
#define KEY_RSHIFT_MASK  (1 << 5)
#define KEY_ALTGR_MASK   (1 << 6)
#define KEY_RWIN_MASK    (1 << 7)

enum TSpecialKey
{
	KeyNone  = 0x00,
	KeySpace = 0x100,
	KeyEscape,
	KeyBackspace,
	KeyTabulator,
	KeyReturn,
	KeyInsert,
	KeyHome,
	KeyPageUp,
	KeyDelete,
	KeyEnd,
	KeyPageDown,
	KeyUp,
	KeyDown,
	KeyLeft,
	KeyRight,
	KeyMaxCode
};

class CKeyMap
{
public:
	CKeyMap (void)
	{
		for (unsigned i = 0; i <= PHY_MAX_CODE; i++)
		{
			m_NormTab[i] = KeyNone;
			m_ShiftTab[i] = KeyNone;
		}

		// Letters: usage 0x04-0x1D = 'a'-'z' / 'A'-'Z'.
		for (u8 i = 0; i < 26; i++)
		{
			m_NormTab[0x04 + i] = (u16) ('a' + i);
			m_ShiftTab[0x04 + i] = (u16) ('A' + i);
		}

		// Digits: usage 0x1E-0x26 = '1'-'9', 0x27 = '0' (not after '9').
		static const char ShiftedDigits[] = "!@#$%^&*(";
		for (u8 i = 0; i < 9; i++)
		{
			m_NormTab[0x1E + i] = (u16) ('1' + i);
			m_ShiftTab[0x1E + i] = (u16) ShiftedDigits[i];
		}
		m_NormTab[0x27] = '0';
		m_ShiftTab[0x27] = ')';

		SetBoth (0x28, KeyReturn);
		SetBoth (0x29, KeyEscape);
		SetBoth (0x2A, KeyBackspace);
		SetBoth (0x2B, KeyTabulator);
		SetBoth (0x2C, ' ');

		Set (0x2D, '-', '_');
		Set (0x2E, '=', '+');
		Set (0x2F, '[', '{');
		Set (0x30, ']', '}');
		Set (0x31, '\\', '|');
		Set (0x33, ';', ':');
		Set (0x34, '\'', '"');
		Set (0x35, '`', '~');
		Set (0x36, ',', '<');
		Set (0x37, '.', '>');
		Set (0x38, '/', '?');

		SetBoth (0x49, KeyInsert);
		SetBoth (0x4A, KeyHome);
		SetBoth (0x4B, KeyPageUp);
		SetBoth (0x4C, KeyDelete);
		SetBoth (0x4D, KeyEnd);
		SetBoth (0x4E, KeyPageDown);
		SetBoth (0x4F, KeyRight);
		SetBoth (0x50, KeyLeft);
		SetBoth (0x51, KeyDown);
		SetBoth (0x52, KeyUp);
	}

	~CKeyMap (void) {}

	/// Shift-only lookup — no CapsLock shift-inversion for letters, no
	/// Alt/AltGr table, no Ctrl table (see file comment on scope cuts).
	u16 Translate (u8 nPhyCode, u8 nModifiers) const
	{
		if (nPhyCode > PHY_MAX_CODE)
		{
			return KeyNone;
		}

		const boolean bShift = (nModifiers & (KEY_LSHIFT_MASK | KEY_RSHIFT_MASK)) != 0;
		return bShift ? m_ShiftTab[nPhyCode] : m_NormTab[nPhyCode];
	}

	/// Plain ASCII goes through Buffer; special keys return a pointer to
	/// a static string (an ANSI escape sequence for the ones that have an
	/// obvious one) — a best guess pending actual integration with the
	/// shell's `read_line`, not yet reconciled against what it expects.
	const char *GetString (u16 nKeyCode, u8 nModifiers, char Buffer[2]) const
	{
		(void) nModifiers;

		if (nKeyCode == KeyNone)
		{
			return nullptr;
		}

		if (nKeyCode < KeySpace)
		{
			Buffer[0] = (char) nKeyCode;
			Buffer[1] = '\0';
			return Buffer;
		}

		switch (nKeyCode)
		{
		case KeyReturn:    return "\r";
		case KeyBackspace: return "\b";
		case KeyTabulator: return "\t";
		case KeyEscape:    return "\x1b";
		case KeyUp:        return "\x1b[A";
		case KeyDown:      return "\x1b[B";
		case KeyRight:     return "\x1b[C";
		case KeyLeft:      return "\x1b[D";
		case KeyHome:      return "\x1b[H";
		case KeyEnd:       return "\x1b[F";
		case KeyInsert:    return "\x1b[2~";
		case KeyDelete:    return "\x1b[3~";
		case KeyPageUp:    return "\x1b[5~";
		case KeyPageDown:  return "\x1b[6~";
		default:           return nullptr;
		}
	}

private:
	void SetBoth (u8 nPhyCode, u16 nValue)
	{
		m_NormTab[nPhyCode] = nValue;
		m_ShiftTab[nPhyCode] = nValue;
	}

	void Set (u8 nPhyCode, char chNorm, char chShift)
	{
		m_NormTab[nPhyCode] = (u16) chNorm;
		m_ShiftTab[nPhyCode] = (u16) chShift;
	}

private:
	u16 m_NormTab[PHY_MAX_CODE + 1];
	u16 m_ShiftTab[PHY_MAX_CODE + 1];
};

#endif
