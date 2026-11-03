#include "PluginProcessor.h"
#include "tr909/core/factory.h"
#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>
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

using tr909::Machine;
using tr909::Voice;

namespace {
juce::String knobText(float v) { return juce::String(v * 10.0f, 1); }

// The v4.0 firmware keeps its sync mode in bits 0-1 of VV:63 (0 INT, 1 MIDI, 2 DIN); SHIFT + TEMPO
// MODE (TAPE SYNC) steps it INT -> MIDI -> DIN -> INT, while the sequencer is stopped
// (docs/firmware.md).
constexpr int kSyncModeAddr = 0x63;
constexpr int kTempoAddr = 0x30; // VV:30, the tempo value the firmware runs at (docs/firmware.md, "Sync")

// Each voice's trigger line: its latch (9 IC1, 10 IC10) and bit (VoiceBoard::write).
constexpr int kTrigReg[tr909::kVoices] = {9, 9, 9, 9, 9, 10, 10, 10, 10, 10};
constexpr int kTrigBit[tr909::kVoices] = {1, 2, 3, 4, 5, 1, 6, 2, 4, 5};

// The rear panel's MULTI OUT jacks, in the voicing board's order (tr909::Voice): each a mono output
// bus after the main one, off until the host turns it on.
const char *const kJackNames[tr909::kVoices] = {"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "Crash", "Ride"};

} // namespace

const std::vector<TR909Processor::Knob> &TR909Processor::knobs()
{
	static const std::vector<Knob> k = {
		{"bd_tune", "BD Tune", 0.5f},       {"bd_level", "BD Level", 0.75f},     {"bd_attack", "BD Attack", 0.5f},
		{"bd_decay", "BD Decay", 0.5f},     {"sd_tune", "SD Tune", 0.5f},        {"sd_level", "SD Level", 0.75f},
		{"sd_tone", "SD Tone", 0.5f},       {"sd_snappy", "SD Snappy", 0.5f},    {"lt_tune", "LT Tune", 0.5f},
		{"lt_level", "LT Level", 0.75f},    {"lt_decay", "LT Decay", 0.5f},      {"mt_tune", "MT Tune", 0.5f},
		{"mt_level", "MT Level", 0.75f},    {"mt_decay", "MT Decay", 0.5f},      {"ht_tune", "HT Tune", 0.5f},
		{"ht_level", "HT Level", 0.75f},    {"ht_decay", "HT Decay", 0.5f},      {"rs_level", "RS Level", 0.75f},
		{"hc_level", "HC Level", 0.75f},    {"hh_level", "HH Level", 0.75f},     {"ch_decay", "CH Decay", 0.5f},
		{"oh_decay", "OH Decay", 0.5f},     {"cr_level", "Crash Level", 0.75f},  {"cr_tune", "Crash Tune", 0.5f},
		{"rd_level", "Ride Level", 0.75f},  {"rd_tune", "Ride Tune", 0.5f},      {"accent", "Total Accent", 0.5f},
		{"volume", "Master Volume", 0.7f},
	};
	return k;
}

double TR909Processor::tempoBpmOf(int value)
{
	// docs/firmware.md, "Sync": B = 32 + floor(225 v / 256), a 48 ppqn tick of floor(1099138 / B) us:
	// 36.4-290 BPM.
	const int b = 32 + 225 * std::clamp(value, 0, 255) / 256;
	return 60e6 / (48.0 * double(1099138 / b));
}

double TR909Processor::tempoBpm(double v)
{
	// The TEMPO pot (VR601, 50K(B)) across AN0's reference: the A/D code is linear in its rotation.
	// The firmware's value follows the code with a dead band (13FC): it starts at 128, and when the
	// code is two or more counts away it moves to a count short of it. After a power-on that is
	// the code + 1 below 127, the code - 1 above 129, and 128 between.
	const int code = int(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
	return tempoBpmOf(code <= 126 ? code + 1 : code >= 130 ? code - 1 : 128);
}

double TR909Processor::tempoRotation(double bpm)
{
	// the lowest rotation whose code plays at least `bpm`
	for (int code = 0; code < 256; ++code)
		if (tempoBpm(code / 255.0) >= bpm - 1e-9)
			return code / 255.0;
	return 1.0;
}

int TR909Processor::noteFor(Voice v)
{
	static const int notes[tr909::kVoices] = {36, 38, 43, 47, 50, 37, 39, 42, 49, 51};
	return v < Voice::Count ? notes[size_t(v)] : 36;
}

int TR909Processor::voiceOfNote(int note)
{
	switch (note) {
	case 35: case 36: return int(Voice::BD);
	case 38: case 40: return int(Voice::SD);
	case 41: case 43: return int(Voice::LT);
	case 45: case 47: return int(Voice::MT);
	case 48: case 50: return int(Voice::HT);
	case 37: return int(Voice::RS);
	case 39: return int(Voice::HC);
	case 42: case 44: case 46: return int(Voice::HH);
	case 49: return int(Voice::CR);
	case 51: return int(Voice::RD);
	default: return -1;
	}
}

juce::AudioProcessorValueTreeState::ParameterLayout TR909Processor::createLayout()
{
	using namespace juce;
	auto number = [](const String &t) { return t.retainCharacters("-+.0123456789").getFloatValue(); };
	std::vector<std::unique_ptr<RangedAudioParameter>> p;
	for (const auto &k : knobs())
		p.push_back(std::make_unique<AudioParameterFloat>(
			ParameterID{k.id, 1}, k.name, NormalisableRange<float>(0.0f, 1.0f, 0.0f), k.def,
			AudioParameterFloatAttributes()
				.withStringFromValueFunction([](float v, int) { return knobText(v); })
				.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 10.0f); })));
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"tempo", 1}, "Tempo", NormalisableRange<float>(0.0f, 1.0f, 0.0f), float(tempoRotation(120.0)),
		AudioParameterFloatAttributes()
			.withStringFromValueFunction([](float v, int) { return String(tempoBpm(v), 1) + " BPM"; })
			.withValueFromStringFunction([number](const String &t) { return float(tempoRotation(number(t))); })));
	p.push_back(std::make_unique<AudioParameterBool>(ParameterID{"sync", 1}, "Sync to Host", false));
	// TM, the clap's trimmer on the voicing board (SERVICE): the travel of its wiper from the -15 V
	// end; by default the calibrated unit's trim, not the notes' 2 Vp-p (README, SERVICE)
	p.push_back(std::make_unique<AudioParameterFloat>(
		ParameterID{"tm", 1}, "Clap Trim (TM)", NormalisableRange<float>(0.0f, 1.0f, 0.0f), float(factoryTm()),
		AudioParameterFloatAttributes()
			.withStringFromValueFunction([](float v, int) { return String(v * 100.0f, 1) + " %"; })
			.withValueFromStringFunction([number](const String &t) { return jlimit(0.0f, 1.0f, number(t) / 100.0f); })));
	return {p.begin(), p.end()};
}

