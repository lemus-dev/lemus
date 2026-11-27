#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <cstring>
#include <random>
#if JUCE_MAC
#include <pthread.h>
#elif JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX // windows.h's min and max macros would break std::min and std::max
#endif
#include <windows.h>
#endif

namespace {

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

// A knob on its printed scale.
void knob(Layout &l, const char *id, const char *name, float def, float lo = 0.0f, float hi = 10.0f,
          juce::AudioParameterFloatAttributes attrs = juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
	          return juce::String(v, 1);
          }))
{
	l.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID(id, 1), name, juce::NormalisableRange<float>(lo, hi), def, attrs));
}
// A centre-zero knob (-10..10) as its scale prints it, -5 to +5.
void centredKnob(Layout &l, const char *id, const char *name, float def)
{
	knob(l, id, name, def, -10.0f, 10.0f,
	     juce::AudioParameterFloatAttributes()
	         .withStringFromValueFunction([](float v, int) {
		         const float half = v / 2;
		         return std::abs(half) < 0.05f ? juce::String("0.0") : (half > 0 ? "+" : "") + juce::String(half, 1);
	         })
	         .withValueFromStringFunction([](const juce::String &t) { return t.trim().trimCharactersAtStart("+").getFloatValue() * 2; }));
}
// VOLUME: its counter-clockwise click is POWER OFF.
void volumeKnob(Layout &l, const char *id, const char *name, float def)
{
	knob(l, id, name, def, 0.0f, 10.0f,
	     juce::AudioParameterFloatAttributes()
	         .withStringFromValueFunction([](float v, int) { return v < Ms20Processor::kPowerOffBelow ? juce::String("POWER OFF") : juce::String(v, 1); })
	         .withValueFromStringFunction([](const juce::String &t) { return t.containsIgnoreCase("OFF") ? 0.0f : t.getFloatValue(); }));
}
void choice(Layout &l, const char *id, const char *name, juce::StringArray items, int def)
{
	l.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID(id, 1), name, items, def));
}

} // namespace

