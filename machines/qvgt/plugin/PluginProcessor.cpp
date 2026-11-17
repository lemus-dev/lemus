#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <chrono>
#include <cmath>
#include <cstring>

namespace {
// The INPUT and OUTPUT pots' rotation 0-10 (the panel's scale) as 0-1; the stages around them are
// the unit's (core/unit.h).
double rotation(float r) { return juce::jlimit(0.0, 1.0, double(r) / 10.0); }
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout QVGTProcessor::layout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout l;
	const auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 1); });
	l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("input", 1), "Input", juce::NormalisableRange<float>(0.0f, 10.0f), 6.0f, attrs));
	l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("output", 1), "Output", juce::NormalisableRange<float>(0.0f, 10.0f), 5.0f, attrs));
	// Not a panel control: the circuits' own noise (the bar's HISS)
	l.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID("hiss", 1), "Hiss", true));
	return l;
}

QVGTProcessor::QVGTProcessor(const juce::MemoryBlock *rom, const juce::MemoryBlock *lcdFont, lemus::Artwork artwork)
	: AudioProcessor(BusesProperties().withInput("Input", juce::AudioChannelSet::stereo(), true).withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  m_artwork(std::move(artwork)), m_params(*this, nullptr, "qvgt", layout())
{
	m_midiOut.reserve(8192);
	m_input = m_params.getRawParameterValue("input");
	m_output = m_params.getRawParameterValue("output");
	m_hiss = m_params.getRawParameterValue("hiss");
	if (rom != nullptr) {
		const auto *p = static_cast<const uint8_t *>(rom->getData());
		std::vector<uint8_t> image(p, p + rom->getSize());
		if (qvgt::Board::prepareRom(image))
			m_rom = std::move(image);
	}
	if (lcdFont != nullptr && lcdFont->getSize() == 4096) {
		const auto *p = static_cast<const uint8_t *>(lcdFont->getData());
		m_lcdFont.assign(p, p + 4096);
	}
	startBoot({});
}

QVGTProcessor::~QVGTProcessor() { stopBoot(); }

void QVGTProcessor::stopBoot()
{
	m_cancelBoot = true;
	if (m_boot.joinable())
		m_boot.join();
	m_cancelBoot = false;
}

// The firmware from reset with `ram` (empty: a new unit, which gets the factory programs), as fast
// as it runs, without audio.
void QVGTProcessor::startBoot(const std::vector<uint8_t> &ram)
{
	stopBoot();
	m_bootRam = ram;
	{
		const juce::SpinLock::ScopedLockType lock(m_audioLock);
		m_ready = false;
	}
	if (!romLoaded())
		return;
	auto unit = std::make_unique<qvgt::Unit>(m_rom);
	if (ram.size() == qvgt::kRamSize)
		std::memcpy(unit->board().ram().data(), ram.data(), qvgt::kRamSize);
	m_unit = std::move(unit);
	const bool factory = ram.size() != qvgt::kRamSize;
	m_boot = std::thread([this, factory] {
		auto &u = *m_unit;
		u.powerOn();
		if (factory && !m_cancelBoot)
			u.recallFactoryPrograms();
		if (m_cancelBoot)
			return;
		u.prepare(m_rate.load());
		applyGains();
		m_ready.store(true, std::memory_order_release);
	});
}

void QVGTProcessor::waitForBoot()
{
	if (m_boot.joinable())
		m_boot.join();
}

void QVGTProcessor::setPowered(bool on)
{
	if (on == m_powered)
		return;
	if (!on) {
		stopBoot();
		const juce::SpinLock::ScopedLockType lock(m_audioLock);
		if (m_unit != nullptr && m_ready.load(std::memory_order_acquire))
			m_offRam.assign(m_unit->board().ram().begin(), m_unit->board().ram().end());
		else
			m_offRam = m_bootRam;
		m_ready = false;
		m_powered = false;
		return;
	}
	m_powered = true;
	startBoot(m_offRam);
}

bool QVGTProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto in = layouts.getMainInputChannelSet();
	return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo() &&
	       (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void QVGTProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	m_silence.assign(size_t(juce::jmax(1, samplesPerBlock)), 0.0f);
	const juce::SpinLock::ScopedLockType lock(m_audioLock);
	if (m_unit != nullptr && m_ready.load(std::memory_order_acquire))
		m_unit->prepare(sampleRate);
	setLatencySamples(m_unit != nullptr ? m_unit->latency() : 0);
}

void QVGTProcessor::applyGains()
{
	m_unit->inputPot = rotation(m_input->load());
	m_unit->outputPot = rotation(m_output->load());
	m_unit->setHiss(m_hiss->load() > 0.5f);
}

void QVGTProcessor::setKey(int key, bool down, bool hard)
{
	const juce::SpinLock::ScopedLockType lock(m_keysLock);
	m_keys.push_back({key, down, hard});
}

void QVGTProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	const juce::SpinLock::ScopedTryLockType audio(m_audioLock);
	if (!audio.isLocked() || !m_ready.load(std::memory_order_acquire)) {
		buffer.clear();
		midi.clear();
		return;
	}
	auto &u = *m_unit;
	auto &board = u.board();

	if (m_ramAsk.load(std::memory_order_acquire) == 1) {
		m_ramCopy.assign(board.ram().begin(), board.ram().end());
		m_ramAsk.store(2, std::memory_order_release);
	}
	{
		const juce::SpinLock::ScopedTryLockType lock(m_keysLock);
		if (lock.isLocked() && !m_keys.empty()) {
			for (const auto &k : m_keys) {
				if (k.key >= kFootswitchKey) {
					board.setFootswitch(k.key - kFootswitchKey, k.down);
					continue;
				}
				if (k.down)
					board.setKeyForce(k.hard ? qvgt::Unit::kHardPress : qvgt::Unit::kLightPress);
				board.setKey(k.key, k.down);
			}
			m_keys.clear();
			u.wake();
		}
	}
	for (const auto event : midi) {
		const auto msg = event.getMessage();
		const auto *data = msg.getRawData();
		for (int b = 0; b < msg.getRawDataSize(); ++b)
			board.midiIn(data[b]);
		u.wake();
	}
	applyGains();

	if (int(m_silence.size()) < n)
		m_silence.assign(size_t(n), 0.0f);
	const int inChannels = getTotalNumInputChannels();
	const float *inL = inChannels > 0 ? buffer.getReadPointer(0) : m_silence.data();
	const float *inR = inChannels > 1 ? buffer.getReadPointer(1) : inL;
	u.process(inL, inR, buffer.getWritePointer(0), buffer.getWritePointer(buffer.getNumChannels() > 1 ? 1 : 0), n);
	for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
		buffer.clear(ch, 0, n);
	m_asleep.store(u.asleep(), std::memory_order_relaxed);

	// MIDI out as the UART sends it: whole messages this block, a message still being sent kept for
	// the next one (with the last status, for running status)
	midi.clear();
	const auto sent = board.takeMidiOut();
	m_midiOut.insert(m_midiOut.end(), sent.begin(), sent.end());
	size_t at = 0;
	while (at < m_midiOut.size()) {
		const uint8_t first = m_midiOut[at];
		if (first == 0xf0) {
			size_t end = at;
			while (end < m_midiOut.size() && m_midiOut[end] != 0xf7)
				++end;
			if (end == m_midiOut.size())
				break; // still coming
			midi.addEvent(m_midiOut.data() + at, int(end - at + 1), 0);
			at = end + 1;
			continue;
		}
		if (first >= 0xf8) { // real time, anywhere
			midi.addEvent(m_midiOut.data() + at, 1, 0);
			++at;
			continue;
		}
		uint8_t msg[3] = {first, 0, 0};
		size_t from = at + 1;
		if (first < 0x80) { // running status
			if (m_midiStatus < 0x80) {
				++at;
				continue;
			}
			msg[0] = m_midiStatus;
			from = at;
		}
		const int len = juce::MidiMessage::getMessageLengthFromFirstByte(msg[0]);
		if (from + size_t(len - 1) > m_midiOut.size())
			break; // still coming
		for (int k = 1; k < len; ++k)
			msg[k] = m_midiOut[from + size_t(k - 1)];
		if (msg[0] < 0xf0)
			m_midiStatus = msg[0];
		midi.addEvent(msg, len, 0);
		at = from + size_t(len - 1);
	}
	m_midiOut.erase(m_midiOut.begin(), m_midiOut.begin() + std::ptrdiff_t(at));
	if (m_midiOut.size() > 4096) // a SysEx with no end
		m_midiOut.clear();
	publishDisplay();
}