TR909Processor::TR909Processor(const juce::MemoryBlock *firmware, const juce::MemoryBlock *hihat, const juce::MemoryBlock *crash,
                               const juce::MemoryBlock *ride, lemus::Artwork artwork, const juce::MemoryBlock *v1)
	: AudioProcessor([] {
		  auto b = BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true);
		  for (const char *n : kJackNames)
			  b = b.withOutput(n, juce::AudioChannelSet::mono(), false);
		  return b;
	  }()),
	  m_params(*this, nullptr, "TR909", createLayout()), m_artwork(std::move(artwork))
{
	for (const auto &k : knobs())
		m_knobValues.push_back(m_params.getRawParameterValue(k.id));
	m_tempo = m_params.getRawParameterValue("tempo");
	m_accent = m_params.getRawParameterValue("accent");
	m_sync = m_params.getRawParameterValue("sync");
	m_tm = m_params.getRawParameterValue("tm");

	m_romOk = firmware != nullptr && m_machine.setRom(static_cast<const uint8_t *>(firmware->getData()), firmware->getSize());
	auto rom = [](const juce::MemoryBlock *b) {
		return b != nullptr && b->getSize() == 32768 ? static_cast<const uint8_t *>(b->getData()) : nullptr;
	};
	tr909::SampleRoms roms{rom(hihat), rom(crash), rom(ride)};
	m_samplesOk = {roms.hihat != nullptr, roms.crash != nullptr, roms.ride != nullptr};
	m_board.setSampleRoms(roms);
	setSerial(m_serial);
	if (v1 != nullptr) {
		auto ram = std::make_unique<std::array<uint8_t, Machine::kRamSize>>();
		if (tr909::factoryRam(static_cast<const uint8_t *>(v1->getData()), v1->getSize(), *ram))
			m_factory = std::move(ram);
	}
	// Worker threads for the voices' jobs: two besides the host's audio thread, at the priority of
	// audio work, in the host's audio workgroup once it says which (macOS). The toms' job and the
	// snare's take the longest and the rest fit in beside them, so more threads don't shorten the
	// block; they cost cycles waking, waiting and on colder cores (tr909bench at 48 kHz: the busy
	// kit 2.80 G cycles a second with six, 2.41 G with two, its worst block the same).
	const int cores = int(std::thread::hardware_concurrency());
	int workers = std::clamp(cores - 2, 0, 2);
	if (const char *w = std::getenv("TR909_WORKERS")) // tools: 0 runs every job on the caller
		workers = std::clamp(std::atoi(w), 0, 6);
	m_tokens.resize(size_t(workers));
	m_parallel = std::make_unique<tr909::Parallel>(
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
	m_board.setParallel(m_parallel.get());
	if (m_factory) // a new instance is a new 909; a session's own RAM replaces it
		m_machine.setRam(m_factory->data(), m_factory->size());
	warmStart(); // a new instance is a 909 that is on (the board at its operating points: prepare)
	m_queue.reserve(64);
	m_uiMidi.reserve(256);
}

bool TR909Processor::sampleRomLoaded(Voice v) const
{
	switch (v) {
	case Voice::HH: return m_samplesOk[0];
	case Voice::CR: return m_samplesOk[1];
	case Voice::RD: return m_samplesOk[2];
	default: return true;
	}
}

bool TR909Processor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
	const auto out = layouts.getMainOutputChannelSet();
	if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono())
		return false;
	for (int b = 1; b < layouts.outputBuses.size(); ++b) {
		const auto &set = layouts.outputBuses.getReference(b);
		if (!set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
			return false; // a jack is mono: on a stereo bus, the same on both sides
	}
	return true;
}

