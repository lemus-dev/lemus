#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>

namespace {
// MAME's names for the knobs, in its order (ADC channel * 4 + multiplexer). docs/firmware.md.
const char *const kKnobNames[virusc::kKnobs] = {
	"LFOs Rate", "Effects Send", "Master Volume", "Osc Shape", "Effects Delay Time", "Effects Intensity", "Effects Type Mix", "Effects Feedback",
	"Mix Osc Bal", "Soft Knob 1", "Soft Knob 2", "Mix Sub Osc", "Osc Wave", "Osc Detune", "Osc FM Amount", "Osc Semitone",
	"Amp Attack", "Filters Attack", "Filters Cutoff 2", "Amp Decay", "Mix Osc Vol", "Filters Cutoff", "Mix Noise", "Mix Ring Mod",
	"Filters Release", "Filters Balance", "Amp Sustain", "Amp Release", "Filters Decay", "Filters Sustain", "Filters Resonance", "Filters Env Amount"};

juce::String knobId(int i) { return "knob" + juce::String(i); }

constexpr float kSilence = 1e-6f;              // -120 dB
constexpr int kDacFlush = 64;                  // frames of muted DACs that flush the resamplers (2 x 25 taps)
constexpr int kSleepFrames = int(virusc::kSampleRate / 10);
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout VirusCProcessor::layout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout l;
	for (int i = 0; i < virusc::kKnobs; ++i)
		l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(knobId(i), 1), kKnobNames[i], 0, 127, i == 2 ? 127 : 64));
	return l;
}

VirusCProcessor::VirusCProcessor(const juce::MemoryBlock *rom, lemus::Artwork artwork)
	: AudioProcessor(BusesProperties()
	                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
	                     .withOutput("OUT 2", juce::AudioChannelSet::stereo(), false)
	                     .withOutput("OUT 3", juce::AudioChannelSet::stereo(), false)
	                     .withInput("Input", juce::AudioChannelSet::stereo(), false)),
	  m_artwork(std::move(artwork)), m_params(*this, nullptr, "virusc", layout())
{
	if (rom != nullptr)
		m_rom = *rom;
	for (int i = 0; i < virusc::kKnobs; ++i) {
		m_knobParams[size_t(i)] = m_params.getRawParameterValue(knobId(i));
		m_knobSent[size_t(i)] = i == 2 ? 127 : 64;
	}
	startBoot({});
}

VirusCProcessor::~VirusCProcessor() { stopBoot(); }

void VirusCProcessor::setPowered(bool on)
{
	if (on == m_powered)
		return;
	if (!on) {
		stopBoot();
		const juce::SpinLock::ScopedLockType lock(m_audioLock);
		if (m_machine != nullptr && m_ready.load(std::memory_order_acquire)) {
			m_offRam = m_machine->ram();
			m_offFlash = m_machine->flashChanges();
		} else { // still booting: what it was booting with
			m_offRam = m_bootRam;
			m_offFlash = m_bootFlash;
		}
		m_ready = false;
		m_powered = false;
		return;
	}
	m_powered = true;
	startBoot(m_offRam, m_offFlash);
}

void VirusCProcessor::stopBoot()
{
	m_cancelBoot = true;
	if (m_boot.joinable())
		m_boot.join();
	m_cancelBoot = false;
}

// The firmware from reset, as fast as it runs, until it listens to MIDI.
void VirusCProcessor::startBoot(const std::vector<uint8_t> &ram, const std::vector<uint8_t> &flash)
{
	stopBoot();
	m_bootRam = ram;
	m_bootFlash = flash;
	{
		const juce::SpinLock::ScopedLockType lock(m_audioLock);
		m_ready = false;
	}
	auto machine = std::make_unique<virusc::Machine>();
	if (!machine->setRom(static_cast<const uint8_t *>(m_rom.getData()), m_rom.getSize()))
		return;
	m_machine = std::move(machine);
	if (ram.size() == m_machine->ram().size())
		m_machine->ram() = ram;
	// the flash's sectors the firmware wrote (STORE TO FLASH), over the ROM as it was loaded
	m_machine->setFlashChanges(flash);
	m_boot = std::thread([this] {
		auto &m = *m_machine;
		m.powerOn();
		const uint64_t limit = uint64_t(60 * virusc::kSampleRate);
		while (!m_cancelBoot && m.frames() < limit && !(m.mcu().scon() & 0x10))
			m.runFrame();
		// the firmware loads and shows its first patch just after it opens the UART
		for (int i = 0; i < int(virusc::kSampleRate / 2) && !m_cancelBoot; ++i)
			m.runFrame();
		if (!m_cancelBoot)
			m.warmUp();
		if (!m_cancelBoot)
			m_ready.store(true, std::memory_order_release);
	});
}

void VirusCProcessor::waitForBoot()
{
	if (m_boot.joinable())
		m_boot.join();
}

