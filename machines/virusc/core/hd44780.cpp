// license:BSD-3-Clause
// copyright-holders:Sandro Ronco (MAME hd44780 device), standalone port for the DX7 emulator
#include "hd44780.h"

#include <cstring>

namespace virusc {

void Hd44780::reset()
{
	std::memset(m_ddram, 0x20, sizeof(m_ddram)); // filled with spaces
	std::memset(m_cgram, 0, sizeof(m_cgram));
	m_ac = 0;
	m_dr = 0;
	m_ir = 0;
	m_activeRam = DDRAM;
	m_displayOn = m_cursorOn = m_blinkOn = m_shiftOn = false;
	m_direction = 1;
	m_dataLen = 8;
	m_numLine = 1;
	m_charSize = 8;
	m_dispShift = 0;
	m_nibble = false;
	m_firstCmd = true;
	m_rsState = m_rwState = 0;
	setBusy(410);
}

void Hd44780::advance(uint64_t clocks)
{
	m_now += clocks;
}

void Hd44780::correctAc()
{
	if (m_activeRam == DDRAM) {
		const int maxAc = m_numLine == 1 ? 0x4f : 0x67;
		if (m_ac > maxAc)
			m_ac -= maxAc + 1;
		else if (m_ac < 0)
			m_ac = maxAc;
		else if (m_numLine == 2 && m_ac > 0x27 && m_ac < 0x40)
			m_ac = 0x40 + (m_ac - 0x28);
	} else
		m_ac &= 0x3f;
}

void Hd44780::updateAc(int direction)
{
	if (m_activeRam == DDRAM && m_numLine == 2 && direction == -1 && m_ac == 0x40)
		m_ac = 0x27;
	else
		m_ac += direction;
	correctAc();
}

void Hd44780::shiftDisplay(int direction)
{
	m_dispShift += direction;
	if (m_dispShift == 0x50)
		m_dispShift = 0;
	else if (m_dispShift == -1)
		m_dispShift = 0x4f;
}

void Hd44780::updateNibble(int rs, int rw)
{
	if (m_rsState != rs || m_rwState != rw) {
		m_rsState = rs;
		m_rwState = rw;
		m_nibble = false;
	}
	m_nibble = !m_nibble;
}

void Hd44780::write(int offset, uint8_t data)
{
	if (m_dataLen == 4)
		updateNibble(offset & 1, 0);
	if (offset & 1)
		dataWrite(data);
	else
		controlWrite(data);
}

uint8_t Hd44780::read(int offset)
{
	if (m_dataLen == 4)
		updateNibble(offset & 1, 1);
	return (offset & 1) ? dataRead() : controlRead();
}

void Hd44780::controlWrite(uint8_t data)
{
	if (m_dataLen == 4) {
		if (m_nibble) {
			m_ir = data & 0xf0;
			return;
		}
		m_ir |= (data >> 4) & 0x0f;
	} else
		m_ir = data;

	if (m_ir & 0x80) { // set DDRAM address
		m_activeRam = DDRAM;
		m_ac = m_ir & 0x7f;
		correctAc();
		setBusy(10);
		return;
	}
	if (m_ir & 0x40) { // set CGRAM address
		m_activeRam = CGRAM;
		m_ac = m_ir & 0x3f;
		setBusy(10);
		return;
	}
	if (m_ir & 0x20) { // function set
		const bool eight = m_ir & 0x10, two = m_ir & 0x08;
		if (!m_firstCmd && m_dataLen == (eight ? 8 : 4) && (m_charSize != ((m_ir & 0x04) ? 10 : 8) || m_numLine != (two ? 2 : 1)))
			return; // only executable after other instructions when the data length changes
		m_firstCmd = true;
		m_charSize = two ? 8 : ((m_ir & 0x04) ? 10 : 8); // no 5x10 font in 2-line mode
		m_dataLen = eight ? 8 : 4;
		m_numLine = two ? 2 : 1;
		correctAc();
		setBusy(10);
		return;
	}
	if (m_ir & 0x10) { // cursor or display shift
		const int direction = (m_ir & 0x04) ? +1 : -1;
		if (m_ir & 0x08)
			shiftDisplay(direction);
		else
			updateAc(direction);
		setBusy(10);
	} else if (m_ir & 0x08) { // display on/off
		m_displayOn = m_ir & 0x04;
		m_cursorOn = m_ir & 0x02;
		m_blinkOn = m_ir & 0x01;
		setBusy(10);
	} else if (m_ir & 0x04) { // entry mode set
		m_direction = (m_ir & 0x02) ? +1 : -1;
		m_shiftOn = m_ir & 0x01;
		setBusy(10);
	} else if (m_ir & 0x02) { // return home
		m_ac = 0;
		m_activeRam = DDRAM;
		m_direction = 1;
		m_dispShift = 0;
		setBusy(410);
	} else if (m_ir & 0x01) { // clear display
		m_ac = 0;
		m_activeRam = DDRAM;
		m_direction = 1;
		m_dispShift = 0;
		std::memset(m_ddram, 0x20, sizeof(m_ddram));
		setBusy(410);
		return; // MAME: some machines clear first, before the function set
	}
	m_firstCmd = false;
}

uint8_t Hd44780::controlRead()
{
	if (m_dataLen == 4) {
		if (m_nibble)
			return uint8_t((busy() ? 0x80 : 0) | (m_ac & 0x70));
		return uint8_t((m_ac << 4) & 0xf0);
	}
	return uint8_t((busy() ? 0x80 : 0) | (m_ac & 0x7f));
}

void Hd44780::dataWrite(uint8_t data)
{
	if (m_dataLen == 4) {
		if (m_nibble) {
			m_dr = data & 0xf0;
			return;
		}
		m_dr |= (data >> 4) & 0x0f;
	} else
		m_dr = data;
	if (m_activeRam == DDRAM)
		m_ddram[m_ac] = m_dr;
	else
		m_cgram[m_ac] = m_dr;
	setBusy(10);
	updateAc(m_direction);
	if (m_shiftOn)
		shiftDisplay(m_direction);
}

uint8_t Hd44780::dataRead()
{
	uint8_t data = m_activeRam == DDRAM ? m_ddram[m_ac] : m_cgram[m_ac];
	if (m_dataLen == 4) {
		if (m_nibble)
			return data & 0xf0;
		data = uint8_t((data << 4) & 0xf0);
	}
	setBusy(10);
	updateAc(m_direction);
	return data;
}

void Hd44780::render(uint8_t out[kLines][kChars][8]) const
{
	std::memset(out, 0, sizeof(uint8_t) * kLines * kChars * 8);
	if (!m_displayOn)
		return;
	const int lineSize = 80 / m_numLine;
	for (int line = 0; line < m_numLine && line < kLines; ++line)
		for (int pos = 0; pos < kChars; ++pos) {
			const int at = line * 0x40 + (pos + m_dispShift) % lineSize;
			const uint8_t c = m_ddram[at];
			const uint8_t *src = c < 0x10 ? (m_charSize == 8 ? m_cgram + (c & 7) * 8 : m_cgram + ((c >> 1) & 3) * 16)
			                              : (m_cgrom ? m_cgrom + c * 16 : nullptr);
			uint8_t *dest = out[line][pos];
			if (src)
				for (int y = 0; y < 8; ++y)
					dest[y] = src[y] & 0x1f;
			if (at == m_ac && m_activeRam == DDRAM) {
				if (m_cursorOn)
					dest[7] = 0x1f;
				if (!blinkPhase() && m_blinkOn)
					std::memset(dest, 0x1f, 8);
			}
		}
}

void Hd44780::text(int line, char out[kChars + 1]) const
{
	const int lineSize = 80 / m_numLine;
	for (int pos = 0; pos < kChars; ++pos)
		out[pos] = char(line < m_numLine ? m_ddram[line * 0x40 + (pos + m_dispShift) % lineSize] : ' ');
	out[kChars] = 0;
}

void Hd44780::save(State &s) const
{
	std::memcpy(s.ddram, m_ddram, sizeof(m_ddram));
	std::memcpy(s.cgram, m_cgram, sizeof(m_cgram));
	s.now = m_now;
	s.busyUntil = m_busyUntil;
	s.ac = m_ac;
	s.dispShift = m_dispShift;
	s.direction = m_direction;
	s.dr = m_dr;
	s.ir = m_ir;
	s.activeRam = m_activeRam;
	s.displayOn = m_displayOn;
	s.cursorOn = m_cursorOn;
	s.blinkOn = m_blinkOn;
	s.shiftOn = m_shiftOn;
	s.dataLen = m_dataLen;
	s.numLine = m_numLine;
	s.charSize = m_charSize;
	s.blink = 0;
	s.firstCmd = m_firstCmd;
	s.nibble = m_nibble;
	s.rsState = uint8_t(m_rsState);
	s.rwState = uint8_t(m_rwState);
}

void Hd44780::load(const State &s)
{
	std::memcpy(m_ddram, s.ddram, sizeof(m_ddram));
	std::memcpy(m_cgram, s.cgram, sizeof(m_cgram));
	m_now = s.now;
	m_busyUntil = s.busyUntil;
	m_ac = s.ac;
	m_dispShift = s.dispShift;
	m_direction = s.direction;
	m_dr = s.dr;
	m_ir = s.ir;
	m_activeRam = s.activeRam;
	m_displayOn = s.displayOn;
	m_cursorOn = s.cursorOn;
	m_blinkOn = s.blinkOn;
	m_shiftOn = s.shiftOn;
	m_dataLen = s.dataLen;
	m_numLine = s.numLine;
	m_charSize = s.charSize;
	m_firstCmd = s.firstCmd;
	m_nibble = s.nibble;
	m_rsState = s.rsState;
	m_rwState = s.rwState;
}

} // namespace virusc
