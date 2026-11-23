// Headless plugin test: runs SH101Processor without a host and checks what a host would see.
//   plugintest [--quick] [OUT_DIR]   (writes WAVs of some runs when OUT_DIR is given)
//   plugintest --panel OUT.png [SCALE] [off|voice|keys|held] [--artwork PACK.svg]   (draws the hardware view)
// Exits non-zero if any check fails.
#include "HardwareView.h"
#include "PanelData.h"
#include "PluginProcessor.h"

#include <algorithm>

namespace {
// The CPU's mask ROM, from the private folder (SH101_ROM_PATH, set by machine.cmake).
const juce::MemoryBlock &sh101Rom()
{
	static const juce::MemoryBlock rom = [] {
		juce::MemoryBlock b;
		juce::File(SH101_ROM_PATH).loadFileAsData(b);
		return b;
	}();
	return rom;
}
} // namespace

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <set>
#include <cstdio>
#include <functional>
#include <random>
#include <utility>

namespace {
using Id = SH101Processor::Id;
using Button = sh101::Machine::Button;

int g_failures = 0;
bool g_quick = false;
juce::File g_outDir;

void check(bool ok, const char *what, const juce::String &detail = {})
{
	std::printf("  %s %s%s%s\n", ok ? "ok  " : "FAIL", what, detail.isEmpty() ? "" : ": ", detail.toRawUTF8());
	if (!ok)
		++g_failures;
}

struct FakePlayHead : juce::AudioPlayHead {
	bool playing = false;
	double bpm = 120, ppq = 0;
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(playing);
		p.setBpm(bpm);
		p.setPpqPosition(ppq);
		p.setTimeSignature(TimeSignature{4, 4});
		return p;
	}
};

void setParam(SH101Processor &p, const char *id, double v)
{
	auto *param = p.params().getParameter(id);
	if (auto *c = dynamic_cast<juce::AudioParameterChoice *>(param))
		param->setValueNotifyingHost(c->convertTo0to1(float(v)));
	else
		param->setValueNotifyingHost(param->convertTo0to1(float(v)));
}

// Runs the processor for `seconds`, calling `perBlock` before each block (it may add MIDI and
// move the play head). Returns the left channel. `onBlock` sees the snapshot after each block.
struct Runner {
	SH101Processor &p;
	FakePlayHead head;
	double rate;
	int block;
	double time = 0;

	// settled: the unit as switched on long ago, past jack board A's power-on mute.
	Runner(SH101Processor &proc, double r = 44100, int b = 64, bool settled = true) : p(proc), rate(r), block(b)
	{
		if (settled)
			p.settlePower();
		p.setPlayHead(&head);
		p.setRateAndBufferSizeDetails(rate, block);
		p.prepareToPlay(rate, block);
	}

	std::vector<float> run(double seconds, const std::function<void(juce::MidiBuffer &, double t, int n)> &perBlock = {},
	                       const std::function<void(const SH101Processor::Snapshot &, double t)> &onBlock = {},
	                       const std::function<int()> &nextBlockSize = {})
	{
		std::vector<float> out;
		const int total = int(seconds * rate);
		juce::AudioBuffer<float> buf(2, std::max(block, 4096));
		int done = 0;
		while (done < total) {
			const int n = std::min(nextBlockSize ? nextBlockSize() : block, total - done);
			buf.setSize(2, n, false, false, true);
			buf.clear();
			juce::MidiBuffer midi;
			if (perBlock)
				perBlock(midi, time, n);
			p.processBlock(buf, midi);
			out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
			if (head.playing)
				head.ppq += n / rate * head.bpm / 60.0;
			time += n / rate;
			done += n;
			if (onBlock)
				onBlock(p.snapshot(), time);
		}
		return out;
	}
};

bool allFinite(const std::vector<float> &x, float limit = 4.0f)
{
	for (float v : x)
		if (!std::isfinite(v) || std::abs(v) > limit)
			return false;
	return true;
}

double rms(const std::vector<float> &x, size_t from, size_t n)
{
	double s = 0;
	n = std::min(n, x.size() > from ? x.size() - from : 0);
	for (size_t i = from; i < from + n; ++i)
		s += double(x[i]) * x[i];
	return n ? std::sqrt(s / double(n)) : 0;
}

// Fundamental by normalised autocorrelation over [from, from + n), searching 20 Hz - 5 kHz.
double pitch(const std::vector<float> &x, size_t from, size_t n, double rate)
{
	if (from + n > x.size())
		return 0;
	std::vector<double> s(x.begin() + long(from), x.begin() + long(from + n));
	double mean = 0;
	for (double v : s)
		mean += v;
	mean /= double(n);
	for (double &v : s)
		v -= mean;
	const int minLag = int(rate / 5000), maxLag = std::min(int(rate / 20), int(n / 2));
	std::vector<double> r(size_t(maxLag + 1), 0.0);
	for (int lag = minLag; lag <= maxLag; ++lag) {
		double a = 0, e0 = 0, e1 = 0;
		for (size_t i = 0; i + size_t(lag) < n; ++i) {
			a += s[i] * s[i + size_t(lag)];
			e0 += s[i] * s[i];
			e1 += s[i + size_t(lag)] * s[i + size_t(lag)];
		}
		r[size_t(lag)] = a / std::sqrt(e0 * e1 + 1e-30);
	}
	// First peak above 0.9 of the maximum (avoids octave errors on rich waves).
	double best = 0;
	for (int lag = minLag; lag <= maxLag; ++lag)
		best = std::max(best, r[size_t(lag)]);
	for (int lag = minLag + 1; lag < maxLag; ++lag) {
		const double v = r[size_t(lag)];
		if (v >= 0.9 * best && v >= r[size_t(lag - 1)] && v >= r[size_t(lag + 1)]) {
			const double a = r[size_t(lag - 1)], b = v, c = r[size_t(lag + 1)];
			const double d = 0.5 * (a - c) / (a - 2 * b + c);
			return rate / (lag + d);
		}
	}
	return 0;
}

double cents(double hz, double ref) { return hz > 0 ? 1200 * std::log2(hz / ref) : 1e9; }

