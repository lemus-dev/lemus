#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

namespace {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

// A pot: its position 0..1, shown on the scale the panel prints around it.
enum class Scale { Ten, PlusMinusFive, Semitone, Detune, Mix };

struct PotInfo {
	const char *id, *name;
	Scale scale;
	float def; // position 0..1
};

// In panel order (core/controls.h). Defaults: a plain patch, the pots where the owner's manual's
// TB-303 set-up (p. 19) or the centre detent puts them.
const PotInfo kPots[bsrack::kPots] = {
	{"volume", "Volume", Scale::Ten, 0.7f},
	{"portamento", "Portamento", Scale::Ten, 0.0f},
	{"lfo_speed", "LFO Speed", Scale::Ten, 0.5f},
	{"lfo_delay", "LFO Delay", Scale::Ten, 0.0f},
	{"mix", "Osc Mix", Scale::Mix, 0.5f},
	{"detune", "Osc 2 Detune", Scale::Detune, 0.5f},
	{"semitone", "Osc 2 Semi-Tone", Scale::Semitone, 0.0f},
	{"mod_env", "Osc Mod Envelope", Scale::PlusMinusFive, 0.5f},
	{"mod_lfo", "Osc Mod LFO", Scale::PlusMinusFive, 0.5f},
	{"pulse_width", "Osc Mod Pulse Width", Scale::PlusMinusFive, 0.5f},
	{"frequency", "Filter Frequency", Scale::Ten, 0.6f},
	{"resonance", "Filter Resonance", Scale::Ten, 0.0f},
	{"mod_depth", "Filter Mod Depth", Scale::PlusMinusFive, 0.5f},
	{"attack", "Envelope Attack", Scale::Ten, 0.0f},
	{"decay", "Envelope Decay", Scale::Ten, 0.4f},
	{"sustain", "Envelope Sustain", Scale::Ten, 1.0f},
	{"release", "Envelope Release", Scale::Ten, 0.3f},
	{"velocity", "Envelope Velocity", Scale::Ten, 0.0f},
};

struct SwitchInfo {
	const char *id, *name;
	std::initializer_list<const char *> positions;
	int def;
};

const SwitchInfo kSwitches[bsrack::kSwitches] = {
	{"write", "Write", {"Protect", "Enable"}, 0},
	{"lfo_shape", "LFO Shape", {"R'dom", "Tri", "Saw"}, 1},
	{"osc1_wave", "Osc 1 Waveform", {"Saw", "Pulse"}, 0},
	{"osc2_range", "Osc 2 Range", {"16'", "8'", "4'", "2'"}, 1},
	{"osc2_wave", "Osc 2 Waveform", {"Saw", "Pulse"}, 0},
	{"sync", "Osc 1-2 Sync", {"Off", "On"}, 0},
	{"oscillator", "Osc Mod Oscillator", {"1", "2"}, 0},
	{"pwm_source", "PWM Source", {"Env 2", "Man.", "LFO"}, 1},
	{"cut_off", "Filter Cut-Off", {"12dB", "24dB"}, 1},
	{"mod_source", "Filter Mod Source", {"LFO", "Env 2"}, 1},
	{"envelope", "Envelope", {"1", "2", "1&2"}, 0},
	{"triggering", "Envelope Triggering", {"Autoglide", "Single", "Multi"}, 2},
};

juce::String potText(Scale scale, float v)
{
	switch (scale) {
	case Scale::Ten:
		return juce::String(v * 10.0f, 1);
	case Scale::PlusMinusFive:
		return juce::String(v * 10.0f - 5.0f, 1);
	case Scale::Semitone:
		return "+" + juce::String(v * 12.0f, 1);
	case Scale::Detune:
		return juce::String(v * 2.0f - 1.0f, 2);
	case Scale::Mix:
		break;
	}
	// MIX: EXT. at the left, OSC1, 1&2 at the top, OSC2 at the right (the panel's legends).
	if (v < 0.17f)
		return "Ext.";
	if (v < 0.42f)
		return "Osc 1";
	if (v < 0.58f)
		return "1&2";
	return v < 0.83f ? "1&2 > Osc 2" : "Osc 2";
}

} // namespace

const char *BassStationProcessor::potId(int pot) { return kPots[pot].id; }
const char *BassStationProcessor::switchId(int sw) { return kSwitches[sw].id; }

Layout BassStationProcessor::layout()
{
	Layout l;
	for (const auto &p : kPots) {
		const Scale scale = p.scale;
		const auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([scale](float v, int) { return potText(scale, v); });
		l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(p.id, 1), p.name, juce::NormalisableRange<float>(0.0f, 1.0f), p.def, attrs));
	}
	for (const auto &s : kSwitches) {
		juce::StringArray items;
		for (const char *pos : s.positions)
			items.add(pos);
		l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(s.id, 1), s.name, items, s.def));
	}
	return l;
}

