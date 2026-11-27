// Headless plugin test: runs TR909Processor without a host. Checks that MIDI plays every voice
// through the firmware and in voice mode (without it), that velocity accents, that SYNC puts the
// firmware in MIDI sync and the host transport runs it, that the idle board sleeps (exact zeros)
// and wakes sounding as before, and that state (parameters + RAM) round-trips.
//   tr909plugintest [OUT_DIR]   (writes WAVs of what it plays when given)
//   tr909plugintest --panel OUT.png [SCALE] [running] [rear] [off] [service] [zoom=Z@X,Y] [tm] [part]
//     [--artwork PACK.svg]   (draws the editor; zoom: SERVICE Z times onto panel mm X, Y; tm: the
//     TM procedure's scope on a clap)
//   tr909plugintest --service   (the SERVICE checks alone: serial numbers, TM)
//   tr909plugintest --render HITS.json OUT.wav   (a hit list through the processor, for comparing
//     with recordings: the main pair, then with "jacks" the ten MULTI OUTs, a cable in each; with
//     "factory" a new unit's memory, "start" pressing START at that time, "keys" other panel keys
//     pressed, "changes" knobs turned
//     during it, "serial" the unit)
#include "PluginProcessor.h"
#include "HardwareView.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <functional>
#include <map>
#include <cstdio>

namespace {
struct FakePlayHead : juce::AudioPlayHead {
	bool playing = false;
	double bpm = 120, ppq = 0;
	// A loop (cycle) on: run() wraps the playhead at the block after it passes loopEnd, as a host
	// that does not split its blocks at the loop, or with splitAtLoop ends a block on the loop's end
	// (as Cubase does); reportLoop says whether the host tells the loop points.
	bool looping = false, reportLoop = true, splitAtLoop = false;
	double loopStart = 0, loopEnd = 4;
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(playing);
		p.setBpm(bpm);
		p.setPpqPosition(ppq);
		p.setTimeSignature(TimeSignature{4, 4});
		if (looping && reportLoop) {
			p.setIsLooping(true);
			p.setLoopPoints(LoopPoints{loopStart, loopEnd});
		}
		return p;
	}
};

juce::MemoryBlock load(const char *name)
{
	juce::MemoryBlock b;
	juce::File(juce::String(TR909_ROM_DIR) + "/" + name).loadFileAsData(b);
	return b;
}

struct Roms {
	juce::MemoryBlock fw = load("tr909_v4.ic609"), hh = load("hn61256p__c43.ic69"), cr = load("hn61256p__c42.ic62"),
	                  rd = load("hn61256p__c44.ic54"), v1 = load("tr909_v1.ic609");
};

double rms(const juce::AudioBuffer<float> &b, int ch, int from, int n)
{
	double s = 0;
	int k = 0;
	for (int i = std::max(0, from); i < from + n && i < b.getNumSamples(); ++i, ++k)
		s += double(b.getSample(ch, i)) * b.getSample(ch, i);
	return std::sqrt(s / std::max(1, k));
}