void writeWav(const char *name, const std::vector<float> &x, double rate)
{
	if (g_outDir == juce::File())
		return;
	const auto file = g_outDir.getChildFile(name);
	file.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> stream(file.createOutputStream());
	if (!stream)
		return;
	auto w = wav.createWriterFor(stream, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
	if (!w)
		return;
	const float *ch[] = {x.data()};
	w->writeFromFloatArrays(ch, 1, int(x.size()));
}

// A plain patch: saw only, filter open, gate VCA, so pitch and timing are easy to read.
void plainPatch(SH101Processor &p)
{
	setParam(p, Id::sawLevel, 1);
	setParam(p, Id::pulseLevel, 0);
	setParam(p, Id::subLevel, 0);
	setParam(p, Id::noiseLevel, 0);
	setParam(p, Id::vcfFreq, 1);
	setParam(p, Id::vcfRes, 0);
	setParam(p, Id::vcfEnv, 0);
	setParam(p, Id::vcfKybd, 0);
	setParam(p, Id::vcfMod, 0);
	setParam(p, Id::vcoMod, 0);
	setParam(p, Id::vcaMode, 1);
	setParam(p, Id::range, 1);
	setParam(p, Id::transpose, 1);
	setParam(p, Id::tune, 0.5);
	setParam(p, Id::portaMode, 2);
	setParam(p, Id::volume, 0.8);
}

double midiHz(int note, double a4 = 442.0) { return a4 * std::pow(2.0, (note - 69) / 12.0); }

void testPitch()
{
	std::printf("MIDI pitch (RANGE 8', TRANSPOSE M, calibrated to A4 = 442 Hz)\n");
	for (int note : {41, 53, 60, 69, 84, 96}) {
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		p.setCalibrationOverride(sh101::Calibration::serviceNotes());
		Runner r(p);
		const auto x = r.run(0.6, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0)
				m.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(100)), 0);
		});
		const double hz = pitch(x, size_t(0.3 * r.rate), 8192, r.rate);
		const double c = cents(hz, midiHz(note));
		check(std::abs(c) < 10, juce::String("note " + juce::String(note)).toRawUTF8(),
		      juce::String(hz, 2) + " Hz, " + juce::String(c, 1) + " cents");
	}
}

void testRepeatedNotes()
{
	std::printf("Repeated notes back to back (16ths at 140 BPM, no gap)\n");
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	Runner r(p, 44100, 32);
	const double step = 60.0 / 140 / 4;
	int rises = 0;
	bool last = false;
	int sent = 0;
	r.run(16 * step + 0.3, [&](juce::MidiBuffer &m, double t, int n) {
		for (int i = 0; i < n; ++i) {
			const double ts = t + i / r.rate;
			const int k = int(ts / step);
			if (k < 16 && k >= sent) {
				if (k > 0)
					m.addEvent(juce::MidiMessage::noteOff(1, 48), i);
				m.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), i);
				sent = k + 1;
			}
			if (k == 16 && sent == 16) {
				m.addEvent(juce::MidiMessage::noteOff(1, 48), i);
				sent = 17;
			}
		}
	}, [&](const SH101Processor::Snapshot &s, double) {
		if (s.lines.gate && !last)
			++rises;
		last = s.lines.gate;
	});
	check(rises == 16, "16 gates", juce::String(rises) + " gate starts");
}

void testSyncArpeggio()
{
	std::printf("Host sync: ARP UP on three held notes follows the host's 16ths\n");
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	setParam(p, Id::sync, 1);
	setParam(p, Id::clockDiv, 3);
	Runner r(p, 48000, 64);
	r.head.bpm = 120;
	std::vector<std::pair<double, int>> changes;
	int lastKcv = -1;
	r.run(0.3, [&](juce::MidiBuffer &m, double t, int) {
		if (t == 0) {
			p.setButton(Button::ArpUp, true);
			for (int n : {53, 57, 60})
				m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(100)), 0);
		}
		if (t > 0.1)
			p.setButton(Button::ArpUp, false);
	});
	r.head.playing = true;
	r.head.ppq = 0;
	const double t0 = r.time;
	r.run(2.0, {}, [&](const SH101Processor::Snapshot &s, double t) {
		if (s.lines.kcv != lastKcv) {
			changes.push_back({t - t0, s.lines.kcv});
			lastKcv = s.lines.kcv;
		}
	});
	// 120 BPM 16ths = 125 ms per step; expect about 16 steps in 2 s, cycling 3 pitches.
	int steps = 0;
	double maxErr = 0;
	for (auto &[t, kcv] : changes) {
		if (t < 0.01)
			continue;
		++steps;
		const double err = std::abs(t - std::round(t / 0.125) * 0.125);
		maxErr = std::max(maxErr, err);
	}
	check(steps >= 14 && steps <= 17, "arpeggio steps in 2 s", juce::String(steps));
	check(maxErr < 0.006, "steps on the 16th grid", juce::String(maxErr * 1000, 2) + " ms max error");
}

// The pitch the CPU asks for, as a MIDI note: KCV code c at 8' plays MIDI 36 + c, and RANGE
// adds (code - 24) semitones.
int cpuNote(const SH101Processor::Snapshot &s) { return 36 + s.lines.kcv + s.lines.range - 24; }

void testMidiKeyboard()
{
	std::printf("MIDI keyboard: folding, hold pedal, all notes off\n");
	for (const auto &nw : {std::pair{41, 41}, {53, 53}, {84, 84}, {96, 96}, {36, 48}, {100, 88}}) {
		const int note = nw.first, want = nw.second;
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		Runner r(p);
		int got = 0;
		r.run(0.1, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0)
				m.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(100)), 0);
		}, [&](const SH101Processor::Snapshot &s, double) { got = cpuNote(s); });
		check(got == want, juce::String("note " + juce::String(note) + (note == want ? " plays itself" : " folds by octaves")).toRawUTF8(),
		      "plays " + juce::String(got));
	}
	{
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		Runner r(p);
		bool gateAfterRelease = false, gateAfterPedal = true;
		r.run(0.5, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0) {
				m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 0);
				m.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 1);
			}
			if (std::abs(t - 0.1) < 0.0007)
				m.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
			if (std::abs(t - 0.3) < 0.0007)
				m.addEvent(juce::MidiMessage::controllerEvent(1, 64, 0), 0);
		}, [&](const SH101Processor::Snapshot &s, double t) {
			if (t > 0.2 && t < 0.3)
				gateAfterRelease = s.lines.gate && s.leds[size_t(sh101::Machine::Led::Hold)] > 0.5f;
			if (t > 0.35)
				gateAfterPedal = s.lines.gate;
		});
		check(gateAfterRelease && !gateAfterPedal, "CC 64 is the HOLD pedal: the gate outlasts the note until it lifts");
	}
	{
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		Runner r(p);
		bool on = false, off = true;
		r.run(0.3, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0) {
				m.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
				m.addEvent(juce::MidiMessage::noteOn(1, 64, juce::uint8(100)), 0);
			}
			if (std::abs(t - 0.1) < 0.0007)
				m.addEvent(juce::MidiMessage::allNotesOff(1), 0);
		}, [&](const SH101Processor::Snapshot &s, double t) {
			if (t > 0.05 && t < 0.1)
				on = s.lines.gate;
			if (t > 0.15)
				off = off && !s.lines.gate;
		});
		check(on && off, "all notes off releases every key");
	}
}

