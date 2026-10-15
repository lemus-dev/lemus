#include "PluginProcessor.h"
#include "PluginEditor.h"


#include <cmath>

namespace {
juce::String knobText(float v) { return juce::String(v * 10.0f, 1); }

// POWER SW, ganged with VOLUME: it clicks on just clockwise of OFF, so VOLUME under this is off.
constexpr float kPowerOnFrom = 0.005f;

// FULL CUT OFF (the bar's CUT OFF MAX): how high the cutoff reaches at full CUT OFF (the ring at
// full RESONANCE, no env mod), 1.2 to 3 kHz, as a unit whose CUT OFF pot has the share of its
// track at mid rotation that gets there (TM3 still holds cutoff centre). Real units spread from
// Din Sync's ~1.4 kHz (the fitted unit's own law) to acidvoice.com's ~3.3 kHz. It ships at 2.3
// kHz, a typical unit's (the nominal parts give ~2.2, Whittle's typical ~2.5): the fitted unit
// is at the dark end.
constexpr float kTopLowHz = 1200, kTopHighHz = 3000, kTopDefaultHz = 2300;

// The pot law against the top it gives, at the unit's calibration: (log Hz, log share at mid),
// Hz rising. Worked out once, on the message thread (the parameter layout asks first).
const std::vector<std::pair<double, double>> &topLaw()
{
	static const auto law = [] {
		auto c = tb303::unitCalibration();
		std::vector<double> mids{c.taperAMid}; // the unit's own law exactly, and a grid around it
		for (double m = 0.6; m > 0.003; m *= 0.94)
			if (std::abs(m / c.taperAMid - 1) > 0.01)
				mids.push_back(m);
		std::sort(mids.begin(), mids.end(), std::greater<>());
		std::vector<std::pair<double, double>> pts;
		for (double m : mids) {
			c.cutoffTaperMid = m;
			pts.push_back({std::log(tb303::fullCutOffHz(c)), std::log(m)});
		}
		return pts;
	}();
	return law;
}

double potMidForTop(double hz)
{
	const auto &law = topLaw();
	const double x = std::log(hz);
	if (x <= law.front().first)
		return std::exp(law.front().second);
	for (size_t i = 1; i < law.size(); ++i)
		if (x <= law[i].first) {
			const double t = (x - law[i - 1].first) / (law[i].first - law[i - 1].first);
			return std::exp(law[i - 1].second + t * (law[i].second - law[i - 1].second));
		}
	return std::exp(law.back().second);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TB303Processor::createLayout()
{
	using namespace juce;
	auto number = [](const String &t) { return t.retainCharacters("-+.0123456789").getFloatValue(); };
	auto knob = [number](const char *id, const String &name, float def) {
		return std::make_unique<AudioParameterFloat>(
			ParameterID{id, 1}, name, NormalisableRange<float>(0.0f, 1.0f, 0.0f), def,
			AudioParameterFloatAttributes()
				.withStringFromValueFunction([](float v, int) { return knobText(v); })
				.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 10.0f); }));
	};
	std::vector<std::unique_ptr<RangedAudioParameter>> p;
	p.push_back(knob("tuning", "Tuning", 0.5f));
	p.push_back(knob("cutoff", "Cut Off Freq", 0.5f));
	p.push_back(knob("resonance", "Resonance", 0.5f));
	p.push_back(knob("envmod", "Env Mod", 0.5f));
	p.push_back(knob("decay", "Decay", 0.5f));
	p.push_back(knob("accent", "Accent", 0.5f));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"volume", 1}, "Volume", NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.7f,
		AudioParameterFloatAttributes()
			.withStringFromValueFunction([](float v, int) { return v < kPowerOnFrom ? String("Off") : knobText(v); })
			.withValueFromStringFunction([number](const String &t) {
				return t.trim().equalsIgnoreCase("off") ? 0.0f : jlimit(0.0f, 1.0f, number(t) / 10.0f);
			})));
	p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{"waveform", 1}, "Waveform", StringArray{"Saw", "Square"}, 0));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"tempo", 1}, "Tempo", NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.42f,
		AudioParameterFloatAttributes().withStringFromValueFunction(
			[](float v, int) { return String(tempoBpm(v), 1) + " BPM"; })));
	p.push_back(std::make_unique<AudioParameterChoice>(
		ParameterID{kModeId, 1}, "Mode", StringArray{"Track Write", "Track Play", "Pattern Play", "Pattern Write"}, 2));
	p.push_back(std::make_unique<AudioParameterChoice>(
		ParameterID{kTrackId, 1}, "Track / Patt. Group",
		StringArray{"1 (I)", "2 (I)", "3 (II)", "4 (II)", "5 (III)", "6 (III)", "7 (IV)"}, 0));
	p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"sync", 1}, "Sync to Host", false));
	// The service trimmers (SERVICE on the bar), as rotations; not automatable, as on the board.
	// Each one's text is what it alone sets, from its trimmed value.
	namespace tr = tb303::trims;
	const tr::Trims nominal;
	auto trim = [](const char *id, const String &name, double def, std::function<String(double)> text) {
		return std::make_unique<AudioParameterFloat>(
			ParameterID{id, 1}, name, NormalisableRange<float>(0.0f, 1.0f, 0.0f), float(def),
			AudioParameterFloatAttributes().withAutomatable(false).withStringFromValueFunction(
				[text](float v, int) { return text(double(v)); }));
	};
	p.push_back(trim("tm1", "TM1 Tempo", nominal.tm1, [](double v) { return String(8.3333 * tr::tempoRScale(v), 2) + " ms"; }));
	p.push_back(trim("tm2", "TM2 Int Clock", nominal.tm2, [](double v) { return String(1e3 * tr::irqSeconds(v), 2) + " ms"; }));
	p.push_back(trim("tm3", "TM3 VCF Freq", nominal.tm3, [](double v) { return String(1e3 / tr::tm3RingHz(v), 2) + " ms"; }));
	p.push_back(trim("tm4", "TM4 Tune", nominal.tm4, [](double v) {
		const double c = 1200 * std::log2(tr::iRef(v, 1) / tr::iRef(tr::kTm4Nominal, 1));
		return (c >= 0 ? "+" : "") + String(c, 1) + " cents";
	}));
	p.push_back(trim("tm5", "TM5 Width", nominal.tm5, [](double v) { return String(1 / tr::width(v), 3) + " V/oct"; }));
	p.push_back(trim("tm6", "TM6 CV", nominal.tm6, [](double v) { return String(5.3333 * tr::cvRef(v), 3) + " V"; }));
	topLaw();
	NormalisableRange<float> top(kTopLowHz, kTopHighHz);
	top.setSkewForCentre(std::sqrt(kTopLowHz * kTopHighHz)); // even in octaves along the bar
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"fullcutoff", 1}, "Full Cut Off", top, kTopDefaultHz,
		AudioParameterFloatAttributes()
			.withStringFromValueFunction([](float hz, int) { return String(hz / 1000, 2) + " kHz"; })
			.withValueFromStringFunction([](const String &s) {
				// "2.5 kHz", "2500 Hz", "2.5" (kHz below 100): what older scenes and sessions name.
				const double v = s.retainCharacters("0123456789.").getDoubleValue();
				return float(s.containsIgnoreCase("k") || v < 100 ? v * 1000 : v);
			})));
	return {p.begin(), p.end()};
}

