#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <thread>
#if JUCE_MAC
#include <pthread.h>
#elif JUCE_WINDOWS
#ifndef NOMINMAX
#define NOMINMAX // windows.h's min and max macros would break std::min and std::max
#endif
#include <windows.h>
#endif

namespace {
juce::String knobText(float v) { return juce::String(v * 10.0f, 1); }

// The knobs: parameter id, name, the Controls field, and the default rotation. The LEVEL knobs
// start at their printed "standard" dot (+75 degrees: the service notes' "red mark"), the others
// at 12 o'clock.
struct KnobSpec {
	const char *id, *name;
	double tr808::Controls::*field;
	float def;
};
constexpr float kRedMark = 0.75f;
const KnobSpec kKnobs[] = {
	{"ac_level", "Accent Level", &tr808::Controls::accent, kRedMark},
	{"bd_level", "BD Level", &tr808::Controls::bdLevel, kRedMark},
	{"bd_tone", "BD Tone", &tr808::Controls::bdTone, 0.5f},
	{"bd_decay", "BD Decay", &tr808::Controls::bdDecay, 0.5f},
	{"sd_level", "SD Level", &tr808::Controls::sdLevel, kRedMark},
	{"sd_tone", "SD Tone", &tr808::Controls::sdTone, 0.5f},
	{"sd_snappy", "SD Snappy", &tr808::Controls::sdSnappy, 0.5f},
	{"lt_level", "LT/LC Level", &tr808::Controls::ltLevel, kRedMark},
	{"lt_tuning", "LT/LC Tuning", &tr808::Controls::ltTuning, 0.5f},
	{"mt_level", "MT/MC Level", &tr808::Controls::mtLevel, kRedMark},
	{"mt_tuning", "MT/MC Tuning", &tr808::Controls::mtTuning, 0.5f},
	{"ht_level", "HT/HC Level", &tr808::Controls::htLevel, kRedMark},
	{"ht_tuning", "HT/HC Tuning", &tr808::Controls::htTuning, 0.5f},
	{"rs_level", "RS/CL Level", &tr808::Controls::rsLevel, kRedMark},
	{"cp_level", "CP/MA Level", &tr808::Controls::cpLevel, kRedMark},
	{"cb_level", "CB Level", &tr808::Controls::cbLevel, kRedMark},
	{"cy_level", "CY Level", &tr808::Controls::cyLevel, kRedMark},
	{"cy_tone", "CY Tone", &tr808::Controls::cyTone, 0.5f},
	{"cy_decay", "CY Decay", &tr808::Controls::cyDecay, 0.5f},
	{"oh_level", "OH Level", &tr808::Controls::ohLevel, kRedMark},
	{"oh_decay", "OH Decay", &tr808::Controls::ohDecay, 0.5f},
	{"ch_level", "CH Level", &tr808::Controls::chLevel, kRedMark},
	{"volume", "Master Volume", &tr808::Controls::volume, 0.7f},
};

// The unit's trims: each trimmer's rotation, the value it sets there and whether that is the unit's
// own (not at the end of a travel that can't reach it), from its calibration. TM2's own is its mid
// travel, where TempoClock::bpm sets the 8.33 ms.
struct UnitTrims {
	std::array<double, tr808::kTrimmers> rotation{}, value{};
	std::array<bool, tr808::kTrimmers> exact{};
};
UnitTrims unitTrims(const tr808::TrimParts &parts)
{
	using tr808::Trimmer;
	const auto cal = tr808::unitCalibration();
	const double targets[tr808::kTrimmers] = {tr808::Machine::kIrqSeconds, tr808::trimmerValue(Trimmer::TempoClock, 0.5, parts), cal.cpOffset,
	                                          cal.noiseRms, cal.cbHz1, cal.cbHz2};
	UnitTrims u;
	for (int i = 0; i < tr808::kTrimmers; ++i) {
		const double r = i == int(Trimmer::TempoClock) ? 0.5 : tr808::trimmerRotation(Trimmer(i), targets[i], parts);
		u.rotation[size_t(i)] = r;
		u.exact[size_t(i)] = i == int(Trimmer::CpOffset) || (r > 0 && r < 1);
		u.value[size_t(i)] = u.exact[size_t(i)] ? targets[i] : tr808::trimmerValue(Trimmer(i), r, parts);
	}
	return u;
}

// The TEMPO rotation that gives 120 BPM with FINE at 12 o'clock.
float defaultTempo()
{
	double lo = 0, hi = 1;
	for (int k = 0; k < 60; ++k) {
		const double mid = (lo + hi) / 2;
		(tr808::TempoClock::bpm(mid, 0.5) < 120 ? lo : hi) = mid;
	}
	return float((lo + hi) / 2);
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout TR808Processor::createLayout()
{
	using namespace juce;
	auto number = [](const String &t) { return t.retainCharacters("-+.0123456789").getFloatValue(); };
	std::vector<std::unique_ptr<RangedAudioParameter>> p;
	for (const auto &k : kKnobs)
		p.push_back(std::make_unique<AudioParameterFloat>(
			ParameterID{k.id, 1}, k.name, NormalisableRange<float>(0.0f, 1.0f, 0.0f), k.def,
			AudioParameterFloatAttributes()
				.withStringFromValueFunction([](float v, int) { return knobText(v); })
				.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 10.0f); })));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"tempo", 1}, "Tempo", NormalisableRange<float>(0.0f, 1.0f, 0.0f), defaultTempo(),
		AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return knobText(v); })));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"fine", 1}, "Fine", NormalisableRange<float>(0.0f, 1.0f, 0.0f), 0.5f,
		AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) { return knobText(v); })));
	auto choice = [&](const char *id, const char *name, StringArray items, int def) {
		p.push_back(std::make_unique<AudioParameterChoice>(ParameterID{id, 1}, name, items, def));
	};
	choice(kModeId, "Mode", {"Pattern Clear", "1st Part", "2nd Part", "Manual Play", "Play", "Compose"}, 3);
	choice(kInstrumentId, "Instrument / Rhythm Track",
	       {"1 AC", "2 BD", "3 SD", "4 LT", "5 MT", "6 HT", "7 RS", "8 CP", "9 CB", "10 CY", "11 OH", "12 CH"}, 0);
	choice("autofill", "Auto Fill In", {"Manual", "16", "12", "8", "4", "2"}, 0);
	choice("variation", "Basic Variation", {"A", "AB", "B"}, 0);
	choice("ifvariation", "I/F Variation", {"A", "B"}, 0);
	choice("prescale", "Pre-Scale", {"1", "2", "3", "4"}, 2);
	auto toggle = [&](const char *id, const char *name) { p.push_back(std::make_unique<AudioParameterBool>(ParameterID{id, 1}, name, false)); };
	toggle("lc", "Low Conga");
	toggle("mc", "Mid Conga");
	toggle("hc", "Hi Conga");
	toggle("cl", "Claves");
	toggle("ma", "Maracas");
	toggle("sync", "Sync to Host");
	// The service trimmers (SERVICE), their rotations: by default the fitted unit's trims
	static const char *const trimNames[tr808::kTrimmers] = {"TM1 INT Clock (main)", "TM2 Tempo Clock (main)", "TM3 C.P. Offset (main)",
	                                                        "TM4 W.N. Level (main)", "TM1 Cowbell Freq.1 (voicing)", "TM2 Cowbell Freq.2 (voicing)"};
	const auto fitted = unitTrims(tr808::TrimParts{});
	for (int i = 0; i < tr808::kTrimmers; ++i)
		p.push_back(std::make_unique<AudioParameterFloat>(
			ParameterID{kTrimIds[i], 1}, trimNames[i], NormalisableRange<float>(0.0f, 1.0f, 0.0f), float(fitted.rotation[size_t(i)]),
			AudioParameterFloatAttributes()
				.withStringFromValueFunction([](float v, int) { return String(v * 100.0f, 1) + " %"; })
				.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 100.0f); })));
	return {p.begin(), p.end()};
}