void testMidiAlone()
{
	std::printf("Defaults: MIDI notes with the host playing run neither the sequencer nor the arpeggio\n");
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	Runner r(p, 48000, 128);
	r.head.playing = true;
	int rises = 0, notes = 0, changes = 0, lastNote = -1;
	bool last = false, modeLed = false;
	r.run(3.0, [&](juce::MidiBuffer &m, double t, int n) {
		const int k0 = int(t / 0.25), k1 = int((t + n / r.rate) / 0.25);
		if ((k1 != k0 || t == 0) && t < 2.7) {
			m.addEvent(juce::MidiMessage::noteOff(1, 48 + (k0 % 5) * 2), 0);
			m.addEvent(juce::MidiMessage::noteOn(1, 48 + (k1 % 5) * 2, juce::uint8(100)), 10);
			++notes;
		}
	}, [&](const SH101Processor::Snapshot &s, double) {
		if (s.lines.gate && !last)
			++rises;
		last = s.lines.gate;
		if (s.lines.gate && cpuNote(s) != lastNote) {
			++changes;
			lastNote = cpuNote(s);
		}
		for (auto l : {sh101::Machine::Led::Load, sh101::Machine::Led::Play, sh101::Machine::Led::ArpUp,
		               sh101::Machine::Led::ArpUpDown, sh101::Machine::Led::ArpDown})
			modeLed = modeLed || s.leds[size_t(l)] > 0.5f;
	});
	check(!modeLed && changes == notes, "the pitch follows every note, no mode lit",
	      juce::String(changes) + " pitches for " + juce::String(notes) + " notes, " + juce::String(rises) + " gate (legato)");
}

void testArpeggioFromMidi()
{
	std::printf("Arpeggio from a MIDI chord (all notes at once), MODULATOR clock\n");
	for (const auto &mw : {std::pair{Button::ArpUp, std::vector<int>{60, 64, 67, 60, 64}},
	                          std::pair{Button::ArpDown, std::vector<int>{67, 64, 60, 67, 64}},
	                          std::pair{Button::ArpUpDown, std::vector<int>{60, 64, 67, 72, 67, 64, 60}}}) {
		const Button mode = mw.first;
		const std::vector<int> &want = mw.second;
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		setParam(p, Id::lfoRate, 0.8);
		Runner r(p, 48000, 64);
		std::vector<int> got;
		r.run(0.1, [&](juce::MidiBuffer &, double t, int) {
			if (t == 0)
				p.setButton(mode, true);
			if (t > 0.05)
				p.setButton(mode, false);
		});
		const std::vector<int> chordNotes = mode == Button::ArpUpDown ? std::vector<int>{60, 64, 67, 72} : std::vector<int>{60, 64, 67};
		bool sent = false;
		r.run(4.0, [&](juce::MidiBuffer &m, double, int) {
			if (!sent)
				for (int n : chordNotes)
					m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(100)), 0);
			sent = true;
		}, [&](const SH101Processor::Snapshot &s, double) {
			if (s.lines.gate && (got.empty() || got.back() != cpuNote(s)))
				got.push_back(cpuNote(s));
		});
		juce::String g;
		for (int n : got)
			g << n << " ";
		const bool ok = got.size() >= want.size() && std::equal(want.begin(), want.end(), got.begin());
		check(ok, mode == Button::ArpUp ? "UP starts on the lowest note" : mode == Button::ArpDown ? "DOWN starts on the highest" : "U&D up and down, ends once", g);
	}
}

// The reported latency is the voice's own (its decimator) and the firmware's lag behind the
// host's grid in SYNC, the middle of the arpeggio's (its steps one CPU loop after the clock edge)
// and the sequencer's (the gate down for that loop, then up with the new note): with it taken
// off the two sit either side of the grid, each step within 3 ms of it. A MIDI note's
// gate rises that long after the note (the firmware takes it at its key scan; the plugin holds
// it back by the rest), and in voice mode too (all held back), so notes sit on the grid.
void testSyncLatency()
{
	std::printf("Host sync latency: steps and notes on the grid, less the latency\n");
	const double L = SH101Processor::kLatencySeconds;
	auto gateRises = [](Runner &r, double seconds, const std::vector<std::pair<double, juce::MidiMessage>> &events) {
		std::vector<double> rises;
		bool last = r.p.snapshot().lines.gate;
		size_t next = 0;
		const double t0 = r.time;
		r.run(seconds, [&](juce::MidiBuffer &m, double t, int n) {
			while (next < events.size() && events[next].first + t0 < t + n / r.rate) {
				m.addEvent(events[next].second, std::clamp(int((events[next].first + t0 - t) * r.rate), 0, n - 1));
				++next;
			}
		}, [&](const SH101Processor::Snapshot &s, double t) {
			if (s.lines.gate && !last)
				rises.push_back(t - 0.5 * r.block / r.rate - t0); // the block's middle
			last = s.lines.gate;
		});
		return rises;
	};
	auto stats = [](const std::vector<double> &v) {
		double lo = 1, hi = -1, s = 0;
		for (double x : v) {
			lo = std::min(lo, x);
			hi = std::max(hi, x);
			s += x;
		}
		return std::array<double, 3>{v.empty() ? 0 : s / double(v.size()), lo, hi};
	};
	// the sequence C4 E4 G4 C5, as testSyncSequencer enters it, played by the host's transport
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	setParam(p, Id::sync, 1);
	setParam(p, Id::clockDiv, 3);
	Runner r(p, 48000, 8);
	r.head.bpm = 120;
	const int seq[] = {60, 64, 67, 72};
	r.run(1.2, [&](juce::MidiBuffer &m, double t, int n) {
		auto at = [&](double when) { return t <= when && when < t + n / r.rate; };
		if (at(0.0))
			p.setButton(Button::Load, true);
		if (at(0.05))
			p.setButton(Button::Load, false);
		for (int i = 0; i < 4; ++i) {
			if (at(0.1 + 0.2 * i))
				m.addEvent(juce::MidiMessage::noteOn(1, seq[i], juce::uint8(100)), 0);
			if (at(0.2 + 0.2 * i))
				m.addEvent(juce::MidiMessage::noteOff(1, seq[i]), 0);
		}
		if (at(1.0))
			p.setButton(Button::Load, true);
		if (at(1.05))
			p.setButton(Button::Load, false);
	});
	const double tPlay = r.time;
	r.run(0.5, [&](juce::MidiBuffer &, double t, int) { p.setButton(Button::Play, t - tPlay < 0.02); });
	r.head.playing = true;
	r.head.ppq = 0;
	std::vector<double> steps;
	for (double t : gateRises(r, 2.0, {}))
		steps.push_back(t - std::floor((t + 0.004) / 0.125) * 0.125 - L);
	// ARP UP on three held notes: its steps (the KCV moving) on the host's 16ths
	std::vector<double> arp;
	{
		SH101Processor a(&sh101Rom());
		plainPatch(a);
		setParam(a, Id::sync, 1);
		setParam(a, Id::clockDiv, 3);
		Runner ra(a, 48000, 8);
		ra.head.bpm = 120;
		ra.run(0.3, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0) {
				a.setButton(Button::ArpUp, true);
				for (int n : {53, 57, 60})
					m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(100)), 0);
			}
			if (t > 0.1)
				a.setButton(Button::ArpUp, false);
		});
		ra.head.playing = true;
		ra.head.ppq = 0;
		int lastKcv = a.snapshot().lines.kcv;
		const double t0 = ra.time;
		ra.run(2.0, {}, [&](const SH101Processor::Snapshot &s, double t) {
			const double at = t - 0.5 * ra.block / ra.rate - t0;
			if (s.lines.kcv != lastKcv) {
				arp.push_back(at - std::floor((at + 0.004) / 0.125) * 0.125 - L);
				lastKcv = s.lines.kcv;
			}
		});
	}
	// MIDI notes, one an 8th, on a new instance (no sequence, SYNC off)
	std::vector<std::pair<double, juce::MidiMessage>> notes;
	for (int k = 0; k < 16; ++k) {
		notes.push_back({0.1 + k * 0.25, juce::MidiMessage::noteOn(1, 60 + (k % 4) * 2, juce::uint8(100))});
		notes.push_back({0.2 + k * 0.25, juce::MidiMessage::noteOff(1, 60 + (k % 4) * 2)});
	}
	auto noteLags = [&](const std::vector<double> &rises) {
		std::vector<double> lags;
		for (double t : rises)
			lags.push_back(t - 0.1 - std::floor((t - 0.1 + 0.004) / 0.25) * 0.25 - L);
		return lags;
	};
	SH101Processor fresh(&sh101Rom());
	plainPatch(fresh);
	Runner rf(fresh, 48000, 8);
	rf.run(0.5);
	const auto midiLags = noteLags(gateRises(rf, 4.2, notes));
	SH101Processor bare(nullptr);
	plainPatch(bare);
	Runner rb(bare, 48000, 8);
	rb.run(0.5);
	const auto voiceLags = noteLags(gateRises(rb, 4.2, notes));
	const auto st = stats(steps), mi = stats(midiLags), vo = stats(voiceLags), ar = stats(arp);
	std::printf("    latency %d samples (the voice's and %.2f ms); less that, the sequencer's gates rise %.2f ms after the 16ths on "
	            "average (%.2f to %.2f, %zu steps, the first %.2f), the arpeggio's steps %.2f (%.2f to %.2f), MIDI's gates %.2f after "
	            "their notes (%.2f to %.2f, %zu), voice mode's %.2f (%zu)\n",
	            p.getLatencySamples(), L * 1000, st[0] * 1000, st[1] * 1000, st[2] * 1000, steps.size(),
	            steps.empty() ? 0.0 : steps.front() * 1000, ar[0] * 1000, ar[1] * 1000, ar[2] * 1000, mi[0] * 1000, mi[1] * 1000,
	            mi[2] * 1000, midiLags.size(), vo[0] * 1000, voiceLags.size());
	check(steps.size() >= 15 && arp.size() >= 15 && std::abs(st[0] + ar[0]) < 0.001,
	      "the sequencer's steps and the arpeggio's either side of the grid on average");
	check(std::min(st[1], ar[1]) > -0.003 && std::max(st[2], ar[2]) < 0.003, "each within 3 ms of it, the first after the start too");
	check(midiLags.size() == 16 && std::abs(mi[0]) < 0.0005, "MIDI's gates on their notes on average");
	check(voiceLags.size() == 16 && std::abs(vo[0]) < 0.0005, "and voice mode's");
}