tr909::Controls TR909Processor::currentControls() const
{
	static double tr909::Controls::*const fields[] = {
		&tr909::Controls::bdTune,  &tr909::Controls::bdLevel,  &tr909::Controls::bdAttack, &tr909::Controls::bdDecay,
		&tr909::Controls::sdTune,  &tr909::Controls::sdLevel,  &tr909::Controls::sdTone,   &tr909::Controls::sdSnappy,
		&tr909::Controls::ltTune,  &tr909::Controls::ltLevel,  &tr909::Controls::ltDecay,  &tr909::Controls::mtTune,
		&tr909::Controls::mtLevel, &tr909::Controls::mtDecay,  &tr909::Controls::htTune,   &tr909::Controls::htLevel,
		&tr909::Controls::htDecay, &tr909::Controls::rsLevel,  &tr909::Controls::hcLevel,  &tr909::Controls::hhLevel,
		&tr909::Controls::chDecay, &tr909::Controls::ohDecay,  &tr909::Controls::crLevel,  &tr909::Controls::crTune,
		&tr909::Controls::rdLevel, &tr909::Controls::rdTune,   nullptr /* accent: the CPU's */, &tr909::Controls::volume,
	};
	tr909::Controls c;
	for (size_t k = 0; k < m_knobValues.size(); ++k)
		if (fields[k] != nullptr)
			c.*fields[k] = m_knobValues[k]->load();
	// TM at its factory position is the unit's own trim, exactly (the parameter is a float)
	const float tm = m_tm->load();
	c.hcTm = tm == float(factoryTm()) ? -1.0 : double(tm);
	return c;
}

double TR909Processor::factoryTm()
{
	const auto cal = tr909::unitCalibration();
	const auto it = cal.parts.find("hc.tm");
	return it != cal.parts.end() ? it->second : 0.255;
}

void TR909Processor::setSerial(int serial)
{
	serial = std::max(serial, tr909::kFirstSerial);
	auto cal = tr909::unitCalibration();
	for (const auto &[k, v] : tr909::revisionParts(serial))
		cal.parts[k] = v;
	const juce::ScopedLock sl(getCallbackLock()); // between blocks: the circuits are rebuilt
	m_serial = serial;
	m_board.setCalibration(cal);
	m_asleep = false;
}

void TR909Processor::prepareToPlay(double sampleRate, int)
{
	m_rate = sampleRate;
	m_lastControls = currentControls();
	m_board.setControls(m_lastControls);
	m_board.prepare(sampleRate);
	m_asleep = false;
	m_voiceTrig.fill(0);
	m_trigSamples = std::max(1, int(std::lround(0.002 * sampleRate)));
	setLatencySamples(0);
}

void TR909Processor::setPower(bool on)
{
	const juce::ScopedLock sl(getCallbackLock()); // between blocks
	if (on == m_powered)
		return;
	if (on)
		boot();
	else
		m_running = false;
	m_powered = on;
}

void TR909Processor::boot()
{
	m_machine.powerOn();
	m_running = false;
	m_powerOn = true;
}

