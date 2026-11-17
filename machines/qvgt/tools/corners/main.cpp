// qvgtcorners: the extremes, checked for what the other checks can't see. Every other check
// compares against something (ngspice, golden renders) only where that something exists; this one
// drives what nobody recorded and fails on:
//   - any iterative solve that didn't converge (qvgt::SolverStats::failures: every preamp circuit's
//     steps and DC solves, the compressor's control node, the dry VCA's control voltage): an
//     unconverged sample is silent in every other check (docs/improvements.md, 2026-11-16);
//   - output that isn't finite, or past its bound;
//   - broadband noise where a pure tone goes in: with a 1 kHz sine in, a case whose median spectral
//     flatness (40 Hz-16 kHz, 2048-sample windows above -70 dBFS) passes kNoiseFlatness. Every
//     factory program's harmonics, chorus and reverb measured 0.004 at most (a 1 kHz sine at -12
//     and +6 dBFS); white noise is about 0.6.
// It also reports how many solves needed a retry in shorter steps.
//
// Two sets of cases, each at 44.1 and 48 kHz:
//   preamp  the preamp alone (core/preamp.h) with its latches as the firmware writes them for the
//           PRE-AMP pages' extremes, which no factory program reaches (COMPRESSION, OVERDRIVE,
//           DISTORTION 1-8, TONE, BASS BOOST, CAB, NOISE GATE), the INPUT pot, fed through the
//           unit's own input resampler with sweeps, squares, DC steps, sines at 20 Hz and 20 kHz,
//           noise and silence, up to +24 dBFS; bound: U13's output inside the op-amps' rails;
//   unit    the plugin's processor: every factory program with a 1 kHz sine at 0 dBFS and then a
//           sweep at +12 dBFS, INPUT and OUTPUT at 10, and the programs with overdrive 7 and
//           distortion on with every signal at 0 and +24 dBFS;
//           bound: the output stages' swing (kMaxPeak).
//
//   qvgtcorners [--rate 44100|48000] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --wav writes each case's output to DIR/<rate>-<case>.wav (a preamp case's U13 pin 8 at the
// preamp's rate, in host full scale), to listen to one.
//
// Exit status 0 when every case passes. 65-80 s on an M5 Max.
#include "PluginProcessor.h"
#include "qvgt/core/preamp.h"
#include "qvgt/core/resampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
// The unit's output: the output stages' swing (10.5 V, 6.048 of full scale), which Unit clamps
// at the DSP's rate and again at the host's: before the second clamp (2026-11-17) the resampler's
// peaks between samples reached 12.9 (programs 30, 32 and 34 with a sweep at +12 dBFS).
constexpr float kMaxPeak = 6.05f;
constexpr double kMaxVolts = 13.0; // the preamp's outputs: the TL084s' swing is 10.5 V
constexpr double kNoiseFlatness = 0.1;

const juce::MemoryBlock &rom()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(QVGT_ROM).loadFileAsData(b);
		return b;
	}();
	return r;
}

const juce::MemoryBlock &font()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(QVGT_LCD_ROM).loadFileAsData(b);
		return b;
	}();
	return r;
}

// The input signals, -1 to 1 (host full scale at 0 dBFS).
enum Signal { kSweep, kSquare1k, kSquare50, kDcSteps, kSine20, kSine1k, kSine20k, kNoise, kSilence, kSignals };
const char *const kSignalNames[] = {"sweep 20 Hz-20 kHz", "square 1 kHz", "square 50 Hz", "DC steps", "sine 20 Hz",
                                    "sine 1 kHz", "sine 20 kHz", "noise", "silence"};

struct Source {
	Signal signal = kSilence;
	double seconds = 1;
	uint32_t seed = 1;
	double operator()(double t)
	{
		switch (signal) {
		case kSweep: {
			const double k = std::log(20000.0 / 20) / seconds;
			return std::sin(2 * kPi * 20 * (std::exp(k * t) - 1) / k);
		}
		case kSquare1k: return std::fmod(t * 1000, 1.0) < 0.5 ? 1 : -1;
		case kSquare50: return std::fmod(t * 50, 1.0) < 0.5 ? 1 : -1;
		case kDcSteps: { // 0, +1, 0, -1, 0.1 s each
			const int q = int(t / 0.1) % 4;
			return q == 1 ? 1 : q == 3 ? -1 : 0;
		}
		case kSine20: return std::sin(2 * kPi * 20 * t);
		case kSine1k: return std::sin(2 * kPi * 1000 * t);
		case kSine20k: return std::sin(2 * kPi * 20000 * t);
		case kNoise:
			seed = seed * 1664525u + 1013904223u;
			return double(seed >> 8) / double(1u << 23) - 1;
		default: return 0;
		}
	}
};