void testSyncSequencer()
{
	std::printf("Host sync: the sequence waits for the transport and steps on its 16ths\n");
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	setParam(p, Id::sync, 1);
	setParam(p, Id::clockDiv, 3);
	Runner r(p, 48000, 64);
	r.head.bpm = 120;
	// Enter C4 E4 G4 C5 with LOAD from MIDI, then press PLAY with the host stopped.
	const int seq[] = {60, 64, 67, 72};
	r.run(1.2, [&](juce::MidiBuffer &m, double t, int n) {
		auto at = [&](double when) { return t <= when && when < t + n / r.rate; };
		if (at(0.0))
			p.setButton(Button::Load, true);
		if (at(0.05))
			p.setButton(Button::Load, false);
		for (int i = 0; i < 4; ++i) {
			if (at(0.1 + 0.2 * i))
				m.addEvent(juce::MidiMessage::noteOn(1, seq[i], juce::uint8(100)), 0);
			if (at(0.2 + 0.2 * i))
				m.addEvent(juce::MidiMessage::noteOff(1, seq[i]), 0);
		}
		if (at(1.0))
			p.setButton(Button::Load, true);
		if (at(1.05))
			p.setButton(Button::Load, false);
	});
	int gatesStopped = 0;
	bool playLed = false, last = false;
	const double tPlay = r.time;
	r.run(0.5, [&](juce::MidiBuffer &, double t, int) {
		if (t - tPlay < 0.02)
			p.setButton(Button::Play, true);
		else
			p.setButton(Button::Play, false);
	}, [&](const SH101Processor::Snapshot &s, double) {
		playLed = s.leds[size_t(sh101::Machine::Led::Play)] > 0.5f;
		if (s.lines.gate && !last)
			++gatesStopped;
		last = s.lines.gate;
	});
	check(playLed && gatesStopped == 0, "PLAY lit, silent while the host is stopped",
	      juce::String("PLAY LED ") + (playLed ? "on" : "off") + ", " + juce::String(gatesStopped) + " gates");
	r.head.playing = true;
	r.head.ppq = 0;
	const double t0 = r.time;
	std::vector<std::pair<double, int>> starts;
	r.run(1.05, {}, [&](const SH101Processor::Snapshot &s, double t) {
		if (s.lines.gate && !last)
			starts.push_back({t - t0, cpuNote(s)});
		last = s.lines.gate;
	});
	juce::String g;
	double maxErr = 0;
	for (size_t i = 0; i < starts.size(); ++i) {
		g << starts[i].second << " ";
		maxErr = std::max(maxErr, std::abs(starts[i].first - 0.125 * double(i)));
	}
	const bool order = starts.size() >= 8 && starts[0].second == 60 && starts[1].second == 64 &&
	                   starts[2].second == 67 && starts[3].second == 72 && starts[4].second == 60;
	check(order, "the first step on the transport's start, then one per 16th, looping", g);
	check(maxErr < 0.008, "steps on the grid (one CPU loop late at most)", juce::String(maxErr * 1000, 2) + " ms max");
}