void TR909Processor::warmStart()
{
	m_machine.powerOn();
	m_running = false;
	if (!m_romOk)
		return;
	// the pots where the knobs are as it boots, so the firmware's TEMPO value is the same after
	// every load (tempoBpm), whatever the knob was before
	m_machine.setTempoPot(m_tempo->load());
	m_machine.setTotalAccentPot(m_accent->load());
	// The firmware's boot (0.6 s from RESET to taking MIDI and keys) run at once, its writes dropped:
	// the latches it leaves are all clear, as the board's operating points have them, and its one
	// pulse on the HH, crash and ride trigger lines (0.4 ms of 3Ch to latch 10) isn't heard.
	m_machine.run(int(kWarmSeconds * Machine::kStateHz));
	m_machine.takeMidiOutTimed();
}

void TR909Processor::setKey(Key key, bool down)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_queue.push_back({key, down});
}

void TR909Processor::playNote(int note, int velocity)
{
	const juce::SpinLock::ScopedLockType lock(m_queueLock);
	m_uiMidi.push_back(0x99);
	m_uiMidi.push_back(uint8_t(note & 127));
	m_uiMidi.push_back(uint8_t(std::clamp(velocity, 1, 127)));
}

bool TR909Processor::outputOn(tr909::Voice v) const
{
	const auto *bus = getBus(false, 1 + int(v));
	return bus != nullptr && bus->isEnabled();
}

void TR909Processor::setRam(const std::vector<uint8_t> &ram)
{
	const juce::ScopedLock sl(getCallbackLock()); // between blocks
	m_machine.setRam(ram.data(), ram.size());
	warmStart(); // the CPU starts again from the new memory; the board stays on
}

std::vector<uint8_t> TR909Processor::ram() const
{
	return {m_machine.ram().begin(), m_machine.ram().end()};
}

void TR909Processor::voiceModeNote(int note, int velocity)
{
	// The firmware's way with a MIDI note, without the firmware: the voice's accent DAC set from the
	// velocity, then its trigger high for one panel scan. The v4.0 firmware's velocity-to-code laws,
	// measured by playing it every velocity (tools/compare/real909.py, midi_code), every 8th velocity
	// and straight lines between: the analog voices' rises like its accent curve (velocity 50 gives
	// code 10, its sequencer's normal level, 80 gives 21, its accent), the hats' and cymbals' is
	// straight (code = velocity / 2).
	const int v = voiceOfNote(note);
	if (v < 0 || velocity <= 0)
		return;
	static constexpr int kDac[tr909::kVoices] = {1, 2, 3, 4, 5, 6, 6, 7, 8, 8};
	static constexpr int kAnalog[17] = {2, 3, 4, 5, 6, 8, 10, 12, 14, 17, 21, 27, 33, 41, 49, 57, 63};
	static constexpr int kSampled[17] = {1, 4, 8, 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 63};
	const bool sampled = Voice(v) == Voice::HH || Voice(v) == Voice::CR || Voice(v) == Voice::RD;
	const int *law = sampled ? kSampled : kAnalog;
	const int k = std::min(15, velocity / 8);
	const double x0 = 8.0 * k, x1 = k == 15 ? 127.0 : 8.0 * (k + 1);
	const int code = std::clamp(int(std::lround(law[k] + (law[k + 1] - law[k]) * (velocity - x0) / (x1 - x0))), 0, 63);
	m_board.write(kDac[v], uint8_t(code << 1));
	if (Voice(v) == Voice::HH) // CLOSED/OPEN: high for the closed hat
		m_board.write(10, uint8_t((m_board.latch(10) & ~0x08) | (note == 46 ? 0 : 0x08)));
	m_board.write(kTrigReg[v], uint8_t(m_board.latch(kTrigReg[v]) | (1u << kTrigBit[v])));
	m_voiceTrig[size_t(v)] = m_trigSamples;
	m_asleep = false;
}

