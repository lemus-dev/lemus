#include "machine.h"

#include "board.h"
#include "voice.h"

#include <algorithm>
#include <cmath>

namespace bsrack {

Machine::Machine(std::vector<uint8_t> rom) : m_board(std::make_unique<Board>()), m_voice(std::make_unique<Voice>())
{
	m_romLoaded = m_board->setRom(rom);
	m_board->onSync = [this](int osc, uint64_t t) {
		// the edge's place in this sample: (t - start) / step, from the whole and fractional clocks
		double at = (double(t - m_sampleStart) - double(m_sampleStartFrac) * 0x1p-32) / m_step;
		at = std::clamp(at, 0.0, 0.99999994);
		int &n = osc == 0 ? m_frame.nEdgesA : m_frame.nEdgesB;
		if (n < VoiceFrame::kMaxEdges)
			(osc == 0 ? m_frame.edgesA : m_frame.edgesB)[size_t(n++)] = float(at);
		else {
			m_frame.droppedEdges++;
			++m_droppedTotal;
		}
	};
	m_board->onMidiOut = [this](uint8_t byte) {
		if (m_midiOutCount == m_midiOut.size())
			return;
		const size_t at = (m_midiOutHead + m_midiOutCount++) % m_midiOut.size();
		m_midiOut[at] = byte;
		m_midiOutAt[at] = m_samples;
	};
	prepare(m_rate);
	if (m_romLoaded)
		m_board->reset();
}

Machine::~Machine() = default;

void Machine::prepare(double hostRate)
{
	m_rate = hostRate > 0 ? hostRate : 48000;
	m_step = Board::kClock / m_rate;
	const uint64_t step = uint64_t(std::llround(m_step * 0x1p32));
	m_stepClocks = step >> 32;
	m_stepFrac = uint32_t(step);
	m_voice->prepare(m_rate);
}

void Machine::powerOn()
{
	m_powered = true;
	m_asleep = false;
	if (m_romLoaded)
		m_board->reset();
}

void Machine::powerOff()
{
	m_powered = false;
	m_asleep = true;
}

void Machine::setControls(const Controls &c)
{
	m_controls = c;
	for (int p = 0; p < kPots; ++p)
		if (kPotMux[size_t(p)] >= 0)
			m_board->pots[size_t(kPotMux[size_t(p)])] = uint8_t(std::lround(std::clamp(c.pots[size_t(p)], 0.0, 1.0) * 255.0));
	for (int s = 0; s < kSwitches; ++s)
		m_board->setSwitch(s, c.switches[size_t(s)]);
}

void Machine::setButton(int button, bool down)
{
	if (button < 0 || button >= kButtons)
		return;
	if (kKeyCode[size_t(button)] < 0)
		m_board->audition = down;
	else
		m_board->setKey(kKeyCode[size_t(button)], down);
}

void Machine::midiIn(uint8_t byte)
{
	if (m_romLoaded && m_powered)
		m_board->cpu.uart_rx_byte(byte);
}

void Machine::runSample()
{
	m_sampleStart = m_clock;
	m_sampleStartFrac = m_clockFrac;
	const uint64_t frac = uint64_t(m_clockFrac) + m_stepFrac;
	m_clockFrac = uint32_t(frac);
	m_clock += m_stepClocks + (frac >> 32);

	m_frame.nEdgesA = m_frame.nEdgesB = m_frame.droppedEdges = 0;
	const uint64_t target = m_clock / mn1880_device::clocks_per_cycle;
	const uint64_t now = m_board->cpu.total_cycles();
	if (target > now)
		m_board->run(int(target - now));

	const Board &b = *m_board;
	for (int c = 0; c < kCvChannels; ++c)
		m_frame.dac[size_t(c)] = b.cv[size_t(c)];
	m_frame.volA = b.cpu.reg(0x34);
	m_frame.volB = b.cpu.reg(0x32);
	m_frame.osc1Pulse = BIT(b.port1(), 7) != 0;
	m_frame.osc2Pulse = BIT(b.port1(), 6) != 0;
	m_frame.slope12 = BIT(b.port3(), 1) != 0;
	m_frame.gateOut = BIT(b.port3(), 7) == 0;
	m_frame.mixPot = float(m_controls.pots[kMix]);
}

void Machine::render(float *out, int n, const float *filterIn)
{
	if (!m_powered || !m_romLoaded)
	{
		std::fill(out, out + n, 0.0f);
		m_asleep = true;
		m_samples += uint64_t(n);
		return;
	}
	const double volume = m_controls.pots[kVolume];
	for (int i = 0; i < n; ++i, ++m_samples)
	{
		runSample();
		if (m_asleep && m_frame.dac[kVcaCv] == m_sleepVca)
		{
			out[i] = 0.0f;
			continue;
		}
		if (m_asleep)
			m_voice->wake();
		m_asleep = false;
		out[i] = m_voice->process(m_frame, filterIn != nullptr ? filterIn[i] : 0.0f, volume);
		if (m_voice->settled())
		{
			m_asleep = true;
			m_sleepVca = m_frame.dac[kVcaCv];
		}
	}
	const Board &b = *m_board;
	m_display.store(uint32_t(Board::segments(b.digits[1])) | uint32_t(Board::segments(b.digits[0])) << 8 | uint32_t(b.b000) << 16,
	                std::memory_order_relaxed);
}

namespace {
constexpr uint8_t kSnapshotMagic[4] = {'B', 'S', 'R', 'K'};
constexpr uint8_t kSnapshotVersion = 1;
} // namespace

std::vector<uint8_t> Machine::snapshot() const
{
	using lemus_state::put;
	if (!m_romLoaded || m_clock == 0)
		return {}; // nothing has run: a session of it boots, as a new unit does
	std::vector<uint8_t> v(kSnapshotMagic, kSnapshotMagic + 4);
	v.push_back(kSnapshotVersion);
	put(v, m_clock);
	put(v, m_clockFrac);
	put(v, m_powered);
	m_board->saveState(v);
	return v;
}

bool Machine::restore(const std::vector<uint8_t> &data)
{
	using lemus_state::get;
	if (!m_romLoaded || data.size() < 5 || !std::equal(kSnapshotMagic, kSnapshotMagic + 4, data.begin()) || data[4] != kSnapshotVersion)
		return false;
	uint64_t clock = 0; // a unit that never ran (saved without its ROM): boot instead
	const uint8_t *q = data.data() + 5;
	if (!get(q, data.data() + data.size(), clock) || clock == 0)
		return false;
	const auto before = snapshot();
	auto attempt = [this](const std::vector<uint8_t> &d) {
		const uint8_t *p = d.data() + 5, *end = d.data() + d.size();
		return get(p, end, m_clock) && get(p, end, m_clockFrac) && get(p, end, m_powered) && m_board->loadState(p, end) && p == end;
	};
	if (!attempt(data)) {
		attempt(before); // what it was: its own snapshot always fits
		return false;
	}
	m_sampleStart = m_clock;
	m_sampleStartFrac = m_clockFrac;
	m_frame = VoiceFrame{};
	m_midiOutHead = m_midiOutCount = 0;
	m_asleep = !m_powered;
	m_voice->restart();
	return true;
}

std::vector<uint8_t> Machine::saveEeprom() const
{
	return std::vector<uint8_t>(m_board->eeprom.begin(), m_board->eeprom.end());
}

bool Machine::loadEeprom(const std::vector<uint8_t> &data)
{
	if (data.size() != kEepromSize)
		return false;
	std::copy(data.begin(), data.end(), m_board->eeprom.begin());
	if (m_romLoaded && m_powered)
		m_board->reset();
	m_asleep = false;
	return true;
}

Display Machine::display() const
{
	const uint32_t v = m_display.load(std::memory_order_relaxed);
	Display d;
	d.digits = {uint8_t(v), uint8_t(v >> 8)};
	const uint8_t leds = uint8_t(~(v >> 16));  // active low
	d.lfo = BIT(leds, 0);
	d.program = BIT(leds, 1);
	d.midiRx = BIT(leds, 2);
	d.midiTx = BIT(leds, 3);
	d.utility = BIT(leds, 4);
	d.save = BIT(leds, 5);
	d.write = BIT(leds, 6);
	d.edit = BIT(d.digits[0], 7);
	return d;
}

int Machine::takeMidiOut(uint8_t *dest, int max, uint64_t *at)
{
	int n = 0;
	while (n < max && m_midiOutCount > 0)
	{
		if (at != nullptr)
			at[n] = m_midiOutAt[m_midiOutHead];
		dest[n++] = m_midiOut[m_midiOutHead];
		m_midiOutHead = (m_midiOutHead + 1) % m_midiOut.size();
		m_midiOutCount--;
	}
	return n;
}

} // namespace bsrack

namespace bsrack {

const VoiceParts &Machine::voiceParts() const { return m_voice->parts(); }

void Machine::setVoiceParts(const VoiceParts &p)
{
	m_voice->setParts(p);
	m_voice->prepare(m_rate);
}

double Machine::latencySamples() const { return m_voice->latency() + 0.5; }

int Machine::takeUnconverged() { return m_voice->takeUnconverged(); }

} // namespace bsrack