TB303Processor::TB303Processor(const juce::MemoryBlock *rom, const juce::MemoryBlock *artwork)
	: AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
	  m_params(*this, nullptr, "TB303", createLayout()), m_artwork(lemus::Artwork::parse(artwork))
{
	auto get = [this](const char *id) { return m_params.getRawParameterValue(id); };
	m_tuning = get("tuning");
	m_cutoff = get("cutoff");
	m_resonance = get("resonance");
	m_envMod = get("envmod");
	m_decay = get("decay");
	m_accent = get("accent");
	m_volume = get("volume");
	m_tempo = get("tempo");
	m_waveform = get("waveform");
	m_mode = get(kModeId);
	m_track = get(kTrackId);
	m_sync = get("sync");
	for (size_t i = 0; i < m_trims.size(); ++i)
		m_trims[i] = get(kTrimIds[i]);
	m_fullCutOff = get("fullcutoff");
	m_lastCal.fill(-1);
	for (auto &c : m_scope)
		c.assign(size_t(kScopeFrames), 0.0f);

	m_romOk = rom != nullptr && m_machine.setRom(static_cast<const uint8_t *>(rom->getData()), rom->getSize());
	m_machine.clearPatterns(); // no factory patterns: a new unit starts with every pattern cleared
	m_machine.powerOn();
	m_queue.reserve(64);
}