// Runs the processor for `seconds`, the MIDI given at its sample positions; returns the main output
// (L, R) and, with jacks, the ten MULTI OUT buses after it; with midiOut, the MIDI it sends at its
// sample positions from the start.
juce::AudioBuffer<float> run(TR909Processor &p, FakePlayHead &ph, double seconds, double rate, const juce::MidiBuffer *midiAll = nullptr,
                             bool jacks = false, juce::MidiBuffer *midiOut = nullptr, int block = 512)
{
	const int total = int(seconds * rate);
	const int channels = jacks ? 2 + tr909::kVoices : 2;
	juce::AudioBuffer<float> out(channels, total);
	juce::AudioBuffer<float> buf(channels, block);
	for (int pos = 0, n = 0; pos < total; pos += n) {
		n = std::min(block, total - pos);
		if (ph.playing && ph.looping && ph.splitAtLoop && ph.ppq < ph.loopEnd)
			n = std::max(1, std::min(n, int(std::lround((ph.loopEnd - ph.ppq) * 60.0 / ph.bpm * rate))));
		buf.setSize(channels, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		if (midiAll)
			for (const auto m : *midiAll)
				if (m.samplePosition >= pos && m.samplePosition < pos + n)
					midi.addEvent(m.getMessage(), m.samplePosition - pos);
		p.processBlock(buf, midi);
		if (midiOut)
			midiOut->addEvents(midi, 0, n, pos);
		for (int ch = 0; ch < channels; ++ch)
			out.copyFrom(ch, pos, buf, ch, 0, n);
		if (ph.playing) {
			const double was = ph.ppq;
			ph.ppq += n / rate * ph.bpm / 60.0;
			if (ph.looping && was < ph.loopEnd && ph.ppq >= ph.loopEnd - 1e-9)
				ph.ppq -= ph.loopEnd - ph.loopStart;
		}
	}
	return out;
}

void save(const juce::File &dir, const char *name, const juce::AudioBuffer<float> &b, double rate)
{
	if (dir == juce::File())
		return;
	auto f = dir.getChildFile(name);
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	const auto opts = juce::AudioFormatWriterOptions{}
	                      .withSampleRate(rate)
	                      .withNumChannels(std::min(2, b.getNumChannels()))
	                      .withBitsPerSample(32)
	                      .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	auto w = wav.createWriterFor(s, opts);
	w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

int failures = 0;
void check(bool ok, const char *what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++failures;
}

void set(TR909Processor &p, const char *id, float v)
{
	p.params().getParameter(id)->setValueNotifyingHost(v);
}

// Enables every MULTI OUT output, with a cable in each jack.
void allJacks(TR909Processor &p)
{
	auto layout = p.getBusesLayout();
	for (int b = 1; b < layout.outputBuses.size(); ++b)
		layout.outputBuses.getReference(b) = juce::AudioChannelSet::mono();
	p.setBusesLayout(layout);
	for (int v = 0; v < tr909::kVoices; ++v)
		p.setCable(tr909::Voice(v), true);
}

const char *const kVoiceNames[tr909::kVoices] = {"BD", "SD", "LT", "MT", "HT", "RS", "HC", "HH", "CR", "RD"};

// Each voice from a MIDI note: its jack sounds, louder accented.
void voices(TR909Processor &p, const juce::File &dir, const char *tag, double rate)
{
	FakePlayHead ph;
	p.setPlayHead(&ph);
	allJacks(p);
	p.prepareToPlay(rate, 512);
	run(p, ph, 1.0, rate); // the firmware boots: it takes MIDI after its 0.6 s power-on delay
	for (int v = 0; v < tr909::kVoices; ++v) {
		juce::MidiBuffer midi;
		const int note = TR909Processor::noteFor(tr909::Voice(v));
		midi.addEvent(juce::MidiMessage::noteOn(10, note, juce::uint8(64)), int(0.05 * rate));
		midi.addEvent(juce::MidiMessage::noteOn(10, note, juce::uint8(127)), int(0.55 * rate));
		const auto out = run(p, ph, 1.05, rate, &midi, true);
		const int ch = 2 + v;
		const double soft = rms(out, ch, int(0.05 * rate), int(0.1 * rate)), loud = rms(out, ch, int(0.55 * rate), int(0.1 * rate));
		char what[160];
		std::snprintf(what, sizeof what, "%s: %s note %d sounds at its jack (%.1f / %.1f dBFS rms, accent %+.1f dB)", tag, kVoiceNames[v], note,
		              20 * std::log10(soft + 1e-12), 20 * std::log10(loud + 1e-12), 20 * std::log10((loud + 1e-12) / (soft + 1e-12)));
		const bool silentOk = (v == int(tr909::Voice::HH) || v == int(tr909::Voice::CR) || v == int(tr909::Voice::RD)) && !p.sampleRomLoaded(tr909::Voice(v));
		check(silentOk || (soft > 1e-4 && loud > soft), what);
		char name[64];
		std::snprintf(name, sizeof name, "%s-%s.wav", tag, kVoiceNames[v]);
		juce::AudioBuffer<float> mono(1, out.getNumSamples());
		mono.copyFrom(0, 0, out, ch, 0, out.getNumSamples());
		save(dir, name, mono, rate);
	}
}

// Idle: once a hit has died away the outputs are exact zeros, and the next hit sounds as the first.
void sleep(TR909Processor &p, const juce::File &dir, double rate)
{
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	// until the board sleeps after its power-on: the firmware's delay, its power-up trigger (HH,
	// crash and ride at DAC code 0) and the master mute's C143 charging
	double booted = 0;
	for (; booted < 120 && (booted < 4 || !p.voiceAsleep()); booted += 1)
		run(p, ph, 1.0, rate);
	std::printf("        sleep: asleep %.0f s after power-on\n", booted);
	juce::MidiBuffer a, b;
	a.addEvent(juce::MidiMessage::noteOn(10, 36, juce::uint8(100)), int(0.01 * rate));
	const auto first = run(p, ph, 4.0, rate, &a);
	const int tail = int(0.5 * rate);
	bool zeros = true;
	for (int i = first.getNumSamples() - tail; i < first.getNumSamples(); ++i)
		zeros = zeros && first.getSample(0, i) == 0.0f && first.getSample(1, i) == 0.0f;
	check(zeros && p.voiceAsleep(), "sleep: exact zeros once a bass drum has died away");
	b.addEvent(juce::MidiMessage::noteOn(10, 36, juce::uint8(100)), int(0.01 * rate));
	const auto second = run(p, ph, 0.5, rate, &b);
	save(dir, "sleep-first.wav", first, rate);
	save(dir, "sleep-second.wav", second, rate);
	// The same hit, as its envelope in 25 ms windows (longer than the BD's 20 ms period: shorter ones
	// swing with a fraction of a millisecond of onset): the noise generator has run on between them,
	// the BD's VCO runs freely between hits (its phase at the trigger moves the hit by up to 0.4 dB,
	// awake or not), and the firmware's scan places each trigger within its 2 ms tick, so the samples
	// differ.
	auto onset = [](const juce::AudioBuffer<float> &x) {
		const float peak = x.getMagnitude(0, 0, x.getNumSamples());
		for (int i = 0; i < x.getNumSamples(); ++i)
			if (std::abs(x.getSample(0, i)) > 0.05f * peak)
				return i;
		return 0;
	};
	const int o1 = onset(first), o2 = onset(second);
	double worst = 0, ref = 0;
	const int w = int(0.025 * rate);
	// over the hit's first 30 dB (below that a steep tail turns a fraction of a millisecond into dB)
	for (int i = 0; std::max(o1, o2) + i + w < second.getNumSamples() && i < int(0.4 * rate); i += w)
		ref = std::max(ref, rms(first, 0, o1 + i, w));
	for (int i = 0; std::max(o1, o2) + i + w < second.getNumSamples() && i < int(0.4 * rate); i += w) {
		const double a = rms(first, 0, o1 + i, w), c = rms(second, 0, o2 + i, w);
		if (a > ref * 0.0316)
			worst = std::max(worst, std::abs(20 * std::log10((c + 1e-12) / a)));
	}
	char what[128];
	std::snprintf(what, sizeof what, "sleep: a hit after waking sounds as the first (envelope within %.2f dB)", worst);
	check(ref > 0 && worst < 1.0, what);
}

// SYNC: the firmware goes into MIDI sync and follows the host's clock.
void sync(TR909Processor &p, double rate)
{
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 1.0, rate); // past the firmware's power-on delay
	set(p, "sync", 1.0f);
	run(p, ph, 1.0, rate);
	check(p.snapshot().syncMode == 1, "sync: SYNC puts the firmware in MIDI sync mode");
	set(p, "sync", 0.0f);
	run(p, ph, 2.0, rate);
	check(p.snapshot().syncMode == 0, "sync: and back to its internal clock");
}

// How far (seconds) the stretch of x from b lags the one from a, by the envelopes (rms in 0.25 ms
// frames, left channel) over len samples, searched within maxLag; and their normalised correlation
// there. Silence on either side correlates 0.
std::pair<double, double> lag(const juce::AudioBuffer<float> &x, int a, int b, int len, double maxLag, double rate)
{
	const int frame = std::max(1, int(rate / 4000));
	auto env = [&](int from, int frames) {
		std::vector<double> e(size_t(std::max(0, frames)));
		for (int f = 0; f < frames; ++f)
			e[size_t(f)] = rms(x, 0, from + f * frame, frame);
		return e;
	};
	const int frames = len / frame, reach = int(maxLag * rate) / frame;
	const auto ea = env(a, frames), eb = env(b - reach * frame, frames + 2 * reach);
	double best = 0;
	int at = 0;
	for (int k = 0; k <= 2 * reach; ++k) {
		double ab = 0, aa = 0, bb = 0;
		for (int f = 0; f < frames; ++f) {
			ab += ea[size_t(f)] * eb[size_t(f + k)];
			aa += ea[size_t(f)] * ea[size_t(f)];
			bb += eb[size_t(f + k)] * eb[size_t(f + k)];
		}
		const double c = aa > 0 && bb > 0 ? ab / std::sqrt(aa * bb) : 0;
		if (c > best) {
			best = c;
			at = k - reach;
		}
	}
	return {at * frame / rate, best};
}

// A session as 0.2.9 and before saved it: the battery RAM without the whole board.
juce::MemoryBlock ramOnly(const juce::MemoryBlock &s)
{
	auto xml = juce::AudioProcessor::getXmlFromBinary(s.getData(), int(s.getSize()));
	juce::MemoryBlock out;
	if (xml) {
		xml->removeAttribute("board");
		xml->removeAttribute("running");
		juce::AudioProcessor::copyXmlToBinary(*xml, out);
	}
	return out;
}

// The bar of x from `from` against the reference's first bar (sync's lag, both in one buffer).
std::pair<double, double> barLag(const juce::AudioBuffer<float> &ref, const juce::AudioBuffer<float> &x, int from, int bar, double rate)
{
	juce::AudioBuffer<float> both(2, 2 * bar + int(0.1 * rate));
	both.clear();
	both.copyFrom(0, 0, ref, 0, 0, std::min(bar, ref.getNumSamples()));
	both.copyFrom(0, bar + int(0.05 * rate), x, 0, from, std::min(bar, x.getNumSamples() - from));
	return lag(both, 0, bar + int(0.05 * rate), bar - int(0.05 * rate), 0.15, rate);
}

// SYNC after a session load (forum t/67: Logic doesn't process a stopped track, so a bounce made
// right after a project opened ran the sync macro during the bounce, and every step came a 16th
// late, two with 4096-sample blocks): a load, then the host playing from the top at once, plays
// the bar the 909 played before the save, in time; from a whole-board session and a RAM-only one,
// at 512 and 4096 samples. Play and Stop inside the 16th the start waits for (no start goes out)
// leave the next start playing on (a stop with no start before it left every later start playing
// one step). A session saved while the host played (an autosave) reopens stopped, the host stopped.
void syncLoad(const Roms &r, double rate)
{
	auto synced = [&](TR909Processor &p, FakePlayHead &ph) {
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		run(p, ph, 1.0, rate);
		set(p, "sync", 1.0f);
		run(p, ph, 1.0, rate);
	};
	TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
	FakePlayHead ph;
	synced(a, ph);
	juce::MemoryBlock s;
	a.getStateInformation(s);
	const int bar = int(std::lround(4 * 60.0 / ph.bpm * rate));
	ph.playing = true;
	ph.ppq = 0;
	const auto ref = run(a, ph, 1.2 * bar / rate, rate);
	ph.playing = false;
	run(a, ph, 0.5, rate);
	const auto old = ramOnly(s);
	for (const juce::MemoryBlock *state : std::initializer_list<const juce::MemoryBlock *>{&s, &old})
		for (const int block : {512, 4096}) {
			TR909Processor b(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
			FakePlayHead pb;
			b.setPlayHead(&pb);
			b.prepareToPlay(rate, block);
			b.setStateInformation(state->getData(), int(state->getSize()));
			pb.playing = true;
			pb.ppq = 0;
			const auto x = run(b, pb, 1.2 * bar / rate, rate, nullptr, false, nullptr, block);
			const auto [late, corr] = barLag(ref, x, 0, bar, rate);
			char line[200];
			std::snprintf(line, sizeof line, "sync: a %s session loaded, the host playing at once (%d samples), plays in time (%+.1f ms, correlation %.3f)",
			              state == &s ? "whole-board" : "RAM-only", block, late * 1000, corr);
			check(std::abs(late) <= 0.003 && corr > 0.8, line);
		}
	{
		ph.ppq = 0.1; // the start waits for the 16th at 0.25
		ph.playing = true;
		run(a, ph, 0.05, rate);
		ph.playing = false;
		run(a, ph, 0.3, rate);
		ph.ppq = 0;
		ph.playing = true;
		const auto x = run(a, ph, 1.2 * bar / rate, rate);
		ph.playing = false;
		run(a, ph, 0.5, rate);
		const auto [late, corr] = barLag(ref, x, 0, bar, rate);
		char line[200];
		std::snprintf(line, sizeof line, "sync: Play and Stop before the first step, then Play: the bar plays on, in time (%+.1f ms, correlation %.3f)",
		              late * 1000, corr);
		check(rms(x, 0, bar / 2, bar / 2) > 1e-3 && std::abs(late) <= 0.003 && corr > 0.8, line);
	}
	{
		ph.ppq = 0;
		ph.playing = true;
		run(a, ph, 1.0, rate);
		const bool was = a.snapshot().running;
		juce::MemoryBlock playing;
		a.getStateInformation(playing);
		ph.playing = false;
		TR909Processor b(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
		FakePlayHead pb;
		b.setPlayHead(&pb);
		b.prepareToPlay(rate, 512);
		b.setStateInformation(playing.getData(), int(playing.getSize()));
		const auto x = run(b, pb, 1.0, rate);
		check(was && !b.snapshot().running && rms(x, 0, int(0.5 * rate), int(0.5 * rate)) < 1e-4,
		      "sync: a session saved while the host played reopens stopped, the host stopped");
		pb.playing = true;
		pb.ppq = 0;
		const auto y = run(b, pb, 1.2 * bar / rate, rate);
		const auto [late, corr] = barLag(ref, y, 0, bar, rate);
		char line[200];
		std::snprintf(line, sizeof line, "sync: and then plays the host's bar in time (%+.1f ms, correlation %.3f)", late * 1000, corr);
		check(std::abs(late) <= 0.003 && corr > 0.8, line);
	}
}

// SYNC with the host looping (forum t/40: the 909 stopped at the loop's end and stayed silent):
// each pass of a one-bar loop plays pattern 1 as the first did, in time; so does a bar after a
// click back on the timeline while playing, and the first bar after a pre-roll. reportLoop: the
// host tells its loop points (the wrap is placed inside the block) or not (the jump is seen at
// the next block, up to a block late).
void hostLoop(const Roms &r, double rate, bool reportLoop, bool split = false, double bpm = 120)
{
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
	FakePlayHead ph;
	ph.bpm = bpm;
	ph.splitAtLoop = split;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 1.0, rate);
	set(p, "sync", 1.0f);
	run(p, ph, 1.0, rate);
	const int bar = int(std::lround(4 * 60.0 / ph.bpm * rate));
	// within the firmware's panel scan (2.1-2.3 ms, where it places each step's triggers) and the
	// MIDI bytes; without the loop points told, up to a block late and the stop's gap too
	const double tol = 0.003 + (reportLoop ? 0.0 : 512 / rate + 0.004);
	char how[96];
	std::snprintf(how, sizeof how, "%s%s, %.0f BPM", reportLoop ? "loop points told" : "loop points not told",
	              split ? ", blocks split at the loop" : "", bpm);
	auto same = [&](const juce::AudioBuffer<float> &ref, const juce::AudioBuffer<float> &x, int from, const char *what) {
		// the stretch of x from `from` against the reference's first bar, both in one buffer
		juce::AudioBuffer<float> both(2, 2 * bar + int(0.1 * rate));
		both.clear();
		both.copyFrom(0, 0, ref, 0, 0, bar);
		both.copyFrom(0, bar + int(0.05 * rate), x, 0, from, std::min(bar, x.getNumSamples() - from));
		const auto [late, corr] = lag(both, 0, bar + int(0.05 * rate), bar - int(0.05 * rate), 0.03, rate);
		char line[200];
		std::snprintf(line, sizeof line, "sync (%s): %s (%+.1f ms, correlation %.3f)", how, what, late * 1000, corr);
		check(std::abs(late) <= tol && corr > 0.8, line);
	};

	ph.playing = true;
	ph.ppq = 0;
	ph.looping = true;
	ph.reportLoop = reportLoop;
	ph.loopStart = 0;
	ph.loopEnd = 4;
	const auto loop = run(p, ph, 4.0 * bar / rate, rate);
	{
		char line[160];
		std::snprintf(line, sizeof line, "sync (%s): the host's transport plays pattern 1", how);
		check(rms(loop, 0, 0, bar) > 1e-3, line);
	}
	same(loop, loop, bar, "the second pass of a one-bar loop as the first");
	same(loop, loop, 3 * bar, "the fourth pass as the first");

	// stopped, then a click back to the top while playing (no loop)
	ph.playing = false;
	ph.looping = false;
	run(p, ph, 0.5, rate);
	ph.playing = true;
	ph.ppq = 0;
	run(p, ph, 1.5, rate);
	ph.ppq = 0;
	same(loop, run(p, ph, double(bar) / rate, rate), 0, "a bar after a click back to the top while playing");

	// a start from a pre-roll: the first bar from the top in time
	ph.playing = false;
	run(p, ph, 0.5, rate);
	ph.playing = true;
	ph.ppq = -1;
	const int pre = int(std::lround(60.0 / ph.bpm * rate));
	same(loop, run(p, ph, double(pre + bar) / rate, rate), pre, "the first bar after a beat of pre-roll");
}

// Each hit's start on channel `ch` against the grid line it belongs to (every `period` s from
// `from` s), less `latency` samples (what the host takes off): in the window from 3 ms before the
// line to 12 ms after, where the level (abs, 0.25 ms average) first rises a tenth of the way from
// the tail before it (the highest level in the 15 ms before the window: a BD rings at 50 Hz or
// so) to the window's peak. Lines without a new hit (12 dB over that tail) are skipped.
std::vector<double> hitLags(const juce::AudioBuffer<float> &b, int ch, double rate, double from, double period, int latency)
{
	const int w = std::max(1, int(0.00025 * rate));
	auto level = [&](int i) {
		double s = 0;
		for (int j = std::max(0, i - w + 1); j <= i; ++j)
			s += std::abs(b.getSample(ch, j));
		return s / w;
	};
	std::vector<double> lags;
	for (int k = 0;; ++k) {
		const double line = from + k * period;
		const int a = int(std::lround((line - 0.003) * rate)) + latency, e = a + int(0.015 * rate);
		if (e >= b.getNumSamples())
			break;
		const int tailFrom = a - int(0.015 * rate);
		if (tailFrom < w)
			continue;
		double before = 0;
		for (int i = tailFrom; i < a; ++i)
			before = std::max(before, level(i));
		double peak = before;
		for (int i = a; i < e; ++i)
			peak = std::max(peak, level(i));
		if (peak < 1e-3 || peak < 4 * before)
			continue;
		for (int i = a; i < e; ++i)
			if (level(i) > before + 0.1 * (peak - before)) {
				lags.push_back((i - latency) / rate - line);
				break;
			}
	}
	return lags;
}

// The reported latency is the firmware's lag behind the host's grid in SYNC: the factory pattern's
// BD (at its MULTI OUT) starts that long after the grid on average, each step within the panel
// scan of that, where voice mode's (no firmware: the note's BD at once) starts after its note; a
// MIDI note's BD through the firmware starts there too on average (the firmware takes notes at
// its scan; the plugin holds them back by the rest), so with the latency taken off all three sit
// on the grid.
void syncLatency(const Roms &r, double rate)
{
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
	allJacks(p);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 3.0, rate); // past the power-on mute
	set(p, "sync", 1.0f);
	run(p, ph, 1.0, rate);
	const int latency = p.getLatencySamples(), bd = 2 + int(tr909::Voice::BD);
	const double sixteenth = 60.0 / ph.bpm / 4;
	ph.playing = true;
	ph.ppq = 0;
	const auto played = run(p, ph, 8 * 4 * 60.0 / ph.bpm, rate, nullptr, true);
	ph.playing = false;
	run(p, ph, 1.0, rate);
	auto steps = hitLags(played, bd, rate, 0, sixteenth, latency);
	juce::MidiBuffer notes;
	for (int k = 0; k < 16; ++k)
		notes.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)),
		               int(std::lround((0.1 + k * 0.25) * rate)));
	const auto byMidi = run(p, ph, 4.5, rate, &notes, true);
	const auto midiLags = hitLags(byMidi, bd, rate, 0.1, 0.25, latency);
	// the voice alone: voice mode (no firmware) plays a note's BD at once, after the latency
	TR909Processor bare(nullptr, &r.hh, &r.cr, &r.rd);
	allJacks(bare);
	FakePlayHead stopped;
	bare.setPlayHead(&stopped);
	bare.prepareToPlay(rate, 512);
	run(bare, stopped, 3.0, rate);
	const auto voiceLags = hitLags(run(bare, stopped, 4.5, rate, &notes, true), bd, rate, 0.1, 0.25, bare.getLatencySamples());
	auto stats = [](const std::vector<double> &v) {
		double lo = 1, hi = -1, s = 0;
		for (double x : v) {
			lo = std::min(lo, x);
			hi = std::max(hi, x);
			s += x;
		}
		return std::array<double, 3>{v.empty() ? 0 : s / double(v.size()), lo, hi};
	};
	const auto st = stats(steps), mi = stats(midiLags), vo = stats(voiceLags);
	std::printf("      latency %d samples (%.2f ms); less that, the pattern's BD starts %.2f ms after the grid on average (%.2f to %.2f, %zu "
	            "hits), MIDI's %.2f (%.2f to %.2f), voice mode's %.2f\n",
	            latency, latency * 1000 / rate, st[0] * 1000, st[1] * 1000, st[2] * 1000, steps.size(), mi[0] * 1000, mi[1] * 1000,
	            mi[2] * 1000, vo[0] * 1000);
	check(steps.size() >= 16 && voiceLags.size() == 16 && std::abs(st[0] - vo[0]) < 0.0005,
	      "sync latency: with it taken off, the sequencer's BD starts where voice mode's does (on its note), on average");
	check(st[1] - vo[0] > -0.0015 && st[2] - vo[0] < 0.0015, "sync latency: each step within 1.5 ms of that");
	check(midiLags.size() == 16 && std::abs(mi[0] - vo[0]) < 0.0005, "sync latency: and MIDI's BD, through the firmware, there too on average");
}

