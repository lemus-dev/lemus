// qvcorners: the extremes, checked for what the other checks can't see. Every other check
// compares against something (ngspice, golden renders) only where that something exists; this one
// drives what nobody recorded and fails on:
//   - any iterative solve that didn't converge (qv::SolverStats::failures: the dry VCA's control
//     voltage): an unconverged sample is silent in every other check;
//   - output that isn't finite, or past its bound;
//   - broadband noise where a pure tone goes in: with a 1 kHz sine in, a case whose median spectral
//     flatness (40 Hz-16 kHz, 2048-sample windows above -70 dBFS) passes kNoiseFlatness. Every
//     factory program's harmonics, chorus and reverb measured 0.004 at most (a 1 kHz sine at -12
//     and +6 dBFS); white noise is about 0.6.
//
// The cases, each at 44.1 and 48 kHz, through the plugin's processor (the firmware it finds,
// v2.03 by default; QV_CORNERS_ROM=v107 for v1.07): every factory program with a 1 kHz sine at
// 0 dBFS and then a sweep at +12 dBFS, INPUT and OUTPUT at 10; and a few programs (a hall, the
// Leslie, a delay, the Plus's ring modulator and resonators) with every signal (sweeps, squares, DC
// steps, sines at 20 Hz, 1 kHz and 20 kHz, noise, silence) at 0 and +24 dBFS with INPUT at 0, 5.5
// and 10. Bound: the output stages' swing (kMaxPeak).
//
//   qvcorners [--rate 44100|48000] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --wav writes each case's output to DIR/<rate>-<case>.wav, to listen to one.
//
// Exit status 0 when every case passes.
#include "PluginProcessor.h"

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
constexpr double kNoiseFlatness = 0.1;

const juce::MemoryBlock &rom()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		const char *which = std::getenv("QV_CORNERS_ROM");
		juce::File(which && std::string(which) == "v107" ? QV_ROM_V107 : QV_ROM).loadFileAsData(b);
		return b;
	}();
	return r;
}

const juce::MemoryBlock &font()
{
	static const juce::MemoryBlock r = [] {
		juce::MemoryBlock b;
		juce::File(QV_LCD_ROM).loadFileAsData(b);
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

struct Case {
	std::string name;
	int program = 0;
	double inputPot = 1, outputPot = 1; // 0-1
	Signal signal = kSweep;
	double db = 0, seconds = 0.5;
	double toneSeconds = 0; // a 1 kHz sine at 0 dBFS for this long before the signal
};

std::vector<Case> cases()
{
	std::vector<Case> c;
	// Every factory program, a 1 kHz sine at 0 dBFS, then a sweep at +12 dBFS.
	for (int p = 0; p < 90; ++p) {
		Case k;
		char n[64];
		std::snprintf(n, sizeof n, "program %02d, sine 1 kHz 0 dB, sweep +12 dB", p);
		k.name = n;
		k.program = p;
		k.toneSeconds = 0.5;
		k.signal = kSweep;
		k.db = 12;
		k.seconds = 0.6;
		c.push_back(k);
	}
	// A few programs with every signal, at three INPUT settings (docs/firmware.md: 00 all four
	// effects, 15 a hall, 38 the Leslie, 43 the longest delay, 88 and 89 the Plus's ring modulator and
	// resonators).
	for (int p : {0, 15, 38, 43, 88, 89})
		for (double pot : {0.0, 0.55, 1.0})
			for (int sig = 0; sig < kSignals; ++sig)
				for (double db : {0.0, 24.0}) {
					if (sig == kSilence && db != 0)
						continue;
					Case k;
					char n[96];
					std::snprintf(n, sizeof n, "program %02d, INPUT %.1f, %s %+.0f dB", p, pot * 10, kSignalNames[sig], db);
					k.name = n;
					k.program = p;
					k.inputPot = pot;
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
	qv::SolverStats solver;
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

juce::MemoryBlock g_session; // a booted unit with the factory programs (boots skip the recall)

Result runUnit(const Case &k, double rate, const std::string &wavDir)
{
	const int block = 512;
	QVProcessor p(&rom(), &font());
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
			std::fprintf(stderr, "usage: qvcorners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR]\n");
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
	if (rom().getSize() == 0 || font().getSize() == 0) {
		std::fprintf(stderr, "no ROMs at %s, %s\n", QV_ROM, QV_LCD_ROM);
		return 2;
	}
	{
		QVProcessor p(&rom(), &font());
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
					res[i] = runUnit(all[i], rate, wavDir);
			});
		for (auto &t : pool)
			t.join();
		qv::SolverStats total;
		size_t noisiest = 0;
		double peak = 0, flat = -1;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[i];
			total += r.solver;
			peak = std::max(peak, r.peak);
			if (r.flatness > flat) {
				flat = r.flatness;
				noisiest = i;
			}
			std::string why;
			if (r.solver.failures > 0)
				why += " unconverged " + std::to_string(r.solver.failures);
			if (r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > kMaxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %5.0f  %-90s%s\n", rate, all[i].name.c_str(), why.c_str());
			}
		}
		std::printf("%5.0f Hz %4zu cases: %ld solves, %ld unconverged; peak %.3f FS (limit %.3f)\n", rate, all.size(), total.solves,
		            total.failures, peak, double(kMaxPeak));
		if (flat >= 0)
			std::printf("%5.0f Hz noisiest with a sine in: %s (median flatness %.4f, limit %.2f)\n", rate, all[noisiest].name.c_str(), flat,
			            kNoiseFlatness);
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
