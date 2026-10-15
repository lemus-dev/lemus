#include "Player.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace lemus::host {

namespace {

// A plugin state for setStateInformation on a hosted plugin. Accepts what a host saved before
// (JUCE's format for VST3: a VST3PluginState element), Onyx Tracker's ("OV3S", then the
// component's and the controller's state, each after a little-endian 32-bit length), or a VST3
// component's bare state.
juce::MemoryBlock hostState(const juce::MemoryBlock &data, bool vst3)
{
	if (!vst3 || juce::AudioProcessor::getXmlFromBinary(data.getData(), int(data.getSize())) != nullptr)
		return data;
	juce::XmlElement xml("VST3PluginState");
	const auto *b = static_cast<const uint8_t *>(data.getData());
	auto u32 = [&](size_t at) { return uint32_t(b[at]) | uint32_t(b[at + 1]) << 8 | uint32_t(b[at + 2]) << 16 | uint32_t(b[at + 3]) << 24; };
	if (data.getSize() >= 12 && std::memcmp(b, "OV3S", 4) == 0) {
		const size_t a = u32(4);
		if (8 + a + 4 <= data.getSize()) {
			const size_t c = u32(8 + a);
			xml.createNewChildElement("IComponent")->addTextElement(juce::MemoryBlock(b + 8, a).toBase64Encoding());
			if (c > 0 && 12 + a + c <= data.getSize())
				xml.createNewChildElement("IEditController")->addTextElement(juce::MemoryBlock(b + 12 + a, c).toBase64Encoding());
		}
	} else
		xml.createNewChildElement("IComponent")->addTextElement(data.toBase64Encoding());
	juce::MemoryBlock out;
	juce::AudioProcessor::copyXmlToBinary(xml, out);
	return out;
}

} // namespace

juce::File findPlugin(const juce::String &name)
{
	if (juce::File::isAbsolutePath(name))
		return juce::File(name);
	const auto home = juce::File::getSpecialLocation(juce::File::userHomeDirectory);
	const bool au = name.startsWith("au:");
	const juce::String base = au ? name.fromFirstOccurrenceOf("au:", false, false) : name;
	const juce::File dirs[] = {home.getChildFile("Library/Audio/Plug-Ins"), juce::File("/Library/Audio/Plug-Ins")};
	for (const auto &d : dirs) {
		const auto f = au ? d.getChildFile("Components/" + base + ".component") : d.getChildFile("VST3/" + base + ".vst3");
		if (f.exists())
			return f;
	}
	return {};
}