bool VirusCProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto offMonoStereo = [](const juce::AudioChannelSet &set) {
		return set.isDisabled() || set == juce::AudioChannelSet::mono() || set == juce::AudioChannelSet::stereo();
	};
	if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
		return false;
	for (int b = 1; b < layouts.outputBuses.size(); ++b)
		if (!offMonoStereo(layouts.outputBuses.getReference(b)))
			return false;
	for (const auto &set : layouts.inputBuses)
		if (!offMonoStereo(set))
			return false;
	return true;
}

void VirusCProcessor::prepareToPlay(double sampleRate, int)
{
	m_rate = sampleRate;
	for (int pair = 0; pair < virusc::Machine::kOutputs / 2; ++pair)
		m_pairOn[size_t(pair)] = pair == 0 || (pair < getBusCount(false) && getChannelCountOfBus(false, pair) > 0);
	// VIRUSC_NO_DAC=1: the DSP's samples as they are, through a plain resampler (for comparisons)
	m_dacOn = std::getenv("VIRUSC_NO_DAC") == nullptr;
	for (auto &r : m_outRes)
		if (m_dacOn)
			virusc::Dac::filter(r, virusc::kSampleRate, sampleRate);
		else
			r.setup(virusc::kSampleRate, sampleRate);
	for (size_t p = 0; p < m_dac.size(); ++p)
		m_dac[p].reset(0x9e3779b97f4a7c15ull * (p + 1)); // the same noise each time: renders repeat
	m_dacQuiet = m_dacOn ? 0 : kDacFlush;
	for (auto &r : m_inRes)
		r.setup(sampleRate, virusc::kSampleRate);
	resetInput();
	setLatencySamples(0);
}

void VirusCProcessor::resetInput()
{
	for (auto &r : m_inRes)
		r.reset();
	m_inRead = m_inCount = 0;
	m_inPrimed = false;
}

// One frame of the board's jacks through their DACs to the resamplers; with no board, the DSP held at
// silence while asleep. Once every DAC in use has muted and its resampler holds only zeros, the
// output is exactly zero.
void VirusCProcessor::pushDac(const virusc::Machine *m)
{
	bool muted = true;
	for (int p = 0; p < virusc::Machine::kOutputs / 2; ++p) {
		if (!m_pairOn[size_t(p)])
			continue;
		float l = m != nullptr ? m->out(2 * p) : 0.0f, r = m != nullptr ? m->out(2 * p + 1) : 0.0f;
		if (m_dacOn) {
			m_dac[size_t(p)].frame(l, r);
			muted = muted && m_dac[size_t(p)].muted();
		}
		m_outRes[size_t(2 * p)].push(l);
		m_outRes[size_t(2 * p + 1)].push(r);
	}
	m_dacQuiet = muted ? std::min(m_dacQuiet + 1, kDacFlush) : 0;
}

// One sample of the side-chain at the host's rate, to the board's rate.
void VirusCProcessor::pushInput(float left, float right)
{
	m_inRes[0].push(left);
	m_inRes[1].push(right);
	while (!m_inRes[0].needsInput()) {
		const float l = m_inRes[0].pop(), r = m_inRes[1].pop();
		if (m_inCount == kInFifo)
			continue; // the board stopped taking frames (it can't, while awake): drop
		const int at = (m_inRead + m_inCount) % kInFifo;
		m_inFifo[0][size_t(at)] = l;
		m_inFifo[1][size_t(at)] = r;
		++m_inCount;
	}
}

void VirusCProcessor::setKey(int row, int bit, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_keysLock);
	m_keys.push_back({row, bit, down});
}

void VirusCProcessor::publishDisplay()
{
	const juce::SpinLock::ScopedTryLockType lock(m_displayLock);
	if (!lock.isLocked())
		return;
	m_machine->lcd().text(0, m_lcd[0]);
	m_machine->lcd().text(1, m_lcd[1]);
	virusc::Hd44780::State st;
	m_machine->lcd().save(st);
	std::memcpy(m_cgram, st.cgram, sizeof(m_cgram));
	m_lcdOn = st.displayOn != 0;
	m_machine->ledDuty(m_leds);
	m_lfoLeds[0] = m_machine->lfoLed(0);
	m_lfoLeds[1] = m_machine->lfoLed(1);
}

void VirusCProcessor::lcdText(char out[2][17]) const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	std::memcpy(out, m_lcd, sizeof(m_lcd));
}

VirusCProcessor::Lcd VirusCProcessor::lcd() const
{
	Lcd out;
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	std::memcpy(out.text, m_lcd, sizeof(m_lcd));
	std::memcpy(out.cgram, m_cgram, sizeof(m_cgram));
	out.on = m_lcdOn;
	return out;
}