// Every control on the panel (core/controls.h). Defaults: the manual's "normal setting" with VCO 1's
// sawtooth at 8' through an open low-pass.
Layout Ms20Processor::layout()
{
	const ms20::Controls d;
	Layout l;
	// VCO 1
	choice(l, "vco1_wave", "VCO 1 Wave Form", {"Triangle", "Sawtooth", "Pulse", "White Noise"}, int(d.vco1Wave));
	knob(l, "vco1_pw", "VCO 1 PW", float(d.vco1Pw));
	choice(l, "vco1_scale", "VCO 1 Scale", {"32'", "16'", "8'", "4'"}, d.vco1Scale);
	// VCO 2
	choice(l, "vco2_wave", "VCO 2 Wave Form", {"Sawtooth", "Square", "Narrow Pulse", "Ring"}, int(d.vco2Wave));
	centredKnob(l, "vco2_pitch", "VCO 2 Pitch", float(d.vco2Pitch));
	choice(l, "vco2_scale", "VCO 2 Scale", {"16'", "8'", "4'", "2'"}, d.vco2Scale);
	// VCO common
	knob(l, "portamento", "Portamento", float(d.portamento));
	centredKnob(l, "master_tune", "Master Tune", float(d.masterTune));
	knob(l, "fm_mg", "FM MG/T.EXT", float(d.fmMg));
	knob(l, "fm_eg1", "FM EG 1/EXT", float(d.fmEg1));
	// VCO MIXER
	knob(l, "vco1_level", "VCO 1 Level", float(d.vco1Level));
	knob(l, "vco2_level", "VCO 2 Level", float(d.vco2Level));
	// VC HPF
	knob(l, "hpf_cutoff", "HPF Cutoff Freq", float(d.hpfCutoff));
	knob(l, "hpf_peak", "HPF Peak", float(d.hpfPeak));
	knob(l, "hpf_mg", "HPF MG/T.EXT", float(d.hpfMg));
	knob(l, "hpf_eg2", "HPF EG 2/EXT", float(d.hpfEg2));
	// VC LPF
	knob(l, "lpf_cutoff", "LPF Cutoff Freq", float(d.lpfCutoff));
	knob(l, "lpf_peak", "LPF Peak", float(d.lpfPeak));
	knob(l, "lpf_mg", "LPF MG/T.EXT", float(d.lpfMg));
	knob(l, "lpf_eg2", "LPF EG 2/EXT", float(d.lpfEg2));
	// MODULATION GENERATOR
	knob(l, "mg_wave", "MG Wave Form", float(d.mgWave));
	knob(l, "mg_freq", "MG Frequency", float(d.mgFreq));
	// EG 1
	knob(l, "eg1_delay", "EG 1 Delay Time", float(d.eg1Delay));
	knob(l, "eg1_attack", "EG 1 Attack Time", float(d.eg1Attack));
	knob(l, "eg1_release", "EG 1 Release Time", float(d.eg1Release));
	// EG 2
	knob(l, "eg2_hold", "EG 2 Hold Time", float(d.eg2Hold));
	knob(l, "eg2_attack", "EG 2 Attack Time", float(d.eg2Attack));
	knob(l, "eg2_decay", "EG 2 Decay Time", float(d.eg2Decay));
	knob(l, "eg2_sustain", "EG 2 Sustain Level", float(d.eg2Sustain));
	knob(l, "eg2_release", "EG 2 Release Time", float(d.eg2Release));
	// VOLUME
	volumeKnob(l, "volume", "Volume", float(d.volume));
	// EXTERNAL SIGNAL PROCESSOR
	knob(l, "esp_level", "ESP Signal Level", float(d.espLevel));
	knob(l, "esp_lowcut", "ESP Low Cut Freq", float(d.espLowCut));
	knob(l, "esp_highcut", "ESP High Cut Freq", float(d.espHighCut));
	knob(l, "esp_cvadj", "ESP CV Adjust", float(d.espCvAdj));
	knob(l, "esp_threshold", "ESP Threshold Level", float(d.espThreshold));
	return l;
}

Ms20Processor::Ms20Processor(lemus::Artwork artwork)
	: AudioProcessor(BusesProperties()
	                     .withOutput("Output", juce::AudioChannelSet::stereo(), true)
	                     .withInput("SIGNAL IN", juce::AudioChannelSet::stereo(), false)),
	  m_artwork(std::move(artwork)), m_params(*this, nullptr, "ms20", layout())
{
	m_queue.reserve(256);
	if (const char *e = std::getenv("MS20_SEED"); s_fixedSeeds.load() || (e != nullptr && std::string(e) == "0")) {
		m_fixedSeeds = true;
	} else {
		std::random_device rd;
		m_seedState = (uint64_t(rd()) << 32) ^ rd() ^ uint64_t(reinterpret_cast<uintptr_t>(this));
	}
	m_machine.setControls(controls());
	// The machine's second thread (the control side, Machine::setThreaded): at the priority of audio
	// work, in the host's audio workgroup once it says which (macOS), as the TR-909's workers.
	ms20::Worker::Hooks hooks;
	hooks.onStart = [] {
#if JUCE_MAC
		pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif JUCE_WINDOWS
		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
	};
	hooks.onJoin = [this] {
		const juce::SpinLock::ScopedLockType lock(m_workgroupLock);
		m_workgroupToken.reset();
		if (m_workgroup)
			m_workgroup.join(m_workgroupToken);
	};
	hooks.onStop = [this] { m_workgroupToken.reset(); };
	m_machine.setWorkerHooks(std::move(hooks));
}

void Ms20Processor::audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup)
{
	{
		const juce::SpinLock::ScopedLockType lock(m_workgroupLock);
		m_workgroup = workgroup;
	}
	m_machine.rejoinWorker();
}

