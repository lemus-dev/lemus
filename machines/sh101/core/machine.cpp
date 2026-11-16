// The SH-101 CPU board around the MCS-48 core: key matrix and switch columns on DB0-DB5/P1,
// the inverted 6-bit DAC and the 4052-selected sample-and-holds on the BUS, and the P2 latch.
// docs/firmware.md has the I/O map; every polarity here was checked by running the ROM with
// tools/sh101seq.
#include "machine.h"

#include "mcs48.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace sh101 {

namespace {
// P1 bits on the DB5 column (function buttons), by Machine::Button.
constexpr uint8_t kButtonBit[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x80};
// P2 bits.
constexpr uint8_t kP2Gate = 0x80;     // P27, active low (TR9 inverts it to GATE)
constexpr uint8_t kP2KeyTrLed = 0x40; // P26, active high
constexpr uint8_t kP2HoldLed = 0x20;  // P25, active low
constexpr uint8_t kP2ClkRst = 0x10;   // P24, active low
constexpr uint8_t kP2PortaOff = 0x08; // P23, active high
constexpr double kLedTau = 0.020;
} // namespace

struct Machine::Impl : mcs48::Bus {
	mcs48::Cpu cpu{*this};
	std::vector<uint8_t> rom;

	// Panel inputs. Bits are "closed" (the firmware reads them inverted on P1). `want` is what
	// the setters ask for; `live` is what the CPU reads. want is copied to live at each loop
	// boundary (the P2 write that ends a loop), so that a TRANSPOSE change and the key changes
	// that go with it are always seen by the same loop; the firmware polls them once a loop
	// anyway. The pedal (T0) and the clock (T1) are live at once.
	struct Panel {
		std::array<uint8_t, 4> keyBank{}; // bank b = keys 8b..8b+7, bit = key % 8
		uint8_t buttons = 0;              // DB5 column
		Range range = Range::Ft8;
		Trigger trigger = Trigger::GateTrig;
		Transpose transpose = Transpose::Mid;
	};
	Panel want, live;
	bool holdPedal = false;
	bool clock = false;
	uint64_t loops = 0;

	// Outputs.
	uint8_t bus = 0xff;
	uint8_t p2 = 0xff;
	std::array<int, 4> sh{};
	CpuLines lines;
	bool changed = false;
	std::array<bool, size_t(Led::Count)> ledOn{};
	std::array<float, size_t(Led::Count)> ledLevel{};
	Observer *observer = nullptr;

	// Timing.
	double cycleAcc = 0;
	uint64_t lastP2Cycle = 0;
	double loopSeconds = 0.0015;
	double ledDt = 0;
	float ledK = 0;

	uint8_t switchColumn() const
	{
		uint8_t v = 0;
		switch (live.range) {
		case Range::Ft16: v |= 0x03; break;
		case Range::Ft8: v |= 0x02; break;
		case Range::Ft4: v |= 0x01; break;
		case Range::Ft2: break;
		}
		switch (live.trigger) {
		case Trigger::GateTrig: v |= 0x10; break;
		case Trigger::Lfo: v |= 0x08; break;
		case Trigger::Gate: break;
		}
		switch (live.transpose) {
		case Transpose::High: v |= 0x20; break; // OCT UP
		case Transpose::Mid: v |= 0x60; break;  // both
		case Transpose::Low: v |= 0x40; break;  // OCT DOWN
		}
		return v;
	}

	// P1: the returns of every column whose DB line is low; a closed switch pulls its bit low.
	uint8_t portIn(int port) override
	{
		if (port != 1)
			return 0xff;
		uint8_t closed = 0;
		for (int b = 0; b < 4; ++b)
			if (!(bus & (1 << b)))
				closed |= live.keyBank[size_t(b)];
		if (!(bus & 0x10))
			closed |= switchColumn();
		if (!(bus & 0x20))
			closed |= live.buttons;
		return uint8_t(~closed);
	}

	void busOut(uint8_t v) override
	{
		bus = v;
		const int ch = v >> 6; // DB7 = 4052 B, DB6 = A
		if (ch == 3)
			return;            // Y3: not connected, the park position
		const int code = ~v & 0x3f;
		sh[size_t(ch)] = code;
		int *line = ch == 0 ? &lines.kcv : ch == 1 ? &lines.range : &lines.random;
		if (*line != code) {
			*line = code;
			changed = true;
		}
		if (observer)
			observer->dacWrite(ch, code, cpu.totalCycles());
	}

