#include "PluginProcessor.h"
#include "PluginEditor.h"

#include "xpander/core/warm.h"

#include <chrono>
#include <mutex>

namespace {

constexpr int kRamSize = xpander::MainBoard::kRamSize;

// One warm-up at a time in the process: other instances wait for its result.
std::mutex g_warmMutex;

int midiLength(uint8_t status)
{
	if (status >= 0xf0) {
		switch (status) {
			case 0xf1: case 0xf3: return 2;
			case 0xf2: return 3;
			default: return 1;
		}
	}
	switch (status & 0xf0) {
		case 0xc0: case 0xd0: return 2;
		default: return 3;
	}
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout XpanderProcessor::layout()
{
	juce::AudioProcessorValueTreeState::ParameterLayout l;
	const auto attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return juce::String(v, 1); });
	l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID("volume", 1), "Volume", juce::NormalisableRange<float>(0.0f, 10.0f),
	                                                  8.0f, attrs));
	return l;
}

XpanderProcessor::XpanderProcessor(const std::vector<const juce::MemoryBlock *> &main,
                                   const std::vector<const juce::MemoryBlock *> &voice, lemus::Artwork artwork,
                                   const juce::MemoryBlock *patches)
	: AudioProcessor([] {
		  auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
		  for (const char *n : {"VOICE 1", "VOICE 2", "VOICE 3", "VOICE 4", "VOICE 5", "VOICE 6"})
			  b = b.withOutput(n, juce::AudioChannelSet::mono(), false);
		  return b;
	  }()),
	  m_params(*this, nullptr, "XPANDER", layout()), m_artwork(std::move(artwork))
{
	m_volume = m_params.getRawParameterValue("volume");
	m_queue.reserve(256);
	m_midiOutBytes.reserve(256);
	m_romOk = main.size() == 4 && voice.size() == 2;
	for (const auto *b : main)
		m_romOk = m_romOk && b != nullptr && b->getSize() == 8192;
	for (const auto *b : voice)
		m_romOk = m_romOk && b != nullptr && b->getSize() == 8192;
	if (!m_romOk)
		return;
	for (const auto *b : main)
		m_mainRom.insert(m_mainRom.end(), static_cast<const uint8_t *>(b->getData()), static_cast<const uint8_t *>(b->getData()) + 8192);
	for (const auto *b : voice)
		m_voiceRom.insert(m_voiceRom.end(), static_cast<const uint8_t *>(b->getData()), static_cast<const uint8_t *>(b->getData()) + 8192);
	if (patches != nullptr)
		m_patches.assign(static_cast<const uint8_t *>(patches->getData()), static_cast<const uint8_t *>(patches->getData()) + patches->getSize());

	juce::MemoryBlock cached;
	if (warmCacheFile().loadFileAsData(cached) && cached.getSize() == size_t(kRamSize)) {
		const auto *p = static_cast<const uint8_t *>(cached.getData());
		const std::vector<uint8_t> ram(p, p + kRamSize);
		m_machine = newMachine(&ram);
	} else {
		m_machine = newMachine(nullptr);
		startWarmUp();
	}
}

XpanderProcessor::~XpanderProcessor()
{
	m_cancelWarm = true;
	if (m_warmThread.joinable())
		m_warmThread.join();
}

juce::File XpanderProcessor::warmCacheFile() const
{
	return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
#if JUCE_MAC
	    .getChildFile("Application Support")
#endif
	    .getChildFile("Lemus")
	    .getChildFile("xpander")
	    .getChildFile(m_patches.empty() ? xpander::kFactoryFile : xpander::kFactoryPatchesFile);
}

std::unique_ptr<xpander::Machine> XpanderProcessor::newMachine(const std::vector<uint8_t> *ram) const
{
	auto m = std::make_unique<xpander::Machine>(m_mainRom.data(), m_voiceRom.data());
	m->prepare(m_rate);
	if (ram != nullptr && ram->size() == size_t(kRamSize))
		std::copy(ram->begin(), ram->end(), m->main().ram().begin());
	m->main().setMemoryProtect(m_protect.load());
	xpander::boot(*m);
	return m;
}

void XpanderProcessor::startWarmUp()
{
	m_warming = true;
	m_warmThread = std::thread([this] {
		std::unique_lock<std::mutex> lock(g_warmMutex, std::defer_lock);
		while (!lock.try_lock()) {
			if (m_cancelWarm)
				return;
			std::this_thread::sleep_for(std::chrono::milliseconds(200));
		}
		std::vector<uint8_t> ram;
		juce::MemoryBlock cached;
		if (warmCacheFile().loadFileAsData(cached) && cached.getSize() == size_t(kRamSize)) {
			// Another instance did it while this one waited.
			const auto *p = static_cast<const uint8_t *>(cached.getData());
			ram.assign(p, p + kRamSize);
		} else {
			xpander::Machine m(m_mainRom.data(), m_voiceRom.data());
			m.prepare(48000);
			const auto result = xpander::warmUp(m, m_mainRom.data(), [this] { return m_cancelWarm.load(); }, m_patches);
			if (m_cancelWarm)
				return;
			ram.assign(m.main().ram().begin(), m.main().ram().end());
			if (result.ok) {
				auto f = warmCacheFile();
				f.getParentDirectory().createDirectory();
				f.replaceWithData(ram.data(), ram.size());
			}
		}
		auto next = newMachine(&ram);
		{
			const juce::SpinLock::ScopedLockType l(m_swapLock);
			m_pending = std::move(next);
		}
		m_warming = false;
	});
}