// The factory patterns (from the v1.0 ROM's loader): a new instance holds them, START plays
// pattern 1, the sequencer's run state follows, and STOP/CONT stops it.
void factory(const Roms &r, const juce::File &dir, double rate)
{
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd, {}, &r.v1);
	check(p.hasFactoryPatterns(), "factory: the v1.0 ROM's loader makes the factory memory");
	TR909Processor blank(&r.fw, &r.hh, &r.cr, &r.rd);
	check(p.ram() != blank.ram(), "factory: a new instance holds it");
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 1.0, rate);
	p.setKey(TR909Processor::Key::Start, true);
	run(p, ph, 0.05, rate);
	p.setKey(TR909Processor::Key::Start, false);
	juce::MidiBuffer sent;
	const auto out = run(p, ph, 4.0, rate, nullptr, false, &sent);
	check(p.snapshot().running && rms(out, 0, 0, out.getNumSamples()) > 1e-3, "factory: START plays pattern 1, and the 909 says it runs");
	// MIDI OUT: the internal voices are never sent (docs/firmware.md, "Out"), and the clock and
	// START stay in the plugin
	check(sent.getNumEvents() == 0, "factory: MIDI OUT sends nothing for the internal voices, nor the clock");
	save(dir, "factory.wav", out, rate);
	p.setKey(TR909Processor::Key::StopCont, true);
	run(p, ph, 0.05, rate);
	p.setKey(TR909Processor::Key::StopCont, false);
	run(p, ph, 0.2, rate);
	check(!p.snapshot().running, "factory: STOP/CONT stops it");
}