void QVGTProcessor::publishDisplay()
{
	const juce::SpinLock::ScopedTryLockType lock(m_displayLock);
	if (!lock.isLocked())
		return;
	auto &board = m_unit->board();
	board.lcd().line(0, m_display.text[0]);
	board.lcd().line(1, m_display.text[1]);
	std::memcpy(m_display.cgram, board.lcd().cgram, sizeof(m_display.cgram));
	m_display.on = board.lcd().displayOn;
	m_display.latch0400 = board.latch(qvgt::Board::kLatch0400);
	m_display.latch0600 = board.latch(qvgt::Board::kLatch0600);
	m_display.latch0800 = board.latch(qvgt::Board::kLatch0800);
	m_display.clip = m_unit->clip();
	m_display.headroom = m_unit->headroom();
	m_solverStats = m_unit->solverStats();
}

QVGTProcessor::Display QVGTProcessor::display() const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	return m_display;
}

qvgt::SolverStats QVGTProcessor::solverStats() const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	return m_solverStats;
}

juce::AudioProcessorEditor *QVGTProcessor::createEditor() { return new QVGTEditor(*this); }

void QVGTProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", kModelVersion, nullptr);
	std::vector<uint8_t> ram;
	if (m_ready.load(std::memory_order_acquire)) {
		m_ramAsk.store(1, std::memory_order_release);
		for (int i = 0; i < 400 && m_ramAsk.load(std::memory_order_acquire) != 2; ++i)
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (m_ramAsk.load(std::memory_order_acquire) == 2)
			ram = m_ramCopy;
		else {
			// no audio running: the unit is ours
			const juce::SpinLock::ScopedLockType lock(m_audioLock);
			ram.assign(m_unit->board().ram().begin(), m_unit->board().ram().end());
		}
		m_ramAsk.store(0, std::memory_order_release);
	} else
		ram = m_bootRam; // still booting: what it is booting with
	if (!m_powered)
		ram = m_offRam;
	state.setProperty("powered", m_powered, nullptr);
	if (!ram.empty())
		state.setProperty("ram", juce::var(juce::MemoryBlock(ram.data(), ram.size())), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void QVGTProcessor::setStateInformation(const void *data, int sizeInBytes)
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
	const bool powered = state.getProperty("powered", true);
	state.removeProperty("ram", nullptr);
	state.removeProperty("powered", nullptr);
	m_params.replaceState(state);
	if (powered) {
		m_powered = true;
		startBoot(ram);
	} else {
		stopBoot();
		const juce::SpinLock::ScopedLockType lock(m_audioLock);
		m_ready = false;
		m_powered = false;
		m_offRam = ram;
	}
}
