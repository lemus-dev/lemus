#include "board.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace qvgt {

namespace {
uint8_t bitReverse8(uint8_t v)
{
	uint8_t r = 0;
	for (int i = 0; i < 8; ++i)
		r = uint8_t(r | (((v >> i) & 1) << (7 - i)));
	return r;
}

// Descrambled: LJMP 00E0h at reset, and the GT's banner.
bool looksDescrambled(const std::vector<uint8_t> &r)
{
	if (!(r[0] == 0x02 && r[1] == 0x00 && r[2] == 0xe0))
		return false;
	static const char kBanner[] = "QUADRAVERB  GT";
	return std::search(r.begin(), r.end(), kBanner, kBanner + std::strlen(kBanner)) != r.end();
}

constexpr int kBitCycles = 32; // 1 MHz machine cycles / 31,250 baud
} // namespace

void Hd44780::write(bool rs, uint8_t v)
{
	if (rs) {
		if (cgMode)
			cgram[addr & 0x3f] = v;
		else
			ddram[addr & 0x7f] = v;
		addr += incr ? 1 : -1;
		return;
	}
	if (v & 0x80) {
		cgMode = false;
		addr = v & 0x7f;
	} else if (v & 0x40) {
		cgMode = true;
		addr = v & 0x3f;
	} else if (v & 0x20) {
		// function set
	} else if (v & 0x10) {
		// cursor or display shift
	} else if (v & 0x08)
		displayOn = (v & 0x04) != 0;
	else if (v & 0x04)
		incr = (v & 0x02) != 0;
	else if (v & 0x02) {
		addr = 0;
		cgMode = false;
	} else if (v & 0x01) {
		std::fill(std::begin(ddram), std::end(ddram), uint8_t(' '));
		addr = 0;
		cgMode = false;
		incr = true;
	}
}

void Hd44780::line(int n, uint8_t out[16]) const
{
	std::memcpy(out, ddram + (n ? 0x40 : 0), 16);
}

std::string Hd44780::text(int n) const
{
	std::string s;
	for (int i = 0; i < 16; ++i) {
		const uint8_t c = ddram[(n ? 0x40 : 0) + i];
		s += (c >= 0x20 && c <= 0x7f) ? char(c) : (c < 8 ? '#' : '?');
	}
	return s;
}

bool Board::prepareRom(std::vector<uint8_t> &rom)
{
	if (rom.size() != kRomSize)
		return false;
	if (looksDescrambled(rom))
		return true;
	std::vector<uint8_t> d(kRomSize);
	for (size_t a = 0; a < kRomSize; ++a)
		d[a] = rom[(a & 0xff00) | bitReverse8(uint8_t(a))];
	if (!looksDescrambled(d))
		return false;
	rom.swap(d);
	return true;
}

Board::Board(std::vector<uint8_t> descrambledRom) : m_rom(std::move(descrambledRom))
{
	m_rom.resize(kRomSize, 0xff);
	m_cpu.setBus(this);
	m_cpu.setCode(m_rom.data());
	m_latch.fill(0xff);
	powerOn();
}

void Board::powerOn()
{
	m_cpu.reset();
	m_dsp.reset();
	m_lcd = Hd44780{};
	m_latch.fill(0xff);
	m_dasp.fill(0);
	m_daspActive.fill(0);
	m_daspCommit = false;
	m_midiIn.clear();
	m_rxBits = 0;
	m_midiOut.clear();
	m_envelope = m_capVolts = 0;
	m_piezo = 0;
	m_cycleTarget = 0;
}

uint8_t Board::xread(uint16_t addr)
{
	if (addr & 0x8000)
		return m_sram[addr & 0x7fff];
	return 0xff;
}

void Board::xwrite(uint16_t addr, uint8_t data)
{
	if (addr & 0x8000) {
		m_sram[addr & 0x7fff] = data;
		return;
	}
	const int y = (addr >> 9) & 7;
	switch (y) {
	case 0:
		m_dasp[addr & 0x1ff] = data;
		++m_daspWrites;
		if ((addr & 0x1ff) == 0x17f)
			m_daspCommit = true;
		break;
	case 1: m_lcd.write(addr & 1, data); break;
	default: m_latch[size_t(y)] = data; break;
	}
}