void state(const Roms &r, double rate)
{
	TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd);
	set(a, "bd_tune", 0.2f);
	set(a, "tempo", 0.6f);
	auto ram = a.ram();
	ram[0x123] = 0x5a;
	a.setRam(ram);
	juce::MemoryBlock s;
	a.getStateInformation(s);
	TR909Processor b(&r.fw, &r.hh, &r.cr, &r.rd);
	b.setStateInformation(s.getData(), int(s.getSize()));
	check(std::abs(b.params().getRawParameterValue("bd_tune")->load() - 0.2f) < 1e-6 && b.ram()[0x123] == 0x5a,
	      "state: parameters and battery RAM round-trip");
	a.setCable(tr909::Voice::SD, true);
	a.setCable(tr909::Voice::RD, true);
	a.getStateInformation(s);
	TR909Processor c(&r.fw, &r.hh, &r.cr, &r.rd);
	c.setStateInformation(s.getData(), int(s.getSize()));
	bool same = true;
	for (int v = 0; v < tr909::kVoices; ++v)
		same = same && c.cable(tr909::Voice(v)) == (v == int(tr909::Voice::SD) || v == int(tr909::Voice::RD));
	check(same, "state: the MULTI OUT cables round-trip");

	// The whole board (GitHub #4: the 909 came back in track play): pattern play on pattern 3, or
	// track play on track 2, reopens as saved, and so does a MIDI receive channel set on the panel
	// (SHUFFLE/FLAM + step 3 in track play: channel 3, omni off). A session of 0.2.9 and before
	// (RAM only) still loads, booting: track 1, channel 10.
	FakePlayHead ph;
	auto press = [&](TR909Processor &p, TR909Processor::Key k) {
		p.setKey(k, true);
		run(p, ph, 0.06, rate);
		p.setKey(k, false);
		run(p, ph, 0.1, rate);
	};
	auto chord = [&](TR909Processor &p, TR909Processor::Key hold, TR909Processor::Key k) {
		p.setKey(hold, true);
		run(p, ph, 0.04, rate);
		press(p, k);
		p.setKey(hold, false);
		run(p, ph, 0.1, rate);
	};
	// the step, track and pattern LEDs lit at any time over 0.6 s (some blink)
	auto panel = [&](TR909Processor &p) {
		std::vector<bool> on(size_t(tr909::Machine::Led::Pattern3) + 1, false);
		for (int k = 0; k < 30; ++k) {
			run(p, ph, 0.02, rate);
			const auto snap = p.snapshot();
			for (size_t l = 0; l < on.size(); ++l)
				on[l] = on[l] || snap.leds[l] > 0.5f;
		}
		return on;
	};
	auto lit = [](const std::vector<bool> &on, tr909::Machine::Led l) { return on[size_t(l)]; };
	auto reopened = [&](const juce::MemoryBlock &st) {
		auto q = std::make_unique<TR909Processor>(&r.fw, &r.hh, &r.cr, &r.rd);
		q->setPlayHead(&ph);
		q->prepareToPlay(rate, 512);
		q->setStateInformation(st.getData(), int(st.getSize()));
		run(*q, ph, 0.3, rate);
		return q;
	};
	using Key = TR909Processor::Key;
	using Led = tr909::Machine::Led;
	TR909Processor d(&r.fw, &r.hh, &r.cr, &r.rd);
	d.setPlayHead(&ph);
	d.prepareToPlay(rate, 512);
	run(d, ph, 1.0, rate);
	press(d, Key::Pattern1);
	press(d, Key::Step3);
	const auto pat = panel(d);
	d.getStateInformation(s);
	check(lit(pat, Led::Pattern1) && lit(pat, Led::Step3) && !lit(pat, Led::Track1) && panel(*reopened(s)) == pat,
	      "state: a session reopens pattern play on pattern 3 as saved");
	press(d, Key::Track2);
	const auto trk = panel(d);
	d.getStateInformation(s);
	check(lit(trk, Led::Track2) && !lit(trk, Led::Track1) && panel(*reopened(s)) == trk, "state: and track play on track 2");
	const auto booted = panel(*reopened(ramOnly(s)));
	check(lit(booted, Led::Track1) && !lit(booted, Led::Track2), "state: a RAM-only session (0.2.9 and before) loads, booting into track 1");
	// the receive channel: a BD note on channel 3 plays, one on 10 doesn't (under 0.02: the
	// channel-3 hit's tail is still there)
	auto bdOn = [&](TR909Processor &p, int channel) {
		juce::MidiBuffer m;
		m.addEvent(juce::MidiMessage::noteOn(channel, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(0.05 * rate));
		return run(p, ph, 0.3, rate, &m).getMagnitude(0, 0, int(0.3 * rate));
	};
	press(d, Key::Track1);
	chord(d, Key::ShuffleFlam, Key::Step3);
	run(d, ph, 0.5, rate);
	const bool set3 = bdOn(d, 3) > 0.05f && bdOn(d, 10) < 0.02f;
	d.getStateInformation(s);
	auto e = reopened(s);
	run(*e, ph, 0.5, rate);
	check(set3 && bdOn(*e, 3) > 0.05f && bdOn(*e, 10) < 0.02f, "state: a MIDI channel set on the panel reopens as saved");
	auto f = reopened(ramOnly(s));
	run(*f, ph, 0.5, rate);
	check(bdOn(*f, 10) > 0.05f, "state: from a RAM-only session it goes back to channel 10");
}

// TEMPO: the BPM the plugin shows for a knob (tempoBpm) is what the firmware runs at after a
// session load with the knob there, whichever knob it had before; a new instance runs at the
// default, 120.6 BPM (121 on the display). A knob turned later can run a step either side
// (docs/firmware.md, "Sync"): the panel then shows the firmware's own.
void tempo(const Roms &r, double rate)
{
	FakePlayHead ph;
	auto runsAt = [&](TR909Processor &p) {
		run(p, ph, 0.3, rate);
		return p.snapshot().tempo;
	};
	TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd), b(&r.fw, &r.hh, &r.cr, &r.rd);
	for (auto *p : {&a, &b}) {
		p->setPlayHead(&ph);
		p->prepareToPlay(rate, 512);
	}
	const int t = runsAt(b);
	check(std::abs(TR909Processor::tempoBpmOf(t) - 120.55) < 0.01 && t + 36 == 121, "tempo: a new instance runs at 120.6 BPM, 121 on the display");
	bool same = true, kept = true;
	for (const float v : {0.0f, 0.9f, 0.2f, 126 / 255.0f, 128 / 255.0f, 130 / 255.0f, 0.4f, 1.0f, 0.6f}) {
		set(a, "tempo", v);
		const int was = runsAt(a);
		juce::MemoryBlock s;
		a.getStateInformation(s);
		b.setStateInformation(s.getData(), int(s.getSize()));
		kept = kept && runsAt(b) == was;
		const auto old = ramOnly(s);
		b.setStateInformation(old.getData(), int(old.getSize()));
		same = same && TR909Processor::tempoBpmOf(runsAt(b)) == TR909Processor::tempoBpm(v);
	}
	check(kept, "tempo: a session load runs at the tempo it was saved at, across the travel");
	check(same, "tempo: a RAM-only session (0.2.9 and before) runs at the BPM shown for its knob, across the travel, whatever the knob was");
	set(b, "tempo", 0.7f);
	const double turned = TR909Processor::tempoBpmOf(runsAt(b));
	check(std::abs(turned - TR909Processor::tempoBpm(0.7f)) < 2.5, "tempo: a knob turned runs within a step of the BPM shown");
}