void testSequence()
{
	std::printf("A new instance has no sequence; a written one plays on the MODULATOR clock\n");
	{
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		setParam(p, Id::lfoRate, 0.8);
		Runner r(p, 48000, 64);
		bool gate = false;
		r.run(1.5, [&](juce::MidiBuffer &, double t, int) { p.setButton(Button::Play, t < 0.05); },
		      [&](const SH101Processor::Snapshot &s, double) { gate = gate || s.lines.gate; });
		check(!gate, "PLAY on a new instance plays nothing");
	}
	SH101Processor p(&sh101Rom());
	p.writeSequence(sh101::Machine::demoSequence());
	plainPatch(p);
	setParam(p, Id::lfoRate, 0.8);
	Runner r(p, 48000, 64);
	std::vector<int> starts;
	bool last = false;
	r.run(2.5, [&](juce::MidiBuffer &, double t, int) { p.setButton(Button::Play, t < 0.05); },
	      [&](const SH101Processor::Snapshot &s, double) {
		      if (s.lines.gate && !last)
			      starts.push_back(cpuNote(s));
		      last = s.lines.gate;
	      });
	juce::String g;
	for (int n : starts)
		g << n << " ";
	const std::vector<int> want = {48, 48, 60, 55, 48, 51, 53, 48, 60};
	check(starts.size() >= want.size() && std::equal(want.begin(), want.end(), starts.begin()),
	      "C3 C3 - C4 Bb3~ G3 - C3 Eb3 - F3 G3~ - C3 C4 Bb3 (gates: slurred steps have none)", g);
}

void testIdle()
{
	std::printf("Idle: the settled voice sleeps (exact zeros, no synth board model); notes and the sequencer wake it\n");
	auto peakOf = [](const std::vector<float> &x, size_t from = 0) {
		float pk = 0;
		for (size_t i = from; i < x.size(); ++i)
			pk = std::max(pk, std::abs(x[i]));
		return pk;
	};
	auto dB = [](double v) { return juce::String(20 * std::log10(v + 1e-30), 0) + " dB"; };
	// A note of 0.3 s, then half a second.
	auto note = [](Runner &r, double len = 0.3) {
		const double start = r.time;
		return r.run(len + 0.5, [&](juce::MidiBuffer &m, double t, int n) {
			t -= start;
			if (t == 0)
				m.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
			if (t <= len && t + n / r.rate > len)
				m.addEvent(juce::MidiMessage::noteOff(1, 60), 0);
		});
	};
	auto timed = [](Runner &r, double seconds) {
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		r.run(seconds);
		return juce::Time::getMillisecondCounterHiRes() - t0;
	};

	SH101Processor p(&sh101Rom()), ref(&sh101Rom());
	p.writeSequence(sh101::Machine::demoSequence());
	ref.setIdleSleep(false);
	for (auto *x : {&p, &ref}) {
		plainPatch(*x);
		setParam(*x, Id::vcfFreq, 0.5); // the closed VCA's leak of this patch is under -120 dB
	}
	Runner r(p, 48000, 64), rr(ref, 48000, 64);
	const auto first = note(r);
	note(rr);
	r.run(4.0);
	rr.run(4.0);
	const auto idle = r.run(1.0);
	rr.run(1.0);
	check(p.voiceAsleep() && peakOf(idle) == 0.0f, "a settled voice sleeps and puts out exact zeros");
	const double asleepMs = timed(r, 5.0);
	rr.run(5.0);
	const auto woken = note(r), awake = note(rr);
	std::printf("      notes: first %.4f, after sleeping %.4f, never asleep %.4f\n", peakOf(first), peakOf(woken), peakOf(awake));
	check(std::abs(peakOf(woken) / peakOf(awake) - 1) < 0.05f, "a note wakes it and sounds as one that never slept");
	r.run(4.0);
	check(p.voiceAsleep(), "asleep again");
	const double playAt = r.time;
	const auto seq = r.run(1.5, [&](juce::MidiBuffer &, double t, int) { p.setButton(Button::Play, t - playAt < 0.05); });
	check(!p.voiceAsleep() && peakOf(seq) > 0.05f, "the CPU kept running: PLAY's sequence wakes it", "peak " + juce::String(peakOf(seq), 3));
	p.setButton(Button::Play, false);

	// With the VCF open and every source up, the VCA in ENV mode still leaks at rest (about
	// -115 dBFS: R196 holds S6's contact 0.27 V above the ENV), which is part of the sound, so the
	// voice stays awake.
	SH101Processor q(&sh101Rom());
	plainPatch(q);
	setParam(q, Id::pulseLevel, 1);
	setParam(q, Id::subLevel, 1);
	setParam(q, Id::vcaMode, 0);
	Runner rq(q, 48000, 64);
	note(rq, 0.3);
	rq.run(4.0);
	const auto leak = rq.run(1.0);
	check(!q.voiceAsleep() && peakOf(leak) > 0.0f, "a bright patch's VCA leak keeps it awake", "rest level " + dB(peakOf(leak)));
	const double awakeMs = timed(rq, 5.0);
	std::printf("      5 s idle: %.0f ms awake, %.0f ms asleep\n", awakeMs, asleepMs);
}

void testState()
{
	std::printf("State: parameters and the battery-backed RAM\n");
	SH101Processor a(&sh101Rom());
	setParam(a, Id::vcfFreq, 0.123);
	setParam(a, Id::lfoWave, 2);
	{
		Runner r(a);
		r.run(0.2);
	}
	juce::MemoryBlock state;
	a.getStateInformation(state);
	SH101Processor b(&sh101Rom());
	b.setStateInformation(state.getData(), int(state.getSize()));
	const float f = b.params().getRawParameterValue(Id::vcfFreq)->load();
	const float w = b.params().getRawParameterValue(Id::lfoWave)->load();
	check(std::abs(f - 0.123f) < 1e-4f && std::abs(w - 2) < 1e-4f, "parameters round-trip");
	juce::MemoryBlock again;
	b.getStateInformation(again);
	check(again == state, "state is stable across save/load");

	// A session reopens as the unit was, not as at power-on: ARPEGGIO UP and HOLD on, with a
	// chord held, come back on with the chord still arpeggiating.
	SH101Processor c(&sh101Rom());
	plainPatch(c);
	setParam(c, Id::lfoRate, 0.8);
	auto ledsOf = [](const SH101Processor::Snapshot &s) {
		juce::String l;
		for (float x : s.leds)
			l << (x > 0.5f ? "1" : ".");
		return l;
	};
	auto notesOf = [&](Runner &r) {
		std::set<int> notes;
		juce::String lit(".......");
		r.run(2.0, {}, [&](const SH101Processor::Snapshot &s, double) {
			if (s.lines.gate)
				notes.insert(cpuNote(s));
			const auto l = ledsOf(s);
			for (int i = 0; i < l.length(); ++i)
				if (l[i] == '1')
					lit = lit.replaceSection(i, 1, "1");
		});
		juce::String n;
		for (int x : notes)
			n << x << " ";
		return lit + " notes " + n;
	};
	juce::String before;
	{
		Runner r(c, 48000, 64);
		for (auto btn : {Button::ArpUp, Button::Hold}) {
			c.setButton(btn, true);
			r.run(0.05);
			c.setButton(btn, false);
			r.run(0.1);
		}
		bool sent = false;
		r.run(0.5, [&](juce::MidiBuffer &m, double, int) {
			if (!sent)
				for (int n : {60, 64, 67})
					m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(100)), 0);
			sent = true;
		});
		r.run(0.1, [&](juce::MidiBuffer &m, double, int) {
			for (int n : {60, 64, 67})
				m.addEvent(juce::MidiMessage::noteOff(1, n), 0);
		});
		before = notesOf(r);
	}
	juce::MemoryBlock held;
	c.getStateInformation(held);
	SH101Processor d(&sh101Rom());
	d.setStateInformation(held.getData(), int(held.getSize()));
	Runner rd(d, 48000, 64);
	const auto after = notesOf(rd);
	std::printf("      LEDs (Load Play Down U&D Up Hold Transpose) and notes: saved %s; reopened %s\n", before.toRawUTF8(), after.toRawUTF8());
	check(after == before, "a reopened session keeps ARPEGGIO, HOLD and the held chord");
}