void XpanderProcessor::waitForWarmUp()
{
	if (m_warmThread.joinable())
		m_warmThread.join();
}

bool XpanderProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	for (int b = 1; b < layouts.outputBuses.size(); ++b) {
		const auto &set = layouts.outputBuses.getReference(b);
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono())
			return false;
	}
	return layouts.inputBuses.isEmpty();
}

void XpanderProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	m_scratch.assign(size_t(std::max(samplesPerBlock, 1)) * 8, 0.0f);
	const juce::SpinLock::ScopedLockType l(m_swapLock);
	if (m_machine)
		m_machine->prepare(sampleRate);
	setLatencySamples(m_machine ? juce::roundToInt(m_machine->latencySamples()) : 0);
}

void XpanderProcessor::setSwitch(int column, int row, bool down)
{
	const juce::SpinLock::ScopedLockType l(m_queueLock);
	m_queue.push_back({PanelEvent::Switch, column, row, down});
}

void XpanderProcessor::turnEncoder(int encoder, int steps)
{
	const juce::SpinLock::ScopedLockType l(m_queueLock);
	m_queue.push_back({PanelEvent::Encoder, encoder, steps, false});
}

void XpanderProcessor::setMemoryProtect(bool on) { m_protect = on; }

void XpanderProcessor::setPowered(bool on)
{
	if (on == m_powered.load())
		return;
	if (on && m_romOk) {
		// The firmware starts again from the battery RAM (built here, swapped in by the audio thread).
		const auto r = ram();
		auto next = newMachine(&r);
		const juce::SpinLock::ScopedLockType l(m_swapLock);
		m_pending = std::move(next);
	}
	m_powered = on;
}

void XpanderProcessor::feedMidi(xpander::Machine &m, const juce::MidiMessage &msg)
{
	const auto *d = msg.getRawData();
	for (int i = 0; i < msg.getRawDataSize(); ++i)
		m.main().midiIn(d[i]);
}

void XpanderProcessor::collectMidiOut(xpander::Machine &m, juce::MidiBuffer &out, int sample)
{
	auto &bytes = m.main().midiOut();
	for (const auto b : bytes) {
		if (b >= 0xf8) {
			out.addEvent(&b, 1, sample); // real-time bytes stand alone
			continue;
		}
		if (b >= 0x80 && !(m_midiOutBytes.size() > 0 && m_midiOutBytes[0] == 0xf0 && b == 0xf7))
			m_midiOutBytes.clear();
		m_midiOutBytes.push_back(b);
		const uint8_t status = m_midiOutBytes[0];
		if (status < 0x80) {
			m_midiOutBytes.clear(); // data with no status: dropped
			continue;
		}
		if (status == 0xf0) {
			if (b == 0xf7) {
				out.addEvent(m_midiOutBytes.data(), int(m_midiOutBytes.size()), sample);
				m_midiOutBytes.clear();
			}
			continue;
		}
		if (int(m_midiOutBytes.size()) == midiLength(status)) {
			out.addEvent(m_midiOutBytes.data(), int(m_midiOutBytes.size()), sample);
			m_midiOutBytes.resize(1); // running status
			if (status >= 0xf0)
				m_midiOutBytes.clear();
		}
	}
	bytes.clear();
}

void XpanderProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	{
		const juce::SpinLock::ScopedTryLockType l(m_swapLock);
		if (l.isLocked() && m_pending) {
			m_machine = std::move(m_pending);
			m_machine->prepare(m_rate);
		}
	}
	if (!m_romOk || !m_machine || !m_powered.load()) {
		buffer.clear();
		midi.clear();
		if (!m_powered.load()) {
			const juce::SpinLock::ScopedTryLockType l(m_displayLock);
			if (l.isLocked()) {
				m_display.vfd = {};
				m_display.led1 = m_display.led2 = 0xff;
			}
		}
		return;
	}
	auto &m = *m_machine;
	m.setVolume(double(m_volume->load()) / 10.0);
	m.main().setMemoryProtect(m_protect.load());
	{
		const juce::SpinLock::ScopedLockType l(m_queueLock);
		for (const auto &e : m_queue) {
			if (e.kind == PanelEvent::Switch) {
				m.main().setSwitch(e.a, e.b, e.down);
				auto &c = m_held[size_t(e.a & 7)];
				c = uint8_t(e.down ? (c | 1 << e.b) : (c & ~(1 << e.b)));
			} else
				m.main().turnEncoder(e.a, e.b);
		}
		m_queue.clear();
	}

	auto mainBus = getBusBuffer(buffer, false, 0);
	float *left = mainBus.getWritePointer(0);
	float *right = mainBus.getNumChannels() > 1 ? mainBus.getWritePointer(1) : nullptr;
	if (m_scratch.size() < size_t(n) * 8)
		m_scratch.assign(size_t(n) * 8, 0.0f);
	float *scratchR = m_scratch.data();
	std::array<float *, xpander::Machine::kVoices> voices{};
	bool anyVoice = false;
	for (int v = 0; v < xpander::Machine::kVoices; ++v) {
		if (v + 1 < getBusCount(false)) {
			auto b = getBusBuffer(buffer, false, v + 1);
			if (b.getNumChannels() > 0) {
				voices[size_t(v)] = b.getWritePointer(0);
				anyVoice = true;
			}
		}
		if (voices[size_t(v)] == nullptr)
			voices[size_t(v)] = m_scratch.data() + size_t(n) * size_t(2 + v);
	}

	juce::MidiBuffer out;
	int pos = 0;
	auto run = [&](int to) {
		if (to <= pos)
			return;
		float *r = right != nullptr ? right + pos : scratchR + pos;
		std::array<float *, xpander::Machine::kVoices> vo{};
		for (int v = 0; v < xpander::Machine::kVoices; ++v)
			vo[size_t(v)] = voices[size_t(v)] + pos;
		m.process(left + pos, r, to - pos, anyVoice ? vo.data() : nullptr);
		pos = to;
	};
	for (const auto meta : midi) {
		run(juce::jlimit(0, n, meta.samplePosition));
		feedMidi(m, meta.getMessage());
	}
	run(n);
	m_asleep.store(m.asleep(), std::memory_order_relaxed);
	collectMidiOut(m, out, juce::jmax(0, n - 1));
	midi.swapWith(out);
	if (right == nullptr)
		for (int i = 0; i < n; ++i)
			left[i] = 0.5f * (left[i] + scratchR[i]);

	const juce::SpinLock::ScopedTryLockType l(m_displayLock);
	if (l.isLocked()) {
		m_display.vfd = m.main().display();
		m_display.led1 = m.main().led1();
		m_display.led2 = m.main().led2();
		m_display.switches = m_held;
		m_solvers = m.solverStats();
	}
}

xpander::Solvers XpanderProcessor::solverStats() const
{
	const juce::SpinLock::ScopedLockType l(m_displayLock);
	return m_solvers;
}

XpanderProcessor::Display XpanderProcessor::display() const
{
	const juce::SpinLock::ScopedLockType l(m_displayLock);
	return m_display;
}

std::string XpanderProcessor::displayText(int d) const
{
	if (!m_romOk)
		return {};
	return xpander::displayText(m_mainRom.data(), display().vfd, d);
}

juce::String XpanderProcessor::status() const
{
	if (!m_romOk)
		return "ROMs missing";
	if (m_warming)
		return "TUNING: first start, the unit resets its memory and tunes itself";
	return {};
}

std::vector<uint8_t> XpanderProcessor::ram() const
{
	const juce::SpinLock::ScopedLockType l(m_swapLock);
	if (m_pending)
		return {m_pending->main().ram().begin(), m_pending->main().ram().end()};
	if (!m_machine)
		return {};
	return {m_machine->main().ram().begin(), m_machine->main().ram().end()};
}

void XpanderProcessor::setRam(const std::vector<uint8_t> &ram)
{
	if (!m_romOk)
		return;
	m_cancelWarm = true;
	if (m_warmThread.joinable())
		m_warmThread.join();
	m_warming = false;
	auto next = newMachine(&ram);
	const juce::SpinLock::ScopedLockType l(m_swapLock);
	m_pending = std::move(next);
}

void XpanderProcessor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", m_modelVersion, nullptr);
	state.setProperty("protect", m_protect.load(), nullptr);
	state.setProperty("power", m_powered.load(), nullptr);
	const auto r = ram();
	if (!r.empty())
		state.setProperty("ram", juce::MemoryBlock(r.data(), r.size()).toBase64Encoding(), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void XpanderProcessor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (xml == nullptr || !xml->hasTagName(m_params.state.getType()))
		return;
	auto state = juce::ValueTree::fromXml(*xml);
	m_modelVersion = int(state.getProperty("model", kModelVersion));
	m_protect = bool(state.getProperty("protect", false));
	m_powered = bool(state.getProperty("power", true));
	juce::MemoryBlock block;
	std::vector<uint8_t> r;
	if (block.fromBase64Encoding(state.getProperty("ram").toString()) && block.getSize() == size_t(kRamSize)) {
		const auto *p = static_cast<const uint8_t *>(block.getData());
		r.assign(p, p + kRamSize);
	}
	state.removeProperty("model", nullptr);
	state.removeProperty("protect", nullptr);
	state.removeProperty("power", nullptr);
	state.removeProperty("ram", nullptr);
	m_params.replaceState(state);
	if (!r.empty())
		setRam(r);
}

juce::AudioProcessorEditor *XpanderProcessor::createEditor() { return new XpanderEditor(*this); }