namespace {
// The rear panel's MULTI OUT jacks, in the voice board's order (tr808::Voice): each a mono output
// bus after the main one, off until the host turns it on.
const char *const kJackNames[tr808::kVoices] = {"BD", "SD", "LT/LC", "MT/MC", "HT/HC", "RS/CL", "CP/MA", "CB", "CY", "OH", "CH"};
} // namespace

TR808Processor::TR808Processor(const juce::MemoryBlock *rom, lemus::Artwork artwork)
	: AudioProcessor([] {
		  auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
		  for (const char *n : kJackNames)
			  b = b.withOutput(n, juce::AudioChannelSet::mono(), false);
		  return b;
	  }()),
	  m_params(*this, nullptr, "TR808", createLayout()), m_artwork(std::move(artwork))
{
	auto get = [this](const char *id) { return m_params.getRawParameterValue(id); };
	for (const auto &k : kKnobs)
		m_knobs.push_back({k.id, k.field, get(k.id)});
	m_lc = get("lc");
	m_mc = get("mc");
	m_hc = get("hc");
	m_cl = get("cl");
	m_ma = get("ma");
	m_tempo = get("tempo");
	m_fine = get("fine");
	m_mode = get(kModeId);
	m_instrument = get(kInstrumentId);
	m_autoFill = get("autofill");
	m_variation = get("variation");
	m_ifVariation = get("ifvariation");
	m_prescale = get("prescale");
	m_sync = get("sync");
	for (int i = 0; i < tr808::kTrimmers; ++i)
		m_trims[size_t(i)] = get(kTrimIds[i]);

	m_romOk = rom != nullptr && m_machine.setRom(static_cast<const uint8_t *>(rom->getData()), rom->getSize());
	m_machine.writeFactoryRhythms(); // a new instance is a new 808; a session's own RAM replaces it
	m_machine.powerOn();
	// the fitted unit, SN 103852, its own trims: its calibration as it is
	m_trimParts = tr808::TrimParts::forSerial(m_serial);
	{
		const auto u = unitTrims(m_trimParts);
		m_unitRot = u.rotation;
		m_unitValue = u.value;
		m_unitExact = u.exact;
	}
	for (int i = 0; i < tr808::kTrimmers; ++i)
		m_trimsApplied[size_t(i)] = m_trims[size_t(i)]->load();
	m_board.setCalibration(calibration());
	m_machine.setIntClock(trimValue(0, m_trimsApplied[0]), tr808::intClockLow(m_trimsApplied[0], m_trimParts));
	m_queue.reserve(64);
	m_uiNotes.reserve(16);
	// Worker threads for the voices' jobs: up to four besides the host's audio thread (the cymbal
	// alone takes about a third of a busy block), at the priority of audio work, in the host's audio
	// workgroup once it says which (macOS). TR808_WORKERS (tools) sets how many; 0 runs the voices
	// on the audio thread.
	const int cores = int(std::thread::hardware_concurrency());
	int workers = std::clamp(cores - 2, 0, 4);
	if (const char *w = std::getenv("TR808_WORKERS"))
		workers = std::clamp(std::atoi(w), 0, 16);
	m_tokens.resize(size_t(workers));
	m_parallel = std::make_unique<tr808::Parallel>(
		workers,
		[](int) {
#if JUCE_MAC
			pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif JUCE_WINDOWS
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
		},
		[this](int index) {
			const juce::SpinLock::ScopedLockType lock(m_workgroupLock);
			m_tokens[size_t(index)].reset();
			if (m_workgroup)
				m_workgroup.join(m_tokens[size_t(index)]);
		});
	m_board.setParallel(workers > 0 ? m_parallel.get() : nullptr);
}

