// CPU benchmark and golden-reference check for the TR-808 (TR808Processor: CPU board, voicing
// board, sleep). Renders a fixed set of workloads at 44.1 and 48 kHz, times each (best of N runs),
// and writes them as golden WAVs or compares them with golden WAVs written earlier, so speed work
// can show it leaves the sound alone.
//   tr808bench golden DIR [RUNS]   render, time, and write DIR/<workload>-<rate>.wav
//   tr808bench check DIR [RUNS]    render, time, and compare with DIR: max |difference| in dBFS,
//                                  and the level of the difference against the signal; and, as
//                                  the free-running squares' and the noise's phase moves the
//                                  waveform with any change to the solver's rounding, the largest
//                                  difference of the envelope (50 ms frames) and of the spectrum
//                                  (1/6-octave bands)
//   tr808bench time [RUNS [NAME]]  render and time only (one workload by name, for profiling)
// CPU % is the render's wall time against the audio's length; Mins/s the instructions retired a
// second of audio (Apple Silicon, the whole process), which a loaded machine doesn't change.
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>
#if defined(__APPLE__)
#include <libproc.h>
#include <pthread.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

namespace {
struct PlayHead : juce::AudioPlayHead {
	juce::Optional<PositionInfo> getPosition() const override
	{
		PositionInfo p;
		p.setIsPlaying(false);
		p.setBpm(120);
		return p;
	}
};

void set(TR808Processor &p, const char *id, float v)
{
	p.params().getParameter(id)->setValueNotifyingHost(v);
}

// A 16th-note grid at 120 BPM played from MIDI: kick on the beats, snare on 2 and 4, closed hat on
// the 8ths, open hat on the last 16th of the bar, clap, toms, rim shot, cowbell and cymbal here and
// there, accents (velocity 127) on the downbeats.
juce::MidiBuffer kit(double rate, double seconds, bool everything)
{
	juce::MidiBuffer m;
	const double step = 60.0 / 120 / 4;
	for (int i = 0; i * step < seconds - step; ++i) {
		const int s = i % 16, bar = i / 16;
		std::vector<int> notes;
		if (s % 2 == 0)
			notes.push_back(42);
		if (s % 4 == 0)
			notes.push_back(36);
		if (s == 4 || s == 12)
			notes.push_back(38);
		if (s == 15)
			notes = {46};
		if (everything) {
			if (s == 12)
				notes.push_back(39);
			if (s == 7 || s == 10)
				notes.push_back(s == 7 ? 43 : 47);
			if (s == 13)
				notes.push_back(50);
			if (s == 3)
				notes.push_back(37);
			if (s == 9)
				notes.push_back(56);
			if (s == 0 && bar % 2 == 0)
				notes.push_back(49);
		}
		const int at = int((0.05 + i * step) * rate);
		for (int n : notes)
			m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(s == 0 ? 127 : 90)), at);
	}
	return m;
}

struct Workload {
	const char *name;
	double seconds;
	std::function<void(TR808Processor &)> setup; // knobs, before prepare
	int midi = 1;                                 // 0 none, 1 the basic kit, 2 every voice
	bool run = false;                             // START: BASIC RHYTHM 1 on the internal clock
	std::function<void(TR808Processor &, double)> automate; // per block, given the time
};