Player::Player(juce::AudioPluginFormatManager &formats, const Scene &scene, const Source &source, bool realtime)
	: m_scene(scene), m_source(source)
{
	if (source.audio != juce::File()) {
		// A recording: read it whole, resampled to the scene's rate if it isn't at it.
		juce::AudioFormatManager af;
		af.registerBasicFormats();
		std::unique_ptr<juce::AudioFormatReader> r{af.createReaderFor(source.audio)};
		if (r == nullptr) {
			m_error = "can't read " + source.audio.getFullPathName();
			return;
		}
		juce::AudioBuffer<float> raw(int(std::max(2u, r->numChannels)), int(r->lengthInSamples));
		r->read(&raw, 0, int(r->lengthInSamples), 0, true, true);
		if (r->numChannels == 1)
			raw.copyFrom(1, 0, raw, 0, 0, raw.getNumSamples());
		const double ratio = r->sampleRate / scene.rate;
		m_recording.setSize(2, int(raw.getNumSamples() / ratio));
		for (int c = 0; c < 2; ++c) {
			juce::LagrangeInterpolator li;
			li.process(ratio, raw.getReadPointer(c), m_recording.getWritePointer(c), m_recording.getNumSamples());
		}
		return;
	}
	const auto file = findPlugin(source.plugin);
	if (!file.exists()) {
		m_error = "plugin not found: " + source.plugin;
		return;
	}
	juce::OwnedArray<juce::PluginDescription> types;
	for (auto *format : formats.getFormats())
		if (format->fileMightContainThisPluginType(file.getFullPathName()))
			format->findAllTypesForFile(types, file.getFullPathName());
	if (types.isEmpty()) {
		m_error = "no plugin in " + file.getFullPathName();
		return;
	}
	m_plugin = formats.createPluginInstance(*types[0], scene.rate, scene.block, m_error);
	if (m_plugin == nullptr)
		return;
	m_error.clear();

	// Stereo out (and in, for effects) where the plugin allows it.
	auto layout = m_plugin->getBusesLayout();
	for (auto &bus : layout.outputBuses)
		bus = juce::AudioChannelSet::stereo();
	for (auto &bus : layout.inputBuses)
		bus = juce::AudioChannelSet::stereo();
	if (!m_plugin->setBusesLayout(layout))
		m_warnings.add("kept the plugin's own bus layout");

	const bool vst3 = types[0]->pluginFormatName == "VST3";
	if (source.state != juce::File()) {
		juce::MemoryBlock block;
		if (!source.state.loadFileAsData(block)) {
			m_error = "can't read state " + source.state.getFullPathName();
			m_plugin.reset();
			return;
		}
		block = hostState(block, vst3);
		m_plugin->setStateInformation(block.getData(), int(block.getSize()));
	}
	if (source.machine.isNotEmpty()) {
		// Lemus and Lemus FX: a saved state naming the machine loads it (host/LemusProcessor).
		if (!vst3) {
			m_error = "picking a Lemus machine needs the VST3";
			m_plugin.reset();
			return;
		}
		juce::ValueTree lemus("LEMUS");
		lemus.setProperty("version", 1, nullptr);
		lemus.setProperty("machine", source.machine, nullptr);
		juce::MemoryBlock raw;
		{
			juce::MemoryOutputStream out(raw, false);
			lemus.writeToStream(out);
		}
		const auto block = hostState(raw, true);
		m_plugin->setStateInformation(block.getData(), int(block.getSize()));
		// Lemus renames its slots to the machine's parameters; the host hears of it through the
		// message loop.
		juce::MessageManager::getInstance()->runDispatchLoopUntil(300);
	}
	m_plugin->setNonRealtime(!realtime);
	m_plugin->setPlayHead(this);
	m_plugin->prepareToPlay(scene.rate, scene.block);

	// Parameters: the scene's (generic names), then the source's own (the plugin's names).
	for (const auto &[name, value] : scene.params)
		if (auto *p = findParameter(name))
			setValue(p, value);
		else
			m_warnings.add("no parameter for \"" + name + "\"");
	for (const auto &[name, value] : source.params)
		if (auto *p = findParameter(name))
			setValue(p, value);
		else
			m_warnings.add("no parameter \"" + name + "\"");
	for (const auto &a : scene.automation)
		if (auto *p = findParameter(a.param))
			m_automation.push_back({&a, p});
		else
			m_warnings.add("no parameter to automate for \"" + a.param + "\"");

	// Notes as MIDI events; at the same sample, note-offs go first (overlap the notes to slide)
	// unless the scene asks for note-ons first.
	const double spb = scene.samplesPerBeat();
	for (const auto &n : scene.notes) {
		m_events.push_back({int64_t(std::llround(n.at * spb)), juce::MidiMessage::noteOn(1, n.note, juce::uint8(n.velocity))});
		m_events.push_back({int64_t(std::llround((n.at + n.len) * spb)), juce::MidiMessage::noteOff(1, n.note)});
	}
	std::stable_sort(m_events.begin(), m_events.end(), [&scene](const auto &a, const auto &b) {
		if (a.first != b.first)
			return a.first < b.first;
		return scene.onsFirst ? (a.second.isNoteOn() && !b.second.isNoteOn()) : (a.second.isNoteOff() && !b.second.isNoteOff());
	});

	if (scene.input.existsAsFile()) {
		juce::AudioFormatManager af;
		af.registerBasicFormats();
		if (std::unique_ptr<juce::AudioFormatReader> r{af.createReaderFor(scene.input)}) {
			m_input.setSize(int(r->numChannels), int(r->lengthInSamples));
			r->read(&m_input, 0, int(r->lengthInSamples), 0, true, true);
			if (std::abs(r->sampleRate - scene.rate) > 0.5)
				m_warnings.add("input is at " + juce::String(r->sampleRate) + " Hz, played as is");
		} else
			m_warnings.add("can't read input " + scene.input.getFullPathName());
	}
	const int ch = std::max(m_plugin->getTotalNumInputChannels(), m_plugin->getTotalNumOutputChannels());
	m_work.setSize(ch, scene.block);
}

Player::~Player()
{
	if (m_plugin != nullptr)
		m_plugin->releaseResources();
}

int Player::numOutputs() const { return isRecording() ? 2 : m_plugin ? m_plugin->getTotalNumOutputChannels() : 0; }

void Player::setRealtime(bool realtime)
{
	if (m_plugin != nullptr)
		m_plugin->setNonRealtime(!realtime);
}
int Player::latency() const { return m_plugin ? m_plugin->getLatencySamples() : 0; }
int64_t Player::length() const { return int64_t(std::llround(m_scene.seconds() * m_scene.rate)); }