void testRatesAndBlocks()
{
	std::printf("Sample rates and odd block sizes\n");
	for (double rate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0}) {
		SH101Processor p(&sh101Rom());
		plainPatch(p);
		Runner r(p, rate, 512);
		std::mt19937 rng(1);
		const auto x = r.run(0.8, [&](juce::MidiBuffer &m, double t, int) {
			if (t == 0)
				m.addEvent(juce::MidiMessage::noteOn(1, 57, juce::uint8(100)), 0);
		}, {}, [&] { return 1 + int(rng() % 700); });
		const double hz = pitch(x, size_t(0.5 * rate), size_t(0.2 * rate), rate);
		check(allFinite(x) && std::abs(cents(hz, midiHz(57))) < 10,
		      juce::String(juce::String(rate / 1000, 1) + " kHz").toRawUTF8(), juce::String(hz, 2) + " Hz");
	}
}

void testStability()
{
	const double seconds = g_quick ? 10 : 120;
	std::printf("Long-run stability (%.0f s each, every calibration)\n", seconds);
	struct Case {
		const char *name;
		std::vector<std::pair<const char *, double>> params;
	};
	const Case cases[] = {
		{"max res, all sources", {{Id::vcfRes, 1}, {Id::sawLevel, 1}, {Id::pulseLevel, 1}, {Id::subLevel, 1}, {Id::noiseLevel, 1}, {Id::vcfEnv, 1}, {Id::vcfKybd, 1}}},
		{"self-oscillation only", {{Id::vcfRes, 1}, {Id::sawLevel, 0}, {Id::pulseLevel, 0}, {Id::vcfFreq, 0.5}}},
		{"noise LFO at max depth", {{Id::lfoWave, 3}, {Id::vcoMod, 1}, {Id::vcfMod, 1}, {Id::pwmSource, 0}, {Id::pulseWidth, 1}, {Id::vcfRes, 1}, {Id::lfoRate, 1}}},
		{"low cutoff, max res, fast env", {{Id::vcfFreq, 0}, {Id::vcfRes, 1}, {Id::vcfEnv, 1}, {Id::decay, 0}, {Id::attack, 0}, {Id::sustain, 0}}},
	};
	const char *calNames[] = {"service notes", "real unit", "Roland"};
	for (int cal = 0; cal < 3; ++cal)
	for (const auto &c : cases) {
		SH101Processor p(&sh101Rom());
		for (const auto &[id, v] : c.params)
			setParam(p, id, v);
		setParam(p, Id::sync, 0);
		p.setCalibrationOverride(sh101::Calibration::byIndex(cal));
		Runner r(p, 48000, 256);
		const double beat = 0.25;
		const auto x = r.run(seconds, [&](juce::MidiBuffer &m, double t, int n) {
			const int k0 = int(t / beat), k1 = int((t + n / r.rate) / beat);
			if (k1 != k0) {
				const int note = 41 + (k1 * 7) % 40;
				m.addEvent(juce::MidiMessage::allNotesOff(1), 0);
				m.addEvent(juce::MidiMessage::noteOn(1, note, juce::uint8(100)), 1);
			}
		});
		const size_t tail = std::min<size_t>(x.size(), size_t(5 * r.rate));
		const double early = rms(x, size_t(r.rate), size_t(5 * r.rate));
		const double late = rms(x, x.size() - tail, tail);
		check(allFinite(x) && late < 4 * early + 0.05, (juce::String(c.name) + ", " + calNames[cal]).toRawUTF8(),
		      "rms first 5 s " + juce::String(early, 3) + ", last 5 s " + juce::String(late, 3));
		if (cal == 1 && std::string(c.name) == "max res, all sources")
			writeWav("stability-maxres.wav", x, r.rate);
	}
}