void TB303Processor::renderVoice(float *out, int n, int at)
{
	const bool tap = at >= 0;
	float *s1 = tap ? m_tap[ScopeS1].data() + at : nullptr, *tp6 = tap ? m_tap[ScopeTp6].data() + at : nullptr;
	if (m_asleep) {
		juce::FloatVectorOperations::clear(out, n);
		if (tap) {
			juce::FloatVectorOperations::clear(s1, n);
			juce::FloatVectorOperations::clear(tp6, n);
		}
		return;
	}
	m_voice.render(out, n, s1, tp6);
	if (tap)
		return; // the scope is on: the VCO runs on, as on the board
	// Sleep once the gate is closed, the output has stayed under -120 dB for 100 ms, and the
	// envelopes haven't moved in that time: the MEG can take 4 s to empty at full DECAY, and the
	// accent sweep rests at about 3.2 V, not 0. Waking just carries on from the settled state.
	const auto range = juce::FloatVectorOperations::findMinAndMax(out, n);
	if (m_voiceGate || std::max(-range.getStart(), range.getEnd()) > 1e-6f) {
		m_quietSamples = 0;
		return;
	}
	const auto p = m_voice.probe();
	if (m_quietSamples == 0)
		m_quietFrom = p;
	m_quietSamples += n;
	if (m_quietSamples < int(0.1 * m_rate))
		return;
	if (std::abs(p.meg - m_quietFrom.meg) > 1e-4 || std::abs(p.accentSweep - m_quietFrom.accentSweep) > 1e-4 ||
	    std::abs(p.veg - m_quietFrom.veg) > 1e-9) {
		m_quietFrom = p; // still settling: watch the next 100 ms
		m_quietSamples = n;
		return;
	}
	m_asleep = true;
}

double TB303Processor::tempoBpm(double knob, double rScale)
{
	// Period proportional to (R_fixed + R_pot); TEMPO is 1M C-taper (reverse log), CW = fast.
	// R_fixed (R18 + TM1) is trimmed so full CW is 300 BPM; full CCW is 40 BPM.
	constexpr double rFixed = 1e6 * 40.0 / 260.0;
	constexpr double b = 32.11;
	const double x = 1 - std::clamp(knob, 0.0, 1.0);
	const double rPot = 1e6 * (std::pow(b, x) - 1) / (b - 1);
	return 300.0 * rFixed / (rFixed * rScale + rPot);
}

tb303::trims::Trims TB303Processor::trims() const
{
	const tb303::trims::Trims nominal;
	const double def[6] = {nominal.tm1, nominal.tm2, nominal.tm3, nominal.tm4, nominal.tm5, nominal.tm6};
	double v[6];
	for (size_t i = 0; i < 6; ++i)
		v[i] = trimAtFactory(int(i)) ? def[i] : double(m_trims[i]->load());
	return {v[0], v[1], v[2], v[3], v[4], v[5]};
}

bool TB303Processor::trimAtFactory(int i) const
{
	const tb303::trims::Trims nominal;
	const double def[6] = {nominal.tm1, nominal.tm2, nominal.tm3, nominal.tm4, nominal.tm5, nominal.tm6};
	return std::abs(double(m_trims[size_t(i)]->load()) - def[i]) < 1e-4;
}

void TB303Processor::setScopeOn(bool on)
{
	m_scopeOn = on;
}