uint8_t Board::portIn(int port)
{
	if (port == 1) {
		const uint8_t drive = m_cpu.p(1);
		uint8_t rows = 0x0f;
		static const int kColumnBit[4] = {6, 7, 4, 5};
		for (int k = 0; k < 16; ++k)
			if (m_keys[size_t(k)] && !(drive & (1 << kColumnBit[k >> 2])))
				rows = uint8_t(rows & ~(1 << (k & 3)));
		return uint8_t(0xf0 | rows);
	}
	if (port == 3) {
		uint8_t v = uint8_t(m_p3In & ~0x0c);
		// P3.2: the ramp (C108) above the input's envelope; P3.3: the key piezo above the ramp
		if (m_capVolts > m_envelope)
			v |= 0x04;
		if (m_piezo > m_capVolts)
			v |= 0x08;
		// P3.0, RxD: the serial line
		v = uint8_t((v & ~0x01) | (rxd() ? 1 : 0));
		return v;
	}
	return 0xff;
}

void Board::setFootswitch(int which, bool down)
{
	const int bit = which == 0 ? 4 : 5;
	m_p3In = uint8_t(down ? m_p3In & ~(1 << bit) : m_p3In | (1 << bit));
}

bool Board::rxd()
{
	const uint64_t now = m_cpu.cycles();
	if (m_rxBits == 0) {
		if (m_midiIn.empty())
			return true;
		m_rxByte = m_midiIn.front();
		m_midiIn.pop_front();
		m_rxStart = now;
		m_rxBits = 10;
	}
	const int bit = int((now - m_rxStart) / kBitCycles);
	if (bit >= 10) {
		m_rxBits = 0;
		return rxd();
	}
	if (bit == 0)
		return false;                        // start bit
	if (bit == 9)
		return true;                         // stop bit
	return ((m_rxByte >> (bit - 1)) & 1) != 0; // data, LSB first
}

bool Board::rxIdle()
{
	return m_rxBits == 0 && m_midiIn.empty();
}

std::vector<uint8_t> Board::takeMidiOut()
{
	std::vector<uint8_t> out;
	out.swap(m_midiOut);
	return out;
}

void Board::runCpu()
{
	// 24 machine cycles a sample, an instruction's overshoot carried to the next
	m_cycleTarget += kCyclesPerSample;
	if (m_cpu.cycles() < m_cycleTarget)
		m_cpu.run(int(m_cycleTarget - m_cpu.cycles()));
}

void Board::runCycles(uint64_t machineCycles)
{
	const uint64_t end = m_cycleTarget + machineCycles;
	while (m_cycleTarget < end) {
		runCpu();
		commit();
		updateLevelDetector(0.0);
		m_cpu.setInt0(m_capVolts > m_envelope);
	}
}

void Board::commit()
{
	if (m_daspCommit) {
		std::copy(m_dasp.begin(), m_dasp.begin() + Dasp24::kProgramBytes, m_daspActive.begin());
		m_daspCommit = false;
	}
}

void Board::runSample(double adc, double envelope, double &outL, double &outR)
{
	runCpu();
	commit();
	updateLevelDetector(envelope);
	m_cpu.setInt0(m_capVolts > m_envelope);
	if (m_adcServo) {
		// the node: R143 5.6K (the source, through the 20 kHz module), R138 8.2K, R137 51K to C64,
		// R136 510K to -12 V; the converter's full scale 3 V
		constexpr double g = 1 / 5.6e3 + 1 / 8.2e3 + 1 / 51e3 + 1 / 510e3;
		const double offset = (m_c64 / 51e3 - 12.0 / 510e3) / g;
		adc += offset / 3.0;
		m_lstMsb = adc < 0;
		// C64: R140 from LSTMSB (0 or 5 V), R137 to the node
		const double node = adc * 3.0;
		m_c64 += (1.0 / kSampleRate) / 4.7e-6 * (((m_lstMsb ? 5.0 : 0.0) - m_c64) / 51e3 - (m_c64 - node) / 51e3);
	}
	m_dsp.tick(m_daspActive.data(), adc, outL, outR);
}

void Board::runSampleDspHeld()
{
	runCpu();
	commit();
	updateLevelDetector(0.0);
	m_cpu.setInt0(m_capVolts > m_envelope);
}

void Board::updateLevelDetector(double envelope)
{
	const double dt = 1.0 / kSampleRate;
	static const double charge = 1 - std::exp(-dt / 0.051); // R209 510K, C108 0.1u
	m_envelope = envelope;
	if (m_latch[kLatch0600] & 0x40) // Q5 on: C108 discharged
		m_capVolts = 0;
	else
		m_capVolts += (12.0 - m_capVolts) * charge;
	// the key piezo: a press leaves its voltage on C107 until Q4 (bit 5) shorts it
	bool anyKey = false;
	for (const bool k : m_keys)
		anyKey = anyKey || k;
	if (m_latch[kLatch0600] & 0x20)
		m_piezo = 0;
	else if (anyKey)
		m_piezo = std::max(m_piezo, m_pressVolts);
}

} // namespace qvgt