// The POWER switch off and on is the 909 switched on: the master outputs muted by Q82/Q83 while
// C143 charges (SN: about 1.8 s, -9 dB at 1.8 s, -2 dB at 2.0 s, within 0.5 dB from 2.5 s;
// docs/circuit/power.md), the MULTI OUTs not, so a cable in HI HAT hears the firmware's first
// writes raise the HH, crash and ride trigger lines (SN p.6). A new instance and a session load
// are a 909 that has been on a while: none of it, a hit at once at its full level.
void powerOn(const Roms &r, double rate)
{
	auto bdPeaks = [&](const std::vector<double> &times, double seconds, bool hhCable, bool cycle = true,
	                   const juce::MemoryBlock *session = nullptr) {
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		if (session != nullptr)
			p.setStateInformation(session->getData(), int(session->getSize()));
		if (hhCable) { // cables in HI HAT and CRASH: outputs 1 and 2 (channels 2 and 3)
			auto layout = p.getBusesLayout();
			for (auto v : {tr909::Voice::HH, tr909::Voice::CR}) {
				layout.outputBuses.getReference(1 + int(v)) = juce::AudioChannelSet::mono();
				p.setCable(v, true);
			}
			p.setBusesLayout(layout);
		}
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		if (cycle) {
			p.setPower(false);
			p.setPower(true);
		}
		juce::MidiBuffer midi;
		for (double t : times)
			midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(t * rate));
		const auto out = run(p, ph, seconds, rate, &midi, true);
		std::vector<double> peaks;
		for (double t : times) {
			float m = 0;
			for (int i = int(t * rate); i < int((t + 0.2) * rate); ++i)
				m = std::max(m, std::abs(out.getSample(0, i)));
			peaks.push_back(m);
		}
		float boot = 0, pulse = 0, thump = 0, after = 0;
		for (int i = 0; i < int(0.9 * rate); ++i) {
			boot = std::max(boot, std::abs(out.getSample(0, i)));
			pulse = std::max(pulse, std::abs(out.getSample(2, i)));
		}
		if (hhCable) {
			for (int i = 0; i < int(0.5 * rate); ++i) // before the firmware's pulse
				thump = std::max(thump, std::abs(out.getSample(3, i)));
			for (int i = int(2.5 * rate); i < int(2.9 * rate); ++i)
				after = std::max(after, std::abs(out.getSample(3, i)));
		}
		peaks.push_back(boot);
		peaks.push_back(pulse);
		peaks.push_back(thump);
		peaks.push_back(after);
		return peaks;
	};
	const std::vector<double> times = {1.0, 1.5, 1.8, 2.0, 2.5, 3.0, 4.0};
	const auto on = bdPeaks(times, 4.3, true);
	const double ref = bdPeaks({40.0}, 40.3, false)[0]; // long after: C143 charged
	{
		const auto warm = bdPeaks({0.02}, 3.0, true, false);
		TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd);
		juce::MemoryBlock session;
		a.getStateInformation(session);
		const auto loaded = bdPeaks({0.02}, 1.0, false, false, &session);
		const double w = 20 * std::log10((warm[0] + 1e-12) / ref), l = 20 * std::log10((loaded[0] + 1e-12) / ref);
		char what[220];
		std::snprintf(what, sizeof what,
		              "warm start: a new instance's hit at 20 ms %+.2f dB re 40 s on, a loaded session's %+.2f dB; "
		              "HI HAT jack %.0f dBFS, CRASH jack %.0f dBFS (no boot pulse, no thump)",
		              w, l, 20 * std::log10(warm[2] + 1e-12), 20 * std::log10(warm[3] + 1e-12));
		check(std::abs(w) < 0.5 && std::abs(l) < 0.5 && warm[2] < 1e-4 && warm[3] < 1e-4, what);
	}
	juce::String gains;
	std::vector<double> db;
	for (size_t k = 0; k < times.size(); ++k) {
		db.push_back(20 * std::log10((on[k] + 1e-12) / ref));
		gains << juce::String(times[k], 1) << " s " << juce::String(db.back(), 1) << "  ";
	}
	std::printf("        power-on (POWER off and on): a BD hit on the main outputs, re 40 s on: %s\n", gains.toRawUTF8());
	check(db[0] < -40 && db[4] > -1.5 && std::abs(db[6]) < 0.5, "power-on: the master outputs muted at first, open by 2.5 s");
	char what[200];
	std::snprintf(what, sizeof what, "power-on: the HI HAT jack hears the boot, its thump and pulse (%.1f dBFS), the muted main barely (%.1f dBFS)",
	              20 * std::log10(on[times.size() + 1] + 1e-12), 20 * std::log10(on[times.size()] + 1e-12));
	check(on[times.size() + 1] > 3e-3 && on[times.size()] < 1e-3, what);
	std::snprintf(what, sizeof what, "power-on: the CRASH jack thumps as its stage charges from 0 V (%.1f dBFS), then settles (%.1f dBFS at 2.5 s)",
	              20 * std::log10(on[times.size() + 2] + 1e-12), 20 * std::log10(on[times.size() + 3] + 1e-12));
	check(on[times.size() + 2] > 3e-2 && on[times.size() + 3] < 1e-3, what);
}

// The rear's POWER switch: off, nothing comes out (no audio, no MIDI) and the panel is dark; on,
// the 909 switches on again (the mute, then the hit); a session saved off loads switched on.
void powerSwitch(const Roms &r, double rate)
{
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 3.0, rate);
	p.setPower(false);
	juce::MidiBuffer notes, sent;
	notes.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(0.1 * rate));
	notes.addEvent(juce::MidiMessage::noteOn(3, 60, juce::uint8(100)), int(0.2 * rate)); // the merge would send it on
	const auto off = run(p, ph, 1.0, rate, &notes, false, &sent);
	const auto dark = p.snapshot();
	bool lit = false;
	for (float l : dark.leds)
		lit = lit || l > 0;
	check(!p.powered() && off.getMagnitude(0, off.getNumSamples()) == 0 && sent.getNumEvents() == 0 && !lit && !dark.powered,
	      "power: switched off, no sound, no MIDI out, the panel dark");
	p.setPower(true);
	juce::MidiBuffer early, late;
	early.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(1.0 * rate));
	const auto booting = run(p, ph, 3.0, rate, &early);
	late.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(0.1 * rate));
	const auto on = run(p, ph, 0.5, rate, &late);
	const float muted = booting.getMagnitude(0, int(1.0 * rate), int(0.3 * rate)), full = on.getMagnitude(0, 0, on.getNumSamples());
	char what[160];
	std::snprintf(what, sizeof what, "power: switched on again, a hit 1 s in is muted (%.0f dB), one at 3 s is not (%.1f dBFS)",
	              20 * std::log10((muted + 1e-12f) / (full + 1e-12f)), 20 * std::log10(full + 1e-12f));
	check(p.powered() && full > 0.05f && muted < full * 0.01f, what);
	p.setPower(false);
	juce::MemoryBlock s;
	p.getStateInformation(s);
	TR909Processor q(&r.fw, &r.hh, &r.cr, &r.rd);
	q.setPower(false);
	q.setStateInformation(s.getData(), int(s.getSize()));
	check(q.powered(), "power: a session saved switched off loads switched on");
}

// MULTI OUT: a cable in a jack (the rear panel) takes its voice out of the mix (the jack's
// switching contact) to that output, if the host has it on; without one the voice stays in the
// mix and the output is silent, whatever the host turns on (Onyx turns on every output).
void multiOut(const Roms &r, double rate)
{
	auto hits = [&](bool cable, bool output) {
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		if (output) { // stereo, as Onyx sets them: the jack on both sides
			auto layout = p.getBusesLayout();
			layout.outputBuses.getReference(1 + int(tr909::Voice::BD)) = juce::AudioChannelSet::stereo();
			p.setBusesLayout(layout);
		}
		p.setCable(tr909::Voice::BD, cable);
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		run(p, ph, 3.0, rate); // past the power-on mute
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(127)), int(0.05 * rate));
		midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::SD), juce::uint8(127)), int(0.55 * rate));
		const auto out = run(p, ph, 1.0, rate, &midi, true);
		const int jack = 2; // the BD's output, the only one on: channels 2 and 3
		double diff = 0;
		for (int i = 0; i < out.getNumSamples(); ++i)
			diff = std::max(diff, double(std::abs(out.getSample(jack, i) - out.getSample(jack + 1, i))));
		return std::array<double, 4>{rms(out, 0, int(0.05 * rate), int(0.3 * rate)), rms(out, 0, int(0.55 * rate), int(0.3 * rate)),
		                             output ? rms(out, jack, int(0.05 * rate), int(0.3 * rate)) : 0.0, diff};
	};
	const auto none = hits(false, true), in = hits(true, true), nowhere = hits(true, false);
	auto db = [](double a, double b) { return 20 * std::log10((a + 1e-12) / (b + 1e-12)); };
	char what[240];
	std::snprintf(what, sizeof what, "multi out: no cable, the BD stays in the mix and its output (on) is silent (%.1f dBFS)",
	              20 * std::log10(none[2] + 1e-12));
	check(none[0] > 1e-2 && none[2] == 0, what);
	std::snprintf(what, sizeof what,
	              "multi out: a cable in BD takes it out of the mix (%+.1f dB) to its output, on both sides (%.1f dBFS); the SD stays (%+.2f dB)",
	              db(in[0], none[0]), 20 * std::log10(in[2] + 1e-12), db(in[1], none[1]));
	check(db(in[0], none[0]) < -30 && in[2] > 1e-3 && in[3] == 0 && std::abs(db(in[1], none[1])) < 0.2, what);
	std::snprintf(what, sizeof what, "multi out: a cable into an output the host has off goes nowhere (the mix %+.1f dB)", db(nowhere[0], none[0]));
	check(db(nowhere[0], none[0]) < -30 && std::abs(db(nowhere[1], none[1])) < 0.2, what);
}

