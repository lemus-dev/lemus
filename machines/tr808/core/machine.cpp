#include "machine.h"

#include "voiceboard.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace tr808 {

namespace {
// The switches' codes on PA0-3 while their column (PH) is selected, from the diode matrix on the
// main board (p.8, D2-D51). MODE in panel order (PATTERN CLEAR ... COMPOSE): PLAY and COMPOSE
// share a code; the firmware tells them apart by SW1b, which lets the RAM be written in COMPOSE
// (and in the pattern modes) but not in PLAY or MANUAL PLAY.
constexpr uint8_t kModeCode[] = {5, 6, 7, 1, 3, 3};
// PRE-SCALE 1-4 (PA0-1); the firmware's table (0x478) turns them into 8, 4, 6 and 3 clocks a step.
constexpr uint8_t kPrescaleCode[] = {3, 1, 2, 0};
constexpr uint8_t kVariationCode[] = {0, 8, 4};        // BASIC VARIATION A, AB, B (PA2-3)
constexpr uint8_t kAutoFillCode[] = {0, 5, 4, 3, 2, 1}; // AUTO FILL IN MANUAL, 16, 12, 8, 4, 2
// INSTRUMENT-SELECT: position 1 (AC) reads 11, down to 12 (CH) 0.

// Status buffer & gate (IC3, Q8-Q11), read while no column is selected.
enum : uint8_t { kStatusRun = 1, kStatusFillIn = 2, kStatusResetStart = 4, kStatusClock = 8 };

// The instrument data lines (p.3): PE0-3 CP, RS, HT, MT; PD0-3 CH, OH, CY, CB; PF0-3 LT, SD,
// BD, AC.
uint16_t instrumentBits(uint8_t d, uint8_t e, uint8_t f)
{
	uint16_t b = 0;
	if (e & 1) b |= kBitCP;
	if (e & 2) b |= kBitRS;
	if (e & 4) b |= kBitHT;
	if (e & 8) b |= kBitMT;
	if (d & 1) b |= kBitCH;
	if (d & 2) b |= kBitOH;
	if (d & 4) b |= kBitCY;
	if (d & 8) b |= kBitCB;
	if (f & 1) b |= kBitLT;
	if (f & 2) b |= kBitSD;
	if (f & 4) b |= kBitBD;
	if (f & 8) b |= kBitAC;
	return b;
}
} // namespace

Machine::Machine() : m_cpu(*this) {}

bool Machine::setRom(const uint8_t *data, size_t size)
{
	if (data == nullptr || size != 2048)
		return false;
	m_rom.assign(data, data + size);
	m_cpu.setRom(m_rom.data(), int(m_rom.size()));
	return true;
}

void Machine::powerOn()
{
	m_portC = m_portD = m_portE = m_portF = m_portG = 0;
	m_portH = 0xf;
	m_portI = 1;
	m_cycleAcc = 0;
	m_irqAcc = 0;
	m_data = 0;
	m_cpu.reset();
}

uint8_t Machine::columnA(int column) const
{
	switch (column) {
	case 0:
		return uint8_t(kModeCode[size_t(m_mode)] | (m_clear ? 8 : 0));
	case 1:
		return uint8_t(kPrescaleCode[size_t(m_prescale)] | kVariationCode[size_t(m_variation)]);
	case 2:
		return uint8_t(11 - m_instrument);
	default:
		return uint8_t(kAutoFillCode[size_t(m_autoFill)] | (m_ifB ? 8 : 0));
	}
}

uint8_t Machine::columnB(int column) const
{
	uint8_t v = 0;
	for (int r = 0; r < 4; ++r)
		if (m_step[size_t(column * 4 + r)])
			v |= uint8_t(1 << r);
	return v;
}

bool Machine::writable() const
{
	return m_mode != Mode::Play && m_mode != Mode::ManualPlay;
}