void TB303Processor::readScope(int channel, float *dst, int n, uint32_t end) const
{
	n = juce::jlimit(0, kScopeFrames / 2, n);
	const auto &ring = m_scope[size_t(channel)];
	for (int i = 0; i < n; ++i)
		dst[i] = ring[size_t((end - uint32_t(n) + uint32_t(i)) & uint32_t(kScopeFrames - 1))];
}

juce::String TB303Processor::hzText(double hz)
{
	return hz >= 1000 ? juce::String(hz / 1000, 2) + " kHz" : juce::String(juce::roundToInt(hz)) + " Hz";
}

bool TB303Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

void TB303Processor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
	m_rate = sampleRate;
	for (auto &t : m_tap)
		t.assign(size_t(std::max(samplesPerBlock, 4096)), 0.0f);
	calibrationChanged();
	applyCalibration();
	m_voice.setControls(currentControls());
	m_voice.prepare(sampleRate);
	wake();
	setLatencySamples(0);
}

bool TB303Processor::calibrationChanged()
{
	bool changed = false;
	for (size_t i = 0; i < 7; ++i) {
		const float v = i < 6 ? m_trims[i]->load() : m_fullCutOff->load();
		changed = changed || v != m_lastCal[i];
		m_lastCal[i] = v;
	}
	return changed;
}

void TB303Processor::applyCalibration()
{
	tb303::Calibration c = tb303::unitCalibration();
	const auto t = trims();
	if (t.tm3 != trimDefaults().tm3) // at its default TM3 is the unit's ring exactly
		c.vcfRingHz = tb303::trims::tm3RingHz(t.tm3);
	c.vcoScale = tb303::trims::vcoScale(t);
	c.tuneCents = tb303::trims::tuneCents(t);
	c.cutoffTaperMid = potMidForTop(m_fullCutOff->load());
	m_voice.setCalibration(c);
	m_machine.setIrqSeconds(tb303::trims::irqSeconds(t.tm2));
}

tb303::Controls TB303Processor::currentControls() const
{
	tb303::Controls c;
	c.tuning = m_tuning->load();
	c.cutoff = m_cutoff->load();
	c.resonance = m_resonance->load();
	c.envMod = m_envMod->load();
	c.decay = m_decay->load();
	c.accent = m_accent->load();
	c.volume = m_volume->load();
	c.square = m_waveform->load() > 0.5f;
	return c;
}

void TB303Processor::setKey(tb303::Machine::Key k, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({int(k), down});
}

void TB303Processor::toggleRun()
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({-1, true});
}

void TB303Processor::writePattern(int group, int section, int pattern, const std::vector<tb303::Machine::Step> &steps)
{
	m_machine.writePattern(group, section, pattern, steps);
	m_machine.powerOn();
}

void TB303Processor::handleMidi(const juce::MidiMessage &m)
{
	if (m.isNoteOn())
		m_midi.noteOn(m.getNoteNumber(), m.getVelocity());
	else if (m.isNoteOff())
		m_midi.noteOff(m.getNoteNumber());
	else if (m.isAllNotesOff() || m.isAllSoundOff())
		m_midi.allNotesOff();
	else if (m.isController()) {
		// Roland's TB-03 / TB-303 software control map.
		const char *id = nullptr;
		switch (m.getControllerNumber()) {
		case 74: id = "cutoff"; break;
		case 71: id = "resonance"; break;
		case 12: id = "envmod"; break;
		case 75: id = "decay"; break;
		case 13: id = "decay"; break; // TB-3 map
		case 16: id = "accent"; break;
		case 104: id = "tuning"; break;
		case 11: id = "volume"; break;
		default: break;
		}
		if (id != nullptr)
			if (auto *p = m_params.getParameter(id))
				p->setValueNotifyingHost(float(m.getControllerValue()) / 127.0f);
	}
}