std::vector<Workload> workloads()
{
	std::vector<Workload> w;
	w.push_back({"sequencer", 8, [](TR808Processor &) {}, 0, true, {}});
	w.push_back({"kit", 8, [](TR808Processor &) {}, 1, false, {}});
	w.push_back({"everything", 8, [](TR808Processor &) {}, 2, false, {}});
	w.push_back({"long-decays", 6, [](TR808Processor &p) {
		             set(p, "bd_decay", 1);
		             set(p, "cy_decay", 1);
		             set(p, "oh_decay", 1);
		             set(p, "ac_level", 1);
	             }, 2, false, {}});
	w.push_back({"congas-claves-maracas", 6, [](TR808Processor &p) {
		             for (const char *id : {"lc", "mc", "hc", "cl", "ma"})
			             set(p, id, 1);
	             }, 2, false, {}});
	w.push_back({"automation", 8, [](TR808Processor &) {}, 2, false, [](TR808Processor &p, double t) {
		             set(p, "bd_tone", float(0.5 + 0.5 * std::sin(t * 1.3)));
		             set(p, "bd_decay", float(0.5 + 0.5 * std::sin(t * 0.7 + 1)));
		             set(p, "sd_snappy", float(0.5 + 0.5 * std::sin(t * 0.9 + 2)));
		             set(p, "lt_tuning", float(0.5 + 0.5 * std::sin(t * 0.5 + 3)));
		             set(p, "cy_tone", float(0.5 + 0.5 * std::sin(t * 1.1 + 4)));
		             set(p, "oh_decay", float(0.5 + 0.5 * std::sin(t * 2.3)));
		             set(p, "volume", float(0.6 + 0.4 * std::sin(t * 0.3)));
	             }});
	// A kick, then silence: mostly asleep.
	w.push_back({"idle", 8, [](TR808Processor &) {}, 0, false, {}});
	return w;
}

// The process's instructions retired so far (Apple Silicon; 0 elsewhere): unlike the time, the
// same on a loaded machine.
double instructions()
{
#if defined(__APPLE__)
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0)
		return double(r.ri_instructions);
#endif
	return 0;
}

juce::AudioBuffer<float> render(const juce::MemoryBlock &rom, const Workload &w, double rate, double &seconds, double &ins)
{
	TR808Processor p(&rom);
	PlayHead ph;
	p.setPlayHead(&ph);
	w.setup(p);
	if (w.run) {
		tr808::Machine m;
		std::vector<uint16_t> steps(16, 0);
		for (int s = 0; s < 16; ++s)
			steps[size_t(s)] = uint16_t((s % 2 == 0 ? tr808::kBitCH : 0) | (s % 4 == 0 ? tr808::kBitBD : 0) |
			                            (s == 4 || s == 12 ? tr808::kBitSD : 0) | (s == 0 ? tr808::kBitAC : 0) | (s == 14 ? tr808::kBitCP : 0));
		m.writePattern(0, 0, steps, 2);
		p.setRam(std::vector<uint8_t>(m.ram().begin(), m.ram().end()));
	}
	p.prepareToPlay(rate, 512);
	const int block = 512;
	const int total = int(w.seconds * rate);
	juce::MidiBuffer all = w.midi ? kit(rate, w.seconds, w.midi == 2) : juce::MidiBuffer();
	if (std::string(w.name) == "idle")
		all.addEvent(juce::MidiMessage::noteOn(1, 36, juce::uint8(127)), int(0.1 * rate));
	juce::AudioBuffer<float> out(1, total), buf(2, block);
	const double i0 = instructions();
	const auto t0 = std::chrono::steady_clock::now();
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		if (w.run && pos == int(0.3 * rate) / block * block) {
			p.setKey(TR808Processor::StartStop, true);
			p.setKey(TR808Processor::StartStop, false);
		}
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		for (const auto m : all)
			if (m.samplePosition >= pos && m.samplePosition < pos + n)
				midi.addEvent(m.getMessage(), m.samplePosition - pos);
		if (w.automate)
			w.automate(p, pos / rate);
		p.processBlock(buf, midi);
		out.copyFrom(0, pos, buf, 0, 0, n);
	}
	seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	ins = instructions() - i0;
	return out;
}

