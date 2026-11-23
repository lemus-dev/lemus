#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>

namespace {
using Id = SH101Processor::Id;

juce::String sliderText(float v) { return juce::String(v * 10.0f, 1); }

// Every panel control the synth board sees: a change wakes a sleeping voice.
bool sameControls(const sh101::Controls &a, const sh101::Controls &b)
{
	return a.lfoRate == b.lfoRate && a.lfoWave == b.lfoWave && a.vcoMod == b.vcoMod && a.pulseWidth == b.pulseWidth &&
	       a.pwmSource == b.pwmSource && a.pulseLevel == b.pulseLevel && a.sawLevel == b.sawLevel && a.subLevel == b.subLevel &&
	       a.noiseLevel == b.noiseLevel && a.subMode == b.subMode && a.vcfFreq == b.vcfFreq && a.vcfRes == b.vcfRes &&
	       a.vcfEnv == b.vcfEnv && a.vcfMod == b.vcfMod && a.vcfKybd == b.vcfKybd && a.vcaMode == b.vcaMode &&
	       a.attack == b.attack && a.decay == b.decay && a.sustain == b.sustain && a.release == b.release &&
	       a.trigger == b.trigger && a.volume == b.volume && a.tune == b.tune && a.portamento == b.portamento &&
	       a.portaMode == b.portaMode && a.bendVco == b.bendVco && a.bendVcf == b.bendVcf && a.lfoModDepth == b.lfoModDepth;
}

// MIDI CC -> parameter, after Roland's SH-01A (its MIDI implementation chart). CC 1 pushes the
// bender (LFO MOD) and CC 64 is the HOLD pedal; both are handled in handleMidi. Switch CCs pick
// the item nearest value / 127 across the list.
struct CcMap {
	int cc;
	const char *id;
};
constexpr CcMap kCcMap[] = {
	{3, Id::lfoRate},      {5, Id::portamento},  {12, Id::lfoWave},    {13, Id::vcoMod},
	{14, Id::range},       {15, Id::pulseWidth}, {16, Id::pwmSource},  {17, Id::lfoModDepth},
	{18, Id::bendVco},     {19, Id::pulseLevel}, {20, Id::sawLevel},   {21, Id::subLevel},
	{22, Id::subMode},     {23, Id::noiseLevel}, {24, Id::vcfEnv},     {25, Id::vcfMod},
	{26, Id::vcfKybd},     {27, Id::bendVcf},    {28, Id::vcaMode},    {29, Id::trigger},
	{30, Id::sustain},     {31, Id::portaMode},  {71, Id::vcfRes},     {72, Id::release},
	{73, Id::attack},      {74, Id::vcfFreq},    {75, Id::decay},      {76, Id::tune},
	{77, Id::transpose},
};
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout SH101Processor::createLayout()
{
	using namespace juce;
	auto number = [](const String &t) { return t.retainCharacters("-+.0123456789").getFloatValue(); };
	auto slider = [number](const char *id, const String &name, float def) {
		return std::make_unique<AudioParameterFloat>(
			ParameterID{id, 1}, name, NormalisableRange<float>(0.0f, 1.0f, 0.0f), def,
			AudioParameterFloatAttributes()
				.withStringFromValueFunction([](float v, int) { return sliderText(v); })
				.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 10.0f); }));
	};
	auto choice = [](const char *id, const String &name, const StringArray &items, int def) {
		return std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def);
	};
	std::vector<std::unique_ptr<RangedAudioParameter>> p;
	// MODULATOR
	p.push_back(slider(Id::lfoRate, "LFO Rate", 0.5f));
	p.push_back(choice(Id::lfoWave, "LFO Waveform", {"Triangle", "Square", "Random", "Noise"}, 0));
	// VCO
	p.push_back(slider(Id::vcoMod, "VCO Mod", 0.0f));
	p.push_back(choice(Id::range, "VCO Range", {"16'", "8'", "4'", "2'"}, 1));
	p.push_back(slider(Id::pulseWidth, "Pulse Width", 0.0f));
	p.push_back(choice(Id::pwmSource, "PWM Source", {"LFO", "Manual", "Env"}, 1));
	// SOURCE MIXER
	p.push_back(slider(Id::pulseLevel, "Pulse Level", 0.0f));
	p.push_back(slider(Id::sawLevel, "Saw Level", 1.0f));
	p.push_back(slider(Id::subLevel, "Sub Osc Level", 0.4f));
	p.push_back(slider(Id::noiseLevel, "Noise Level", 0.0f));
	p.push_back(choice(Id::subMode, "Sub Osc", {"1 Oct Down", "2 Oct Down", "2 Oct Down Pulse"}, 1));
	// VCF
	p.push_back(slider(Id::vcfFreq, "VCF Freq", 0.35f));
	p.push_back(slider(Id::vcfRes, "VCF Res", 0.3f));
	p.push_back(slider(Id::vcfEnv, "VCF Env", 0.5f));
	p.push_back(slider(Id::vcfMod, "VCF Mod", 0.0f));
	p.push_back(slider(Id::vcfKybd, "VCF Kybd", 0.5f));
	// VCA
	p.push_back(choice(Id::vcaMode, "VCA Mode", {"Env", "Gate"}, 0));
	// ENV
	p.push_back(slider(Id::attack, "Attack", 0.0f));
	p.push_back(slider(Id::decay, "Decay", 0.4f));
	p.push_back(slider(Id::sustain, "Sustain", 0.3f));
	p.push_back(slider(Id::release, "Release", 0.2f));
	p.push_back(choice(Id::trigger, "Env Trigger", {"Gate+Trig", "Gate", "LFO"}, 0));
	// Controller section
	p.push_back(slider(Id::volume, "Volume", 0.7f));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{Id::tune, 1}, "Tune", NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.5f,
		AudioParameterFloatAttributes().withStringFromValueFunction(
			[](float v, int) { return String((v - 0.5f) * 10.0f, 1); })));
	p.push_back(slider(Id::portamento, "Portamento", 0.0f));
	p.push_back(choice(Id::portaMode, "Portamento Mode", {"On", "Auto", "Off"}, 2));
	p.push_back(slider(Id::bendVco, "Bend VCO", 0.2f));
	p.push_back(slider(Id::bendVcf, "Bend VCF", 0.0f));
	p.push_back(slider(Id::lfoModDepth, "Bend LFO Mod", 0.5f));
	p.push_back(choice(Id::transpose, "Transpose", {"L", "M", "H"}, 1));
	// Plugin
	p.push_back(std::make_unique<AudioParameterBool>(ParameterID{Id::sync, 1}, "Sync to Host", false));
	p.push_back(choice(Id::clockDiv, "Sync Clock", {"1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"}, 3));
	return {p.begin(), p.end()};
}