void TR808Processor::audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup)
{
	{
		const juce::SpinLock::ScopedLockType lock(m_workgroupLock);
		m_workgroup = workgroup;
	}
	if (m_parallel)
		m_parallel->rejoin();
}

bool TR808Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	for (int b = 1; b < layouts.outputBuses.size(); ++b) {
		const auto &set = layouts.outputBuses.getReference(b);
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
			return false;
	}
	return true;
}

bool TR808Processor::outputOn(tr808::Voice v) const
{
	const auto *bus = getBus(false, 1 + int(v));
	return bus != nullptr && bus->isEnabled();
}

void TR808Processor::prepareToPlay(double sampleRate, int)
{
	m_rate = sampleRate;
	m_lastControls = currentControls();
	m_board.setControls(m_lastControls);
	m_board.prepare(sampleRate);
	m_asleep = false;
	m_cpuPulse = m_midiPulse = 0;
	m_pulseLevel = false;
	m_lastPulses = m_machine.stepPulses();
	setLatencySamples(0);
}

tr808::Controls TR808Processor::currentControls() const
{
	tr808::Controls c;
	for (const auto &k : m_knobs)
		c.*(k.field) = k.value->load();
	c.lc = m_lc->load() > 0.5f;
	c.mc = m_mc->load() > 0.5f;
	c.hc = m_hc->load() > 0.5f;
	c.cl = m_cl->load() > 0.5f;
	c.ma = m_ma->load() > 0.5f;
	return c;
}

// ====================================================================== SERVICE: the unit

tr808::Calibration TR808Processor::calibration() const
{
	auto cal = tr808::unitCalibration();
	for (const auto &[k, v] : tr808::revisionParts(m_serial))
		cal.parts[k] = v;
	cal.lateSnare = cal.lateClap = m_portionLate;
	cal.cpOffset = trimValue(int(tr808::Trimmer::CpOffset), m_trims[size_t(tr808::Trimmer::CpOffset)]->load());
	cal.noiseRms = trimValue(int(tr808::Trimmer::Noise), m_trims[size_t(tr808::Trimmer::Noise)]->load());
	cal.cbHz1 = trimValue(int(tr808::Trimmer::Cowbell1), m_trims[size_t(tr808::Trimmer::Cowbell1)]->load());
	cal.cbHz2 = trimValue(int(tr808::Trimmer::Cowbell2), m_trims[size_t(tr808::Trimmer::Cowbell2)]->load());
	return cal;
}

double TR808Processor::trimValue(int i, float rotation) const
{
	if (rotation == float(m_unitRot[size_t(i)]) && m_unitExact[size_t(i)])
		return m_unitValue[size_t(i)];
	return tr808::trimmerValue(tr808::Trimmer(i), rotation, m_trimParts);
}