void TR909Processor::handleMidi(const juce::MidiMessage &m)
{
	if (m.isController()) {
		// The knobs on Roland's TR-8 control changes for the same controls (its MIDI implementation
		// chart): the TR-8's SD DECAY is the 909's TONE, its CH and OH LEVEL the HH LEVEL, its CC and
		// RC the crash and ride. MASTER VOLUME is CC 7. The 909 itself takes no control changes.
		static const std::pair<int, const char *> kMap[] = {
			{20, "bd_tune"}, {21, "bd_attack"}, {23, "bd_decay"}, {24, "bd_level"}, {25, "sd_tune"}, {26, "sd_snappy"},
			{28, "sd_tone"}, {29, "sd_level"}, {46, "lt_tune"}, {47, "lt_decay"}, {48, "lt_level"}, {49, "mt_tune"},
			{50, "mt_decay"}, {51, "mt_level"}, {52, "ht_tune"}, {53, "ht_decay"}, {54, "ht_level"}, {57, "rs_level"},
			{60, "hc_level"}, {62, "ch_decay"}, {63, "hh_level"}, {81, "oh_decay"}, {82, "hh_level"}, {83, "cr_tune"},
			{85, "cr_level"}, {86, "rd_tune"}, {88, "rd_level"}, {71, "accent"}, {7, "volume"},
		};
		for (const auto &[cc, id] : kMap)
			if (m.getControllerNumber() == cc)
				if (auto *p = m_params.getParameter(id))
					p->setValueNotifyingHost(float(m.getControllerValue()) / 127.0f);
		return;
	}
	// Realtime messages from the host are dropped while SYNC makes the clock.
	if ((m.isMidiClock() || m.isMidiStart() || m.isMidiStop() || m.isMidiContinue() || m.isSongPositionPointer())
	    && m_sync->load() > 0.5f)
		return;
	if (m.isActiveSense())
		return;
	if (!m_romOk) {
		if (m.isNoteOn())
			voiceModeNote(m.getNoteNumber(), m.getVelocity());
		return;
	}
	m_machine.midiIn(m.getRawData(), size_t(m.getRawDataSize()));
}

void TR909Processor::runSync(bool hostPlaying, double ppq, double bpm, int n, std::vector<std::pair<int, std::vector<uint8_t>>> &events)
{
	const bool wantMidiSync = m_sync->load() > 0.5f && m_romOk;
	const int mode = m_machine.iram(kSyncModeAddr) & 3;
	// Step the firmware's sync mode with SHIFT + TEMPO MODE until it matches (MIDI with SYNC on, INT
	// with it off): 40 ms SHIFT, 60 ms both, 40 ms SHIFT, 150 ms to settle.
	const int target = wantMidiSync ? 1 : 0;
	if (m_romOk && m_syncMacro <= 0 && mode != target && !m_running) {
		m_syncMacroStep = 0;
		m_syncMacro = 1;
	}
	if (m_syncMacro > 0) {
		m_syncMacro -= n;
		if (m_syncMacro <= 0) {
			const double ms = m_rate / 1000.0;
			switch (m_syncMacroStep++) {
			case 0: m_machine.setKey(Key::Shift, true); m_syncMacro = int(40 * ms); break;
			case 1: m_machine.setKey(Key::TapeSync, true); m_syncMacro = int(60 * ms); break;
			case 2: m_machine.setKey(Key::TapeSync, false); m_syncMacro = int(40 * ms); break;
			case 3: m_machine.setKey(Key::Shift, false); m_syncMacro = int(150 * ms); break;
			default: m_syncMacro = 0; break;
			}
		}
	}
	const bool synced = wantMidiSync && hostPlaying && mode == 1;
	if (synced && !m_hostWasPlaying) {
		// start: FA at the top, otherwise the song position (in 16ths) and continue
		if (ppq < 1.0 / 48.0)
			events.push_back({0, {0xfa}});
		else {
			const int pos = int(std::floor(ppq * 4.0 + 1e-6));
			events.push_back({0, {0xf2, uint8_t(pos & 127), uint8_t((pos >> 7) & 127), 0xfb}});
		}
		m_lastClock = std::ceil(ppq * 24.0 - 1e-6) - 1;
	}
	if (!synced && m_hostWasPlaying)
		events.push_back({0, {0xfc}});
	m_hostWasPlaying = synced;
	if (!synced)
		return;
	const double perSample = bpm / 60.0 / m_rate * 24.0;
	const double c0 = ppq * 24.0;
	for (double c = m_lastClock + 1; c < c0 + perSample * n; c += 1) {
		const int at = std::clamp(int(std::ceil((c - c0) / perSample)), 0, n - 1);
		events.push_back({at, {0xf8}});
		m_lastClock = c;
	}
}

void TR909Processor::renderBoard(float *l, float *r, int n, int offset)
{
	float *jacks[tr909::kVoices];
	bool any = false;
	for (int v = 0; v < tr909::kVoices; ++v) {
		jacks[v] = m_jacks[size_t(v)] ? m_jacks[size_t(v)] + offset : nullptr;
		any = any || jacks[v] != nullptr;
	}
	if (m_asleep && !m_probing) {
		juce::FloatVectorOperations::clear(l, n);
		juce::FloatVectorOperations::clear(r, n);
		for (auto *j : jacks)
			if (j)
				juce::FloatVectorOperations::clear(j, n);
		return;
	}
	m_board.render(l, r, n, any ? jacks : nullptr);
	if (!m_probing)
		return;
	// the scope's rings: the probes and the trigger lines, at the board's rate
	const int frames = n * m_board.factor();
	uint32_t trig = 0;
	for (int v = 0; v < tr909::kVoices; ++v)
		if (m_board.latch(kTrigReg[v]) >> kTrigBit[v] & 1)
			trig |= 1u << v;
	const uint32_t w = m_probeWrite.load(std::memory_order_relaxed);
	for (int c = 0; c <= tr909::VoiceBoard::kProbes; ++c) {
		auto &ring = m_probeRing[size_t(c)];
		const float *src = c < tr909::VoiceBoard::kProbes ? m_board.probeSamples(c) : nullptr;
		for (int i = 0; i < frames; ++i)
			ring[(w + uint32_t(i)) & (kProbeFrames - 1)] = c == tr909::VoiceBoard::kProbes ? float(trig) : src ? src[i] : 0.0f;
	}
	m_probeWrite.store(w + uint32_t(frames), std::memory_order_release);
}