double SH101Processor::clocksPerBeat(int division)
{
	static constexpr double k[] = {1, 2, 3, 4, 6, 8};
	return k[juce::jlimit(0, 5, division)];
}

SH101Processor::SH101Processor(const juce::MemoryBlock *rom, lemus::Artwork artwork)
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  m_params(*this, nullptr, "SH101", createLayout()), m_artwork(std::move(artwork))
{
	m_romOk = rom != nullptr && m_machine.setRom(static_cast<const uint8_t *>(rom->getData()), rom->getSize());
	// A new unit's memory holds no sequence (a session's RAM replaces it). A new instance is a
	// unit that has been on a while, as on every Lemus machine: the CPU boots, but the voice is
	// past jack board A's power-on mute. Only POWER runs the switch-on (switchOn()).
	m_machine.writeSequence({});
	m_machine.powerOn();
	m_voice.settlePower();
	m_queue.reserve(64);
}

bool SH101Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

int SH101Processor::choice(const char *id) const
{
	return juce::roundToInt(m_params.getRawParameterValue(id)->load());
}

sh101::Controls SH101Processor::currentControls() const
{
	auto v = [this](const char *id) { return double(m_params.getRawParameterValue(id)->load()); };
	sh101::Controls c;
	c.lfoRate = v(Id::lfoRate);
	c.lfoWave = sh101::LfoWave(choice(Id::lfoWave));
	c.vcoMod = v(Id::vcoMod);
	c.pulseWidth = v(Id::pulseWidth);
	c.pwmSource = sh101::PwmSource(choice(Id::pwmSource));
	c.pulseLevel = v(Id::pulseLevel);
	c.sawLevel = v(Id::sawLevel);
	c.subLevel = v(Id::subLevel);
	c.noiseLevel = v(Id::noiseLevel);
	c.subMode = sh101::SubMode(choice(Id::subMode));
	c.vcfFreq = v(Id::vcfFreq);
	c.vcfRes = v(Id::vcfRes);
	c.vcfEnv = v(Id::vcfEnv);
	c.vcfMod = v(Id::vcfMod);
	c.vcfKybd = v(Id::vcfKybd);
	c.vcaMode = sh101::VcaMode(choice(Id::vcaMode));
	c.attack = v(Id::attack);
	c.decay = v(Id::decay);
	c.sustain = v(Id::sustain);
	c.release = v(Id::release);
	c.trigger = sh101::Trigger(choice(Id::trigger));
	c.volume = v(Id::volume);
	c.tune = v(Id::tune);
	c.portamento = v(Id::portamento);
	c.portaMode = sh101::PortaMode(choice(Id::portaMode));
	c.bendVco = v(Id::bendVco);
	c.bendVcf = v(Id::bendVcf);
	c.lfoModDepth = v(Id::lfoModDepth);
	return c;
}