uint8_t Machine::read(int port)
{
	switch (port) {
	case ucom43::A:
	case ucom43::B: {
		const uint8_t columns = m_portH ^ 0xf; // PH low selects a column (Q23-Q26)
		if (columns == 0) {
			if (port == ucom43::B)
				return 0;
			// Q18 opens the status gate while every PH is high.
			static const int *map = [] {
				static int m[4] = {0, 1, 2, 3};
				if (const char *e = std::getenv("TR808_STATUS"))
					std::sscanf(e, "%d,%d,%d,%d", &m[0], &m[1], &m[2], &m[3]);
				return m;
			}();
			return uint8_t((m_run ? 1 << map[0] : 0) | (m_fillIn ? 1 << map[1] : 0) |
			               (m_resetStart ? 1 << map[2] : 0) | (m_clock ? 1 << map[3] : 0));
		}
		uint8_t v = 0;
		for (int c = 0; c < 4; ++c)
			if (columns & (1 << c))
				v |= port == ucom43::A ? columnA(c) : columnB(c);
		return v;
	}
	case ucom43::C: {
		// PI1 high enables IC5, which selects the RAM PE2-3 name; PI0 low writes.
		const bool ce = (m_portI & 2) != 0, we = (m_portI & 1) == 0;
		if (!ce || we)
			return 0;
		const int a = (m_portE >> 2 & 3) << 10 | (m_portE & 3) << 8 | (m_portD & 0xf) << 4 | (m_portF & 0xf);
		return m_xram[size_t(a)];
	}
	default:
		return 0;
	}
}

void Machine::write(int port, uint8_t data)
{
	data &= 0xf;
	switch (port) {
	case ucom43::C: m_portC = data; break;
	case ucom43::D: m_portD = data; break;
	case ucom43::E: m_portE = data; break;
	case ucom43::F: m_portF = data; break;
	case ucom43::G: m_portG = data; break;
	case ucom43::H: m_portH = data; break;
	case ucom43::I: {
		const uint8_t old = m_portI;
		m_portI = data & 7;
		if ((m_portI & 4) && !(old & 4)) {
			++m_pulses; // PI2: the step pulse into IC6
			m_changed = true;
		}
		break;
	}
	default:
		return;
	}
	// A write cycle: CE and /WE both active (SW1b passes /WE only in the writing modes).
	if ((m_portI & 2) && !(m_portI & 1) && writable()) {
		const int a = (m_portE >> 2 & 3) << 10 | (m_portE & 3) << 8 | (m_portD & 0xf) << 4 | (m_portF & 0xf);
		if (m_xram[size_t(a)] != (m_portC & 0xf))
			++m_ramWrites;
		static const bool trace = std::getenv("TR808_RAMTRACE") != nullptr;
		if (trace)
			std::fprintf(stderr, "ram write %d.%03x = %x (pc %03x)\n", a >> 10, a & 0x3ff, m_portC & 0xf, m_cpu.pc());
		static const int histOn = std::getenv("TR808_RAMHIST") ? std::atoi(std::getenv("TR808_RAMHIST")) : -1;
		if (histOn >= 0 && (m_portC & 0xf) != 0 && (a >> 10) == histOn) {
			uint16_t pcs[64];
			uint8_t accs[64];
			m_cpu.history(pcs, accs);
			std::fprintf(stderr, "history before %d.%03x = %x:", a >> 10, a & 0x3ff, m_portC & 0xf);
			for (int k = 0; k < 64; ++k)
				std::fprintf(stderr, " %03x/%x", pcs[k], accs[k]);
			std::fprintf(stderr, "\n");
		}
		m_xram[size_t(a)] = m_portC & 0xf;
	}
	if (port == ucom43::D || port == ucom43::E || port == ucom43::F)
		updateData();
	if (port == ucom43::G || port == ucom43::H) {
		const uint8_t columns = m_portH ^ 0xf;
		uint16_t on = 0;
		for (int c = 0; c < 4; ++c)
			if (columns & (1 << c))
				on |= uint16_t((m_portG & 0xf) << (c * 4));
		m_ledNow = on;
	}
}

void Machine::updateData()
{
	const uint16_t d = instrumentBits(m_portD, m_portE, m_portF);
	if (d != m_data) {
		m_data = d;
		m_changed = true;
	}
}