void TR909Processor::setProbe(int channel, const juce::String &where)
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

juce::String TR909Processor::probe(int channel) const
{
	const juce::SpinLock::ScopedLockType lock(m_probeLock);
	return m_probeWant[size_t(channel & 1)];
}

void TR909Processor::readProbe(int channel, float *dst, int n, uint32_t end) const
{
	const auto &ring = m_probeRing[size_t(juce::jlimit(0, tr909::VoiceBoard::kProbes, channel))];
	if (ring.empty())
		return juce::FloatVectorOperations::clear(dst, n);
	for (int i = 0; i < n; ++i)
		dst[i] = ring[(end - uint32_t(n) + uint32_t(i)) & (kProbeFrames - 1)];
}

// A byte from the CPU's serial port (the MIDI OUT jack) at sample `at`: channel messages go out,
// with running status; the real-time bytes (the clock while it runs on INT, start, stop) and
// system messages stay in the plugin (the host has its own transport), start and stop telling it
// whether the sequencer runs.
void TR909Processor::midiOutByte(uint8_t b, int at, juce::MidiBuffer &out)
{
	if (b >= 0xf8) {
		if (b == 0xfa || b == 0xfb)
			m_running = true;
		else if (b == 0xfc)
			m_running = false;
		return;
	}
	if (b >= 0xf0) {
		m_outStatus = 0; // system common and exclusive cancel running status
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

void TR909Processor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
	// The plugin wrappers lock and check the processor they host: in the Lemus plugins that is the
	// host shell (host/LemusProcessor), which calls this directly. So this processor's own lock
	// (setStateInformation's RAM) and suspendProcessing are taken here.
	const juce::ScopedLock sl(getCallbackLock());
	if (isSuspended()) {
		buffer.clear();
		midi.clear();
		return;
	}
	if (!m_powered) { // switched off: nothing runs, nothing comes out
		buffer.clear();
		midi.clear();
		{
			const juce::SpinLock::ScopedLockType lock(m_queueLock);
			m_queue.clear();
			m_uiMidi.clear();
		}
		Snapshot dark;
		dark.powered = false;
		const juce::SpinLock::ScopedLockType lock(m_snapLock);
		m_snap = dark;
		return;
	}
	juce::ScopedNoDenormals noDenormals;
	if (m_probeDirty.exchange(false)) { // the scope's probes moved (setProbe)
		std::array<juce::String, tr909::VoiceBoard::kProbes> want;
		{
			const juce::SpinLock::ScopedLockType lock(m_probeLock);
			want = m_probeWant;
		}
		m_probing = false;
		for (int c = 0; c < tr909::VoiceBoard::kProbes; ++c) {
			m_probeOk[size_t(c)] = m_board.setProbe(c, want[size_t(c)].toStdString());
			m_probing = m_probing || want[size_t(c)].isNotEmpty();
		}
		if (m_probing)
			m_asleep = false;
	}
	const int n = buffer.getNumSamples();
	const int channels = buffer.getNumChannels();
	{
		const auto c = currentControls();
		if (std::memcmp(&c, &m_lastControls, sizeof c) != 0) {
			m_board.setControls(c);
			m_lastControls = c;
			m_asleep = false;
		}
	}
	m_machine.setTempoPot(m_tempo->load());
	m_machine.setTotalAccentPot(m_accent->load());
	if (m_powerOn.exchange(false)) {
		m_board.powerOn();
		m_asleep = false;
	}
	std::vector<uint8_t> uiMidi;
	{
		const juce::SpinLock::ScopedLockType lock(m_queueLock);
		for (const auto &e : m_queue)
			m_machine.setKey(e.key, e.down);
		m_queue.clear();
		uiMidi.swap(m_uiMidi);
	}

	// Transport
	bool hostPlaying = false;
	double ppq = 0, bpm = 120;
	if (auto *ph = getPlayHead())
		if (auto pos = ph->getPosition()) {
			hostPlaying = pos->getIsPlaying();
			if (auto p = pos->getPpqPosition())
				ppq = *p;
			if (auto b = pos->getBpm())
				bpm = *b;
		}
	std::vector<std::pair<int, std::vector<uint8_t>>> events;
	runSync(hostPlaying, ppq, bpm, n, events);
	for (size_t k = 0; k + 2 < uiMidi.size(); k += 3)
		events.push_back({0, {uiMidi[k], uiMidi[k + 1], uiMidi[k + 2]}});
	for (const auto meta : midi) {
		const auto m = meta.getMessage();
		events.push_back({meta.samplePosition, std::vector<uint8_t>(m.getRawData(), m.getRawData() + m.getRawDataSize())});
	}
	std::stable_sort(events.begin(), events.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	juce::MidiBuffer midiOut;

	auto *left = buffer.getWritePointer(0);
	auto *right = channels > 1 ? buffer.getWritePointer(1) : nullptr;
	std::vector<float> scratch;
	if (right == nullptr) {
		scratch.assign(size_t(n), 0.0f);
		right = scratch.data();
	}
	for (int v = 0; v < tr909::kVoices; ++v) {
		// a cable in the MULTI OUT jack takes the voice out of the mix, to its output if the host
		// has it on; an output without a cable is silent
		const bool plugged = m_cables[size_t(v)];
		if (plugged != m_board.plugged(Voice(v))) {
			m_board.setPlugged(Voice(v), plugged);
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
	// The block in segments between events: each segment runs the CPU board for its states, and its
	// writes to the latches reach the board at their own samples; the board renders up to each event
	// and each write.
	size_t ei = 0;
	int done = 0; // the board has rendered up to here
	auto renderTo = [&](int at) {
		if (at > done) {
			renderBoard(left + done, right + done, at - done, done);
			done = at;
		}
	};
	// a write to a latch at sample `at` (one that changes nothing is dropped: the firmware rewrites
	// the latches every panel scan, and only a change wakes the board)
	static const bool logWrites = std::getenv("TR909_LOGWRITES") != nullptr; // tools
	auto latch = [&](int at, int reg, uint8_t value) {
		if (logWrites)
			std::fprintf(stderr, "write %d %02x at %d of %d%s\n", reg, value, at, n, m_board.latch(reg) == value ? " (same)" : "");
		if (m_board.latch(reg) == value)
			return;
		renderTo(at);
		m_board.write(reg, value);
		m_asleep = false;
	};
	for (int pos = 0; pos < n;) {
		if (ei < events.size() && events[ei].first <= pos)
			renderTo(pos); // what the events write takes effect from here
		while (ei < events.size() && events[ei].first <= pos) {
			const auto &bytes = events[ei].second;
			if (!bytes.empty() && bytes[0] >= 0xf2 && bytes[0] != 0xf7) { // the sync's own clock and song position
				if (m_romOk)
					m_machine.midiIn(bytes.data(), bytes.size());
			} else if (!bytes.empty()) {
				handleMidi(juce::MidiMessage(bytes.data(), int(bytes.size())));
			}
			++ei;
		}
		// to the next event, or the end of a voice-mode trigger
		int next = ei < events.size() ? std::min(n, events[ei].first) : n;
		for (const int t : m_voiceTrig)
			if (t > 0)
				next = std::min(next, pos + t);
		next = std::max(next, pos + 1);
		const int len = next - pos;
		if (m_romOk) {
			m_stateAcc += len * Machine::kStateHz / m_rate;
			const int states = int(m_stateAcc);
			m_stateAcc -= states;
			const uint64_t start = m_machine.states();
			m_machine.run(states);
			for (const auto &w : m_machine.writes())
				latch(pos + std::min(len - 1, int(double(w.state) * m_rate / Machine::kStateHz)), w.reg, w.value);
			for (const auto &b : m_machine.takeMidiOutTimed())
				midiOutByte(b.data, pos + std::min(len - 1, int(double(b.state - start) * m_rate / Machine::kStateHz)), midiOut);
		}
		// voice mode's triggers, high for one panel scan
		for (int v = 0; v < tr909::kVoices; ++v) {
			auto &t = m_voiceTrig[size_t(v)];
			if (t <= 0)
				continue;
			t -= len;
			if (t <= 0) {
				t = 0;
				latch(next, kTrigReg[v], uint8_t(m_board.latch(kTrigReg[v]) & ~(1u << kTrigBit[v])));
			}
		}
		pos = next;
	}
	renderTo(n);
	midi.swapWith(midiOut); // out: the MIDI OUT jack's messages, not the input
	if (!m_asleep && !m_probing && m_board.asleep())
		m_asleep = true;
	const int mainChannels = getBus(false, 0) ? getBus(false, 0)->getNumberOfChannels() : channels;
	if (mainChannels == 1)
		for (int i = 0; i < n; ++i)
			left[i] = 0.5f * (left[i] + right[i]);
	for (int ch = 2; ch < std::min(mainChannels, channels); ++ch)
		buffer.clear(ch, 0, n);
	for (int v = 0; v < tr909::kVoices; ++v) // a jack on a stereo bus: the same on both sides
		if (m_jacks[size_t(v)] != nullptr && getBus(false, 1 + v)->getNumberOfChannels() > 1) {
			auto jack = getBusBuffer(buffer, false, 1 + v);
			jack.copyFrom(1, 0, jack, 0, 0, n);
		}

	Snapshot s;
	if (m_romOk) {
		for (size_t l = 0; l < s.leds.size(); ++l)
			s.leds[l] = m_machine.ledLevel(Machine::Led(l));
		for (int d = 0; d < 3; ++d)
			s.digits[size_t(d)] = m_machine.digit(d);
		s.syncMode = m_machine.iram(kSyncModeAddr) & 3;
		if (s.syncMode == 0)
			s.tempo = m_machine.iram(kTempoAddr);
	}
	for (int v = 0; v < tr909::kVoices; ++v)
		s.voices[size_t(v)] = m_asleep ? 0.0f : float(std::abs(m_board.voiceOut(Voice(v))));
	s.synced = m_hostWasPlaying;
	s.running = m_running;
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	m_snap = s;
}

void TR909Processor::audioWorkgroupContextChanged(const juce::AudioWorkgroup &workgroup)
{
	{
		const juce::SpinLock::ScopedLockType lock(m_workgroupLock);
		m_workgroup = workgroup;
	}
	if (m_parallel)
		m_parallel->rejoin();
}

TR909Processor::Snapshot TR909Processor::snapshot() const
{
	const juce::SpinLock::ScopedLockType lock(m_snapLock);
	return m_snap;
}

juce::AudioProcessorEditor *TR909Processor::createEditor()
{
	return new TR909Editor(*this);
}

void TR909Processor::getStateInformation(juce::MemoryBlock &destData)
{
	auto state = m_params.copyState();
	state.setProperty("model", m_modelVersion, nullptr);
	juce::StringArray cables; // the MULTI OUT jacks with a cable in, by voice
	for (int v = 0; v < tr909::kVoices; ++v)
		if (m_cables[size_t(v)])
			cables.add(kJackNames[v]);
	state.setProperty("cables", cables.joinIntoString(","), nullptr);
	state.setProperty("serial", m_serial, nullptr);
	const auto &ram = m_machine.ram();
	state.setProperty("ram", juce::Base64::toBase64(ram.data(), ram.size()), nullptr);
	if (auto xml = state.createXml())
		copyXmlToBinary(*xml, destData);
}

void TR909Processor::setStateInformation(const void *data, int sizeInBytes)
{
	auto xml = getXmlFromBinary(data, sizeInBytes);
	if (!xml || !xml->hasTagName(m_params.state.getType()))
		return;
	auto tree = juce::ValueTree::fromXml(*xml);
	const juce::String ram = tree.getProperty("ram").toString();
	tree.removeProperty("ram", nullptr);
	m_modelVersion = juce::jlimit(1, kModelVersion, int(tree.getProperty("model", kModelVersion)));
	tree.removeProperty("model", nullptr);
	const auto cables = juce::StringArray::fromTokens(tree.getProperty("cables").toString(), ",", "");
	for (int v = 0; v < tr909::kVoices; ++v)
		m_cables[size_t(v)] = cables.contains(kJackNames[v]);
	tree.removeProperty("cables", nullptr);
	const int serial = int(tree.getProperty("serial", tr909::kLateSerial));
	tree.removeProperty("serial", nullptr);
	if (serial != m_serial)
		setSerial(serial);
	m_params.replaceState(tree);
	// A session loads switched on, as a 909 that has been on a while: the CPU from the session's
	// battery RAM, past its boot (warmStart), the board as it is. Only the POWER switch runs the
	// power-on. Hosts load a state while playing: not in the middle of a block.
	juce::MemoryOutputStream mo;
	const bool hasRam = ram.isNotEmpty() && juce::Base64::convertFromBase64(mo, ram);
	const juce::ScopedLock sl(getCallbackLock());
	if (hasRam)
		m_machine.setRam(static_cast<const uint8_t *>(mo.getData()), mo.getDataSize());
	if (hasRam || !m_powered)
		warmStart();
	m_powered = true;
}