// SERVICE: the unit's serial number puts the production changes on the board (SN p.7), each heard
// on its voice and no other; TM moves the clap; both are saved with the session.
void service(const Roms &r, double rate)
{
	auto hit = [&](tr909::Voice v, int serial, float tm, const char *probe = nullptr) {
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		p.setSerial(serial);
		set(p, "tm", tm);
		allJacks(p);
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		run(p, ph, 3.0, rate);
		if (probe != nullptr)
			p.setProbe(0, probe);
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(v), juce::uint8(127)), int(0.05 * rate));
		const auto out = run(p, ph, 0.6, rate, &midi, true);
		std::vector<float> x(size_t(out.getNumSamples()));
		for (int i = 0; i < out.getNumSamples(); ++i)
			x[size_t(i)] = out.getSample(2 + int(v), i);
		return x;
	};
	auto change = [rate](const std::vector<float> &a, const std::vector<float> &b, double seconds = 0.6) {
		double d = 0, e = 0;
		for (size_t i = 0; i < std::min(a.size(), size_t((0.05 + seconds) * rate)); ++i) {
			d += double(a[i] - b[i]) * (a[i] - b[i]);
			e += double(a[i]) * a[i];
		}
		return 20 * std::log10(std::sqrt(d / (e + 1e-30)) + 1e-12); // the difference against the late board's, dB
	};
	const float tm = float(TR909Processor::factoryTm());
	char what[200];
	// each change on its voice: the last unit before it against the late board
	const std::pair<tr909::Voice, int> changed[] = {{tr909::Voice::BD, 381499}, {tr909::Voice::CR, 392599}, {tr909::Voice::RD, 392599},
	                                                {tr909::Voice::RS, 415299}, {tr909::Voice::HH, 415299}, {tr909::Voice::LT, 426699}};
	for (const auto &[v, serial] : changed) {
		// the tom noise is the toms' attack: their first 30 ms
		const double d = change(hit(v, tr909::kLateSerial, tm), hit(v, serial, tm), v == tr909::Voice::LT ? 0.03 : 0.6);
		std::snprintf(what, sizeof what, "service: SN %d's %s against the late board's: %.1f dB of difference", serial, kVoiceNames[int(v)], d);
		check(d > (v == tr909::Voice::LT ? -40 : -30), what);
	}
	// and none on a voice it doesn't touch: the last change is the toms' noise
	{
		const double d = change(hit(tr909::Voice::SD, tr909::kLateSerial, tm), hit(tr909::Voice::SD, 426699, tm));
		std::snprintf(what, sizeof what, "service: SN 426699's SD is the late board's (%.1f dB)", d);
		check(d < -120, what);
	}
	{
		const auto a = hit(tr909::Voice::HC, tr909::kLateSerial, tm), b = hit(tr909::Voice::HC, tr909::kLateSerial, std::min(1.0f, tm + 0.2f));
		std::snprintf(what, sizeof what, "service: TM moves the clap (%.1f dB of difference for +0.2 of its travel)", change(a, b));
		check(change(a, b) > -30, what);
	}
	// a probe doesn't change the sound: its partition is loaded again with the probe as an output,
	// which nudges the solver's path once, as it goes on (the clap's comparators then cross a
	// fraction of a sample apart): the envelope in 5 ms frames stays. A probe keeps the board awake
	// (the noise generator runs on), so the hit without it has one on the BD's partition.
	auto envelope = [rate](const std::vector<float> &a, const std::vector<float> &b) {
		const int f = int(0.005 * rate);
		std::vector<double> ea, eb;
		double top = 0;
		for (size_t i = 0; i + size_t(f) <= a.size(); i += size_t(f)) {
			double sa = 0, sb = 0;
			for (int k = 0; k < f; ++k) {
				sa += double(a[i + size_t(k)]) * a[i + size_t(k)];
				sb += double(b[i + size_t(k)]) * b[i + size_t(k)];
			}
			ea.push_back(std::sqrt(sa / f));
			eb.push_back(std::sqrt(sb / f));
			top = std::max(top, ea.back());
		}
		double worst = 0;
		for (size_t k = 0; k < ea.size(); ++k)
			if (ea[k] > top * 0.01)
				worst = std::max(worst, std::abs(20 * std::log10((eb[k] + 1e-12) / (ea[k] + 1e-12))));
		return worst;
	};
	for (const auto &[v, probe] : {std::pair<tr909::Voice, const char *>{tr909::Voice::HC, "hc.env1"}, {tr909::Voice::SD, "sd.out"}}) {
		const auto plain = hit(v, tr909::kLateSerial, tm, "bd.out"), probed = hit(v, tr909::kLateSerial, tm, probe);
		const double e = envelope(plain, probed);
		std::snprintf(what, sizeof what, "service: a probe on %s leaves the %s's envelope within %.2f dB (waveform %.1f dB)", probe,
		              kVoiceNames[int(v)], e, change(plain, probed));
		check(e < 0.5, what);
	}
	// the scope: a probe on the clap's netlist node "out" reads what the board hands the jack board
	// (jack.hc), sample for sample; the trigger lines come with them
	{
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		run(p, ph, 3.0, rate);
		p.setProbe(0, "hc.out");
		p.setProbe(1, "jack.hc");
		juce::MidiBuffer midi;
		midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::HC), juce::uint8(127)), int(0.05 * rate));
		run(p, ph, 0.3, rate, &midi);
		const int n = int(0.3 * p.probeRate());
		std::vector<float> a(static_cast<size_t>(n)), b(static_cast<size_t>(n)), t(static_cast<size_t>(n));
		const uint32_t end = p.probeWritten();
		p.readProbe(0, a.data(), n, end);
		p.readProbe(1, b.data(), n, end);
		p.readProbe(2, t.data(), n, end);
		double worst = 0, peak = 0;
		bool trig = false;
		for (int i = 0; i < n; ++i) {
			worst = std::max(worst, double(std::abs(a[size_t(i)] - b[size_t(i)])));
			peak = std::max(peak, double(std::abs(b[size_t(i)])));
			trig = trig || (uint32_t(t[size_t(i)]) >> int(tr909::Voice::HC) & 1);
		}
		std::snprintf(what, sizeof what, "service: the scope's probe on hc.out is the clap's jack (%.2f V peak, %.1e V apart), its TRIG seen",
		              peak, worst);
		check(p.probeFound(0) && p.probeFound(1) && peak > 0.5 && worst < 1e-4 && trig, what);
	}
	// every pin of every part the netlists draw is a node the scope can probe
	{
		tr909::VoiceBoard board;
		board.setCalibration(tr909::unitCalibration());
		board.prepare(48000);
		const auto &parts = tr909::VoiceBoard::parts();
		int pins = 0, missing = 0;
		std::map<std::string, int> kinds;
		std::string first;
		for (const auto &pt : parts) {
			++kinds[pt.ref.substr(0, pt.ref.find_first_of("0123456789"))];
			for (const auto &[pin, node] : pt.pins) {
				++pins;
				if (!board.setProbe(0, node)) {
					++missing;
					if (missing <= 8)
						first += pt.ref + " " + pin + " " + node + "; ";
				}
			}
		}
		board.setProbe(0, "");
		std::snprintf(what, sizeof what, "service: %d parts (%d R, %d C, %d D, %d Q, %d IC sections, %d VR), %d pins, %d not probeable %s",
		              int(parts.size()), kinds["R"], kinds["C"], kinds["D"], kinds["Q"], kinds["IC"], kinds["VR"], pins, missing, first.c_str());
		check(parts.size() > 850 && missing == 0, what);
	}
	TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd);
	a.setSerial(392600);
	set(a, "tm", 0.4f);
	juce::MemoryBlock s;
	a.getStateInformation(s);
	TR909Processor b(&r.fw, &r.hh, &r.cr, &r.rd);
	b.setStateInformation(s.getData(), int(s.getSize()));
	check(b.serial() == 392600 && std::abs(b.params().getRawParameterValue("tm")->load() - 0.4f) < 1e-6,
	      "service: the serial number and TM round-trip with the session");
	{ // CIRCUIT by the mouse: EARLY, MIDDLE, LATE; b's unit (392600, between the ages) kept until one is picked
		tr909::HardwareView view(b);
		view.setBounds(0, 0, tr909::HardwareView::kWidth, tr909::HardwareView::kHeight);
		view.showService(true);
		const bool kept = b.serial() == 392600 && tr909::ageOf(b.serial()) < 0;
		int got[3] = {};
		for (int i : {0, 1, 2}) {
			const auto c = tr909::HardwareView::serviceAgeRow(i).getCentre();
			const auto now = juce::Time::getCurrentTime();
			const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), c, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view, now, c, now, 1, false);
			view.mouseDown(e);
			view.mouseUp(e);
			got[i] = b.serial();
		}
		check(kept && got[0] == tr909::kFirstSerial && got[1] == 371500 && got[2] == tr909::kLateSerial,
		      "service: CIRCUIT's EARLY, MIDDLE and LATE by the mouse; a session's unit between them (SN 392600) loads as it was");
	}
}

// BD pitch (SERVICE's PARTS, C14 R27): the kick settles where the slider says, from the measured unit's 49.35 Hz
// (its C14 and R27 exactly, by default) to the printed values' 54.19 Hz; moving it builds the BD
// alone again, as a fresh unit with those parts would play it, while the other voices ring on; it
// is saved with the session.
void bdPitch(const Roms &r, double rate)
{
	char what[240];
	{
		const auto cal = TR909Processor::calibration(tr909::kLateSerial, tr909::kBdPitchUnitHz), unit = tr909::unitCalibration();
		check(cal.parts.at("bd.c14") == unit.parts.at("bd.c14") && cal.parts.at("bd.r27") == unit.parts.at("bd.r27"),
		      "bd pitch: at its default, the measured unit's C14 and R27 exactly");
	}
	// a BD hit at full DECAY and TUNE 0 (settled by 80 ms), its jack; `during` moves BD PITCH to `to`
	// (0-1) 0.3 s into an SD hit
	auto hits = [&](float from, float to, bool during) {
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		set(p, "bdpitch", from);
		set(p, "bd_decay", 1.0f);
		set(p, "bd_tune", 0.0f);
		allJacks(p);
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		run(p, ph, 3.0, rate);
		juce::MidiBuffer sd;
		sd.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::SD), juce::uint8(127)), int(0.05 * rate));
		auto a = run(p, ph, 0.3, rate, &sd, true);
		if (during)
			set(p, "bdpitch", to);
		auto b = run(p, ph, 1.2, rate, nullptr, true);
		juce::MidiBuffer bd;
		bd.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(127)), int(0.05 * rate));
		auto c = run(p, ph, 1.0, rate, &bd, true);
		std::vector<float> snare, kick;
		for (const auto *buf : {&a, &b})
			for (int i = 0; i < buf->getNumSamples(); ++i)
				snare.push_back(buf->getSample(2 + int(tr909::Voice::SD), i));
		for (int i = 0; i < c.getNumSamples(); ++i)
			kick.push_back(c.getSample(2 + int(tr909::Voice::BD), i));
		return std::pair{snare, kick};
	};
	// the kick's pitch from 0.4 to 0.9 s: its rising zero crossings, about its mean, after two
	// one-pole low-passes at 150 Hz (the body's harmonics out)
	auto settled = [rate](const std::vector<float> &x) {
		const size_t from = size_t(0.4 * rate), to = size_t(0.9 * rate);
		double mean = 0;
		for (size_t i = from; i < to; ++i)
			mean += x[i];
		mean /= double(to - from);
		const double k = 1 - std::exp(-2 * juce::MathConstants<double>::pi * 150 / rate),
		             h = 1 - std::exp(-2 * juce::MathConstants<double>::pi * 15 / rate);
		double y1 = 0, y2 = 0, d1 = 0, d2 = 0;
		std::vector<double> y;
		for (size_t i = from; i < to; ++i) {
			y1 += k * (x[i] - mean - y1);
			y2 += k * (y1 - y2);
			d1 += h * (y2 - d1); // and two high-passes at 15 Hz: the jack's settling DC out
			d2 += h * (y2 - d1 - d2);
			y.push_back(y2 - d1 - d2);
		}
		std::vector<double> t;
		for (size_t i = size_t(0.05 * rate); i < y.size(); ++i) // past the filters' start
			if (y[i - 1] < 0 && y[i] >= 0)
				t.push_back(double(i - 1) + y[i - 1] / (y[i - 1] - y[i]));
		return t.size() > 2 ? rate * double(t.size() - 1) / (t.back() - t.front()) : 0.0;
	};
	auto apart = [](const std::vector<float> &a, const std::vector<float> &b) {
		double d = 0, e = 0;
		for (size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
			d += double(a[i] - b[i]) * (a[i] - b[i]);
			e += double(a[i]) * a[i];
		}
		return 20 * std::log10(std::sqrt(d / (e + 1e-30)) + 1e-12);
	};
	const auto unit = hits(0, 0, false), printed = hits(1, 1, false), moved = hits(0, 1, true);
	const double fu = settled(unit.second), fp = settled(printed.second), cents = 1200 * std::log2(fp / fu);
	std::snprintf(what, sizeof what, "bd pitch: the kick settles at %.2f Hz by default, %.2f Hz at the top (%+.0f cents)", fu, fp, cents);
	check(std::abs(fu - 49.35) < 0.3 && std::abs(fp - 54.19) < 0.3, what);
	const double kick = apart(printed.second, moved.second), snare = apart(unit.first, moved.first);
	std::snprintf(what, sizeof what,
	              "bd pitch: moved while the SD rings, the next kick is a fresh unit's (%.1f dB apart), the SD untouched (%.1f dB)", kick, snare);
	check(kick < -50 && snare < -200, what);
	TR909Processor a(&r.fw, &r.hh, &r.cr, &r.rd);
	set(a, "bdpitch", 0.6f);
	juce::MemoryBlock st;
	a.getStateInformation(st);
	TR909Processor b(&r.fw, &r.hh, &r.cr, &r.rd);
	b.setStateInformation(st.getData(), int(st.getSize()));
	std::snprintf(what, sizeof what, "bd pitch: round-trips with the session (%s)",
	              b.params().getParameter("bdpitch")->getCurrentValueAsText().toRawUTF8());
	check(std::abs(b.params().getParameter("bdpitch")->getValue() - 0.6f) < 1e-6, what);
}