	void portOut(int port, uint8_t v) override
	{
		if (port != 2)
			return;
		p2 = v;
		const bool gate = !(v & kP2Gate), portaOff = (v & kP2PortaOff) != 0, clkRst = !(v & kP2ClkRst);
		if (gate != lines.gate || portaOff != lines.portaOff || clkRst != lines.clkRst) {
			lines.gate = gate;
			lines.portaOff = portaOff;
			lines.clkRst = clkRst;
			changed = true;
		}
		// IC8 4556 decodes P20-P22: 1 LOAD .. 5 ARP UP, 0/6/7 none.
		const int code = v & 7;
		for (int l = 0; l < 5; ++l)
			ledOn[size_t(l)] = code == l + 1;
		ledOn[size_t(Led::Hold)] = !(v & kP2HoldLed);
		ledOn[size_t(Led::KeyTranspose)] = (v & kP2KeyTrLed) != 0;
		live = want;
		++loops;
		const uint64_t now = cpu.totalCycles();
		if (lastP2Cycle && now > lastP2Cycle)
			loopSeconds = double(now - lastP2Cycle) / kCycleHz;
		lastP2Cycle = now;
		if (observer)
			observer->p2Write(v, now);
	}

	int testIn(int t) override { return t == 0 ? !holdPedal : !clock; }

	void runSeconds(double dt)
	{
		cycleAcc += kCycleHz * dt;
		if (cycleAcc >= 1)
			cycleAcc -= cpu.run(int(cycleAcc));
		if (dt != ledDt) {
			ledDt = dt;
			ledK = float(1.0 - std::exp(-dt / kLedTau));
		}
		const float k = ledK;
		for (size_t l = 0; l < ledLevel.size(); ++l)
			ledLevel[l] += ((ledOn[l] ? 1.0f : 0.0f) - ledLevel[l]) * k;
	}
};

Machine::Machine() : m(std::make_unique<Impl>()) {}
Machine::~Machine() = default;

bool Machine::setRom(const uint8_t *data, size_t size)
{
	if (data == nullptr || size != 2048)
		return false;
	m->rom.assign(data, data + size);
	m->cpu.setRom(m->rom.data(), int(m->rom.size()));
	return true;
}

bool Machine::romLoaded() const { return m->rom.size() == 2048; }

void Machine::powerOn()
{
	m->cycleAcc = 0;
	m->lastP2Cycle = 0;
	m->bus = 0xff;
	m->live = m->want;
	m->cpu.reset();
	// The sample-and-holds and the latch start from nothing; the first loop writes them all.
	m->sh = {};
	m->lines = CpuLines{};
	m->lines.gate = !(m->p2 & kP2Gate);
	m->lines.portaOff = (m->p2 & kP2PortaOff) != 0;
	m->lines.clkRst = !(m->p2 & kP2ClkRst);
	m->changed = true;
}

void Machine::setKey(int k, bool down)
{
	if (k < 0 || k >= kKeys)
		return;
	uint8_t &b = m->want.keyBank[size_t(k >> 3)];
	const uint8_t bit = uint8_t(1 << (k & 7));
	b = uint8_t(down ? b | bit : b & ~bit);
}

bool Machine::key(int k) const { return k >= 0 && k < kKeys && (m->want.keyBank[size_t(k >> 3)] >> (k & 7)) & 1; }

void Machine::setButton(Button b, bool down)
{
	if (b >= Button::Count)
		return;
	const uint8_t bit = kButtonBit[size_t(b)];
	m->want.buttons = uint8_t(down ? m->want.buttons | bit : m->want.buttons & ~bit);
}

bool Machine::button(Button b) const { return b < Button::Count && (m->want.buttons & kButtonBit[size_t(b)]); }
void Machine::setRange(Range r) { m->want.range = r; }
void Machine::setTrigger(Trigger t) { m->want.trigger = t; }
void Machine::setTranspose(Transpose t) { m->want.transpose = t; }
void Machine::setHoldPedal(bool held) { m->holdPedal = held; }
void Machine::setClock(bool level) { m->clock = level; }

bool Machine::tick(double rate)
{
	if (!romLoaded())
		return false;
	m->changed = false;
	m->runSeconds(1.0 / rate);
	return m->changed;
}

void Machine::run(double seconds)
{
	if (!romLoaded())
		return;
	const int n = int(seconds * 1e6 + 0.5);
	for (int i = 0; i < n; ++i)
		m->runSeconds(1e-6);
}

const CpuLines &Machine::lines() const { return m->lines; }
float Machine::led(Led l) const { return l < Led::Count ? m->ledLevel[size_t(l)] : 0.0f; }
bool Machine::ledOn(Led l) const { return l < Led::Count && m->ledOn[size_t(l)]; }

std::array<uint8_t, Machine::kRamBytes> Machine::ram() const
{
	std::array<uint8_t, kRamBytes> r{};
	std::memcpy(r.data(), m->cpu.ram(), r.size());
	return r;
}

void Machine::setRam(const uint8_t *data, size_t n)
{
	std::memcpy(m->cpu.ram(), data, std::min(n, size_t(kRamBytes)));
}

namespace {
constexpr uint8_t kStateMagic[4] = {'S', '1', '0', '1'};
constexpr uint8_t kStateRam = 1;      // battery-backed RAM
constexpr uint8_t kStateSnapshot = 2; // full machine
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
	std::vector<uint8_t> v(kStateMagic, kStateMagic + 4);
	v.push_back(kStateRam);
	v.insert(v.end(), m->cpu.ram(), m->cpu.ram() + kRamBytes);
	return v;
}