void TR808Processor::rebuild(bool retrim)
{
	std::array<double, tr808::kTrimmers> rotations{};
	{
		const juce::ScopedLock sl(getCallbackLock()); // between blocks: the circuits are rebuilt
		m_trimParts = tr808::TrimParts::forSerial(m_serial);
		m_board.setCalibration(calibration());
		const auto u = unitTrims(m_trimParts);
		m_unitRot = u.rotation;
		m_unitValue = u.value;
		m_unitExact = u.exact;
		rotations = u.rotation;
		m_trimsApplied.fill(-1.0f); // the trimmers again at the next block, on the new board
		m_asleep = false;
	}
	if (retrim)
		for (int i = 0; i < tr808::kTrimmers; ++i)
			if (auto *p = m_params.getParameter(kTrimIds[i]))
				p->setValueNotifyingHost(float(rotations[size_t(i)]));
}

void TR808Processor::setSerial(int serial, bool retrim)
{
	m_serial = std::max(serial, tr808::kFirstSerial);
	rebuild(retrim);
}

void TR808Processor::setPortionChanged(bool late, bool retrim)
{
	m_portionLate = late;
	rebuild(retrim);
}

double TR808Processor::unitTrim(int i) const
{
	return m_unitRot[size_t(juce::jlimit(0, tr808::kTrimmers - 1, i))];
}

tr808::TrimParts TR808Processor::trimParts() const
{
	return m_trimParts;
}

double TR808Processor::trimReading(int i, double rotation) const
{
	const double v = trimValue(i, float(rotation));
	switch (tr808::Trimmer(i)) {
	case tr808::Trimmer::IntClock:
	case tr808::Trimmer::TempoClock: return v * 1000;
	case tr808::Trimmer::CpOffset: return v;
	case tr808::Trimmer::Noise: return v * m_board.tp4PerNoiseRms() * 1000;
	case tr808::Trimmer::Cowbell1:
	case tr808::Trimmer::Cowbell2: return 1000 / v;
	}
	return 0;
}

// On the audio thread, under the callback lock: the trimmers to the board and the CPU board.
void TR808Processor::applyTrims(const std::array<float, tr808::kTrimmers> &t)
{
	using tr808::Trimmer;
	auto v = [&](Trimmer k) { return trimValue(int(k), t[size_t(k)]); };
	m_board.setTrims(v(Trimmer::Cowbell1), v(Trimmer::Cowbell2), v(Trimmer::Noise), v(Trimmer::CpOffset));
	m_machine.setIntClock(v(Trimmer::IntClock), tr808::intClockLow(t[size_t(Trimmer::IntClock)], m_trimParts));
	m_tm2 = t[size_t(Trimmer::TempoClock)];
	m_tm2AtTrim = t[size_t(Trimmer::TempoClock)] == float(m_unitRot[size_t(Trimmer::TempoClock)]);
}

void TR808Processor::playNote(int note, int velocity)
{
	const juce::SpinLock::ScopedLockType lock(m_uiNoteLock);
	m_uiNotes.push_back({note, velocity});
}

// ====================================================================== SERVICE: the scope

void TR808Processor::setProbe(int channel, const juce::String &where)
{
	if (where.isNotEmpty() && m_probeRing[0].empty()) // the rings, before the audio thread can use them
		for (auto &r : m_probeRing)
			r.assign(size_t(kProbeFrames), 0.0f);
	{
		const juce::SpinLock::ScopedLockType lock(m_probeLock);
		m_probeWant[size_t(channel & 1)] = where;
	}
	m_probeDirty = true;
}

juce::String TR808Processor::probe(int channel) const
{
	const juce::SpinLock::ScopedLockType lock(m_probeLock);
	return m_probeWant[size_t(channel & 1)];
}

void TR808Processor::readProbe(int channel, float *dst, int n, uint32_t end) const
{
	const auto &ring = m_probeRing[size_t(juce::jlimit(0, tr808::VoiceBoard::kProbes, channel))];
	if (ring.empty())
		return juce::FloatVectorOperations::clear(dst, n);
	for (int i = 0; i < n; ++i)
		dst[i] = ring[(end - uint32_t(n) + uint32_t(i)) & (kProbeFrames - 1)];
}

void TR808Processor::applyPanelSwitches()
{
	using M = tr808::Machine;
	m_machine.setMode(M::Mode(juce::jlimit(0, 5, int(m_mode->load()))));
	m_machine.setInstrument(juce::jlimit(0, 11, int(m_instrument->load())));
	m_machine.setAutoFill(M::AutoFill(juce::jlimit(0, 5, int(m_autoFill->load()))));
	m_machine.setVariation(M::Variation(juce::jlimit(0, 2, int(m_variation->load()))));
	m_machine.setIfVariationB(m_ifVariation->load() > 0.5f);
	m_machine.setPrescale(juce::jlimit(0, 3, int(m_prescale->load())));
}

void TR808Processor::setKey(int key, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({key, down});
}