void testStress()
{
	std::printf("Stress: random automation and MIDI\n");
	SH101Processor p(&sh101Rom());
	Runner r(p, 48000, 128);
	std::mt19937 rng(7);
	auto &params = p.getParameters();
	const auto x = r.run(g_quick ? 5 : 30, [&](juce::MidiBuffer &m, double, int n) {
		for (int k = 0; k < 3; ++k) {
			auto *param = params[int(rng() % unsigned(params.size()))];
			if (dynamic_cast<juce::AudioParameterBool *>(param) == nullptr)
				param->setValueNotifyingHost(float(rng() % 1000) / 999.0f);
		}
		if (rng() % 4 == 0)
			m.addEvent(juce::MidiMessage::noteOn(1, int(24 + rng() % 84), juce::uint8(1 + rng() % 127)), int(rng() % unsigned(n)));
		if (rng() % 4 == 0)
			m.addEvent(juce::MidiMessage::noteOff(1, int(24 + rng() % 84)), int(rng() % unsigned(n)));
		if (rng() % 8 == 0)
			m.addEvent(juce::MidiMessage::pitchWheel(1, int(rng() % 16384)), int(rng() % unsigned(n)));
		if (rng() % 16 == 0)
			m.addEvent(juce::MidiMessage::controllerEvent(1, 1, int(rng() % 128)), 0);
	});
	check(allFinite(x, 8.0f), "output finite and bounded");
}
void testPower()
{
	std::printf("POWER: a new instance warm; off, and on again through jack board A's mute\n");
	auto peakIn = [](const std::vector<float> &x, double rate, double from, double to) {
		float pk = 0;
		for (size_t i = size_t(from * rate); i < std::min(x.size(), size_t(to * rate)); ++i)
			pk = std::max(pk, std::abs(x[i]));
		return double(pk);
	};
	auto dB = [](double v) { return 20 * std::log10(v + 1e-30); };
	SH101Processor p(&sh101Rom());
	plainPatch(p);
	setParam(p, Id::vcaMode, 1); // GATE: the note at full level throughout
	Runner r(p, 48000, 64, false); // not settled by the test: as the plugin creates it
	// A note held from the start of a run (the runner's time is absolute).
	auto heldFrom = [](bool &sent) {
		return [&sent](juce::MidiBuffer &m, double, int) {
			if (!std::exchange(sent, true))
				m.addEvent(juce::MidiMessage::noteOn(1, 57, juce::uint8(100)), 0);
		};
	};
	// A new instance is a unit that has been on a while (as on every Lemus machine): no switch-on
	// mute, the note open at once. Only POWER runs the switch-on.
	bool sent = false, mutedNew = false;
	const auto x = r.run(3.0, heldFrom(sent), [&](const SH101Processor::Snapshot &s, double) { mutedNew = mutedNew || s.muted; });
	// (0.25-0.5 s: past the coupling capacitors' 0.2 s settle after prepareToPlay; switched on, the
	// mute would still hold the note 10-30 dB down there.)
	const double open = peakIn(x, r.rate, 2.5, 3.0), early = peakIn(x, r.rate, 0.25, 0.5);
	check(!mutedNew && std::abs(dB(early / open)) < 0.5, "a new instance starts warm: not muted, the note open at once",
	      juce::String(dB(early / open), 2) + " dB");

	// Off, the note still held: it stops within a few milliseconds as TR8 and C14 shunt the output,
	// IC1a's fall to ground thumps faintly through C2, and after half a second the plugin gives
	// exact zeros. Dark at once, MIDI ignored, the sequence kept.
	p.writeSequence(sh101::Machine::demoSequence());
	p.setPower(false);
	bool dark = true;
	const double t0 = r.time;
	const auto off = r.run(1.0, [&](juce::MidiBuffer &m, double t, int) {
		if (t - t0 > 0.1 && t - t0 < 0.11)
			m.addEvent(juce::MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
	}, [&](const SH101Processor::Snapshot &s, double) {
		dark = dark && !s.powered && s.leds == decltype(s.leds){} && !s.lines.gate;
	});
	const double lingers = peakIn(off, r.rate, 0.0, 0.0005), thump = peakIn(off, r.rate, 0.003, 0.55);
	check(dB(lingers / open) > -20, "off: the note goes on for half a millisecond", juce::String(dB(lingers / open), 1) + " dB");
	check(dB(thump / open) < -25 && thump > 0, "then the mute holds it down; a faint thump",
	      juce::String(dB(thump / open), 1) + " dB re the note, " + juce::String(dB(thump), 1) + " dBFS");
	check(peakIn(off, r.rate, 0.55, 1.0) == 0 && dark, "exact zeros after half a second, every LED dark, notes ignored");
	// On again, a note held from the switch-on: the CPU boots and jack board A's mute holds the
	// output while C13 charges.
	p.setPower(true);
	bool sentOn = false, mutedAtStart = false, mutedLater = true;
	const double t1 = r.time;
	const auto on = r.run(3.0, heldFrom(sentOn), [&](const SH101Processor::Snapshot &s, double t) {
		if (t - t1 < 0.1)
			mutedAtStart = mutedAtStart || s.muted;
		if (t - t1 > 2.9)
			mutedLater = s.muted;
	});
	const double shut = peakIn(on, r.rate, 0.05, 0.25), neg = peakIn(on, r.rate, 0.9, 1.0), pos = peakIn(on, r.rate, 1.3, 1.31);
	check(mutedAtStart && !mutedLater && p.powered(), "on again: muted at switch-on, and the mute lets go");
	check(dB(shut / open) < -20, "a note at switch-on is held down (first 0.25 s)", juce::String(dB(shut / open), 1) + " dB");
	check(dB(neg / open) > -6 && dB(neg / open) < -0.5, "it opens over the next second (0.9-1 s)",
	      juce::String(dB(neg / open), 1) + " dB");
	check(dB(pos / open) > -3, "and is open by 1.3 s", juce::String(dB(pos / open), 2) + " dB");
	juce::MemoryBlock st;
	p.getStateInformation(st);
	SH101Processor q(&sh101Rom());
	q.setPower(false);
	q.setStateInformation(st.getData(), int(st.getSize()));
	check(q.powered(), "a session loads switched on");
	{
		auto xml = juce::AudioProcessor::getXmlFromBinary(st.getData(), int(st.getSize()));
		check(xml != nullptr && xml->getIntAttribute("model") == SH101Processor::kModelVersion,
		      "the session records the model version");
	}
	juce::MemoryBlock seqState;
	q.getStateInformation(seqState);
	SH101Processor fresh(&sh101Rom());
	juce::MemoryBlock freshState;
	fresh.getStateInformation(freshState);
	check(seqState != freshState, "the battery kept the sequence through off and on");
}

void testVoiceMode()
{
	std::printf("Voice mode (no CPU ROM): MIDI plays the synth board directly\n");
	auto run = [](SH101Processor &p, const std::vector<std::pair<double, juce::MidiMessage>> &events, double seconds,
	              std::vector<int> *kcvs = nullptr) {
		Runner r(p, 48000, 64);
		size_t next = 0;
		return r.run(seconds, [&](juce::MidiBuffer &m, double t, int n) {
			while (next < events.size() && events[next].first < t + n / r.rate)
				m.addEvent(events[next++].second, 0);
		}, [&](const SH101Processor::Snapshot &s, double) {
			if (kcvs != nullptr && s.lines.gate && (kcvs->empty() || kcvs->back() != s.lines.kcv))
				kcvs->push_back(s.lines.kcv);
		});
	};
	using juce::MidiMessage;
	for (int note : {41, 60, 69, 96}) {
		SH101Processor p(nullptr);
		plainPatch(p);
		p.setCalibrationOverride(sh101::Calibration::serviceNotes());
		const auto x = run(p, {{0.0, MidiMessage::noteOn(1, note, juce::uint8(100))}}, 0.6);
		const double hz = pitch(x, size_t(0.3 * 48000), 8192, 48000);
		check(!p.romLoaded() && std::abs(cents(hz, midiHz(note))) < 10, ("note " + juce::String(note) + " in tune, as with the ROM").toRawUTF8(),
		      juce::String(hz, 2) + " Hz");
	}
	{
		// GATE+TRIG: the newest, back to the lowest held; GATE: the lowest.
		SH101Processor p(nullptr);
		plainPatch(p);
		std::vector<int> kcvs;
		run(p, {{0.0, MidiMessage::noteOn(1, 60, juce::uint8(100))}, {0.1, MidiMessage::noteOn(1, 64, juce::uint8(100))},
		        {0.2, MidiMessage::noteOn(1, 67, juce::uint8(100))}, {0.3, MidiMessage::noteOff(1, 67)}},
		    0.5, &kcvs);
		juce::String got;
		for (int k : kcvs)
			got << k << " ";
		check(kcvs == std::vector<int>{24, 28, 31, 24}, "GATE+TRIG: C, E, G, then back to the lowest held (C)", got);
		SH101Processor g(nullptr);
		plainPatch(g);
		setParam(g, Id::trigger, 1);
		kcvs.clear();
		run(g, {{0.0, MidiMessage::noteOn(1, 64, juce::uint8(100))}, {0.1, MidiMessage::noteOn(1, 60, juce::uint8(100))},
		        {0.2, MidiMessage::noteOn(1, 67, juce::uint8(100))}},
		    0.4, &kcvs);
		got = {};
		for (int k : kcvs)
			got << k << " ";
		check(kcvs == std::vector<int>{28, 24}, "GATE: the lowest held", got);
	}
	{
		// The HOLD pedal keeps the gate on; a repeated note drops it for a loop.
		SH101Processor p(nullptr);
		plainPatch(p);
		bool heldOn = true;
		int gaps = 0;
		bool last = false;
		Runner r(p, 48000, 64);
		r.run(1.0, [&](juce::MidiBuffer &m, double t, int n) {
			auto at = [&](double when) { return t <= when && when < t + n / r.rate; };
			if (at(0.0))
				m.addEvent(MidiMessage::controllerEvent(1, 64, 127), 0);
			if (at(0.01))
				m.addEvent(MidiMessage::noteOn(1, 60, juce::uint8(100)), 0);
			if (at(0.2))
				m.addEvent(MidiMessage::noteOff(1, 60), 0);
			if (at(0.6))
				m.addEvent(MidiMessage::controllerEvent(1, 64, 0), 0);
			if (at(0.7))
				m.addEvent(MidiMessage::noteOn(1, 62, juce::uint8(100)), 0);
			if (at(0.8)) {
				m.addEvent(MidiMessage::noteOff(1, 62), 0);
				m.addEvent(MidiMessage::noteOn(1, 62, juce::uint8(100)), 0);
			}
		}, [&](const SH101Processor::Snapshot &s, double t) {
			if (t > 0.25 && t < 0.55)
				heldOn = heldOn && s.lines.gate;
			if (t > 0.7 && last && !s.lines.gate)
				++gaps;
			last = s.lines.gate;
		});
		check(heldOn, "the HOLD pedal (CC 64) keeps the gate on after the key is up");
		check(gaps == 1, "a repeated note drops the gate once, briefly");
	}
	{
		// The panel's keys and buttons don't respond; the sequence is kept and saved.
		SH101Processor p(nullptr);
		plainPatch(p);
		Runner r(p, 48000, 64);
		bool gate = false, led = false;
		r.run(0.5, [&](juce::MidiBuffer &, double t, int) {
			p.setKey(12, t < 0.2);
			p.setButton(Button::Play, t < 0.05);
		}, [&](const SH101Processor::Snapshot &s, double) {
			gate = gate || s.lines.gate;
			for (float l : s.leds)
				led = led || l > 0.1f;
		});
		check(!gate && !led, "the panel's keys and buttons don't respond, the LEDs stay dark");
		SH101Processor withSeq(&sh101Rom());
		withSeq.writeSequence(sh101::Machine::demoSequence());
		juce::MemoryBlock st;
		withSeq.getStateInformation(st);
		p.setStateInformation(st.getData(), int(st.getSize()));
		juce::MemoryBlock back;
		p.getStateInformation(back);
		check(back == st, "a session's sequence and settings survive a save in voice mode");
	}
}

} // namespace