void SH101Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	m_calDirty = false;
	m_voice.setCalibration(m_hasCalOverride ? m_calOverride : sh101::Calibration::realUnit());
	m_voice.setControls(currentControls());
	m_voice.prepare(sampleRate);
	m_lfo.prepare(sampleRate);
	m_voiceKeys.prepare(sampleRate);
	m_mod.assign(size_t(std::max(samplesPerBlock, 1)), {});
	wake();
	m_heldFirst = m_heldCount = 0;
	m_samples = 0;
	setLatencySamples(juce::roundToInt(m_voice.latencySamples() + kLatencySeconds * sampleRate));
}

void SH101Processor::setKey(int key, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({key, down});
}

void SH101Processor::setButton(sh101::Machine::Button b, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({-1 - int(b), down});
}

void SH101Processor::setBender(double bend)
{
	m_uiBend = juce::jlimit(-1.0, 1.0, bend);
	m_uiBendActive = bend != 0.0;
}

void SH101Processor::setModPush(double push)
{
	m_uiMod = juce::jlimit(0.0, 1.0, push);
}

void SH101Processor::writeSequence(const std::vector<sh101::Machine::Step> &steps)
{
	m_machine.writeSequence(steps);
	m_machine.powerOn();
}

void SH101Processor::handleMidi(const juce::MidiMessage &m)
{
	// With the CPU, notes press its keys; in voice mode they go straight to the synth board.
	if (m.isNoteOn()) {
		if (m_romOk)
			m_keyboard.noteOn(m.getNoteNumber());
		else
			m_voiceKeys.noteOn(m.getNoteNumber(), sh101::Trigger(choice(Id::trigger)));
	} else if (m.isNoteOff()) {
		m_keyboard.noteOff(m.getNoteNumber());
		m_voiceKeys.noteOff(m.getNoteNumber());
	} else if (m.isAllNotesOff() || m.isAllSoundOff()) {
		m_keyboard.allNotesOff();
		m_voiceKeys.allNotesOff();
	}
	else if (m.isPitchWheel())
		m_midiBend = juce::jlimit(-1.0, 1.0, (m.getPitchWheelValue() - 8192) / 8191.0);
	else if (m.isController()) {
		const int cc = m.getControllerNumber();
		const float v = float(m.getControllerValue()) / 127.0f;
		if (cc == 1)
			m_midiMod = v;
		else if (cc == 64) {
			m_machine.setHoldPedal(m.getControllerValue() >= 64);
			m_voiceKeys.setPedal(m.getControllerValue() >= 64);
		}
		else
			for (const auto &e : kCcMap)
				if (e.cc == cc)
					if (auto *p = m_params.getParameter(e.id))
						p->setValueNotifyingHost(v);
	}
}

