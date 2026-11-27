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

	// Notes as MIDI events; at the same sample, program changes (each after its bank select) go
	// first, then controller changes, then note-offs (overlap the notes to slide) unless the scene
	// asks for note-ons first.
	const double spb = scene.samplesPerBeat();
	for (const auto &n : scene.notes) {
		m_events.push_back({int64_t(std::llround(n.at * spb)), juce::MidiMessage::noteOn(1, n.note, juce::uint8(n.velocity))});
		m_events.push_back({int64_t(std::llround((n.at + n.len) * spb)), juce::MidiMessage::noteOff(1, n.note)});
	}
	for (const auto &c : scene.controllers)
		m_events.push_back({int64_t(std::llround(c.at * spb)), juce::MidiMessage::controllerEvent(1, c.cc, c.value)});
	std::vector<std::pair<int64_t, juce::MidiMessage>> programs;
	for (const auto &p : scene.programs) {
		const auto t = int64_t(std::llround(p.at * spb));
		if (p.bank >= 0) {
			programs.push_back({t, juce::MidiMessage::controllerEvent(1, 0, p.bank >> 7)});
			programs.push_back({t, juce::MidiMessage::controllerEvent(1, 32, p.bank & 127)});
		}
		programs.push_back({t, juce::MidiMessage::programChange(1, p.program)});
	}
	m_events.insert(m_events.begin(), programs.begin(), programs.end());
	const auto rank = [](const juce::MidiMessage &m) {
		if (m.isProgramChange() || (m.isController() && (m.getControllerNumber() == 0 || m.getControllerNumber() == 32)))
			return 0;
		return m.isController() ? 1 : 2;
	};
	std::stable_sort(m_events.begin(), m_events.end(), [&scene, &rank](const auto &a, const auto &b) {
		if (a.first != b.first)
			return a.first < b.first;
		if (rank(a.second) != rank(b.second))
			return rank(a.second) < rank(b.second);
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

	// The preroll: the plugin runs, the transport stopped and no notes, and what it plays is
	// dropped (a machine switched on before the scene, past its power-on).
	m_prerolling = true;
	const juce::MidiBuffer none;
	for (int64_t done = 0, total = std::llround(source.preroll * scene.rate); done < total; done += scene.block) {
		m_work.clear();
		juce::AudioBuffer<float> view(m_work.getArrayOfWritePointers(), ch, 0, int(std::min<int64_t>(scene.block, total - done)));
		juce::MidiBuffer midi(none);
		m_plugin->processBlock(view, midi);
	}
	m_prerolling = false;
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
	// Each held note's own note-off (not every plugin takes All Notes Off), then All Notes Off.
	juce::MidiBuffer midi;
	bool held = false;
	for (int k = 0; k < 128; ++k)
		for (; m_held[size_t(k)] > 0; --m_held[size_t(k)])
			midi.addEvent(juce::MidiMessage::noteOff(1, k), 0), held = true;
	midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
	const int64_t total = held ? std::max<int64_t>(1, std::llround(kKeyUp * m_scene.rate)) : 1;
	for (int64_t done = 0; done < total;) {
		const int n = int(std::min<int64_t>(m_work.getNumSamples(), total - done));
		m_work.clear();
		juce::AudioBuffer<float> view(m_work.getArrayOfWritePointers(), m_work.getNumChannels(), 0, n);
		m_plugin->processBlock(view, midi);
		midi.clear();
		done += n;
	}
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
	info.setIsPlaying(m_scene.playing && !m_prerolling);
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
	// A loop's key-up stretch (setLoop): from `release` on, no notes; at it, every held note ends.
	const int64_t release = m_loopEnd < 0 ? std::numeric_limits<int64_t>::max()
	                                      : m_loopEnd - std::min<int64_t>(std::llround(kKeyUp * m_scene.rate), m_loopEnd / 4);
	for (int done = 0; done < total;) {
		int n = std::min({step, total - done, m_work.getNumSamples()});
		m_now = position + done;
		if (m_now < release && m_now + n > release)
			n = int(release - m_now); // a block boundary at the release, so its note-offs go first
		for (const auto &[a, p] : m_automation)
			p->setValue(juce::jlimit(0.0f, 1.0f, float(a->valueAt(double(m_now) / spb))));
		juce::MidiBuffer midi;
		if (m_now >= release)
			for (int k = 0; k < 128; ++k)
				for (; m_held[size_t(k)] > 0; --m_held[size_t(k)])
					midi.addEvent(juce::MidiMessage::noteOff(1, k), 0);
		while (m_next < m_events.size() && m_events[m_next].first < m_now + n) {
			const auto &[at, msg] = m_events[m_next++];
			if (at >= release && (msg.isNoteOn() || msg.isNoteOff()))
				continue;
			if (msg.isNoteOn())
				++m_held[size_t(msg.getNoteNumber())];
			else if (msg.isNoteOff() && m_held[size_t(msg.getNoteNumber())] > 0)
				--m_held[size_t(msg.getNoteNumber())];
			midi.addEvent(msg, int(at - m_now));
		}
		m_work.clear();
		// the input file's channels to the plugin's inputs one by one; a mono file to every input, as a
		// DAW plays a mono clip on a stereo track
		if (const int files = m_input.getNumChannels(); files > 0)
			for (int c = 0; c < m_plugin->getTotalNumInputChannels(); ++c) {
				const int from = std::min(c, files - 1);
				if (c >= files && files > 1)
					break;
				for (int i = 0; i < n; ++i) {
					const int64_t at = m_now + i;
					m_work.setSample(c, i, at < m_input.getNumSamples() ? m_input.getSample(from, int(at)) : 0.0f);
				}
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