// MIDI OUT as a merge: what comes in goes out again through the CPU's serial port, a little later
// (the bytes at 31250 baud and the firmware's scan), at its own sample; the input itself is not
// passed on.
void midiThru(const Roms &r, double rate)
{
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	run(p, ph, 1.0, rate);
	juce::MidiBuffer in, out;
	const int at = int(0.1 * rate) + 100;
	in.addEvent(juce::MidiMessage::noteOn(3, 60, juce::uint8(100)), at);
	in.addEvent(juce::MidiMessage::noteOff(3, 60), at + int(0.05 * rate)); // (control changes set the knobs)
	run(p, ph, 0.3, rate, &in, false, &out);
	std::vector<std::pair<int, juce::MidiMessage>> got;
	for (const auto m : out)
		got.push_back({m.samplePosition, m.getMessage()});
	const double late = got.empty() ? -1 : (got[0].first - at) * 1000.0 / rate;
	char what[160];
	std::snprintf(what, sizeof what, "midi out: a note in on channel 3 comes out again (%d messages, %.2f ms later)", int(got.size()), late);
	check(got.size() == 2 && got[0].second.isNoteOn() && got[0].second.getChannel() == 3 && got[0].second.getNoteNumber() == 60
	          && got[0].second.getVelocity() == 100 && got[1].second.isNoteOff() && got[1].second.getNoteNumber() == 60 && late >= 0.96
	          && late < 5,
	      what);
}

// --poweron DIR: the first 5 s after POWER off and on, to hear: the main outputs (a BD every quarter second
// from 0.65 s, into the power-on mute), and cables in HI HAT, CRASH and RIDE (their thumps and the
// firmware's power-up trigger).
int powerOnSounds(const juce::File &dir)
{
	const double rate = 48000;
	Roms r;
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
	auto layout = p.getBusesLayout();
	for (auto v : {tr909::Voice::HH, tr909::Voice::CR, tr909::Voice::RD}) {
		layout.outputBuses.getReference(1 + int(v)) = juce::AudioChannelSet::mono();
		p.setCable(v, true);
	}
	p.setBusesLayout(layout);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	p.setPower(false);
	p.setPower(true);
	juce::MidiBuffer midi;
	for (double t = 0.65; t < 5.0; t += 0.25)
		midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::BD), juce::uint8(100)), int(t * rate));
	const auto out = run(p, ph, 5.0, rate, &midi, true);
	dir.createDirectory();
	save(dir, "poweron-main.wav", out, rate);
	const char *names[] = {"poweron-hihat-jack.wav", "poweron-crash-jack.wav", "poweron-ride-jack.wav"};
	for (int k = 0; k < 3; ++k) { // the enabled outputs, in order: channels 2, 3, 4
		juce::AudioBuffer<float> mono(1, out.getNumSamples());
		mono.copyFrom(0, 0, out, 2 + k, 0, out.getNumSamples());
		save(dir, names[k], mono, rate);
	}
	return 0;
}

// --panel OUT.png [SCALE] [running] [rear] [off] [service] [--artwork PACK.svg]: draws the editor (no
// window) into a PNG; rear: the rear panel, with cables in BD and SD; off: switched off; service:
// SERVICE, the underside; part: SERVICE with the PARTS row under the mouse.
int panelShot(const char *path, float scale, bool running, bool rear, bool off, bool service, juce::Point<float> at, float zoom, bool tm, bool part,
              const char *artworkPath)
{
	Roms r;
	juce::MemoryBlock artwork;
	if (artworkPath != nullptr)
		juce::File::getCurrentWorkingDirectory().getChildFile(artworkPath).loadFileAsData(artwork);
	const double t0 = juce::Time::getMillisecondCounterHiRes();
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd, lemus::Artwork::parse(artworkPath != nullptr ? &artwork : nullptr));
	if (artworkPath != nullptr)
		std::printf("artwork: %d marks, the processor made in %.0f ms\n", int(p.artwork().marks().size()), juce::Time::getMillisecondCounterHiRes() - t0);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(48000, 512);
	run(p, ph, 0.5, 48000);
	if (running) {
		p.setKey(TR909Processor::Key::Start, true);
		run(p, ph, 0.1, 48000);
		p.setKey(TR909Processor::Key::Start, false);
		run(p, ph, 0.6, 48000);
	}
	if (off) {
		p.setPower(false);
		run(p, ph, 0.1, 48000);
	}
	std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
	if (service)
		for (auto *child : ed->getChildren())
			if (auto *view = dynamic_cast<tr909::HardwareView *>(child)) {
				view->showService(true);
				if (zoom > 1)
					view->serviceView(zoom, at);
				if (part)
					view->servicePart();
				if (tm) { // the TM procedure: the scope on the clap's MULTI OUT, a clap played
					view->serviceSetUpTm();
					run(p, ph, 1.0, 48000); // past the firmware's power-on delay: it takes MIDI then
					juce::MidiBuffer midi;
					midi.addEvent(juce::MidiMessage::noteOn(10, TR909Processor::noteFor(tr909::Voice::HC), juce::uint8(64)), 480);
					run(p, ph, 0.3, 48000, &midi);
					view->serviceSweep();
				}
			}
	if (rear) {
		p.setCable(tr909::Voice::BD, true);
		p.setCable(tr909::Voice::SD, true);
		for (auto *child : ed->getChildren())
			if (auto *view = dynamic_cast<tr909::HardwareView *>(child))
				view->showRear(true);
	}
	const auto img = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
	juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(path);
	out.deleteFile();
	juce::FileOutputStream os(out);
	juce::PNGImageFormat png;
	return png.writeImageToStream(img, os) ? 0 : 1;
}