void VirusCProcessor::ledState(float out[virusc::Machine::kLedColumns][virusc::Machine::kLedLines], float lfo[2]) const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	std::memcpy(out, m_leds, sizeof(m_leds));
	lfo[0] = m_lfoLeds[0];
	lfo[1] = m_lfoLeds[1];
}

void VirusCProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	juce::ScopedNoDenormals noDenormals;
	const juce::SpinLock::ScopedTryLockType audio(m_audioLock);
	const int n = buffer.getNumSamples();
	if (!audio.isLocked() || !m_ready.load(std::memory_order_acquire)) {
		buffer.clear();
		midi.clear();
		return;
	}
	auto &m = *m_machine;

	if (int ask = m_ramAsk.load(std::memory_order_acquire); ask == 1) {
		m_ramCopy = m.ram();
		m_flashCopy = m.flashChanges();
		m_ramAsk.store(2, std::memory_order_release);
	}

	bool wake = false;
	{
		const juce::SpinLock::ScopedTryLockType lock(m_keysLock);
		if (lock.isLocked() && !m_keys.empty()) {
			for (const auto &k : m_keys)
				m.setKey(k.row, k.bit, k.down);
			m_keys.clear();
			wake = true;
		}
	}
	for (int i = 0; i < virusc::kKnobs; ++i) {
		const int v = juce::roundToInt(m_knobParams[size_t(i)]->load());
		if (v != m_knobSent[size_t(i)]) {
			m_knobSent[size_t(i)] = v;
			m.setKnob(i, v);
			wake = true;
		}
	}
	if (!midi.isEmpty())
		wake = true;
	if (wake && m_asleep) {
		m_asleep = false;
		m_silentFrames = 0;
	}

	// The jacks' channels in the buffer: OUT 1 on the main output; OUT 2 and OUT 3 where their buses
	// are on (one channel: the jack's left side). The side-chain shares channels with OUT 1, so each
	// sample is read before it is written.
	float *jack[virusc::Machine::kOutputs]{};
	for (int pair = 0; pair < virusc::Machine::kOutputs / 2; ++pair) {
		if (!m_pairOn[size_t(pair)] || pair >= getBusCount(false))
			continue;
		const int channels = getChannelCountOfBus(false, pair);
		for (int side = 0; side < std::min(channels, 2); ++side) {
			const int ch = getChannelIndexInProcessBlockBuffer(false, pair, side);
			if (ch < buffer.getNumChannels())
				jack[2 * pair + side] = buffer.getWritePointer(ch);
		}
	}
	for (int ch = 0; ch < buffer.getNumChannels(); ++ch) // channels of buses that are off or unlisted
		if (std::find(std::begin(jack), std::end(jack), buffer.getReadPointer(ch)) == std::end(jack) &&
		    !(getTotalNumInputChannels() > ch))
			buffer.clear(ch, 0, n);
	const float *inL = nullptr, *inR = nullptr;
	if (getBusCount(true) > 0 && getChannelCountOfBus(true, 0) > 0) {
		inL = buffer.getReadPointer(getChannelIndexInProcessBlockBuffer(true, 0, 0));
		inR = getChannelCountOfBus(true, 0) > 1 ? buffer.getReadPointer(getChannelIndexInProcessBlockBuffer(true, 0, 1)) : inL;
	}
	if ((inL != nullptr) != m_inputWasOn) {
		resetInput();
		m_inputWasOn = inL != nullptr;
		if (inL == nullptr)
			m.setInput(0, 0);
	}

	auto event = midi.begin();
	for (int s = 0; s < n; ++s) {
		if (inL != nullptr) {
			const float l = inL[s], r = inR[s];
			if (std::abs(l) >= kSilence || std::abs(r) >= kSilence) {
				// the inputs are heard: the DSP runs
				m_silentFrames = 0;
				if (m_asleep) {
					m_asleep = false;
					resetInput();
				}
			}
			if (!m_asleep)
				pushInput(l, r);
		}
		for (; event != midi.end() && (*event).samplePosition <= s; ++event) {
			const auto msg = (*event).getMessage();
			const auto *data = msg.getRawData();
			for (int b = 0; b < msg.getRawDataSize(); ++b)
				m.midiIn(data[b]);
			if (msg.isNoteOn())
				++m_notesHeld;
			else if (msg.isNoteOff() && m_notesHeld > 0)
				--m_notesHeld;
			else if (msg.isAllNotesOff() || msg.isAllSoundOff())
				m_notesHeld = 0;
			m_silentFrames = 0;
			m_asleep = false;
		}
		if (m_asleep) {
			m_heldDebt += virusc::kSampleRate / m_rate;
			for (; m_heldDebt >= 1.0 && m_asleep; m_heldDebt -= 1.0) {
				m.runFrameDspHeld();
				if (m.hostAccesses() != m_heldAccesses)
					m_asleep = false;
			}
			if (m_asleep && m_dacQuiet >= kDacFlush) {
				for (auto *j : jack)
					if (j != nullptr)
						j[s] = 0;
				continue;
			}
			if (!m_asleep) {
				m_silentFrames = 0;
				resetInput();
			}
		}
		while (m_outRes[0].needsInput()) {
			if (m_asleep) {
				// the DSP held at silence: the DACs' noise runs on until they mute
				pushDac(nullptr);
				continue;
			}
			if (inL != nullptr) {
				// the next input frame, once a few are waiting (the two rates' steps don't line up)
				if (!m_inPrimed && m_inCount >= 4)
					m_inPrimed = true;
				if (m_inPrimed && m_inCount > 0) {
					m.setInput(m_inFifo[0][size_t(m_inRead)], m_inFifo[1][size_t(m_inRead)]);
					m_inRead = (m_inRead + 1) % kInFifo;
					--m_inCount;
				} else {
					m.setInput(0, 0);
					m_inPrimed = false;
				}
			}
			m.runFrame();
			bool silent = true;
			for (int j = 0; j < virusc::Machine::kOutputs; ++j)
				silent = silent && std::abs(m.out(j)) < kSilence;
			pushDac(&m);
			if (silent) {
				if (++m_silentFrames >= kSleepFrames && m_notesHeld == 0) {
					m_asleep = true;
					m_heldAccesses = m.hostAccesses();
					m_heldDebt = 0;
				}
			} else
				m_silentFrames = 0;
		}
		for (int j = 0; j < virusc::Machine::kOutputs; ++j)
			if (m_pairOn[size_t(j / 2)]) {
				const float y = m_outRes[size_t(j)].pop();
				if (jack[j] != nullptr)
					jack[j][s] = y;
			}
	}

	midi.clear();
	const auto out = m.takeMidiOut();
	if (!out.empty()) {
		for (size_t at = 0; at < out.size();) {
			const int status = out[at];
			if (status == 0xf0) {
				size_t end = at;
				while (end < out.size() && out[end] != 0xf7)
					++end;
				if (end == out.size())
					break;
				midi.addEvent(out.data() + at, int(end - at + 1), 0);
				at = end + 1;
			} else if (status >= 0x80) {
				int len = juce::MidiMessage::getMessageLengthFromFirstByte(uint8_t(status));
				if (at + size_t(len) > out.size())
					break;
				midi.addEvent(out.data() + at, len, 0);
				at += size_t(len);
			} else
				++at;
		}
	}
	publishDisplay();
}

