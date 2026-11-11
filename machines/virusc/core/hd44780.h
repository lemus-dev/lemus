// license:BSD-3-Clause
// copyright-holders:Sandro Ronco (MAME hd44780 device), standalone port from the DX7 machine
//
// Hitachi HD44780 LCD controller, as the Virus C's 2x16 LCD uses it: 8-bit or 4-bit bus, DDRAM,
// CGRAM, the busy flag and the cursor blink. Ported from MAME's src/devices/video/hd44780.cpp with
// the device framework replaced by a clock the board advances; behaviour unchanged (MAME's busy
// timings, its SED1278-style address counter, no 5x10 font in 2-line mode).
#pragma once

#include <cstdint>

namespace virusc {

class Hd44780 {
public:
	static constexpr int kLines = 2, kChars = 16;

	// The character ROM (hd44780_a00.bin, 4096 bytes: 16 bytes a character, rows in bits 4-0).
	void setFont(const uint8_t *rom) { m_cgrom = rom; }
	void reset();
	// Advances the controller's own clock (about 270 kHz) by `clocks`.
	void advance(uint64_t clocks);

	// The bus: offset 0 the instruction register (RS low), 1 data (RS high).
	void write(int offset, uint8_t data);
	uint8_t read(int offset);

	// The display: per line and character, 8 rows of 5 dots (bit 4 leftmost), cursor and blink
	// included; all zero while the display is off.
	void render(uint8_t out[kLines][kChars][8]) const;
	// Text of a line as character codes (DDRAM, shift applied): for tools and tests.
	void text(int line, char out[kChars + 1]) const;
	bool displayOn() const { return m_displayOn; }

	struct State {
		uint8_t ddram[0x80], cgram[0x40];
		uint64_t now, busyUntil;
		int32_t ac, dispShift, direction;
		uint8_t dr, ir, activeRam, displayOn, cursorOn, blinkOn, shiftOn, dataLen, numLine, charSize,
			blink, firstCmd, nibble, rsState, rwState;
	};
	void save(State &s) const;
	void load(const State &s);

private:
	enum { DDRAM, CGRAM };
	void setBusy(int clocks) { m_busyUntil = m_now + uint64_t(clocks); }
	bool busy() const { return m_now < m_busyUntil; }
	void correctAc();
	void updateAc(int direction);
	void updateNibble(int rs, int rw);
	void shiftDisplay(int direction);
	void controlWrite(uint8_t data);
	uint8_t controlRead();
	void dataWrite(uint8_t data);
	uint8_t dataRead();
	bool blinkPhase() const { return (m_now / 102400) & 1; } // the blink inverts every 102400 clocks

	const uint8_t *m_cgrom = nullptr;
	uint64_t m_now = 0, m_busyUntil = 0;
	uint8_t m_ddram[0x80]{}, m_cgram[0x40]{};
	int m_ac = 0;
	uint8_t m_dr = 0, m_ir = 0;
	uint8_t m_activeRam = DDRAM;
	bool m_displayOn = false, m_cursorOn = false, m_blinkOn = false, m_shiftOn = false;
	int m_dispShift = 0, m_direction = 1;
	uint8_t m_dataLen = 8, m_numLine = 1, m_charSize = 8;
	bool m_firstCmd = true, m_nibble = false;
	int m_rsState = 0, m_rwState = 0;
};

} // namespace virusc