// --render: {"rate": 96000, "seconds": S, "jacks": true, "params": {"bd_tune": 0.5, ...},
// "notes": [[SECONDS, NOTE, VELOCITY], ...]} played from a new instance (the 909 switched on: give
// the notes a lead-in past its power-on), written as 32-bit float, every channel.
int render(const char *list, const char *path)
{
	const auto cwd = juce::File::getCurrentWorkingDirectory();
	const auto json = juce::JSON::parse(cwd.getChildFile(list));
	if (!json.isObject()) {
		std::fprintf(stderr, "can't read %s\n", list);
		return 2;
	}
	const double rate = json.getProperty("rate", 48000.0);
	const bool jacks = json.getProperty("jacks", false);
	Roms r;
	const bool factory = json.getProperty("factory", false);
	TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd, {}, factory ? &r.v1 : nullptr);
	if (factory && !p.hasFactoryPatterns()) {
		std::fprintf(stderr, "no v1.0 ROM for the factory patterns\n");
		return 2;
	}
	if (json.hasProperty("serial"))
		p.setSerial(json.getProperty("serial", tr909::kLateSerial));
	if (jacks)
		allJacks(p);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	if (auto *params = json.getProperty("params", {}).getDynamicObject())
		for (const auto &kv : params->getProperties()) {
			auto *q = p.params().getParameter(kv.name.toString());
			if (q == nullptr) {
				std::fprintf(stderr, "no parameter %s\n", kv.name.toString().toRawUTF8());
				return 2;
			}
			q->setValueNotifyingHost(float(double(kv.value)));
		}
	juce::MidiBuffer midi;
	if (auto *notes = json.getProperty("notes", {}).getArray())
		for (const auto &n : *notes) {
			const int at = int(std::lround(double(n[0]) * rate));
			midi.addEvent(juce::MidiMessage::noteOn(10, int(n[1]), juce::uint8(int(n[2]))), at);
			midi.addEvent(juce::MidiMessage::noteOff(10, int(n[1])), at + int(0.005 * rate));
		}
	// The run in parts, between the events: START pressed for 0.1 s at "start", and "changes"
	// ([seconds, parameter, value], as a knob turned then); the MIDI kept at its times.
	const double seconds = json.getProperty("seconds", 4.0), start = json.getProperty("start", -1.0);
	std::multimap<double, std::function<void()>> events;
	if (start >= 0) {
		events.emplace(start, [&p] { p.setKey(TR909Processor::Key::Start, true); });
		events.emplace(start + 0.1, [&p] { p.setKey(TR909Processor::Key::Start, false); });
	}
	// "keys": [[SECONDS, "KEY", HOLD], ...], panel keys by tr909seq's names (PATT1, 6, TEMPO,
	// TRACK1, START, STOP ...), each held HOLD seconds (default 0.1).
	static const char *kKeyNames[] = {
		"1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16",
		"SHIFT", "SCALE", "R2B2", "INST", "SHUFFLE", "LAST", "STOP", "START",
		"CLEAR", "TRACK1", "TRACK2", "TRACK3", "TRACK4", "PATT1", "PATT2", "PATT3",
		"ENTER", "TAPESYNC", "CYCLE", "AVAIL", "FWD", "BACK", "TEMPO", "EXTINST",
	};
	static_assert(std::size(kKeyNames) == size_t(TR909Processor::Key::Count));
	if (auto *keys = json.getProperty("keys", {}).getArray())
		for (const auto &k : *keys) {
			const auto name = k[1].toString();
			int i = 0;
			while (i < int(TR909Processor::Key::Count) && name != kKeyNames[i])
				++i;
			if (i == int(TR909Processor::Key::Count)) {
				std::fprintf(stderr, "no key %s\n", name.toRawUTF8());
				return 2;
			}
			const auto key = TR909Processor::Key(i);
			const double at = double(k[0]), hold = k.size() > 2 ? double(k[2]) : 0.1;
			events.emplace(at, [&p, key] { p.setKey(key, true); });
			events.emplace(at + hold, [&p, key] { p.setKey(key, false); });
		}
	if (auto *changes = json.getProperty("changes", {}).getArray())
		for (const auto &c : *changes) {
			auto *q = p.params().getParameter(c[1].toString());
			if (q == nullptr) {
				std::fprintf(stderr, "no parameter %s\n", c[1].toString().toRawUTF8());
				return 2;
			}
			const float v = float(double(c[2]));
			events.emplace(double(c[0]), [q, v] { q->setValueNotifyingHost(v); });
		}
	events.emplace(seconds, [] {});
	juce::AudioBuffer<float> out(jacks ? 2 + tr909::kVoices : 2, 0);
	int from = 0;
	for (const auto &[when, act] : events) {
		const int to = int(std::lround(std::min(when, seconds) * rate));
		juce::MidiBuffer part;
		part.addEvents(midi, from, to - from, -from);
		const auto b = run(p, ph, (to - from) / rate, rate, &part, jacks);
		const int at = out.getNumSamples();
		out.setSize(out.getNumChannels(), at + b.getNumSamples(), true, true);
		for (int ch = 0; ch < out.getNumChannels(); ++ch)
			out.copyFrom(ch, at, b, ch, 0, b.getNumSamples());
		from = to;
		act();
	}
	auto f = cwd.getChildFile(path);
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	const auto opts = juce::AudioFormatWriterOptions{}
	                      .withSampleRate(rate)
	                      .withNumChannels(out.getNumChannels())
	                      .withBitsPerSample(32)
	                      .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	auto w = wav.createWriterFor(s, opts);
	return w != nullptr && w->writeFromAudioSampleBuffer(out, 0, out.getNumSamples()) ? 0 : 1;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc > 3 && juce::String(argv[1]) == "--state") { // --state OUT SERIAL: ro-drums-909's state for that unit (for A/B scenes)
		Roms r;
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		p.prepareToPlay(48000, 512);
		p.setSerial(std::atoi(argv[3]));
		juce::MemoryBlock machineState, state;
		p.getStateInformation(machineState);
		// as the plugin saves it: the host's LEMUS tree (host/LemusProcessor.cpp) around the 909's own
		juce::ValueTree lemus("LEMUS");
		lemus.setProperty("version", 1, nullptr);
		lemus.setProperty("machine", "tr909", nullptr);
		lemus.setProperty("state", machineState, nullptr);
		{
			juce::MemoryOutputStream out(state, false);
			lemus.writeToStream(out);
		}
		return juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]).replaceWithData(state.getData(), state.getSize()) ? 0 : 1;
	}
	if (argc > 1 && juce::String(argv[1]) == "--panel") {
		const char *artwork = nullptr;
		bool running = false, rear = false, off = false, service = false, tm = false, part = false;
		juce::Point<float> at;
		float zoom = 1;
		float scale = 1.0f;
		for (int a = 3; a < argc; ++a) {
			const juce::String s(argv[a]);
			if (s == "--artwork" && a + 1 < argc)
				artwork = argv[++a];
			else if (s == "running")
				running = true;
			else if (s == "rear")
				rear = true;
			else if (s == "off")
				off = true;
			else if (s == "service")
				service = true;
			else if (s == "tm")
				service = tm = true;
			else if (s == "part")
				service = part = true;
			else if (s.startsWith("zoom=")) { // zoom=Z@X,Y: SERVICE zoomed Z times onto panel mm X, Y
				service = true;
				zoom = s.fromFirstOccurrenceOf("=", false, false).upToFirstOccurrenceOf("@", false, false).getFloatValue();
				const auto xy = s.fromFirstOccurrenceOf("@", false, false);
				at = {xy.upToFirstOccurrenceOf(",", false, false).getFloatValue(), xy.fromFirstOccurrenceOf(",", false, false).getFloatValue()};
			}
			else
				scale = s.getFloatValue();
		}
		return argc > 2 ? panelShot(argv[2], scale > 0 ? scale : 1.0f, running, rear, off, service, at, zoom, tm, part, artwork) : 2;
	}
	if (argc > 3 && juce::String(argv[1]) == "--render")
		return render(argv[2], argv[3]);
	if (argc > 2 && juce::String(argv[1]) == "--poweron")
		return powerOnSounds(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
	if (argc > 1 && juce::String(argv[1]) == "--service") { // tools: the service checks alone
		Roms r;
		service(r, 48000);
		return failures ? 1 : 0;
	}
	if (argc > 1 && juce::String(argv[1]) == "--bdpitch") { // tools: the BD PITCH checks alone
		Roms r;
		bdPitch(r, 48000);
		return failures ? 1 : 0;
	}
	if (argc > 1 && juce::String(argv[1]) == "--hostloop") { // tools: the host loop checks alone
		Roms r;
		hostLoop(r, 48000, true);
		hostLoop(r, 48000, false);
		hostLoop(r, 48000, true, true, 133);
		return failures ? 1 : 0;
	}
	if (argc > 1 && juce::String(argv[1]) == "--load") { // tools: the session-load checks alone
		Roms r;
		syncLoad(r, 48000);
		state(r, 48000);
		tempo(r, 48000);
		return failures ? 1 : 0;
	}
	if (argc > 1 && juce::String(argv[1]) == "--sleep") { // tools: the sleep check alone
		Roms r;
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		sleep(p, argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File(), 48000);
		return failures ? 1 : 0;
	}
	const juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
	if (dir != juce::File())
		dir.createDirectory();
	Roms r;
	check(r.fw.getSize() == 8192 && r.hh.getSize() == 32768, "the ROMs are in the private folder");
	for (double rate : {48000.0, 44100.0}) {
		TR909Processor full(&r.fw, &r.hh, &r.cr, &r.rd);
		voices(full, rate == 48000 ? dir : juce::File(), rate == 48000 ? "firmware" : "firmware-44k", rate);
	}
	{
		TR909Processor bare(nullptr, &r.hh, &r.cr, &r.rd);
		check(!bare.romLoaded(), "voice mode: no firmware");
		voices(bare, dir, "voicemode", 48000);
	}
	{
		TR909Processor noSamples(&r.fw, nullptr, nullptr, nullptr);
		check(!noSamples.sampleRomLoaded(tr909::Voice::HH) && !noSamples.sampleRomLoaded(tr909::Voice::CR), "no sample ROMs: those voices off");
	}
	{
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		sleep(p, dir, 48000);
	}
	{
		TR909Processor p(&r.fw, &r.hh, &r.cr, &r.rd);
		sync(p, 48000);
	}
	if (r.v1.getSize() == 8192) {
		factory(r, dir, 48000);
		syncLatency(r, 48000);
		hostLoop(r, 48000, true);
		hostLoop(r, 48000, false);
		hostLoop(r, 48000, true, true, 133);
		syncLoad(r, 48000);
	}
	state(r, 48000);
	tempo(r, 48000);
	midiThru(r, 48000);
	multiOut(r, 48000);
	service(r, 48000);
	bdPitch(r, 48000);
	powerOn(r, 48000);
	powerSwitch(r, 48000);
	std::printf("%s (%d failed)\n", failures ? "FAILED" : "all passed", failures);
	return failures ? 1 : 0;
}