ms20::Controls Ms20Processor::controls() const
{
	auto v = [this](const char *id) { return double(m_params.getRawParameterValue(id)->load()); };
	auto ch = [&](const char *id) { return juce::roundToInt(v(id)); };
	ms20::Controls c;
	c.vco1Wave = ms20::Vco1Wave(ch("vco1_wave"));
	c.vco1Pw = v("vco1_pw");
	c.vco1Scale = ch("vco1_scale");
	c.vco2Wave = ms20::Vco2Wave(ch("vco2_wave"));
	c.vco2Pitch = v("vco2_pitch");
	c.vco2Scale = ch("vco2_scale");
	c.portamento = v("portamento");
	c.masterTune = v("master_tune");
	c.fmMg = v("fm_mg");
	c.fmEg1 = v("fm_eg1");
	c.vco1Level = v("vco1_level");
	c.vco2Level = v("vco2_level");
	c.hpfCutoff = v("hpf_cutoff");
	c.hpfPeak = v("hpf_peak");
	c.hpfMg = v("hpf_mg");
	c.hpfEg2 = v("hpf_eg2");
	c.lpfCutoff = v("lpf_cutoff");
	c.lpfPeak = v("lpf_peak");
	c.lpfMg = v("lpf_mg");
	c.lpfEg2 = v("lpf_eg2");
	c.mgWave = v("mg_wave");
	c.mgFreq = v("mg_freq");
	c.eg1Delay = v("eg1_delay");
	c.eg1Attack = v("eg1_attack");
	c.eg1Release = v("eg1_release");
	c.eg2Hold = v("eg2_hold");
	c.eg2Attack = v("eg2_attack");
	c.eg2Decay = v("eg2_decay");
	c.eg2Sustain = v("eg2_sustain");
	c.eg2Release = v("eg2_release");
	c.volume = v("volume");
	c.espLevel = v("esp_level");
	c.espLowCut = v("esp_lowcut");
	c.espHighCut = v("esp_highcut");
	c.espCvAdj = v("esp_cvadj");
	c.espThreshold = v("esp_threshold");
	return c;
}

int Ms20Processor::keyForNote(int note)
{
	while (note < 48)
		note += 12;
	while (note > 84)
		note -= 12;
	return note - 48;
}

bool Ms20Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	for (const auto &set : layouts.inputBuses)
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
			return false;
	return true;
}

void Ms20Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	settle();
	m_in.assign(size_t(juce::jmax(1, samplesPerBlock)), 0.0f);
}

std::atomic<bool> Ms20Processor::s_fixedSeeds{false};