void SH101Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The plugin wrappers lock the processor they host: in the Lemus plugins that is the host
	// shell (host/LemusProcessor), which calls this directly. So this processor's own lock
	// (setStateInformation's board) is taken here.
	const juce::ScopedLock sl(getCallbackLock());
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	const int channels = buffer.getNumChannels();
	if (const bool on = m_powered; on != m_on) {
		m_on = on;
		if (on)
			switchOn();
		else
			switchOff();
	}
	if (!m_on) {
		m_heldFirst = m_heldCount = 0; // the notes held back are dropped
		processSwitchedOff(buffer, midi);
		m_samples += n;
		return;
	}
	if (m_calDirty) {
		m_calDirty = false;
		m_voice.setCalibration(m_hasCalOverride ? m_calOverride : sh101::Calibration::realUnit());
		wake();
	}
	const auto controls = currentControls();
	if (!sameControls(controls, m_lastControls)) {
		m_lastControls = controls;
		wake();
	}
	m_voice.setControls(controls);
	m_lfo.setRate(controls.lfoRate);
	m_lfo.setWave(controls.lfoWave);

	// Panel switches the CPU reads, and queued panel presses.
	const auto rangeSwitch = sh101::Range(choice(Id::range));
	m_machine.setRange(rangeSwitch);
	m_machine.setTrigger(controls.trigger);
	// The panel's TRANSPOSE reaches the CPU through the keyboard driver, which moves it for MIDI
	// notes beyond the keys.
	const auto panelTranspose = sh101::Transpose(choice(Id::transpose));
	{
		// In order; a switch changes again only after the firmware has seen its last change (two
		// loop boundaries, as the MIDI keyboard driver does), so a click shorter than a block isn't
		// lost. The rest waits for the next block. In voice mode there is no CPU to read them.
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		if (!m_romOk)
			m_queue.clear();
		const uint64_t loops = m_machine.loops();
		size_t applied = 0;
		for (; applied < m_queue.size(); ++applied) {
			const auto &e = m_queue[applied];
			auto &since = m_switchChanged[size_t(e.key >= 0 ? e.key : sh101::Machine::kKeys - 1 - e.key)];
			if (since != 0 && loops < since + 2)
				break;
			since = loops == 0 ? 1 : loops;
			if (e.key >= 0)
				m_machine.setKey(e.key, e.down);
			else
				m_machine.setButton(sh101::Machine::Button(-1 - e.key), e.down);
		}
		m_queue.erase(m_queue.begin(), m_queue.begin() + long(applied));
	}

	// Transport. SYNC is a cable in EXT CLK IN, which cuts the MODULATOR's square off the CPU's
	// clock: the host's transport is the clock (a square at the division), and while the host is
	// stopped there is none, so the arpeggio and sequencer wait. With SYNC off the MODULATOR
	// clocks them, as with nothing plugged in.
	bool hostPlaying = false;
	double ppq = 0, bpm = 120;
	if (auto *ph = getPlayHead()) {
		if (auto pos = ph->getPosition()) {
			hostPlaying = pos->getIsPlaying();
			if (auto p = pos->getPpqPosition())
				ppq = *p;
			if (auto b = pos->getBpm())
				bpm = *b;
		}
	}
	const bool syncOn = m_params.getRawParameterValue(Id::sync)->load() > 0.5f;
	const bool synced = syncOn && hostPlaying;
	const double cpb = clocksPerBeat(choice(Id::clockDiv));
	// The first clock edge is on the first grid line at or after where the transport starts.
	if (synced && !m_hostWasPlaying)
		m_syncStartPpq = std::ceil(ppq * cpb - 1e-6) / cpb;
	m_hostWasPlaying = synced;

	auto performance = [this] {
		sh101::Performance p;
		p.bend = m_uiBendActive ? m_uiBend.load() : m_midiBend;
		p.modPush = std::max(m_midiMod, m_uiMod.load());
		return p;
	};
	auto perf = performance();
	auto setPerformance = [this](const sh101::Performance &p) {
		if (p.bend != m_lastPerf.bend || p.modPush != m_lastPerf.modPush) {
			m_lastPerf = p;
			wake();
		}
		m_voice.setPerformance(p);
	};
	setPerformance(perf);
	if (int(m_mod.size()) < n)
		m_mod.resize(size_t(n));
	auto *out = buffer.getWritePointer(0);
	auto midiIt = midi.cbegin();
	int done = 0;
	// the host's MIDI, held back until due: the latency less the firmware's own lag on a note (all
	// of it in voice mode), so notes land where the steps do
	const int hold = juce::roundToInt((m_romOk ? kLatencySeconds - kFirmwareNoteSeconds : kLatencySeconds) * m_rate);
	for (int i = 0; i < n; ++i) {
		while (midiIt != midi.cend() && (*midiIt).samplePosition <= i) {
			const auto m = (*midiIt).getMessage();
			++midiIt;
			if (m.getRawDataSize() <= 3 && m_heldCount < int(m_held.size())) {
				auto &h = m_held[size_t((m_heldFirst + m_heldCount++) % int(m_held.size()))];
				h.due = m_samples + i + hold;
				h.size = uint8_t(m.getRawDataSize());
				std::copy_n(m.getRawData(), h.size, h.data);
			} else {
				handleMidi(m);
			}
		}
		while (m_heldCount > 0 && m_held[size_t(m_heldFirst)].due <= m_samples + i) {
			const auto &h = m_held[size_t(m_heldFirst)];
			const juce::MidiMessage msg(h.data, int(h.size), 0.0);
			m_heldFirst = (m_heldFirst + 1) % int(m_held.size());
			--m_heldCount;
			handleMidi(msg);
			if (msg.isPitchWheel() || msg.isControllerOfType(1)) {
				if (i > done) {
					renderVoice(out + done, i - done, m_mod.data() + done);
					done = i;
				}
				perf = performance();
				setPerformance(perf);
			}
		}
		if (m_romOk)
			m_keyboard.tick(m_machine, panelTranspose);

		const auto lfo = m_lfo.tick();
		m_mod[size_t(i)] = {lfo.tri, lfo.square};
		bool clock = lfo.square;
		if (syncOn) {
			clock = false;
			if (synced) {
				const double beat = ppq + i * bpm / 60.0 / m_rate;
				if (beat < m_syncStartPpq - 1.0 / cpb) // the host jumped back: start again
					m_syncStartPpq = std::ceil(beat * cpb - 1e-6) / cpb;
				const double phase = beat * cpb;
				clock = beat >= m_syncStartPpq - 1e-9 && (phase - std::floor(phase)) < 0.5;
			}
		}
		m_machine.setClock(clock);
		if (m_romOk ? m_machine.tick(m_rate) : m_voiceKeys.tick(rangeSwitch, controls.trigger, panelTranspose)) {
			if (i > done) {
				renderVoice(out + done, i - done, m_mod.data() + done);
				done = i;
			}
			const auto &lines = m_romOk ? m_machine.lines() : m_voiceKeys.lines();
			// The RANDOM hold changes on every MODULATOR clock and CLK RST only resets the LFO:
			// neither leaves anything in the voice to settle, so neither wakes it.
			if (lines.gate != m_voiceLines.gate || lines.kcv != m_voiceLines.kcv || lines.range != m_voiceLines.range ||
			    lines.portaOff != m_voiceLines.portaOff)
				wake();
			m_voiceLines = lines;
			m_voice.setLines(lines);
			m_lfo.setClkRst(lines.clkRst);
		}
	}
	if (n > done)
		renderVoice(out + done, n - done, m_mod.data() + done);
	m_samples += n;
	for (int ch = 1; ch < channels; ++ch)
		buffer.copyFrom(ch, 0, buffer, 0, 0, n);

	const float peak = buffer.getMagnitude(0, 0, n);
	m_peak = std::max(peak, m_peak * 0.9f);
	Snapshot s;
	for (size_t l = 0; l < s.leds.size(); ++l)
		s.leds[l] = m_machine.led(sh101::Machine::Led(l));
	for (int k = 0; k < sh101::Machine::kKeys; ++k)
		s.keys[size_t(k)] = m_machine.key(k);
	s.lines = m_romOk ? m_machine.lines() : m_voiceKeys.lines();
	s.probe = m_voice.probe();
	s.lfo = m_lfo.current().tri;
	s.clock = m_lfo.current().square;
	s.synced = synced;
	s.bpm = bpm;
	s.bend = perf.bend;
	s.modPush = perf.modPush;
	s.peak = m_peak;
	s.muted = m_voice.muting();
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

