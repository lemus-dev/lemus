#include "machine.h"

#include <algorithm>
#include <cstring>

namespace tr909 {

Machine::Machine()
{
	m_cpu.set_bus(this);
	m_cpu.rxd_func() = [this] { return rxd(); };
	m_cpu.txd_func() = [this](int level) { txd(level); };
	for (int i = 0; i < 8; i++)
		m_cpu.an_func(i) = [this, i] { return m_an[size_t(i)]; };
	m_cpu.pc_in_cb() = [this] { return portCIn(); };
	m_cpu.pa_out_cb() = [this](uint8_t) { portsChanged(); };
	m_cpu.pb_out_cb() = [this](uint8_t) { portsChanged(); };
	m_an.fill(0); // AN2, AN4-AN7 are tied to AVss (SN p.8)
	m_an[0] = 128; // TEMPO at noon
	m_an[3] = 255; // TOTAL ACCENT full
	// AN1 is the cartridge's SENS line, pulled up on the switch board (47K at F5 pin 3, SN p.8);
	// an M-64C ties it to ground (SN p.13). The firmware takes CR1 < 40h as a cartridge being
	// present (034A, 04CE, 1140), so with none inserted AN1 reads high. (MAME's driver returns 0
	// here, which the firmware reads as an inserted cartridge.)
	m_an[1] = 255;
	m_writes.reserve(256);
}

bool Machine::setRom(const uint8_t *data, size_t size)
{
	if (data == nullptr || size != 0x2000)
		return false;
	m_rom.assign(data, data + size);
	return true;
}

void Machine::powerOn()
{
	m_cpu.reset();
	m_rxQueue.clear();
	m_rxFree = m_states;
	m_txBit = -1;
	m_dac.fill(0);
	m_trig1 = m_trig2 = 0;
}

uint8_t Machine::portCIn() const
{
	// SN p.8: PC2 foot switch (R633 pull-up; the jack grounds it with no plug in), PC5 DIN
	// START/STOP and PC6 DIN CONTINUE through the inverting stages Q603 and Q602 (R628, R624
	// pull-ups: high with no cable), PC7 tied to ground. PC0, PC1, PC3 and PC4 are in control
	// mode (TxD, RxD, INT2, TO) and read by the CPU core itself.
	uint8_t v = 0x1b;
	if (m_foot)
		v |= 0x04;
	if (!m_dinRun)
		v |= 0x20;
	if (!m_dinCont)
		v |= 0x40;
	return v;
}

void Machine::setDinSync(bool run, bool clock, bool cont)
{
	m_dinRun = run;
	m_dinCont = cont;
	// TEMPO (pin 3) drives INT1 through the emitter follower Q601; INT1 latches rising edges.
	if (clock != m_dinClock)
		m_cpu.set_input_line(UPD7810_INTF1, clock ? ASSERT_LINE : CLEAR_LINE);
	m_dinClock = clock;
}

void Machine::setKey(Key k, bool down)
{
	if (k < Key::Count)
		m_keys[size_t(k)] = down;
}

uint8_t Machine::switches() const
{
	// PA0-4 select a row (one-hot, high = selected); a pressed key reads 1.
	const uint8_t pa = m_cpu.pa_out();
	uint8_t data = 0;
	for (int row = 0; row < 5; row++)
		if (pa & (1 << row))
			for (int bit = 0; bit < 8; bit++)
				if (m_keys[size_t(row * 8 + bit)])
					data |= uint8_t(1 << bit);
	return m_keyInvert ? uint8_t(~data) : data;
}

uint8_t Machine::read(uint16_t addr)
{
	if (addr < 0x2000)
		return m_rom.empty() ? 0xff : m_rom[addr];
	if (addr < 0x4000)
		return m_ram[addr - 0x2000];
	if (addr < 0x6000)
		return switches();
	return 0xff; // cartridge slot, empty
}

void Machine::write(uint16_t addr, uint8_t data)
{
	if (addr >= 0x2000 && addr < 0x4000) {
		m_ram[addr - 0x2000] = data;
		return;
	}
	if (addr >= 0x4000 && addr < 0x6000) {
		const int reg = addr & 0x0f;
		if (reg >= 1 && reg <= 8)
			m_dac[size_t(reg - 1)] = data;
		else if (reg == 9)
			m_trig1 = data;
		else if (reg == 10)
			m_trig2 = data;
		else
			return;
		m_writes.push_back({now(), uint8_t(reg), data});
	}
}

uint32_t Machine::now() const
{
	const int elapsed = m_runIcount0 - m_cpu.icount();
	return m_sliceBase + uint32_t(std::max(0, elapsed));
}

void Machine::midiIn(const uint8_t *data, size_t n)
{
	const uint64_t t = m_states; // called between run()s
	uint64_t start = std::max(m_rxFree, t);
	for (size_t i = 0; i < n; i++) {
		m_rxQueue.push_back({start, data[i]});
		start += 10 * kStatesPerMidiBit;
	}
	m_rxFree = start;
}

int Machine::rxd()
{
	return 1; // bytes are handed to the serial port whole, see run()
}

void Machine::txd(int level)
{
	// Called once per bit time while the serial port shifts a frame out.
	if (m_txBit < 0) {
		if (level == 0) { m_txBit = 0; m_txShift = 0; }
		return;
	}
	if (m_txBit < 8) {
		m_txShift |= uint16_t((level & 1) << m_txBit);
		m_txBit++;
		return;
	}
	if (level == 1)
		m_midiOut.push_back({statesNow(), uint8_t(m_txShift)});
	m_txBit = -1;
}

std::vector<uint8_t> Machine::takeMidiOut()
{
	std::vector<uint8_t> out;
	out.reserve(m_midiOut.size());
	for (const auto &b : m_midiOut)
		out.push_back(b.data);
	m_midiOut.clear();
	return out;
}

std::vector<Machine::MidiOutByte> Machine::takeMidiOutTimed()
{
	std::vector<MidiOutByte> out;
	out.swap(m_midiOut);
	return out;
}

void Machine::run(int states)
{
	m_writes.clear();
	if (m_rom.empty() || states <= 0)
		return;
	// Run up to each queued MIDI byte's stop bit, hand the byte over, and go on.
	int done = 0;
	while (done < states) {
		int slice = states - done;
		if (!m_rxQueue.empty()) {
			const uint64_t due = m_rxQueue.front().start + kStatesToRxByte;
			const uint64_t at = m_states + uint64_t(done);
			if (due <= at) {
				m_cpu.sio_rx_byte(m_rxQueue.front().data);
				m_rxQueue.pop_front();
				continue;
			}
			slice = int(std::min<uint64_t>(uint64_t(slice), due - at));
		}
		// The CPU may start the slice a few states in, finishing an instruction that overran the
		// last one (its icount is then negative): states from the slice's start are slice - icount.
		m_runIcount0 = slice;
		m_sliceBase = uint32_t(done);
		m_cpu.run(slice);
		done += slice;
	}
	updateLeds(states);
	m_states += uint64_t(states);
	// Between calls statesNow() is states(): no slice is running.
	m_sliceBase = 0;
	m_runIcount0 = m_cpu.icount();
}

void Machine::tick(double rate)
{
	m_stateAcc += kStateHz / rate;
	const int n = int(m_stateAcc);
	m_stateAcc -= n;
	run(n);
}

void Machine::portsChanged()
{
	const uint8_t pa = m_cpu.pa_out(), pb = m_cpu.pb_out();
	if (!(pb & 0x20))
		m_bcd = pa & 0x0f; // 4511 transparent while LE (PB5) is low
	for (int c = 0; c < 5; c++)
		if (pb & (1 << c)) {
			m_ledLatch[size_t(c)] = pa;
			if (c < 3)
				m_digit[size_t(2 - c)] = m_bcd; // PB0 = right digit .. PB2 = left
		}
}

void Machine::updateLeds(int states)
{
	// The firmware strobes one column per scan (about 2 ms each, 5 columns), so the latched
	// row data is what the eye sees; smooth it over ~60 ms: the dim LEDs (lit one pass in four,
	// a 40 ms cycle) hold steady, and blinking still shows.
	const float k = std::min(1.0f, float(states) / float(kStateHz * 0.06));
	for (size_t i = 0; i < m_ledLevel.size(); i++) {
		const float on = (m_ledLatch[i >> 3] >> (i & 7)) & 1 ? 1.0f : 0.0f;
		m_ledLevel[i] += k * (on - m_ledLevel[i]);
	}
}

uint8_t Machine::segments(int bcd)
{
	// IC601 HD14511BP (the MC14511B's second source, SN p.3): 0-9 decoded, 6 without its top
	// segment and 9 without its bottom one (no tails, as the 4511's display table draws them),
	// 10-15 blank.
	static const uint8_t seg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7c, 0x07, 0x7f, 0x67};
	return bcd >= 0 && bcd < 10 ? seg[bcd] : 0;
}

void Machine::setRam(const uint8_t *data, size_t n)
{
	std::memcpy(m_ram.data(), data, std::min(n, m_ram.size()));
}

std::vector<uint8_t> Machine::saveState() const
{
	std::vector<uint8_t> out(m_ram.begin(), m_ram.end());
	return out;
}

bool Machine::loadState(const std::vector<uint8_t> &data)
{
	if (data.size() < m_ram.size())
		return false;
	setRam(data.data(), m_ram.size());
	return true;
}

} // namespace tr909