void TR808Processor::setRam(const std::vector<uint8_t> &ram)
{
	m_machine.setRam(ram.data(), ram.size());
	m_machine.powerOn();
}

std::vector<uint8_t> TR808Processor::ram() const
{
	return {m_machine.ram().begin(), m_machine.ram().end()};
}

uint16_t TR808Processor::noteVoice(int note)
{
	using namespace tr808;
	switch (note) {
	case 35: case 36: return kBitBD;
	case 37: case 75: return kBitRS; // side stick; claves
	case 38: case 40: return kBitSD;
	case 39: case 70: return kBitCP; // hand clap; maracas
	case 41: case 43: case 64: return kBitLT; // low floor tom, high floor tom; low conga
	case 45: case 47: case 63: return kBitMT; // low tom, low-mid tom; open hi conga (the TR-08's MC)
	case 48: case 50: case 62: return kBitHT; // hi-mid tom, high tom; mute hi conga (HC)
	case 42: case 44: return kBitCH;
	case 46: return kBitOH;
	case 49: case 57: return kBitCY;
	case 56: return kBitCB;
	default: return 0;
	}
}

void TR808Processor::handleMidi(const juce::MidiMessage &m, int)
{
	if (m.isNoteOn()) {
		const uint16_t bit = noteVoice(m.getNoteNumber());
		if (bit == 0)
			return;
		// A new trigger: notes in the same sample share it, as voices on one step do.
		if (m_midiPulse <= 0 || m_midiPulse < int(0.001 * m_rate) - 1)
			m_midiBits = 0;
		m_midiBits |= bit;
		if (m.getVelocity() >= kAccentVelocity)
			m_midiBits |= tr808::kBitAC;
		m_midiPulse = std::max(1, int(0.0012 * m_rate));
		m_pulseLevel = false; // an edge for the trig monostable
	} else if (m.isController()) {
		// The knobs on Roland's TR-8 control changes (its MIDI implementation chart, v1.11), each 808
		// knob on the TR-8's for the same control: TONE is the TR-8's BD ATTACK and SD TUNE, the
		// clap its HC, the cymbal its CC (crash). It has no cowbell: CB LEVEL takes its RC LEVEL.
		// MASTER VOLUME is CC 7.
		static const std::pair<int, const char *> kMap[] = {
			{71, "ac_level"}, {21, "bd_tone"}, {23, "bd_decay"}, {24, "bd_level"}, {25, "sd_tone"}, {26, "sd_snappy"},
			{29, "sd_level"}, {46, "lt_tuning"}, {48, "lt_level"}, {49, "mt_tuning"}, {51, "mt_level"}, {52, "ht_tuning"},
			{54, "ht_level"}, {57, "rs_level"}, {60, "cp_level"}, {63, "ch_level"}, {81, "oh_decay"}, {82, "oh_level"},
			{83, "cy_tone"}, {84, "cy_decay"}, {85, "cy_level"}, {88, "cb_level"}, {7, "volume"},
		};
		for (const auto &[cc, id] : kMap)
			if (m.getControllerNumber() == cc)
				if (auto *p = m_params.getParameter(id))
					p->setValueNotifyingHost(float(m.getControllerValue()) / 127.0f);
	}
}

void TR808Processor::renderBoard(float *out, int n, int offset)
{
	float *jacks[tr808::kVoices];
	bool any = false;
	for (int v = 0; v < tr808::kVoices; ++v) {
		jacks[v] = m_jacks[size_t(v)] ? m_jacks[size_t(v)] + offset : nullptr;
		any = any || jacks[v] != nullptr;
	}
	if (m_asleep) {
		juce::FloatVectorOperations::clear(out, n);
		for (auto *j : jacks)
			if (j)
				juce::FloatVectorOperations::clear(j, n);
		return;
	}
	m_board.render(out, n, any ? jacks : nullptr);
	if (!m_probing)
		return;
	// the scope's rings: the probes (the CPU board's test points held through each host sample)
	// and the voices' gates, at the board's rate
	const int factor = m_board.factor(), frames = n * factor;
	const uint32_t w = m_probeWrite.load(std::memory_order_relaxed);
	for (int c = 0; c < tr808::VoiceBoard::kProbes; ++c) {
		auto &ring = m_probeRing[size_t(c)];
		const float *src = m_board.probeSamples(c);
		if (m_cpuProbe[size_t(c)] != 0) {
			const float *cpu = m_cpuProbeValues[size_t(c)].data() + offset;
			for (int i = 0; i < frames; ++i)
				ring[(w + uint32_t(i)) & (kProbeFrames - 1)] = cpu[i / factor];
		} else {
			for (int i = 0; i < frames; ++i)
				ring[(w + uint32_t(i)) & (kProbeFrames - 1)] = src ? src[i] : 0.0f;
		}
	}
	const uint16_t *gates = m_board.gateSamples();
	auto &ring = m_probeRing[size_t(tr808::VoiceBoard::kProbes)];
	for (int i = 0; i < frames; ++i)
		ring[(w + uint32_t(i)) & (kProbeFrames - 1)] = gates ? float(gates[i]) : 0.0f;
	m_probeWrite.store(w + uint32_t(frames), std::memory_order_release);
}