// A new seed for the machine's hiss and drift (splitmix64; none with fixed seeds)
void Ms20Processor::reseed()
{
	if (m_fixedSeeds)
		return;
	uint64_t z = (m_seedState += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	z ^= z >> 31;
	m_machine.setSeed(z | 1);
}

void Ms20Processor::settle()
{
	reseed();
	m_machine.setControls(controls());
	m_machine.setPatch(patch());
	m_machine.setManual(manual());
	if (m_rate > 0)
		m_machine.prepare(m_rate);
}

bool Ms20Processor::inputOn() const
{
	const auto *bus = getBus(true, 0);
	return bus != nullptr && bus->isEnabled();
}

void Ms20Processor::setKey(int key, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({key, down});
}

void Ms20Processor::setManual(double wheel, bool momentary)
{
	m_wheel = juce::jlimit(-1.0, 1.0, wheel);
	m_momentary = momentary;
}

void Ms20Processor::setPatch(const ms20::Patch &p)
{
	const juce::SpinLock::ScopedLockType lock(m_patchLock);
	m_patch = p;
}

ms20::Patch Ms20Processor::patch() const
{
	const juce::SpinLock::ScopedLockType lock(m_patchLock);
	return m_patch;
}

void Ms20Processor::setPowered(bool on)
{
	if (on != m_powered.load()) {
		m_powered = on;
		m_powerChange = true;
	}
}

void Ms20Processor::handleMidi(const juce::MidiMessage &m)
{
	if (m.isNoteOn()) {
		const int k = keyForNote(m.getNoteNumber());
		if (m_midiHeld[size_t(k)]++ == 0 && !m_panelHeld[size_t(k)])
			m_machine.setKey(k, true);
	} else if (m.isNoteOff()) {
		const int k = keyForNote(m.getNoteNumber());
		if (m_midiHeld[size_t(k)] > 0 && --m_midiHeld[size_t(k)] == 0 && !m_panelHeld[size_t(k)])
			m_machine.setKey(k, false);
	} else if (m.isAllNotesOff() || m.isAllSoundOff()) {
		for (int k = 0; k < ms20::Machine::kKeys; ++k) {
			m_midiHeld[size_t(k)] = 0;
			if (!m_panelHeld[size_t(k)])
				m_machine.setKey(k, false);
		}
	} else if (m.isPitchWheel())
		m_wheel = juce::jlimit(-1.0, 1.0, (m.getPitchWheelValue() - 8192) / 8191.0);
}

void Ms20Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The plugin wrappers lock the processor they host: in the Lemus plugins that is the host shell
	// (host/LemusProcessor), which calls this directly. So this processor's own lock (a session load
	// settling the machine) is taken here.
	const juce::ScopedLock sl(getCallbackLock());
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	// VOLUME's counter-clockwise click is the power switch (docs/panel.json: POWER OFF at -180 deg,
	// the printed 1 at -150): turning into it switches the unit off, out of it on
	{
		const bool on = m_params.getRawParameterValue("volume")->load() >= kPowerOffBelow;
		if (on != m_volumeOn) {
			m_volumeOn = on;
			setPowered(on);
		}
	}
	// the filter board from the bar: the board rebuilt, settled at the controls (as a load)
	if (const auto rev = m_rev2 ? ms20::Machine::FilterRev::Rev2 : ms20::Machine::FilterRev::Rev1; rev != m_machine.filterRev()) {
		m_machine.setControls(controls());
		m_machine.setFilterRev(rev);
	}
	// switched on, the unit boots from cold; off, it runs down (Machine::switchOn)
	if (m_powerChange.exchange(false)) {
		if (m_powered) {
			reseed();
			m_machine.switchOn();
		} else {
			m_machine.switchOff();
		}
	}
	{
		const juce::SpinLock::ScopedTryLockType lock(m_queueLock);
		if (lock.isLocked()) {
			for (const auto &e : m_queue) {
				m_panelHeld[size_t(e.key)] = e.down;
				if (m_midiHeld[size_t(e.key)] == 0)
					m_machine.setKey(e.key, e.down);
			}
			m_queue.clear();
		}
	}
	{
		const juce::SpinLock::ScopedTryLockType lock(m_patchLock);
		if (lock.isLocked())
			m_machine.setPatch(m_patch);
	}
	m_machine.setControls(controls());
	m_machine.setManual(manual());

	// The cable from outside: what the host routes to the side-chain (its channels mixed), read only
	// while the host has the bus on.
	const float *in = nullptr;
	const auto *bus = getBus(true, 0);
	if (bus != nullptr && bus->isEnabled()) {
		if (int(m_in.size()) < n)
			m_in.assign(size_t(n), 0.0f);
		std::fill(m_in.begin(), m_in.begin() + n, 0.0f);
		const int chans = juce::jmin(getTotalNumInputChannels(), buffer.getNumChannels());
		for (int ch = 0; ch < chans; ++ch)
			for (int i = 0; i < n; ++i)
				m_in[size_t(i)] += buffer.getSample(ch, i) / float(chans);
		in = m_in.data();
	}

	auto *out = buffer.getWritePointer(0);
	int pos = 0;
	for (const auto event : midi) {
		const int at = juce::jlimit(0, n, event.samplePosition);
		if (at > pos) {
			m_machine.render(out + pos, at - pos, in != nullptr ? in + pos : nullptr);
			pos = at;
		}
		handleMidi(event.getMessage());
	}
	if (pos < n)
		m_machine.render(out + pos, n - pos, in != nullptr ? in + pos : nullptr);
	midi.clear();
	for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
		buffer.copyFrom(ch, 0, buffer, 0, 0, n);
	m_asleep.store(m_machine.asleep(), std::memory_order_relaxed);

	const juce::SpinLock::ScopedTryLockType lock(m_displayLock);
	if (lock.isLocked()) {
		for (int k = 0; k < ms20::Machine::kKeys; ++k)
			m_display.keys[size_t(k)] = m_machine.key(k);
		m_display.leds = m_machine.leds();
		m_display.solver = m_machine.solverStats();
	}
}