// The latches as the firmware writes them for the PRE-AMP pages (docs/firmware.md, "The preamp's
// controls").
struct PreampSettings {
	int compression = 0; // 0 off, 1-7
	int overdrive = 0;   // 0 off, 1-7
	int distortion = 0;  // 0 off, 1-8
	int tone = 0;        // 0 flat, 1 presence, 2 bright
	bool bass = false;
	int cab = 0;         // 0 off, 1, 2
	int gate = 0;        // 0 off, 1-16
	uint8_t u10() const
	{
		unsigned v = 0;
		if (distortion > 0) {
			const unsigned s = unsigned(~(distortion - 1)) & 7; // inverted, and b4 its lowest bit
			v |= 1u | (s & 1) << 4 | (s >> 1 & 1) << 3 | (s >> 2 & 1) << 2;
		} else {
			v |= 0x1c;
		}
		return uint8_t(v | unsigned(tone == 0) << 1 | unsigned(bass) << 5 | (cab ? 0x80u : 0u) | (cab == 2 ? 0x40u : 0u));
	}
	uint8_t u11() const
	{
		unsigned c = 0;
		for (int b = 0; b < 3; ++b) // b3-b1, bit-reversed
			if (compression >> b & 1)
				c |= 1u << (3 - b);
		return uint8_t(c | unsigned(overdrive) << 4 | unsigned(tone == 1));
	}
	uint8_t l0400() const { return uint8_t(gate == 0 ? 0x1f : 16 - gate); }
	std::string name() const
	{
		static const char *const tones[] = {"flat", "presence", "bright"};
		char n[128];
		std::snprintf(n, sizeof n, "comp %d od %d dist %d %s bass %d cab %d gate %d", compression, overdrive, distortion,
		              tones[tone], int(bass), cab, gate);
		return n;
	}
};

struct Case {
	std::string name;
	bool unit = false;
	PreampSettings preamp; // preamp cases
	int program = 0;       // unit cases
	double inputPot = 1, outputPot = 1; // 0-1
	Signal signal = kSweep;
	double db = 0, seconds = 0.5;
	double toneSeconds = 0; // unit cases: a 1 kHz sine at 0 dBFS for this long before the signal
};