void TR808Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The plugin wrappers lock and check the processor they host: in the Lemus plugins that is the
	// host shell (host/LemusProcessor), which calls this directly. So this processor's own lock (the
	// board rebuilt for SERVICE's serial number and portion) and suspendProcessing are taken
	// here.
	const juce::ScopedLock sl(getCallbackLock());
	if (isSuspended()) {
		buffer.clear();
		midi.clear();
		return;
	}
	juce::ScopedNoDenormals noDenormals;
	const int n = buffer.getNumSamples();
	const int channels = buffer.getNumChannels();
	if (m_probeDirty.exchange(false)) { // the scope's probes moved (setProbe)
		std::array<juce::String, tr808::VoiceBoard::kProbes> want;
		{
			const juce::SpinLock::ScopedLockType lock(m_probeLock);
			want = m_probeWant;
		}
		m_probing = false;
		for (int c = 0; c < tr808::VoiceBoard::kProbes; ++c) {
			const auto &w = want[size_t(c)];
			m_cpuProbe[size_t(c)] = w == "tp2" ? 2 : w == "tp3" ? 3 : 0;
			m_probeOk[size_t(c)] = m_board.setProbe(c, m_cpuProbe[size_t(c)] != 0 ? std::string() : w.toStdString());
			m_probing = m_probing || w.isNotEmpty();
		}
		if (m_probing)
			m_asleep = false;
	}
	if (m_probing)
		for (auto &v : m_cpuProbeValues)
			if (int(v.size()) < n)
				v.resize(size_t(n), 0.0f);
	{ // the trimmers, moved by SERVICE, the host or a session
		std::array<float, tr808::kTrimmers> t;
		for (int i = 0; i < tr808::kTrimmers; ++i)
			t[size_t(i)] = m_trims[size_t(i)]->load();
		if (t != m_trimsApplied) {
			applyTrims(t);
			m_trimsApplied = t;
			m_asleep = false;
		}
	}
	const auto controls = currentControls();
	{
		const auto &a = controls, &b = m_lastControls;
		bool changed = a.lc != b.lc || a.mc != b.mc || a.hc != b.hc || a.cl != b.cl || a.ma != b.ma;
		for (const auto &k : m_knobs)
			changed = changed || a.*(k.field) < b.*(k.field) || a.*(k.field) > b.*(k.field); // any move at all
		if (changed) {
			m_board.setControls(controls);
			m_lastControls = controls;
			m_asleep = false;
		}
	}
	const bool power = m_power.load();
	if (power && !m_wasPowered) {
		// switched on: the CPU from reset with the RAM as it was, the run flip-flop cleared; the
		// voicing board from discharged capacitors, its output muted while they charge
		m_machine.powerOn();
		m_board.coldStart();
		m_asleep = false;
		m_internalRun = false;
		m_cpuPulse = m_midiPulse = 0;
		m_midiBits = 0;
		m_pulseLevel = false;
		m_board.setLines(false, 0);
		m_lastPulses = m_machine.stepPulses();
	}
	m_wasPowered = power;

	// Transport: with SYNC on and the host playing, the host is the DIN sync source.
	bool hostPlaying = false;
	const double knobBpm = m_tm2AtTrim ? tempoBpm(m_tempo->load(), m_fine->load()) : tr808::TempoClock::bpm(m_tempo->load(), m_fine->load(), m_tm2);
	double ppq = 0, bpm = knobBpm;
	const bool syncOn = m_sync->load() > 0.5f;
	if (auto *ph = getPlayHead())
		if (auto pos = ph->getPosition()) {
			hostPlaying = pos->getIsPlaying();
			if (auto p = pos->getPpqPosition())
				ppq = *p;
			if (auto b = pos->getBpm(); b && syncOn)
				bpm = *b;
		}
	const bool synced = power && syncOn && hostPlaying;
	if (synced && !m_hostWasPlaying)
		m_internalRun = false; // the host takes over; START/STOP runs the unit again afterwards
	m_hostWasPlaying = synced;

	// Panel events
	{
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		for (const auto &e : m_queue) {
			if (e.key >= Step1 && e.key < Step1 + 16)
				m_machine.setStepKey(e.key - Step1, e.down);
			else if (e.key == Clear)
				m_machine.setClearKey(e.down);
			else if (e.key == StartStop) {
				if (e.down && !synced && m_romOk && power)
					m_internalRun = !m_internalRun;
			} else if (e.key == Tap)
				m_tapDown = e.down;
		}
		m_queue.clear();
	}
	applyPanelSwitches();
	const bool run = synced || m_internalRun;
	{ // notes from the panel or a tool (playNote)
		const juce::SpinLock::ScopedLockType lock(m_uiNoteLock);
		if (power)
			for (const auto &[note, velocity] : m_uiNotes)
				handleMidi(juce::MidiMessage::noteOn(10, note, juce::uint8(juce::jlimit(1, 127, velocity))), 0);
		m_uiNotes.clear();
	}

	auto *out = buffer.getWritePointer(0);
	for (int v = 0; v < tr808::kVoices; ++v) {
		// a cable in the MULTI OUT jack takes the voice out of the mix, to its output if the host
		// has it on; an output without a cable is silent
		const bool plugged = m_cables[size_t(v)];
		if (plugged != m_board.plugged(tr808::Voice(v))) {
			m_board.setPlugged(tr808::Voice(v), plugged);
			m_asleep = false;
		}
		m_jacks[size_t(v)] = nullptr;
		if (auto *bus = getBus(false, 1 + v); bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0
		    && getChannelIndexInProcessBlockBuffer(false, 1 + v, 0) < channels) {
			auto jack = getBusBuffer(buffer, false, 1 + v);
			if (plugged)
				m_jacks[size_t(v)] = jack.getWritePointer(0);
			else
				jack.clear();
		}
	}
	auto midiIt = midi.cbegin();
	int done = 0;
	const int monoSamples = std::max(1, int(0.0012 * m_rate));
	for (int i = 0; i < n; ++i) {
		while (midiIt != midi.cend() && (*midiIt).samplePosition <= i) {
			if (power)
				handleMidi((*midiIt).getMessage(), i);
			++midiIt;
		}
		// The 24 ppqn clock: the internal one runs while stopped too; with SYNC, the host's.
		bool clock = power && m_clock.advance(1.0 / m_rate, knobBpm, m_internalRun);
		const bool tempoClock = clock; // TP3
		if (synced) {
			// The host's 24 ppqn, its rising edges 2 ms after the grid: the firmware steps on the
			// first rising edge it sees after the run line goes up, so the downbeat's edge comes
			// just after the start (the steps then land about 4.5 ms after the grid).
			const double phase = (ppq + i * bpm / 60.0 / m_rate) * 24.0 - 0.002 * bpm / 60.0 * 24.0;
			clock = phase - std::floor(phase) < 0.5;
		}
		bool edge = false;
		if (m_romOk && power) {
			m_machine.setRun(run);
			m_machine.setClock(clock);
			m_machine.setFillIn(m_tapDown);
			m_machine.tick(m_rate);
			if (m_machine.stepPulses() != m_lastPulses) {
				m_lastPulses = m_machine.stepPulses();
				m_cpuPulse = monoSamples;
				edge = true;
			}
		}
		if (m_probing && (m_cpuProbe[0] != 0 || m_cpuProbe[1] != 0)) {
			// TP2 (the INT clock, IC1 pin 6) and TP3 (the tempo clock): the CPU's own while it runs;
			// in voice mode the INT clock runs on its own
			bool tp2 = m_machine.intLevel();
			if (!m_romOk && power) {
				m_intAcc += 1.0 / m_rate;
				if (m_intAcc >= m_machine.intClock())
					m_intAcc -= m_machine.intClock();
				tp2 = m_intAcc >= m_machine.intLow();
			}
			for (int c = 0; c < tr808::VoiceBoard::kProbes; ++c)
				m_cpuProbeValues[size_t(c)][size_t(i)] = m_cpuProbe[size_t(c)] == 2 ? (tp2 && power ? 5.0f : 0.0f)
				                                       : m_cpuProbe[size_t(c)] == 3 ? (tempoClock ? 5.0f : 0.0f) : 0.0f;
		}
		if (m_midiPulse > 0 && !m_pulseLevel) {
			edge = true;
			m_pulseLevel = true;
		}
		const bool active = m_cpuPulse > 0 || m_midiPulse > 0;
		if (edge || active) {
			if (i > done) {
				renderBoard(out + done, i - done, done);
				done = i;
			}
			const uint16_t data = uint16_t((m_cpuPulse > 0 ? m_machine.instrumentData() : 0) | (m_midiPulse > 0 ? m_midiBits : 0));
			m_board.setLines(edge, data);
			if (edge)
				m_asleep = false;
			if (m_cpuPulse > 0)
				--m_cpuPulse;
			if (m_midiPulse > 0 && --m_midiPulse == 0) {
				m_midiBits = 0;
				m_pulseLevel = false;
			}
			if (!(m_cpuPulse > 0 || m_midiPulse > 0)) {
				// the pulse is over: render up to here with its lines, then let them go
				renderBoard(out + done, i + 1 - done, done);
				done = i + 1;
				m_board.setLines(false, 0);
			}
		}
	}
	if (n > done)
		renderBoard(out + done, n - done, done);
	// POWER: the output fades over 5 ms as the switch goes off (and back as it goes on)
	if (!power || m_outGain < 1.0f) {
		const float target = power ? 1.0f : 0.0f, stepGain = float(1.0 / (0.005 * m_rate));
		for (int i = 0; i < n; ++i) {
			m_outGain = target > m_outGain ? std::min(target, m_outGain + stepGain) : std::max(target, m_outGain - stepGain);
			out[i] *= m_outGain;
			for (auto *j : m_jacks)
				if (j)
					j[i] *= m_outGain;
		}
	}
	// Sleep once nothing on the board is running (its own test: settled jacks and timing
	// capacitors, docs/model.md "Sleep").
	if (!m_asleep && !m_probing && m_cpuPulse == 0 && m_midiPulse == 0 && m_board.asleep())
		m_asleep = true;
	const int mainChannels = getBus(false, 0) ? getBus(false, 0)->getNumberOfChannels() : channels;
	for (int ch = 1; ch < std::min(mainChannels, channels); ++ch)
		buffer.copyFrom(ch, 0, buffer, 0, 0, n);
	for (int v = 0; v < tr808::kVoices; ++v) // a jack on a stereo bus: the same on both sides
		if (m_jacks[size_t(v)] != nullptr && getBus(false, 1 + v)->getNumberOfChannels() > 1) {
			auto jack = getBusBuffer(buffer, false, 1 + v);
			jack.copyFrom(1, 0, jack, 0, 0, n);
		}

	Snapshot s;
	for (int k = 0; k < 16; ++k)
		s.steps[size_t(k)] = m_romOk && power ? m_machine.stepLed(k) : 0.0f;
	for (size_t l = 0; l < s.leds.size(); ++l)
		s.leds[l] = m_romOk && power ? m_machine.led(tr808::Machine::Led(l)) : 0.0f;
	for (int v = 0; v < tr808::kVoices; ++v)
		s.voices[size_t(v)] = m_asleep || !power ? 0.0f : float(std::abs(m_board.voiceOut(tr808::Voice(v))));
	s.running = run && power;
	s.power = power;
	s.synced = synced;
	s.bpm = bpm;
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