Ms20Processor::Display Ms20Processor::display() const
{
	const juce::SpinLock::ScopedLockType lock(m_displayLock);
	return m_display;
}

juce::AudioProcessorEditor *Ms20Processor::createEditor() { return new Ms20Editor(*this); }

void Ms20Processor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", kModelVersion, nullptr);
	state.setProperty("powered", m_powered.load(), nullptr);
	// the control wheel: it has a centre click but no spring, so it stays where it was left
	state.setProperty("wheel", m_wheel.load(), nullptr);
	state.setProperty("filterRev", m_rev2.load() ? 2 : 1, nullptr);
	// the cables: "<input>:<output>" pairs by index (core/patch.h), kHostInput for the outside
	const auto p = patch();
	juce::StringArray cables;
	for (int i = 0; i < ms20::kFirstOutput; ++i)
		if (p.patched(ms20::Jack(i)))
			cables.add(juce::String(i) + ":" + juce::String(int(p.from[size_t(i)])));
	state.setProperty("cables", cables.joinIntoString(" "), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void Ms20Processor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (xml == nullptr || !xml->hasTagName(m_params.state.getType()))
		return;
	auto state = juce::ValueTree::fromXml(*xml);
	m_modelVersion = state.getProperty("model", kModelVersion);
	const bool powered = state.getProperty("powered", true);
	const double wheel = state.getProperty("wheel", 0.0); // sessions before it was saved: at the centre
	const bool rev2 = int(state.getProperty("filterRev", 1)) == 2; // sessions before Rev 2: Rev 1
	ms20::Patch p;
	for (const auto &cable : juce::StringArray::fromTokens(state.getProperty("cables").toString(), " ", "")) {
		const int in = cable.upToFirstOccurrenceOf(":", false, false).getIntValue();
		const int out = cable.fromFirstOccurrenceOf(":", false, false).getIntValue();
		if (in >= 0 && in < ms20::kFirstOutput && ((out >= ms20::kFirstOutput && out < ms20::kJacks) || out == ms20::kHostInput))
			p.connect(ms20::Jack(in), out);
	}
	state.removeProperty("cables", nullptr);
	state.removeProperty("powered", nullptr);
	state.removeProperty("wheel", nullptr);
	state.removeProperty("filterRev", nullptr);
	state.removeProperty("model", nullptr);
	m_params.replaceState(state);
	setPatch(p);
	setWheel(wheel);
	// A session loads as a unit that has been on a while at its settings: the circuit settled at the
	// session's controls, cables and wheel, as a plugin load is, rather than stepping there from the
	// defaults' operating point in the first blocks (a thump where the VCA is open). Nor is it a turn
	// of the POWER switch: on settled, or off and silent, with no boot or run-down. Hosts load a
	// state while playing: not in the middle of a block.
	const juce::ScopedLock sl(getCallbackLock());
	m_powered = powered;
	m_powerChange = false;
	m_volumeOn = m_params.getRawParameterValue("volume")->load() >= kPowerOffBelow;
	m_rev2 = rev2;
	m_machine.setFilterRev(rev2 ? ms20::Machine::FilterRev::Rev2 : ms20::Machine::FilterRev::Rev1);
	if (powered)
		m_machine.powerOn();
	else
		m_machine.powerOff();
	settle();
}