juce::AudioProcessorEditor *VirusCProcessor::createEditor() { return new VirusCEditor(*this); }

void VirusCProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", kModelVersion, nullptr);
	std::vector<uint8_t> ram, flash;
	if (m_ready.load(std::memory_order_acquire)) {
		m_ramAsk.store(1, std::memory_order_release);
		for (int i = 0; i < 400 && m_ramAsk.load(std::memory_order_acquire) != 2; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (m_ramAsk.load(std::memory_order_acquire) == 2) {
			ram = m_ramCopy;
			flash = m_flashCopy;
		} else {
			// no audio running: the machine is ours
			const juce::SpinLock::ScopedLockType lock(m_audioLock);
			ram = m_machine->ram();
			flash = m_machine->flashChanges();
		}
		m_ramAsk.store(0, std::memory_order_release);
	}
	if (!m_powered) { // switched off: what the battery and the flash hold
		ram = m_offRam;
		flash = m_offFlash;
	}
	if (!ram.empty())
		state.setProperty("ram", juce::var(juce::MemoryBlock(ram.data(), ram.size())), nullptr);
	if (!flash.empty())
		state.setProperty("flash", juce::var(juce::MemoryBlock(flash.data(), flash.size())), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void VirusCProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (xml == nullptr || !xml->hasTagName(m_params.state.getType()))
		return;
	auto state = juce::ValueTree::fromXml(*xml);
	m_modelVersion = state.getProperty("model", kModelVersion);
	std::vector<uint8_t> ram;
	if (auto *block = state.getProperty("ram").getBinaryData(); block != nullptr) {
		const auto *p = static_cast<const uint8_t *>(block->getData());
		ram.assign(p, p + block->getSize());
	}
	std::vector<uint8_t> flash;
	if (auto *block = state.getProperty("flash").getBinaryData(); block != nullptr) {
		const auto *p = static_cast<const uint8_t *>(block->getData());
		flash.assign(p, p + block->getSize());
	}
	state.removeProperty("ram", nullptr);
	state.removeProperty("flash", nullptr);
	m_params.replaceState(state);
	for (int i = 0; i < virusc::kKnobs; ++i)
		m_knobSent[size_t(i)] = juce::roundToInt(m_knobParams[size_t(i)]->load());
	m_powered = true;
	startBoot(ram, flash);
}