void Machine::updateLeds(double dt)
{
	// The step LEDs: each column is lit a quarter of the time; average over ~30 ms like the eye.
	const float k = float(dt / 0.03);
	for (int i = 0; i < 16; ++i) {
		const float target = (m_ledNow >> i) & 1 ? 4.0f : 0.0f;
		m_stepOn[size_t(i)] += (target - m_stepOn[size_t(i)]) * k;
		m_stepLevel[size_t(i)] = std::min(1.0f, m_stepOn[size_t(i)]);
	}
	// Q3 and Q4 take PE0 and PE1 through 10k into 33 uF (C4) and 10 uF (C5), their bases clamping
	// the capacitors at ~0.65 V; Q2 and Q5 invert them. The firmware holds PE0 at the playing part
	// (low = 1st) and PE1 at the variation (low = A). Q4 lights B and Q5 A. Q3 lights 2nd PART and
	// Q2 1st PART: the PCB (SN p.10-11) puts Q3's LED (R12) at the lower, 2nd PART hole; the p.8
	// schematic swaps D78/D79 and their captions.
	auto hold = [dt](float &v, bool high, double tau) {
		v += float((high ? 5.0 - v : -v) * dt / tau);
		v = std::clamp(v, 0.0f, 0.65f);
		return std::clamp((v - 0.45f) / 0.15f, 0.0f, 1.0f);
	};
	const float q3 = hold(m_holdE0, m_portE & 1, 0.33);
	const float b = hold(m_holdE1, m_portE & 2, 0.1);
	m_ledLevel[size_t(Led::SecondPart)] = q3;
	m_ledLevel[size_t(Led::FirstPart)] = 1 - q3;
	m_ledLevel[size_t(Led::VariationB)] = b;
	m_ledLevel[size_t(Led::VariationA)] = 1 - b;
}

bool Machine::tick(double rate)
{
	if (!romLoaded())
		return false;
	m_changed = false;
	const double dt = 1.0 / rate;
	m_cycleAcc += kCycleHz * dt;
	m_irqAcc += dt;
	if (m_irqAcc >= m_irqSeconds) {
		m_irqAcc -= m_irqSeconds;
		m_cpu.setInt(true);
		m_cpu.setInt(false);
	}
	if (m_cycleAcc >= 1) {
		const int want = int(m_cycleAcc);
		m_cycleAcc -= m_cpu.run(want);
	}
	updateLeds(dt);
	return m_changed;
}

void Machine::writePattern(int pattern, int variation, const std::vector<uint16_t> &steps, int prescale)
{
	const auto mid = steps.begin() + std::min<std::ptrdiff_t>(std::ptrdiff_t(steps.size()), 16);
	writePattern(pattern, variation, {steps.begin(), mid}, {mid, steps.end()}, prescale);
}

void Machine::writePattern(int pattern, int variation, const std::vector<uint16_t> &first, const std::vector<uint16_t> &second,
                           int prescale)
{
	pattern &= 15;
	const int len1 = std::clamp(int(first.size()), 1, 16), len2 = std::min(int(second.size()), 16);
	for (int s = 0; s < 32; ++s) {
		const auto &part = s < 16 ? first : second;
		const int i = s & 15;
		const uint16_t v = i < (s < 16 ? len1 : len2) && size_t(i) < part.size() ? part[size_t(i)] : 0;
		const int e = (s / 16) | (variation & 1) << 1;
		const int a = e << 8 | pattern << 4 | i;
		// IC8 (PE2-3 = 0) CB CY OH CH on PD's bits, IC10 MT HT RS CP on PE's, IC7 AC BD SD LT on PF's
		m_xram[size_t(0 << 10 | a)] = uint8_t((v & kBitCH ? 1 : 0) | (v & kBitOH ? 2 : 0) | (v & kBitCY ? 4 : 0) | (v & kBitCB ? 8 : 0));
		m_xram[size_t(1 << 10 | a)] = uint8_t((v & kBitCP ? 1 : 0) | (v & kBitRS ? 2 : 0) | (v & kBitHT ? 4 : 0) | (v & kBitMT ? 8 : 0));
		m_xram[size_t(2 << 10 | a)] = uint8_t((v & kBitLT ? 1 : 0) | (v & kBitSD ? 2 : 0) | (v & kBitBD ? 4 : 0) | (v & kBitAC ? 8 : 0));
	}
	// IC9: the parts' lengths (16 as 0) and the pre-scale's clocks a step, bit 3 set while the 2nd
	// part is empty.
	m_xram[size_t(3 << 10 | 0x000 | pattern)] = uint8_t(len1 & 15);
	m_xram[size_t(3 << 10 | 0x100 | pattern)] = uint8_t(len2 & 15);
	m_xram[size_t(3 << 10 | 0x200 | pattern)] = uint8_t(prescaleClocks(prescale) | (len2 == 0 ? 8 : 0));
}