std::vector<Case> cases()
{
	std::vector<Case> c;
	auto add = [&](const PreampSettings &s, Signal sig, double db, double pot = 1, double seconds = 0.5) {
		Case k;
		char n[64];
		std::snprintf(n, sizeof n, ", %s %+.0f dB, INPUT %.0f", kSignalNames[sig], db, pot * 10);
		k.name = "preamp " + s.name() + n;
		k.preamp = s;
		k.signal = sig;
		k.db = db;
		k.inputPot = pot;
		k.seconds = seconds;
		c.push_back(k);
	};
	// Every distortion, tone, bass and cabinet, hard into compression and overdrive at 7.
	for (int dist = 0; dist <= 8; ++dist)
		for (int tone = 0; tone < 3; ++tone)
			for (int bass = 0; bass < 2; ++bass)
				for (int cab = 0; cab < 3; ++cab)
					add({7, 7, dist, tone, bass != 0, cab, 0}, kSweep, 24);
	// Every compression and overdrive, with distortion 8 and without.
	for (int comp = 0; comp <= 7; ++comp)
		for (int od = 0; od <= 7; ++od)
			for (int dist : {0, 8})
				for (Signal sig : {kSquare1k, kDcSteps})
					add({comp, od, dist, 1, true, 1, 0}, sig, 24);
	// Every signal at four levels, both ends of the INPUT pot, with and without the gate, on a few
	// settings: clean, program 30's (the factory's busiest), everything at its top, and the least.
	for (const PreampSettings &s : {PreampSettings{0, 0, 0, 0, false, 0, 0}, PreampSettings{7, 7, 8, 1, false, 2, 0},
	                                PreampSettings{7, 7, 8, 2, true, 2, 0}, PreampSettings{1, 1, 1, 0, false, 1, 0}})
		for (int gate : {0, 16})
			for (double pot : {0.0, 1.0})
				for (int sig = 0; sig < kSignals; ++sig)
					for (double db : {-20.0, 0.0, 12.0, 24.0}) {
						if (sig == kSilence && db != 0)
							continue;
						PreampSettings g = s;
						g.gate = gate;
						add(g, Signal(sig), db, pot, 0.4);
					}
	// Longer: +24 dBFS of noise takes U1's followers past their rails from one side to the other
	// within a step, where plain Newton circles (1 or 2 steps in 1.5 million; docs/circuit.md).
	for (const PreampSettings &s : {PreampSettings{0, 0, 0, 0, false, 0, 0}, PreampSettings{7, 7, 8, 1, false, 2, 0}})
		add(s, kNoise, 24, 1, 4);

	// The unit: every factory program, a 1 kHz sine at 0 dBFS, then a sweep at +12 dBFS.
	for (int p = 0; p < 90; ++p) {
		Case k;
		char n[64];
		std::snprintf(n, sizeof n, "unit program %02d, sine 1 kHz 0 dB, sweep +12 dB", p);
		k.name = n;
		k.unit = true;
		k.program = p;
		k.toneSeconds = 0.5;
		k.signal = kSweep;
		k.db = 12;
		k.seconds = 0.6;
		c.push_back(k);
	}
	// The factory programs with overdrive 7 and distortion on, every signal.
	for (int p : {30, 46, 62, 63})
		for (int sig = 0; sig < kSignals; ++sig)
			for (double db : {0.0, 24.0}) {
				if (sig == kSilence && db != 0)
					continue;
				Case k;
				char n[80];
				std::snprintf(n, sizeof n, "unit program %02d, %s %+.0f dB", p, kSignalNames[sig], db);
				k.name = n;
				k.unit = true;
				k.program = p;
				k.signal = Signal(sig);
				k.db = db;
				k.seconds = 0.6;
				c.push_back(k);
			}
	return c;
}

// Median spectral flatness over 40 Hz-16 kHz, 2048-sample Hann windows above -70 dBFS.
double medianFlatness(const std::vector<float> &x, double rate)
{
	const int n = 2048;
	std::vector<double> flat;
	std::vector<std::complex<double>> a(static_cast<size_t>(n));
	for (size_t start = 0; start + size_t(n) <= x.size(); start += size_t(n)) {
		double e = 0;
		for (int i = 0; i < n; ++i)
			e += double(x[start + size_t(i)]) * x[start + size_t(i)];
		if (std::sqrt(e / n) < 3.2e-4)
			continue;
		for (int i = 0; i < n; ++i)
			a[size_t(i)] = x[start + size_t(i)] * (0.5 - 0.5 * std::cos(2 * kPi * i / n));
		for (int i = 1, j = 0; i < n; ++i) { // radix-2 FFT
			int bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(a[size_t(i)], a[size_t(j)]);
		}
		for (int len = 2; len <= n; len <<= 1) {
			const std::complex<double> w = std::polar(1.0, -2 * kPi / len);
			for (int i = 0; i < n; i += len) {
				std::complex<double> wk = 1;
				for (int k = 0; k < len / 2; ++k, wk *= w) {
					const auto u = a[size_t(i + k)], v = a[size_t(i + k + len / 2)] * wk;
					a[size_t(i + k)] = u + v;
					a[size_t(i + k + len / 2)] = u - v;
				}
			}
		}
		const int lo = int(std::ceil(40.0 * n / rate)), hi = int(16000.0 * n / rate);
		double lg = 0, ar = 0;
		for (int k = lo; k <= hi; ++k) {
			const double p = std::norm(a[size_t(k)]) + 1e-30;
			lg += std::log(p);
			ar += p;
		}
		const int m = hi - lo + 1;
		flat.push_back(std::exp(lg / m) / (ar / m));
	}
	if (flat.empty())
		return 0;
	std::nth_element(flat.begin(), flat.begin() + long(flat.size() / 2), flat.end());
	return flat[flat.size() / 2];
}

struct Result {
	qvgt::SolverStats solver;
	double peak = 0;
	bool finite = true;
	double flatness = -1; // with a 1 kHz sine in; -1: none
};