void writeWav(const juce::File &f, const juce::AudioBuffer<float> &b, double rate)
{
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
	    juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	auto wr = wav.createWriterFor(s, opts);
	wr->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

bool readWav(const juce::File &f, juce::AudioBuffer<float> &b)
{
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::AudioFormatReader> r(wav.createReaderFor(f.createInputStream().release(), true));
	if (r == nullptr)
		return false;
	b.setSize(1, int(r->lengthInSamples));
	r->read(&b, 0, int(r->lengthInSamples), 0, true, false);
	return true;
}
// The level in 50 ms frames, in dB.
std::vector<double> envelope(const juce::AudioBuffer<float> &b, double rate)
{
	const int n = int(0.05 * rate);
	std::vector<double> e;
	for (int at = 0; at + n <= b.getNumSamples(); at += n) {
		double s = 0;
		for (int i = at; i < at + n; ++i)
			s += double(b.getSample(0, i)) * b.getSample(0, i);
		e.push_back(10 * std::log10(s / n + 1e-30));
	}
	return e;
}

// In-place radix-2 FFT of n (a power of two) complex values.
void fft(std::vector<double> &re, std::vector<double> &im)
{
	const size_t n = re.size();
	for (size_t i = 1, j = 0; i < n; ++i) {
		size_t bit = n >> 1;
		for (; j & bit; bit >>= 1)
			j ^= bit;
		j ^= bit;
		if (i < j) {
			std::swap(re[i], re[j]);
			std::swap(im[i], im[j]);
		}
	}
	for (size_t len = 2; len <= n; len <<= 1) {
		const double a = -2 * juce::MathConstants<double>::pi / double(len);
		for (size_t i = 0; i < n; i += len)
			for (size_t k = 0; k < len / 2; ++k) {
				const double c = std::cos(a * double(k)), sn = std::sin(a * double(k));
				const double xr = re[i + k + len / 2] * c - im[i + k + len / 2] * sn;
				const double xi = re[i + k + len / 2] * sn + im[i + k + len / 2] * c;
				re[i + k + len / 2] = re[i + k] - xr;
				im[i + k + len / 2] = im[i + k] - xi;
				re[i + k] += xr;
				im[i + k] += xi;
			}
	}
}

// The long-term spectrum (4096-point Hann frames, half overlapped) in 1/6-octave bands from 25 Hz
// up to 20 kHz, in dB.
std::vector<double> bands(const juce::AudioBuffer<float> &b, double rate)
{
	constexpr int n = 4096;
	std::vector<double> power(n / 2 + 1, 0.0), re(n), im(n);
	for (int at = 0; at + n <= b.getNumSamples(); at += n / 2) {
		for (int i = 0; i < n; ++i) {
			re[size_t(i)] = b.getSample(0, at + i) * (0.5 - 0.5 * std::cos(2 * juce::MathConstants<double>::pi * i / n));
			im[size_t(i)] = 0;
		}
		fft(re, im);
		for (int k = 0; k <= n / 2; ++k)
			power[size_t(k)] += re[size_t(k)] * re[size_t(k)] + im[size_t(k)] * im[size_t(k)];
	}
	std::vector<double> out;
	for (double lo = 25; lo < 20000; lo *= std::pow(2.0, 1.0 / 6)) {
		const double hi = lo * std::pow(2.0, 1.0 / 6);
		double s = 0;
		for (int k = 0; k <= n / 2; ++k) {
			const double f = k * rate / n;
			if (f >= lo && f < hi)
				s += power[size_t(k)];
		}
		out.push_back(10 * std::log10(s + 1e-30));
	}
	return out;
}

// The largest difference (dB) between two level curves where the reference is within `range` dB
// of its top.
double curveDiff(const std::vector<double> &ref, const std::vector<double> &out, double range)
{
	double top = -1e30, worst = 0;
	for (double r : ref)
		top = std::max(top, r);
	for (size_t k = 0; k < ref.size() && k < out.size(); ++k)
		if (ref[k] > top - range)
			worst = std::max(worst, std::abs(out[k] - ref[k]));
	return worst;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
#if defined(__APPLE__)
	// on the performance cores where it can be (a loaded machine puts a default thread on the
	// efficiency cores, at a third of the speed)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
	const juce::String mode = argc > 1 ? argv[1] : "time";
	const bool hasDir = mode == "golden" || mode == "check";
	const juce::File dir = hasDir && argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
	const int runs = argc > (hasDir ? 3 : 2) ? juce::String(argv[hasDir ? 3 : 2]).getIntValue() : 2;
	if (hasDir && dir == juce::File()) {
		std::printf("usage: tr808bench golden|check DIR [RUNS] | time [RUNS [NAME]]\n");
		return 2;
	}
	if (mode == "golden")
		dir.createDirectory();
	juce::MemoryBlock rom;
	juce::File(TR808_ROM_PATH).loadFileAsData(rom);

	double worstDb = -400, worstEnv = 0, worstSpec = 0, totalCpu = 0, totalIns = 0;
	int missing = 0;
	const bool check = mode == "check";
	std::printf("%-24s %6s %9s %8s %14s %14s %9s %9s\n", "workload", "rate", "CPU %", "Mins/s", check ? "max diff dB" : "", check ? "diff/signal dB" : "",
	            check ? "env dB" : "", check ? "spec dB" : "");
	const juce::String only = !hasDir && argc > 3 ? argv[3] : "";
	for (const auto &w : workloads())
		for (double rate : {44100.0, 48000.0}) {
			if (only.isNotEmpty() && only != w.name)
				continue;
			juce::AudioBuffer<float> out;
			double best = 1e9, bestIns = 1e30;
			for (int r = 0; r < std::max(1, runs); ++r) {
				double s = 0, in = 0;
				out = render(rom, w, rate, s, in);
				best = std::min(best, s);
				bestIns = std::min(bestIns, in);
			}
			const double cpu = 100 * best / w.seconds, mins = 1e-6 * bestIns / w.seconds;
			totalCpu += cpu;
			totalIns += mins;
			const auto file = dir.getChildFile(juce::String(w.name) + "-" + juce::String(int(rate)) + ".wav");
			juce::String extra, rel, envCol, specCol;
			if (mode == "golden") {
				writeWav(file, out, rate);
			} else if (mode == "check") {
				juce::AudioBuffer<float> ref;
				if (!readWav(file, ref) || ref.getNumSamples() != out.getNumSamples()) {
					extra = "no golden";
					++missing;
				} else {
					double d = 0, e = 0, sig = 0;
					for (int i = 0; i < out.getNumSamples(); ++i) {
						const double x = out.getSample(0, i) - ref.getSample(0, i);
						d = std::max(d, std::abs(x));
						e += x * x;
						sig += double(ref.getSample(0, i)) * ref.getSample(0, i);
					}
					const double db = d > 0 ? 20 * std::log10(d) : -400;
					worstDb = std::max(worstDb, db);
					extra = d > 0 ? juce::String(db, 1) : juce::String("identical");
					rel = e > 0 ? juce::String(10 * std::log10(e / std::max(sig, 1e-30)), 1) : juce::String("-");
					if (d > 0) {
						// what doesn't depend on a free-running oscillator's phase: the level in 50 ms
						// frames within 40 dB of the top, the 1/6-octave bands within 50 dB
						const double env = curveDiff(envelope(ref, rate), envelope(out, rate), 40);
						const double spec = curveDiff(bands(ref, rate), bands(out, rate), 50);
						worstEnv = std::max(worstEnv, env);
						worstSpec = std::max(worstSpec, spec);
						envCol = juce::String(env, 3);
						specCol = juce::String(spec, 3);
					}
				}
			}
			std::printf("%-24s %6d %9.2f %8.0f %14s %14s %9s %9s\n", w.name, int(rate), cpu, mins, extra.toRawUTF8(), rel.toRawUTF8(),
			            envCol.toRawUTF8(), specCol.toRawUTF8());
			std::fflush(stdout);
		}
	std::printf("total CPU %% (sum of workloads): %.2f; Mins/s: %.0f\n", totalCpu, totalIns);
	if (check) {
		if (worstDb <= -400)
			std::printf("worst difference: identical (bit-exact)\n");
		else
			std::printf("worst difference: %.1f dBFS; envelope %.3f dB, spectrum %.3f dB\n", worstDb, worstEnv, worstSpec);
	}
	return missing > 0 ? 1 : 0;
}