void Machine::setRam(const uint8_t *data, size_t n)
{
	for (size_t i = 0; i < m_xram.size(); ++i)
		m_xram[i] = i < n ? data[i] & 0xf : 0;
}

namespace {
constexpr uint32_t kStateMagic = 0x38303801; // "808" v1
template <typename T> void put(std::vector<uint8_t> &v, const T &x)
{
	const auto *p = reinterpret_cast<const uint8_t *>(&x);
	v.insert(v.end(), p, p + sizeof(T));
}
template <typename T> bool get(const std::vector<uint8_t> &v, size_t &pos, T &x)
{
	if (pos + sizeof(T) > v.size())
		return false;
	std::memcpy(&x, v.data() + pos, sizeof(T));
	pos += sizeof(T);
	return true;
}
} // namespace

std::vector<uint8_t> Machine::saveState() const
{
	std::vector<uint8_t> v;
	put(v, kStateMagic);
	v.insert(v.end(), m_xram.begin(), m_xram.end());
	return v;
}

bool Machine::loadState(const std::vector<uint8_t> &v)
{
	size_t pos = 0;
	uint32_t magic = 0;
	if (!get(v, pos, magic) || magic != kStateMagic || pos + m_xram.size() > v.size())
		return false;
	std::memcpy(m_xram.data(), v.data() + pos, m_xram.size());
	return true;
}

double TempoClock::bpm(double tempo, double fine)
{
	// Q14's base node: Thevenin sources into R41 (560 ohm) to ground.
	auto vb = [](double x, double f) {
		x = std::clamp(x, 0.0, 1.0);
		f = std::clamp(f, 0.0, 1.0);
		const double rt = (100e3 + 10e3 * x * (1 - x)) / 2; // the two TEMPO gangs, 100k each
		const double rf = 560e3 + 50e3 * f * (1 - f);
		const double g = 1 / 560.0 + 1 / rt + 1 / rf + 1 / 222.5e3 + 1 / 47e3;
		const double i = 5 * x / rt + 5 * f / rf - 15 / 47e3; // TM2's share is the same everywhere
		return i / g;
	};
	constexpr double vt = 0.0257; // 25 C
	return 300.0 * std::exp((vb(tempo, fine) - vb(1, 1)) / vt);
}

double TempoClock::bpm(double tempo, double fine, double tm2)
{
	// as bpm() with TM2's own current: its wiper at -15 V x (1 - tm2) through R43 and the wiper's
	// share of the track
	auto vb = [](double x, double f, double t) {
		x = std::clamp(x, 0.0, 1.0);
		f = std::clamp(f, 0.0, 1.0);
		const double fromGround = 1 - std::clamp(t, 0.0, 1.0);
		const double rt = (100e3 + 10e3 * x * (1 - x)) / 2;
		const double rf = 560e3 + 50e3 * f * (1 - f);
		const double rtm = 220e3 + 10e3 * fromGround * (1 - fromGround);
		const double g = 1 / 560.0 + 1 / rt + 1 / rf + 1 / rtm + 1 / 47e3;
		const double i = 5 * x / rt + 5 * f / rf - 15 * fromGround / rtm - 15 / 47e3;
		return i / g;
	};
	constexpr double vt = 0.0257;
	return 300.0 * std::exp((vb(tempo, fine, tm2) - vb(1, 1, 0.5)) / vt);
}

bool TempoClock::advance(double dt, double bpm, bool run)
{
	if (run && !m_run)
		m_hold = kStartSeconds;
	m_run = run;
	if (m_hold > 0) {
		m_hold -= dt;
		if (m_hold > 0)
			return false;
		m_phase = 0; // the oscillator starts again from its reset: the clock rises
		return true;
	}
	m_phase += bpm / 60.0 * 24.0 * dt;
	m_phase -= std::floor(m_phase);
	return m_phase < 0.5;
}

} // namespace tr808