juce::AudioProcessorParameter *Player::findParameter(const juce::String &generic) const
{
	if (m_plugin == nullptr)
		return nullptr;
	const auto it = m_source.names.find(generic);
	const juce::String name = it != m_source.names.end() ? it->second : generic;
	for (auto *p : m_plugin->getParameters())
		if (p->getName(256).equalsIgnoreCase(name))
			return p;
	return nullptr;
}

void Player::setValue(juce::AudioProcessorParameter *p, const juce::var &v)
{
	const float x = v.isString() ? p->getValueForText(v.toString()) : float(double(v));
	p->setValueNotifyingHost(juce::jlimit(0.0f, 1.0f, x));
}

void Player::reset()
{
	if (m_plugin == nullptr)
		return;
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
	m_work.clear();
	juce::AudioBuffer<float> view(m_work.getArrayOfWritePointers(), m_work.getNumChannels(), 0, 1);
	m_plugin->processBlock(view, midi);
	m_next = 0;
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Player::getPosition() const
{
	PositionInfo info;
	const double seconds = double(m_now) / m_scene.rate;
	info.setBpm(m_scene.bpm);
	info.setTimeSignature(TimeSignature{4, 4});
	info.setTimeInSamples(m_now);
	info.setTimeInSeconds(seconds);
	info.setPpqPosition(seconds * m_scene.bpm / 60.0);
	info.setPpqPositionOfLastBarStart(std::floor(seconds * m_scene.bpm / 60.0 / 4.0) * 4.0);
	info.setIsPlaying(m_scene.playing);
	return info;
}

void Player::process(juce::AudioBuffer<float> &out, int64_t position)
{
	out.clear();
	if (isRecording()) {
		const float gain = juce::Decibels::decibelsToGain(float(m_source.gainDb));
		const int n = int(juce::jlimit<int64_t>(0, out.getNumSamples(), m_recording.getNumSamples() - position));
		for (int c = 0; c < std::min(2, out.getNumChannels()) && n > 0; ++c)
			out.addFrom(c, 0, m_recording, c, int(position), n, gain);
		m_now = position + out.getNumSamples();
		return;
	}
	if (m_plugin == nullptr)
		return;
	if (position < m_now || m_next > m_events.size())
		m_next = 0;
	while (m_next < m_events.size() && m_events[m_next].first < position)
		++m_next;
	const int total = out.getNumSamples();
	// Whole blocks unless something is automated: some plugins work out their envelopes per block.
	const int step = m_automation.empty() ? m_work.getNumSamples() : std::max(1, m_scene.automationStep);
	const double spb = m_scene.samplesPerBeat();
	for (int done = 0; done < total;) {
		const int n = std::min({step, total - done, m_work.getNumSamples()});
		m_now = position + done;
		for (const auto &[a, p] : m_automation)
			p->setValue(juce::jlimit(0.0f, 1.0f, float(a->valueAt(double(m_now) / spb))));
		juce::MidiBuffer midi;
		while (m_next < m_events.size() && m_events[m_next].first < m_now + n) {
			midi.addEvent(m_events[m_next].second, int(m_events[m_next].first - m_now));
			++m_next;
		}
		m_work.clear();
		for (int c = 0; c < std::min(m_input.getNumChannels(), m_plugin->getTotalNumInputChannels()); ++c)
			for (int i = 0; i < n; ++i) {
				const int64_t at = m_now + i;
				m_work.setSample(c, i, at < m_input.getNumSamples() ? m_input.getSample(c, int(at)) : 0.0f);
			}
		juce::AudioBuffer<float> view(m_work.getArrayOfWritePointers(), m_work.getNumChannels(), 0, n);
		m_plugin->processBlock(view, midi);
		if (m_source.clipLevel > 0) {
			const bool hard = m_source.clipHard;
			const auto shape = [hard](double v) { return hard ? std::clamp(v, -1.0, 1.0) : std::tanh(v); };
			const double level = m_source.clipLevel, bias = m_source.clipBias, offset = shape(bias);
			for (int c = 0; c < view.getNumChannels(); ++c)
				for (auto &x : juce::Span(view.getWritePointer(c), size_t(n)))
					x = float(level * (shape(double(x) / level + bias) - offset));
		}
		const float gain = juce::Decibels::decibelsToGain(float(m_source.gainDb));
		for (int c = 0; c < std::min(out.getNumChannels(), m_work.getNumChannels()); ++c)
			out.addFrom(c, done, m_work, c, 0, n, gain);
		done += n;
	}
	m_now = position + total;
}

} // namespace lemus::host