// DIR/<rate>-<case>.wav
void writeWav(const std::string &dir, double rate, const std::string &name, double fileRate, const std::vector<float> &x)
{
	const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(dir).getChildFile(
	  juce::String(int(rate)) + "-" + juce::String(name).replaceCharacters(" /,+-", "___pm") + ".wav");
	f.getParentDirectory().createDirectory();
	f.deleteFile();
	juce::AudioBuffer<float> b(1, int(x.size()));
	b.copyFrom(0, 0, x.data(), int(x.size()));
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> st(f.createOutputStream());
	if (st == nullptr)
		return;
	auto w = wav.createWriterFor(st, juce::AudioFormatWriterOptions{}.withSampleRate(fileRate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
	                                   juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
	if (w != nullptr)
		w->writeFromAudioSampleBuffer(b, 0, int(x.size()));
}

Result runPreamp(const Case &k, double rate, const std::string &wavDir)
{
	const double preampRate = qvgt::kSampleRate * qvgt::kPreampOver;
	qvgt::Preamp p;
	p.setControls(k.preamp.u10(), k.preamp.u11(), k.preamp.l0400(), k.inputPot);
	p.prepare(preampRate);
	qvgt::Resampler inL(rate, preampRate, 0.9); // as Unit::prepare
	Source src{k.signal, k.seconds};
	const double a = qvgt::Unit::kVoltsFS * std::pow(10.0, k.db / 20);
	Result r;
	std::vector<float> buf, out;
	const bool tone = k.signal == kSine1k && k.inputPot > 0; // INPUT at 0: only the hiss gets in
	const int n = int(k.seconds * rate);
	for (int i = 0; i < n; ++i) {
		buf.clear();
		inL.push(float(a * src(i / rate)), buf);
		for (const float x : buf) {
			const auto o = p.step(x, x);
			for (const double v : {o.toAdc, o.dryL, o.dryR, o.send, o.envelope}) {
				r.finite = r.finite && std::isfinite(v);
				r.peak = std::max(r.peak, std::abs(v));
			}
			if (!wavDir.empty() || tone)
				out.push_back(float(o.toAdc / qvgt::Unit::kVoltsFS));
		}
	}
	r.solver = p.solverStats();
	if (tone) // after the first 0.1 s
		r.flatness = medianFlatness(std::vector<float>(out.begin() + std::min(out.size(), size_t(0.1 * preampRate)), out.end()), preampRate);
	if (!wavDir.empty())
		writeWav(wavDir, rate, k.name, preampRate, out);
	return r;
}

juce::MemoryBlock g_session; // a booted unit with the factory programs (boots skip the recall)

Result runUnit(const Case &k, double rate, const std::string &wavDir)
{
	const int block = 512;
	QVGTProcessor p(&rom(), &font());
	p.setRateAndBufferSizeDetails(rate, block);
	p.setStateInformation(g_session.getData(), int(g_session.getSize()));
	p.waitForBoot();
	p.params().getParameter("input")->setValueNotifyingHost(float(k.inputPot));
	p.params().getParameter("output")->setValueNotifyingHost(float(k.outputPot));
	p.prepareToPlay(rate, block);
	// the program by MIDI, 0.4 s to load, then the tone (if any), the signal and 0.3 s of tail
	const int lead = int(0.4 * rate), tone = int(k.toneSeconds * rate);
	const int total = lead + tone + int((k.seconds + 0.3) * rate);
	Source src{k.signal, k.seconds}, sine{kSine1k};
	const double a = std::pow(10.0, k.db / 20);
	juce::AudioBuffer<float> buf(2, block);
	std::vector<float> out;
	Result r;
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		juce::MidiBuffer midi;
		if (pos == 0)
			midi.addEvent(juce::MidiMessage::programChange(1, k.program), 0);
		for (int i = 0; i < n; ++i) {
			const int t = pos + i - lead - tone;
			const float x = t < -tone ? 0.0f
			              : t < 0     ? float(sine((t + tone) / rate))
			              : t < int(k.seconds * rate) ? float(a * src(t / rate))
			                                          : 0.0f;
			buf.setSample(0, i, x);
			buf.setSample(1, i, x);
		}
		p.processBlock(buf, midi);
		for (int ch = 0; ch < 2; ++ch)
			for (int i = 0; i < n; ++i) {
				const float y = buf.getSample(ch, i);
				r.finite = r.finite && std::isfinite(y);
				r.peak = std::max(r.peak, double(std::abs(y)));
			}
		out.insert(out.end(), buf.getReadPointer(0), buf.getReadPointer(0) + n);
	}
	r.solver = p.solverStats();
	// the sine's flatness, from 0.1 s into it to its end (the latency is a few ms)
	const int from = lead + int(0.1 * rate), to = tone > 0 ? lead + tone : k.signal == kSine1k ? lead + int(k.seconds * rate) : 0;
	if (to > from)
		r.flatness = medianFlatness(std::vector<float>(out.begin() + from, out.begin() + to), rate);
	if (!wavDir.empty())
		writeWav(wavDir, rate, k.name, rate, out);
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
	std::vector<double> rates = {44100, 48000};
	std::string only, wavDir;
	bool list = false;
	int jobs = int(std::max(1u, std::thread::hardware_concurrency() / 2));
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--rate" && i + 1 < argc)
			rates = {std::atof(argv[++i])};
		else if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (a == "--jobs" && i + 1 < argc)
			jobs = std::max(1, std::atoi(argv[++i]));
		else if (a == "--wav" && i + 1 < argc)
			wavDir = argv[++i];
		else if (a == "--list")
			list = true;
		else {
			std::fprintf(stderr, "usage: qvgtcorners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]\n");
			return 2;
		}
	}
	std::vector<Case> all;
	for (auto &k : cases())
		if (only.empty() || k.name.find(only) != std::string::npos)
			all.push_back(k);
	if (list) {
		for (auto &k : all)
			std::printf("%s\n", k.name.c_str());
		return 0;
	}
	const bool anyUnit = std::any_of(all.begin(), all.end(), [](const Case &k) { return k.unit; });
	if (anyUnit) {
		if (rom().getSize() == 0 || font().getSize() == 0) {
			std::fprintf(stderr, "no ROMs at %s, %s: the unit's cases need them\n", QVGT_ROM, QVGT_LCD_ROM);
			return 2;
		}
		QVGTProcessor p(&rom(), &font());
		p.setRateAndBufferSizeDetails(48000, 512);
		p.waitForBoot();
		p.getStateInformation(g_session);
	}

	int bad = 0;
	for (double rate : rates) {
		std::vector<Result> res(all.size());
		std::atomic<size_t> next{0};
		std::vector<std::thread> pool;
		for (int j = 0; j < jobs; ++j)
			pool.emplace_back([&] {
				for (size_t i; (i = next++) < all.size();)
					res[i] = all[i].unit ? runUnit(all[i], rate, wavDir) : runPreamp(all[i], rate, wavDir);
			});
		for (auto &t : pool)
			t.join();
		qvgt::SolverStats total[2];
		size_t cases[2] = {}, noisiest[2] = {};
		double peak[2] = {}, flat[2] = {-1, -1};
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			const int u = all[i].unit;
			total[u] += r.solver;
			++cases[u];
			peak[u] = std::max(peak[u], r.peak);
			if (r.flatness > flat[u]) {
				flat[u] = r.flatness;
				noisiest[u] = i;
			}
			const double bound = u ? kMaxPeak : kMaxVolts;
			std::string why;
			if (r.solver.failures > 0)
				why += " unconverged " + std::to_string(r.solver.failures);
			if (r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > bound)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %5.0f  %-90s%s\n", rate, all[i].name.c_str(), why.c_str());
			}
		}
		for (int u = 0; u < 2; ++u) {
			if (!cases[u])
				continue;
			std::printf("%5.0f Hz %-6s %4zu cases: %ld solves, %ld retried in shorter steps (%.3f %%), %ld unconverged; peak %.3f %s (limit %.3f)\n",
			            rate, u ? "unit" : "preamp", cases[u], total[u].solves, total[u].retried,
			            total[u].solves ? 100.0 * double(total[u].retried) / double(total[u].solves) : 0.0, total[u].failures,
			            peak[u], u ? "FS" : "V", u ? double(kMaxPeak) : kMaxVolts);
			if (flat[u] >= 0)
				std::printf("%5.0f Hz %-6s noisiest with a sine in: %s (median flatness %.4f, limit %.2f)\n", rate, u ? "unit" : "preamp",
				            all[noisiest[u]].name.c_str(), flat[u], kNoiseFlatness);
		}
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