namespace {
std::vector<uint8_t> romBytes(const juce::MemoryBlock *rom)
{
	if (rom == nullptr)
		return {};
	const auto *p = static_cast<const uint8_t *>(rom->getData());
	return std::vector<uint8_t>(p, p + rom->getSize());
}
} // namespace

BassStationProcessor::BassStationProcessor(const juce::MemoryBlock *rom, lemus::Artwork artwork)
	: AudioProcessor(BusesProperties()
	                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
	                     .withInput("FILTER INPUT", juce::AudioChannelSet::stereo(), false)),
	  m_artwork(std::move(artwork)), m_params(*this, nullptr, "bsrack", layout()), m_machine(romBytes(rom))
{
	m_queue.reserve(64);
	m_machine.setControls(controls());
}

bsrack::Controls BassStationProcessor::controls() const
{
	bsrack::Controls c;
	for (int i = 0; i < bsrack::kPots; ++i)
		c.pots[size_t(i)] = double(m_params.getRawParameterValue(kPots[i].id)->load());
	for (int i = 0; i < bsrack::kSwitches; ++i)
		c.switches[size_t(i)] = juce::roundToInt(m_params.getRawParameterValue(kSwitches[i].id)->load());
	return c;
}

bool BassStationProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	for (const auto &set : layouts.inputBuses)
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
			return false;
	return true;
}

void BassStationProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_machine.prepare(sampleRate);
	m_in.assign(size_t(juce::jmax(1, samplesPerBlock)), 0.0f);
	m_midiOut.ensureSize(4096);
	m_sysex.reserve(4096);
}

// One byte from the UART: running status, the channel messages' lengths, system exclusive (a
// program dump) passed whole. Real-time bytes stay on the cable: a host plays its own transport.
void BassStationProcessor::midiOutByte(uint8_t b, int at, juce::MidiBuffer &out)
{
	if (b >= 0xf8)
		return;
	if (m_inSysex) {
		if (b & 0x80 && b != 0xf7) { // cut short: dropped, the byte taken as itself below
			m_inSysex = false;
			m_sysex.clear();
		} else {
			if (m_sysex.size() < m_sysex.capacity())
				m_sysex.push_back(b);
			if (b == 0xf7) {
				out.addEvent(m_sysex.data(), int(m_sysex.size()), at);
				m_inSysex = false;
				m_sysex.clear();
			}
			return;
		}
	}
	if (b == 0xf0) {
		m_inSysex = true;
		m_sysex.clear();
		m_sysex.push_back(b);
		m_outStatus = 0;
		return;
	}
	if (b >= 0xf0) { // system common: cancels running status (the unit sends none)
		m_outStatus = 0;
		return;
	}
	if (b & 0x80) {
		m_outStatus = b;
		m_outCount = 0;
		return;
	}
	if (m_outStatus == 0)
		return;
	m_outData[size_t(m_outCount++)] = b;
	const int need = (m_outStatus & 0xe0) == 0xc0 ? 1 : 2; // program change and channel pressure: one
	if (m_outCount < need)
		return;
	m_outCount = 0;
	out.addEvent(need == 1 ? juce::MidiMessage(m_outStatus, m_outData[0]) : juce::MidiMessage(m_outStatus, m_outData[0], m_outData[1]), at);
}

void BassStationProcessor::setButton(int button, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({button, down});
}

void BassStationProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The plugin wrappers lock the processor they host: in the Lemus plugins that is the host shell
	// (host/LemusProcessor), which calls this directly. So this processor's own lock (the session's
	// snapshot, get/setStateInformation) is taken here.
	const juce::ScopedLock sl(getCallbackLock());
	if (isSuspended()) {
		buffer.clear();
		midi.clear();
		return;
	}
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	const uint64_t blockStart = m_machine.samples();
	if (m_haveEeprom) {
		const juce::SpinLock::ScopedTryLockType lock(m_eepromLock);
		if (lock.isLocked()) {
			m_machine.loadEeprom(m_pendingEeprom);
			m_haveEeprom = false;
		}
	}
	{
		const juce::SpinLock::ScopedTryLockType lock(m_queueLock);
		if (lock.isLocked()) {
			for (const auto &e : m_queue)
				m_machine.setButton(e.button, e.down);
			m_queue.clear();
		}
	}
	m_machine.setControls(controls());

	// FILTER INPUT is the side-chain: what the host routes there (its channels mixed) is a cable in
	// the rear's jack; nothing routed, nothing plugged in.
	const float *in = nullptr;
	const auto *bus = getBus(true, 0);
	const int chans = bus != nullptr && bus->isEnabled() ? juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels()) : 0;
	m_filterIn.store(chans > 0, std::memory_order_relaxed);
	if (chans > 0) {
		if (int(m_in.size()) < n)
			m_in.assign(size_t(n), 0.0f);
		std::fill(m_in.begin(), m_in.begin() + n, 0.0f);
		for (int ch = 0; ch < chans; ++ch)
			for (int i = 0; i < n; ++i)
				m_in[size_t(i)] += buffer.getSample(ch, i) / float(chans);
		in = m_in.data();
	}

	// MIDI bytes at their sample positions
	auto *out = buffer.getWritePointer(0);
	int pos = 0;
	for (const auto event : midi) {
		const int at = juce::jlimit(0, n, event.samplePosition);
		if (at > pos) {
			m_machine.render(out + pos, at - pos, in != nullptr ? in + pos : nullptr);
			pos = at;
		}
		const auto m = event.getMessage();
		for (int i = 0; i < m.getRawDataSize(); ++i)
			m_machine.midiIn(m.getRawData()[i]);
	}
	if (pos < n)
		m_machine.render(out + pos, n - pos, in != nullptr ? in + pos : nullptr);
	// MIDI OUT: the bytes the firmware sent, each at the sample it left in; the input taken out
	m_midiOut.clear();
	for (int got; (got = m_machine.takeMidiOut(m_outBytes.data(), int(m_outBytes.size()), m_outAt.data())) > 0;)
		for (int i = 0; i < got; ++i)
			midiOutByte(m_outBytes[size_t(i)], juce::jlimit(0, juce::jmax(0, n - 1), int(m_outAt[size_t(i)] - blockStart)), m_midiOut);
	midi.swapWith(m_midiOut);
	for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
		buffer.copyFrom(ch, 0, buffer, 0, 0, n);
	m_asleep.store(m_machine.asleep(), std::memory_order_relaxed);

	const juce::SpinLock::ScopedTryLockType lock(m_displayLock);
	if (lock.isLocked())
		m_display = m_machine.display();
}

BassStationProcessor::Display BassStationProcessor::display() const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	return m_display;
}

juce::AudioProcessorEditor *BassStationProcessor::createEditor() { return new BassStationEditor(*this); }

void BassStationProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", kModelVersion, nullptr);
	// The whole unit (Machine::snapshot): a session reopens it as it was, the sound being edited, the
	// mode and the display included, not booted from the EEPROM. Taken between blocks. With an
	// EEPROM still waiting for the audio thread, that EEPROM alone (the next load boots from it).
	std::vector<uint8_t> eeprom, board;
	{
		const juce::ScopedLock sl(getCallbackLock());
		const juce::SpinLock::ScopedLockType lock(m_eepromLock);
		if (m_haveEeprom) {
			eeprom = m_pendingEeprom;
		} else {
			eeprom = m_machine.saveEeprom();
			board = m_machine.snapshot();
		}
	}
	state.setProperty("eeprom", juce::var(juce::MemoryBlock(eeprom.data(), eeprom.size())), nullptr);
	if (!board.empty())
		state.setProperty("board", juce::var(juce::MemoryBlock(board.data(), board.size())), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void BassStationProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (xml == nullptr || !xml->hasTagName(m_params.state.getType()))
		return;
	auto state = juce::ValueTree::fromXml(*xml);
	m_modelVersion = state.getProperty("model", kModelVersion);
	std::vector<uint8_t> eeprom;
	if (auto *block = state.getProperty("eeprom").getBinaryData(); block != nullptr) {
		const auto *p = static_cast<const uint8_t *>(block->getData());
		eeprom.assign(p, p + block->getSize());
	}
	std::vector<uint8_t> board;
	if (auto *block = state.getProperty("board").getBinaryData(); block != nullptr) {
		const auto *p = static_cast<const uint8_t *>(block->getData());
		board.assign(p, p + block->getSize());
	}
	for (const char *prop : {"eeprom", "board", "filterIn", "model"}) // filterIn: the old bar switch
		state.removeProperty(prop, nullptr);
	m_params.replaceState(state);
	// The unit as the session left it; a session without one (or from another firmware) boots from
	// its EEPROM. Hosts load a state while playing: not in the middle of a block.
	if (!board.empty()) {
		const juce::ScopedLock sl(getCallbackLock());
		if (m_machine.restore(board)) {
			const juce::SpinLock::ScopedLockType lock(m_eepromLock);
			m_haveEeprom = false;
			return;
		}
	}
	if (eeprom.size() == bsrack::Machine::kEepromSize) {
		const juce::SpinLock::ScopedLockType lock(m_eepromLock);
		m_pendingEeprom = eeprom;
		m_haveEeprom = true;
	}
}