bool Machine::loadState(const std::vector<uint8_t> &d)
{
	if (d.size() == size_t(kRamBytes)) { // bare RAM image
		setRam(d.data(), d.size());
		return true;
	}
	if (d.size() < 5 + size_t(kRamBytes) || !std::equal(kStateMagic, kStateMagic + 4, d.begin()))
		return false;
	if (d[4] == kStateSnapshot)
		return restore(d);
	if (d[4] != kStateRam)
		return false;
	setRam(d.data() + 5, kRamBytes);
	return true;
}

std::vector<uint8_t> Machine::snapshot() const
{
	std::vector<uint8_t> v(kStateMagic, kStateMagic + 4);
	v.push_back(kStateSnapshot);
	v.insert(v.end(), m->cpu.ram(), m->cpu.ram() + kRamBytes);
	mcs48::Cpu::State s{};
	m->cpu.save(s);
	put(v, s);
	put(v, m->bus);
	put(v, m->live);
	put(v, m->loops);
	put(v, m->p2);
	put(v, m->sh);
	put(v, m->lines);
	put(v, m->ledOn);
	put(v, m->ledLevel);
	put(v, m->cycleAcc);
	put(v, m->lastP2Cycle);
	put(v, m->loopSeconds);
	return v;
}

bool Machine::restore(const std::vector<uint8_t> &d)
{
	if (d.size() < 5 + size_t(kRamBytes) || !std::equal(kStateMagic, kStateMagic + 4, d.begin()) || d[4] != kStateSnapshot)
		return false;
	size_t pos = 5 + size_t(kRamBytes);
	mcs48::Cpu::State s{};
	if (!get(d, pos, s))
		return false;
	m->cpu.load(s);
	setRam(d.data() + 5, kRamBytes);
	get(d, pos, m->bus);
	get(d, pos, m->live);
	get(d, pos, m->loops);
	get(d, pos, m->p2);
	get(d, pos, m->sh);
	get(d, pos, m->lines);
	get(d, pos, m->ledOn);
	get(d, pos, m->ledLevel);
	get(d, pos, m->cycleAcc);
	get(d, pos, m->lastP2Cycle);
	get(d, pos, m->loopSeconds);
	m->changed = true;
	return true;
}

double Machine::lastLoopSeconds() const { return m->loopSeconds; }

// The step format, from LOAD (0x20F-0x284) and PLAY (0x286-0x2D9): docs/sequencer.md.
uint8_t Machine::encodeStep(const Step &s, int lastNote)
{
	if (s.rest)
		return uint8_t(0x40 | (std::clamp(lastNote, 0, 58) & 0x3f));
	return uint8_t(0x80 | (s.legato ? 0x40 : 0) | std::clamp(s.note, 0, 58));
}

Machine::Step Machine::decodeStep(uint8_t b)
{
	Step s;
	if (!(b & 0x80)) {
		s.rest = true;
		return s;
	}
	s.note = b & 0x3f;
	s.legato = (b & 0x40) != 0;
	return s;
}

void Machine::writeSequence(const std::vector<Step> &steps)
{
	uint8_t *ram = m->cpu.ram();
	int last = 0;
	for (int i = 0; i < kMaxSteps; ++i) {
		uint8_t b = 0;
		if (size_t(i) < steps.size()) {
			b = encodeStep(steps[size_t(i)], last);
			if (!steps[size_t(i)].rest)
				last = steps[size_t(i)].note;
		}
		ram[kSequenceStart + i] = b;
	}
	ram[kPointer] = kPointer;
}

std::vector<Machine::Step> Machine::demoSequence()
{
	// 16 sixteenths in C minor, entered at TRANSPOSE L (note 7 = C3 .. 19 = C4 at 8'), with two
	// slurs: C3 C3 - C4 Bb3~ G3 - C3 Eb3 - F3 G3~ - C3 C4 Bb3.
	const Step rest{true, 0, false};
	auto note = [](int n, bool legato = false) { return Step{false, n, legato}; };
	return {note(7), note(7), rest, note(19), note(17, true), note(14), rest, note(7),
	        note(10), rest, note(12), note(14, true), rest, note(7), note(19), note(17)};
}

std::vector<Machine::Step> Machine::readSequence() const
{
	std::vector<Step> r;
	const uint8_t *ram = m->cpu.ram();
	for (int i = 0; i < kMaxSteps && ram[kSequenceStart + i] != 0; ++i)
		r.push_back(decodeStep(ram[kSequenceStart + i]));
	return r;
}

void Machine::setObserver(Observer *o) { m->observer = o; }
uint64_t Machine::cycles() const { return m->cpu.totalCycles(); }
uint64_t Machine::loops() const { return m->loops; }
uint8_t Machine::p2() const { return m->p2; }
uint8_t Machine::tFlags() const { return m->cpu.timer(); }
uint16_t Machine::pc() const { return m->cpu.pc(); }

} // namespace sh101