void TB303Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	const int channels = buffer.getNumChannels();
	if (calibrationChanged()) {
		applyCalibration();
		wake();
	}
	// The scope taps this block, if it is on, the unit is on and the block fits them.
	const bool scope = m_scopeOn.load() && m_volume->load() >= kPowerOnFrom && n <= int(m_tap[0].size());
	if (scope)
		wake();
	const auto controls = currentControls();
	const auto &was = m_lastControls;
	if (controls.tuning != was.tuning || controls.cutoff != was.cutoff || controls.resonance != was.resonance ||
	    controls.envMod != was.envMod || controls.decay != was.decay || controls.accent != was.accent ||
	    controls.volume != was.volume || controls.square != was.square) {
		m_lastControls = controls;
		wake();
	}
	m_voice.setControls(controls);

	// POWER SW: off, the unit is dark and silent and takes no keys or notes; on again, the CPU
	// starts afresh, as at power-on (pattern RAM is battery backed).
	const bool power = controls.volume >= kPowerOnFrom;
	if (power != m_powered) {
		m_powered = power;
		if (power) {
			m_machine.powerOn();
		} else {
			m_internalRun = false;
			m_midi.allNotesOff();
			m_midiActive = false;
			auto lines = m_machine.lines();
			lines.gate = lines.accent = lines.slide = false;
			m_voice.setLines(lines, m_machine.strobes());
			m_voiceGate = false;
			wake();
		}
	}
	if (!power) {
		processPoweredOff(buffer, midi);
		return;
	}

	// Panel events
	{
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		for (const auto &e : m_queue) {
			if (e.key < 0)
				m_internalRun = !m_internalRun;
			else
				m_machine.setKey(tb303::Machine::Key(e.key), e.down);
		}
		m_queue.clear();
	}
	m_machine.setMode(tb303::Machine::Mode(juce::jlimit(0, 3, int(m_mode->load()))));
	m_machine.setTrack(juce::jlimit(0, 6, int(m_track->load())));

	// Transport: with SYNC on and the host playing, the host is the DIN sync source.
	bool hostPlaying = false;
	const double knobBpm = tempoBpm(m_tempo->load(), tb303::trims::tempoRScale(trims().tm1));
	double ppq = 0, bpm = knobBpm;
	if (auto *ph = getPlayHead()) {
		if (auto pos = ph->getPosition()) {
			hostPlaying = pos->getIsPlaying();
			if (auto p = pos->getPpqPosition())
				ppq = *p;
			if (auto b = pos->getBpm(); b && m_sync->load() > 0.5f)
				bpm = *b;
		}
	}
	const bool synced = m_sync->load() > 0.5f && hostPlaying;

	if (synced && !m_hostWasPlaying)
		m_internalRun = false; // the host takes over; RUN/STOP resumes its own state afterwards
	m_hostWasPlaying = synced;
	const bool run = synced || m_internalRun;

	auto *out = buffer.getWritePointer(0);
	auto midiIt = midi.cbegin();
	int done = 0;
	bool linesDirty = false;
	for (int i = 0; i < n; ++i) {
		// MIDI at this sample
		while (midiIt != midi.cend() && (*midiIt).samplePosition <= i) {
			handleMidi((*midiIt).getMessage());
			linesDirty = true;
			++midiIt;
		}
		// 24 ppqn clock, high for the first half of each period: the host's as DIN sync, or the
		// internal one, which runs while stopped too (the DIN jack only switches between them).
		const bool internalClock = m_clock.advance(1.0 / m_rate, knobBpm, m_internalRun);
		bool clock = internalClock;
		if (synced) {
			const double phase = (ppq + i * bpm / 60.0 / m_rate) * 24.0;
			clock = phase - std::floor(phase) < 0.5;
		}
		m_machine.setRun(run);
		m_machine.setClock(clock);
		if (m_machine.tick(m_rate))
			linesDirty = true;
		if (scope) {
			m_tap[ScopeTp1][size_t(i)] = internalClock ? 6.0f : 0.0f; // 4013 on the 6 V rail
			m_tap[ScopeTp2][size_t(i)] = m_machine.tp2() ? 6.0f : 0.0f;
		}
		if (linesDirty) {
			if (i > done) {
				renderVoice(out + done, i - done, scope ? done : -1);
				done = i;
			}
			const bool midiHeld = m_midi.lines().gate;
			if (midiHeld)
				m_midiActive = true;
			else if (m_machine.lines().gate)
				m_midiActive = false;
			const auto &lines = m_midiActive ? m_midi.lines() : m_machine.lines();
			m_voice.setLines(lines, m_midiActive ? m_midi.triggers() : m_machine.strobes());
			m_voiceGate = lines.gate;
			wake();
			linesDirty = false;
		}
	}
	if (n > done)
		renderVoice(out + done, n - done, scope ? done : -1);
	if (scope) {
		const uint32_t w = m_scopeWrite.load(std::memory_order_relaxed);
		for (size_t ch = 0; ch < kScopeChannels; ++ch)
			for (int i = 0; i < n; ++i)
				m_scope[ch][size_t((w + uint32_t(i)) & uint32_t(kScopeFrames - 1))] = m_tap[ch][size_t(i)];
		m_scopeWrite.store(w + uint32_t(n), std::memory_order_release);
	}
	for (int ch = 1; ch < channels; ++ch)
		buffer.copyFrom(ch, 0, buffer, 0, 0, n);

	Snapshot s;
	for (size_t l = 0; l < s.leds.size(); ++l)
		s.leds[l] = m_machine.led(tb303::Machine::Led(l));
	s.leds[size_t(tb303::Machine::Led::Run)] = run ? 1.0f : 0.0f;
	s.running = run;
	s.synced = synced;
	s.bpm = bpm;
	s.lines = m_midiActive ? m_midi.lines() : m_machine.lines();
	s.probe = m_voice.probe();
	s.cvRef = tb303::trims::cvRef(trims().tm6);
	s.rate = m_rate;
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