TR808Processor::Snapshot TR808Processor::snapshot() const
{
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	return m_snap;
}

juce::AudioProcessorEditor *TR808Processor::createEditor()
{
	return new TR808Editor(*this);
}

void TR808Processor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", m_modelVersion, nullptr);
	const auto ram = m_machine.saveState();
	state.setProperty("machine", juce::Base64::toBase64(ram.data(), ram.size()), nullptr);
	// SERVICE's unit: its serial number, the snare's and clap's portion
	state.setProperty("serial", m_serial, nullptr);
	state.setProperty("portion", m_portionLate ? "changed" : "drawn", nullptr);
	juce::StringArray cables; // the MULTI OUT jacks with a cable in, by voice
	for (int v = 0; v < tr808::kVoices; ++v)
		if (m_cables[size_t(v)])
			cables.add(kJackNames[v]);
	state.setProperty("cables", cables.joinIntoString(","), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void TR808Processor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (!xml || !xml->hasTagName(m_params.state.getType()))
		return;
	auto tree = juce::ValueTree::fromXml(*xml);
	const juce::String machine = tree.getProperty("machine").toString();
	tree.removeProperty("machine", nullptr);
	m_modelVersion = juce::jlimit(1, kModelVersion, int(tree.getProperty("model", kModelVersion)));
	tree.removeProperty("model", nullptr);
	const auto cables = juce::StringArray::fromTokens(tree.getProperty("cables").toString(), ",", "");
	for (int v = 0; v < tr808::kVoices; ++v)
		m_cables[size_t(v)] = cables.contains(kJackNames[v]);
	tree.removeProperty("cables", nullptr);
	// the unit first (its board rebuilt only if it is another), then the parameters with its trims
	const int serial = std::max(tr808::kFirstSerial, int(tree.getProperty("serial", tr808::kFittedSerial)));
	const bool late = tree.getProperty("portion", "changed").toString() != "drawn";
	for (const char *k : {"serial", "portion"})
		tree.removeProperty(k, nullptr);
	if (serial != m_serial || late != m_portionLate) {
		m_serial = serial;
		m_portionLate = late;
		rebuild(false);
	}
	m_params.replaceState(tree);
	if (machine.isNotEmpty()) {
		juce::MemoryOutputStream mo;
		if (juce::Base64::convertFromBase64(mo, machine)) {
			const auto *p = static_cast<const uint8_t *>(mo.getData());
			// The battery-backed RAM only; the CPU boots fresh, as at power-on.
			if (m_machine.loadState(std::vector<uint8_t>(p, p + mo.getDataSize())))
				m_machine.powerOn();
		}
	}
}