void SH101Processor::switchOn()
{
	// The CPU boots (INITIAL SET; the sequence was kept by the battery), the synth board starts
	// from rest and jack board A's mute holds the output while C13 charges.
	m_machine.powerOn();
	m_switchChanged.fill(0);
	m_voice.reset();
	m_voice.powerOn();
	m_lfo.prepare(m_rate);
	m_voiceKeys.reset();
	m_voiceLines = {};
	m_hostWasPlaying = false;
	wake();
}

void SH101Processor::switchOff()
{
	// Whatever was held is let go; the battery keeps the RAM. The rails fall: jack board A's
	// power-off mute and the thump through C2 play out over the next half second.
	m_voice.powerOff();
	wake();
	m_keyboard = {};
	m_voiceKeys.reset();
	for (int k = 0; k < sh101::Machine::kKeys; ++k)
		m_machine.setKey(k, false);
	for (int b = 0; b < int(sh101::Machine::Button::Count); ++b)
		m_machine.setButton(sh101::Machine::Button(b), false);
}

void SH101Processor::processSwitchedOff(juce::AudioBuffer<float> &buffer, const juce::MidiBuffer &midi)
{
	{
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		m_queue.clear();
	}
	for (const auto m : midi) // the sliders still move, from their CCs too
		if (m.getMessage().isController() && !m.getMessage().isControllerOfType(64))
			handleMidi(m.getMessage());
	buffer.clear();
	m_peak = 0;
	if (m_voice.poweringDown()) {
		// Just switched off: the voice runs down with its rails (the CPU is gone with them).
		const int n = buffer.getNumSamples();
		if (int(m_mod.size()) < n)
			m_mod.resize(size_t(n));
		for (int i = 0; i < n; ++i) {
			const auto lfo = m_lfo.tick();
			m_mod[size_t(i)] = {lfo.tri, lfo.square};
		}
		m_voice.setControls(currentControls());
		renderVoice(buffer.getWritePointer(0), n, m_mod.data());
		for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
			buffer.copyFrom(ch, 0, buffer, 0, 0, n);
	}
	Snapshot s; // every LED dark
	s.powered = false;
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

void SH101Processor::renderVoice(float *out, int n, const sh101::ModFrame *mod)
{
	if (m_asleep) {
		juce::FloatVectorOperations::clear(out, n);
		return;
	}
	m_voice.render(out, n, mod);
	// Sleep once the gate is closed, the output has stayed under -120 dB for 100 ms, and the ENV,
	// the VCA and the portamento haven't moved in that time (the ENV keeps creeping for a second
	// or so after it sounds silent; RELEASE at maximum takes 33 s). With the VCF open, the sources
	// up and the VCA in ENV mode, its leak at rest (about -115 dBFS: R196 holds S6's contact above
	// the ENV) keeps it awake: that is part of the sound. Waking just carries on from the settled
	// state.
	const auto range = juce::FloatVectorOperations::findMinAndMax(out, n);
	if (m_voiceLines.gate || m_voice.powering() || m_voice.poweringDown() ||
	    std::max(-range.getStart(), range.getEnd()) > 1e-6f) {
		m_quietSamples = 0;
		return;
	}
	const auto p = m_voice.probe();
	if (m_quietSamples == 0)
		m_quietFrom = p;
	m_quietSamples += n;
	if (m_quietSamples < int(0.1 * m_rate))
		return;
	if (std::abs(p.env - m_quietFrom.env) > 1e-6 || std::abs(p.vca - m_quietFrom.vca) > 1e-8 ||
	    std::abs(p.cvVolts - m_quietFrom.cvVolts) > 1e-5) {
		m_quietFrom = p; // still settling: watch the next 100 ms
		m_quietSamples = n;
		return;
	}
	m_asleep = m_idleSleep;
}

SH101Processor::Snapshot SH101Processor::snapshot() const
{
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	return m_snap;
}

juce::AudioProcessorEditor *SH101Processor::createEditor()
{
	return new SH101Editor(*this);
}

void SH101Processor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", m_modelVersion, nullptr);
	// The whole board (Machine::snapshot): the CPU with its RAM (the sequence, and ARPEGGIO, HOLD,
	// the held keys and KEY TRANSPOSE as the firmware keeps them), the latches, the
	// sample-and-holds. Without the ROM the CPU doesn't run: the board as the session left it.
	const auto ram = [this] {
		const juce::ScopedLock sl(getCallbackLock()); // between blocks: not a CPU half way through an instruction
		return m_machine.snapshot();
	}();
	state.setProperty("machine", juce::Base64::toBase64(ram.data(), ram.size()), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void SH101Processor::setStateInformation(const void *data, int sizeInBytes)
{
	if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
		if (!xml->hasTagName(m_params.state.getType()))
			return;
		setPower(true); // a session loads switched on
		auto tree = juce::ValueTree::fromXml(*xml);
		const juce::String machine = tree.getProperty("machine").toString();
		tree.removeProperty("machine", nullptr);
		m_modelVersion = juce::jlimit(1, kModelVersion, int(tree.getProperty("model", 1)));
		tree.removeProperty("model", nullptr);
		m_params.replaceState(tree);
		// A session loads as a 101 that has been on a while: the board as it was saved, so its modes
		// and held keys stay as they were. Only POWER boots. A session with the RAM alone (0.1.2 and
		// before) boots the CPU. Hosts load a state while playing: not in the middle of a block.
		juce::MemoryOutputStream mo;
		if (machine.isNotEmpty() && juce::Base64::convertFromBase64(mo, machine)) {
			const auto *p = static_cast<const uint8_t *>(mo.getData());
			const std::vector<uint8_t> v(p, p + mo.getDataSize());
			const juce::ScopedLock sl(getCallbackLock());
			if (!m_on) { // switched off until now: on, then the session's board
				m_on = true;
				switchOn();
			}
			if (!m_machine.restore(v)) {
				m_machine.loadState(v);
				m_machine.powerOn();
			}
		}
	}
}