void TB303Processor::processPoweredOff(juce::AudioBuffer<float> &buffer, const juce::MidiBuffer &midi)
{
	{
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		m_queue.clear();
	}
	for (const auto m : midi) // the controls still turn, CC 11 (VOLUME) included
		if (m.getMessage().isController())
			handleMidi(m.getMessage());
	m_hostWasPlaying = false;
	// The voice runs on unheard until it has settled and sleeps, so it is at rest when switched on.
	renderVoice(buffer.getWritePointer(0), buffer.getNumSamples());
	buffer.clear();

	Snapshot s; // every LED dark
	s.bpm = tempoBpm(m_tempo->load(), tb303::trims::tempoRScale(trims().tm1));
	s.lines = m_machine.lines();
	s.lines.gate = s.lines.accent = s.lines.slide = false;
	s.probe = m_voice.probe();
	s.cvRef = tb303::trims::cvRef(trims().tm6);
	s.rate = m_rate;
	s.powered = false;
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

TB303Processor::Snapshot TB303Processor::snapshot() const
{
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	return m_snap;
}

juce::AudioProcessorEditor *TB303Processor::createEditor()
{
	return new TB303Editor(*this);
}

void TB303Processor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", m_modelVersion, nullptr);
	const auto ram = m_machine.saveState();
	state.setProperty("machine", juce::Base64::toBase64(ram.data(), ram.size()), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void TB303Processor::setStateInformation(const void *data, int sizeInBytes)
{
	if (auto xml = getXmlFromBinary(data, sizeInBytes)) {
		if (!xml->hasTagName(m_params.state.getType()))
			return;
		auto tree = juce::ValueTree::fromXml(*xml);
		const juce::String machine = tree.getProperty("machine").toString();
		tree.removeProperty("machine", nullptr);
		m_modelVersion = juce::jlimit(1, kModelVersion, int(tree.getProperty("model", 1)));
		tree.removeProperty("model", nullptr);
		m_params.replaceState(tree);
		if (machine.isNotEmpty()) {
			juce::MemoryOutputStream mo;
			if (juce::Base64::convertFromBase64(mo, machine)) {
				const auto *p = static_cast<const uint8_t *>(mo.getData());
				std::vector<uint8_t> v(p, p + mo.getDataSize());
				// Restore only the battery-backed RAM; the CPU boots fresh, as at power-on.
				std::vector<uint8_t> ramOnly(v.begin(), v.begin() + std::min<size_t>(v.size(), 4 + tb303::Machine::kRamNibbles));
				m_machine.loadState(ramOnly);
				m_machine.powerOn();
			}
		}
	}
}