// --panel OUT.png [SCALE] [off|voice|keys|held] [--artwork PACK.svg]: draws the hardware view (no
// window) into a PNG and quits; off: switched off; voice: without the ROM; keys: the key list open;
// held: HOLD and a key held by a right-click; with an artwork pack, its marks.
int panelShot(const char *path, float scale, const std::string &mode, const char *artworkPath)
{
	const bool off = mode == "off", voice = mode == "voice";
	juce::MemoryBlock artwork;
	if (artworkPath != nullptr)
		juce::File::getCurrentWorkingDirectory().getChildFile(artworkPath).loadFileAsData(artwork);
	SH101Processor p(voice ? nullptr : &sh101Rom(), lemus::Artwork::parse(artworkPath != nullptr ? &artwork : nullptr));
	p.prepareToPlay(48000, 512);
	p.setPower(!off);
	juce::AudioBuffer<float> buf(2, 512);
	juce::MidiBuffer midi;
	for (int b = 0; b < 8; ++b)
		p.processBlock(buf, midi);
	sh101::HardwareView view(p);
	view.setBounds(0, 0, sh101::HardwareView::kWidth, sh101::HardwareView::kHeight);
	if (mode == "keys")
		view.keyPressed(juce::KeyPress('?', {}, '?'));
	if (mode == "held") {
		// Right-clicks on HOLD and on the C4 key (key 7), at their centres.
		for (const auto &e : sh101::panel::elements)
			if ((e.type == sh101::panel::PType::Button && std::string(e.label) == "hold") ||
			    (e.type == sh101::panel::PType::Key && juce::roundToInt(e.v[0]) == 7)) {
				const juce::Point<float> at(0.5f * (e.box[0] + e.box[2]) * sh101::HardwareView::kPxPerMm,
				                            0.5f * (e.box[1] + e.box[3]) * sh101::HardwareView::kPxPerMm);
				const auto now = juce::Time::getCurrentTime();
				const juce::MouseEvent me(juce::Desktop::getInstance().getMainMouseSource(), at, juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier),
				                          0, 0, 0, 0, 0, &view, &view, now, at, now, 1, false);
				view.mouseDown(me);
				view.mouseUp(me.withNewPosition(at));
			}
	}
	const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, scale);
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(path);
	out.deleteFile();
	juce::FileOutputStream stream(out);
	juce::PNGImageFormat().writeImageToStream(image, stream);
	std::printf("%s: %d x %d\n", out.getFullPathName().toRawUTF8(), image.getWidth(), image.getHeight());
	return 0;
}

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juceInit;
	if (argc > 2 && std::string(argv[1]) == "--panel") {
		const char *art = nullptr;
		for (int a = 2; a + 1 < argc; ++a)
			if (std::string(argv[a]) == "--artwork")
				art = argv[a + 1];
		const std::string mode = argc > 4 ? argv[4] : "";
		return panelShot(argv[2], argc > 3 && argv[3][0] != '-' ? float(std::atof(argv[3])) : 1.0f, mode, art);
	}
	for (int i = 1; i < argc; ++i) {
		const juce::String a(argv[i]);
		if (a == "--quick")
			g_quick = true;
		else {
			g_outDir = juce::File::getCurrentWorkingDirectory().getChildFile(a);
			g_outDir.createDirectory();
		}
	}
	{
		SH101Processor p(&sh101Rom());
		std::printf("ROM\n");
		check(p.romLoaded(), "firmware ROM embedded");
	}
	testPitch();
	testRepeatedNotes();
	testMidiKeyboard();
	testMidiAlone();
	testArpeggioFromMidi();
	testSyncArpeggio();
	testSyncSequencer();
	testSyncLatency();
	testSequence();
	testPower();
	testVoiceMode();
	testIdle();
	testState();
	testRatesAndBlocks();
	testStability();
	testStress();
	std::printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
	return g_failures ? 1 : 0;
}
