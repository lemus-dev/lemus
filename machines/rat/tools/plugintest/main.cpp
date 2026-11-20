// Headless plugin test: runs RatProcessor without a host. Checks silence, bypass, the knobs'
// effect, random-settings stress, switching calibration and op amp mid-stream, a long run at
// full distortion, every sample rate and block size, mono, and state round trip. With an
// output directory it also writes a WAV of the stress run and a PNG of the view.
//   plugintest [OUT_DIR]
#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>

namespace {
int g_failures = 0;

void check(bool ok, const char *what)
{
	std::printf("  [%s] %s\n", ok ? "ok" : "FAIL", what);
	if (!ok)
		++g_failures;
}

void setParam(RatProcessor &p, const char *id, float plain)
{
	auto *par = p.params().getParameter(id);
	par->setValueNotifyingHost(par->convertTo0to1(plain));
}

// Runs the processor over a generated input. gen(i) gives input sample i (both channels).
template <class Gen>
juce::AudioBuffer<float> run(RatProcessor &p, double seconds, double rate, int block, Gen gen, int channels = 2,
                             std::function<void(int)> perBlock = {})
{
	const int total = int(seconds * rate);
	juce::AudioBuffer<float> out(channels, total);
	juce::AudioBuffer<float> buf(channels, block);
	juce::MidiBuffer midi;
	int b = 0;
	for (int pos = 0; pos < total; pos += block, ++b) {
		if (perBlock)
			perBlock(b);
		const int n = std::min(block, total - pos);
		buf.setSize(channels, n, false, false, true);
		for (int ch = 0; ch < channels; ++ch)
			for (int i = 0; i < n; ++i)
				buf.setSample(ch, i, gen(pos + i));
		p.processBlock(buf, midi);
		for (int ch = 0; ch < channels; ++ch)
			out.copyFrom(ch, pos, buf, ch, 0, n);
	}
	return out;
}

double rms(const juce::AudioBuffer<float> &b, int from, int n, int ch = 0)
{
	double s = 0;
	for (int i = from; i < from + n && i < b.getNumSamples(); ++i)
		s += double(b.getSample(ch, i)) * b.getSample(ch, i);
	return std::sqrt(s / std::max(1, n));
}

bool finiteAndBounded(const juce::AudioBuffer<float> &b, float limit)
{
	for (int ch = 0; ch < b.getNumChannels(); ++ch)
		for (int i = 0; i < b.getNumSamples(); ++i) {
			const float v = b.getSample(ch, i);
			if (!std::isfinite(v) || std::abs(v) > limit)
				return false;
		}
	return true;
}

void save(const juce::File &dir, const char *name, const juce::AudioBuffer<float> &b, double rate)
{
	if (dir == juce::File())
		return;
	auto f = dir.getChildFile(name);
	f.deleteFile();
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> s(f.createOutputStream());
	std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(s.get(), rate, unsigned(b.getNumChannels()), 32, {}, 0));
	s.release();
	w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

double sine(int i, double hz, double amp, double rate)
{
	return amp * std::sin(2 * juce::MathConstants<double>::pi * hz * i / rate);
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
	if (outDir != juce::File())
		outDir.createDirectory();
	const double rate = 48000;

	std::printf("basic\n");
	{
		RatProcessor p;
		p.prepareToPlay(rate, 512);
		const auto quiet = run(p, 1.0, rate, 512, [](int) { return 0.0f; });
		check(rms(quiet, 0, quiet.getNumSamples()) < 1e-5, "silence in, silence out (no bias thump left after prepare)");

		const auto tone = run(p, 1.0, rate, 512, [&](int i) { return float(sine(i, 220, 0.25, rate)); });
		const double wet = rms(tone, int(rate / 2), int(rate / 2));
		check(wet > 1e-3, "a tone comes out");

		setParam(p, "on", 0.0f);
		const auto byp = run(p, 0.5, rate, 512, [&](int i) { return float(sine(i, 220, 0.25, rate)); });
		double err = 0;
		for (int i = int(rate / 4); i < byp.getNumSamples(); ++i)
			err = std::max(err, std::abs(byp.getSample(0, i) - sine(i, 220, 0.25, rate)));
		check(err < 1e-6, "footswitch off is true bypass (output = input)");
		setParam(p, "on", 1.0f);

		// More DISTORTION: more harmonics relative to the fundamental (crest factor falls).
		auto crest = [&](float dist) {
			setParam(p, "distortion", dist);
			const auto y = run(p, 0.6, rate, 512, [&](int i) { return float(sine(i, 220, 0.05, rate)); });
			float pk = 0;
			for (int i = int(rate * 0.3); i < y.getNumSamples(); ++i)
				pk = std::max(pk, std::abs(y.getSample(0, i)));
			return pk / float(rms(y, int(rate * 0.3), int(rate * 0.3)));
		};
		const float c0 = crest(0.5f), c10 = crest(10.0f);
		check(c10 < c0 * 0.85f, "full DISTORTION squares the wave up (lower crest factor)");
		setParam(p, "distortion", 5.0f);

		// FILTER clockwise darkens: less energy in a high-passed copy.
		auto bright = [&](float f) {
			setParam(p, "filter", f);
			const auto y = run(p, 0.6, rate, 512, [&](int i) { return float(sine(i, 220, 0.25, rate)); });
			double hp = 0, prev = 0;
			for (int i = int(rate * 0.3); i < y.getNumSamples(); ++i) {
				const double v = y.getSample(0, i);
				hp += (v - prev) * (v - prev);
				prev = v;
			}
			return hp / (rms(y, int(rate * 0.3), int(rate * 0.3)) + 1e-12);
		};
		check(bright(0.0f) > 3 * bright(10.0f), "FILTER clockwise is darker");
		setParam(p, "filter", 4.0f);

		auto level = [&](float v) {
			setParam(p, "volume", v);
			const auto y = run(p, 0.5, rate, 512, [&](int i) { return float(sine(i, 220, 0.25, rate)); });
			return rms(y, int(rate * 0.25), int(rate * 0.25));
		};
		const double v2 = level(2.0f), v8 = level(8.0f);
		check(v8 > v2 * 4, "VOLUME raises the level");
		setParam(p, "volume", 5.0f);
	}

	std::printf("idle\n");
	{
		// A tone, then silence: each channel sleeps once its output has settled, puts out exact
		// zeros, and wakes on the next input as if it had run all along.
		auto tone = [&](int i) { return float(sine(i, 110, 0.5, rate)); };
		RatProcessor p;
		setParam(p, "distortion", 10.0f);
		p.prepareToPlay(rate, 512);
		run(p, 0.5, rate, 512, tone);
		run(p, 7.0, rate, 512, [](int) { return 0.0f; });
		const auto idle = run(p, 1.0, rate, 512, [](int) { return 0.0f; });
		check(p.asleep(0) && p.asleep(1) && idle.getMagnitude(0, idle.getNumSamples()) == 0.0f, "silent input: both channels sleep, exact zeros out");
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		run(p, 10.0, rate, 512, [](int) { return 0.0f; });
		std::printf("    10 s asleep took %.1f ms\n", juce::Time::getMillisecondCounterHiRes() - t0);
		setParam(p, "filter", 7.0f);
		run(p, 0.01, rate, 512, [](int) { return 0.0f; });
		check(!p.asleep(0), "a settings change wakes it");
		run(p, 1.0, rate, 512, [](int) { return 0.0f; });
		check(p.asleep(0), "and it sleeps again once settled");
		// The same history through a pedal that never sleeps.
		RatProcessor awake;
		awake.setIdleSleep(false);
		setParam(awake, "distortion", 10.0f);
		awake.prepareToPlay(rate, 512);
		run(awake, 0.5, rate, 512, tone);
		run(awake, 19.0, rate, 512, [](int) { return 0.0f; });
		setParam(awake, "filter", 7.0f);
		run(awake, 1.01, rate, 512, [](int) { return 0.0f; });
		const auto woken = run(p, 2.0, rate, 512, tone);
		const auto ref = run(awake, 2.0, rate, 512, tone);
		auto diffDb = [&](double from, double to) {
			double d = 0, pk = 0;
			for (int i = int(from * rate); i < int(to * rate); ++i) {
				d = std::max(d, double(std::abs(woken.getSample(0, i) - ref.getSample(0, i))));
				pk = std::max(pk, double(std::abs(ref.getSample(0, i))));
			}
			return 20 * std::log10(d / pk + 1e-30);
		};
		std::printf("    woken vs never asleep: first 50 ms %.0f dB, 0.05-1 s %.0f dB, 1-2 s %.0f dB\n", diffDb(0, 0.05), diffDb(0.05, 1), diffDb(1, 2));
		check(!p.asleep(0) && diffDb(1, 2) < -90, "input wakes it; from a second in it plays as if it had never slept");
	}

	std::printf("stress\n");
	{
		RatProcessor p;
		p.prepareToPlay(rate, 256);
		juce::Random r(303);
		const char *knobs[] = {"distortion", "filter", "volume"};
		const auto y = run(
			p, 20.0, rate, 256,
			[&](int i) {
				// Guitar-ish: decaying harmonics, plus bursts of full-scale square.
				const double t = std::fmod(i / rate, 0.5);
				const double s = (i / int(rate)) % 4 == 3 ? (std::sin(i * 0.01) > 0 ? 0.9 : -0.9)
				                                          : 0.4 * std::exp(-t * 6) * std::sin(2 * juce::MathConstants<double>::pi * 110 * i / rate);
				return float(s);
			},
			2, [&](int b) {
				if (b % 37 == 0)
					for (auto *k : knobs)
						setParam(p, k, r.nextFloat() * 10.0f);
				if (b % 211 == 0)
					setParam(p, "input", -12.0f + 36.0f * r.nextFloat());
				if (b % 613 == 0)
					setParam(p, "on", float(r.nextInt(2)));
			});
		check(finiteAndBounded(y, 8.0f), "random knob jumps, INPUT changes and footswitch presses stay finite and bounded");
		save(outDir, "stress.wav", y, rate);
	}

	std::printf("long run\n");
	{
		// INPUT at its top, +24 dB, on a sine 3 dB under full scale.
		RatProcessor p;
		setParam(p, "input", 24.0f);
		setParam(p, "distortion", 10.0f);
		setParam(p, "filter", 0.0f);
		setParam(p, "volume", 10.0f);
		p.prepareToPlay(rate, 512);
		const auto y = run(p, 60.0, rate, 512, [&](int i) { return i < int(57 * rate) ? float(sine(i, 41.2, 0.7, rate)) : 0.0f; });
		const double tail = rms(y, int(58.5 * rate), int(1.4 * rate));
		check(finiteAndBounded(y, 8.0f) && tail < 1e-4, "57 s at full distortion with INPUT +24 dB, then silence settles");
	}

	std::printf("rates and blocks\n");
	for (double sr : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
		for (int block : {1, 64, 512, 4096}) {
			RatProcessor p;
			p.prepareToPlay(sr, block);
			const auto y = run(p, 0.25, sr, block, [&](int i) { return float(sine(i, 440, 0.3, sr)); });
			const double v = rms(y, int(sr * 0.125), int(sr * 0.125));
			char what[128];
			std::snprintf(what, sizeof what, "%.1f kHz, block %d: finite, rms %.4f, %dx", sr / 1000, block, v, p.oversampling());
			check(finiteAndBounded(y, 8.0f) && v > 1e-3, what);
		}

	std::printf("mono\n");
	{
		RatProcessor p;
		juce::AudioProcessor::BusesLayout mono;
		mono.inputBuses.add(juce::AudioChannelSet::mono());
		mono.outputBuses.add(juce::AudioChannelSet::mono());
		check(p.setBusesLayout(mono), "mono layout accepted");
		p.prepareToPlay(rate, 512);
		const auto y = run(p, 0.3, rate, 512, [&](int i) { return float(sine(i, 220, 0.3, rate)); }, 1);
		check(finiteAndBounded(y, 8.0f) && rms(y, 0, y.getNumSamples()) > 1e-3, "mono processes");
	}

	std::printf("stereo\n");
	{
		// Each side of a stereo instance comes out exactly as a mono instance fed that side alone:
		// while the sides are the same (the left channel works for both), when they part (the right
		// takes a copy of the left's state), solved as a pair, asleep, and through knob moves.
		auto guitarAt = [&](int i, double f) {
			const double t = double(i) / rate, tn = std::fmod(t, 0.25);
			return 0.4 * std::sin(2 * 3.14159265358979 * f * tn) * std::exp(-tn * 9);
		};
		auto left = [&](int i) {
			const double t = double(i) / rate;
			return t < 0.4 || (t >= 2.0 && t < 2.5) ? 0.0f : float(guitarAt(i, 82.41));
		};
		auto right = [&](int i) { return double(i) / rate < 1.0 ? left(i) : left(i) == 0.0f ? 0.0f : float(guitarAt(i, 123.47)); };
		juce::AudioProcessor::BusesLayout mono;
		mono.inputBuses.add(juce::AudioChannelSet::mono());
		mono.outputBuses.add(juce::AudioChannelSet::mono());
		RatProcessor st, ml, mr;
		ml.setBusesLayout(mono);
		mr.setBusesLayout(mono);
		for (auto *p : {&st, &ml, &mr}) {
			setParam(*p, "distortion", 8.0f);
			p->prepareToPlay(rate, 256);
		}
		const int total = int(3.0 * rate), block = 256;
		juce::AudioBuffer<float> bs(2, block), bl(1, block), br(1, block);
		juce::MidiBuffer midi;
		double worst = 0;
		bool sleptTogether = false;
		for (int pos = 0, b = 0; pos < total; pos += block, ++b) {
			if (b == 300 || b == 420)
				for (auto *p : {&st, &ml, &mr})
					setParam(*p, "filter", b == 300 ? 8.0f : 2.0f);
			for (int i = 0; i < block; ++i) {
				bs.setSample(0, i, left(pos + i));
				bs.setSample(1, i, right(pos + i));
				bl.setSample(0, i, left(pos + i));
				br.setSample(0, i, right(pos + i));
			}
			st.processBlock(bs, midi);
			ml.processBlock(bl, midi);
			mr.processBlock(br, midi);
			sleptTogether = sleptTogether || (pos < int(0.4 * rate) && st.asleep(0) && st.asleep(1));
			for (int i = 0; i < block; ++i)
				worst = std::max({worst, double(std::abs(bs.getSample(0, i) - bl.getSample(0, i))),
					double(std::abs(bs.getSample(1, i) - br.getSample(0, i)))});
		}
		check(worst == 0.0 && sleptTogether, "each side exactly as a mono instance: same input, parted, paired, asleep, knob moves");

		// What a stereo instance costs, with the same input on both sides (a mono source) and
		// with different sides.
		for (int differ = 0; differ < 2; ++differ) {
			RatProcessor q;
			q.prepareToPlay(rate, 512);
			juce::AudioBuffer<float> buf(2, 512);
			double best = 1e9;
			for (int r = 0; r < 3; ++r) {
				const auto t0 = juce::Time::getMillisecondCounterHiRes();
				for (int pos = 0; pos < int(2 * rate); pos += 512) {
					for (int i = 0; i < 512; ++i) {
						buf.setSample(0, i, float(guitarAt(pos + i, 82.41)));
						buf.setSample(1, i, float(differ ? guitarAt(pos + i, 123.47) : guitarAt(pos + i, 82.41)));
					}
					q.processBlock(buf, midi);
				}
				best = std::min(best, juce::Time::getMillisecondCounterHiRes() - t0);
			}
			std::printf("    stereo instance, %s: %.1f%% of a core\n", differ ? "sides differ" : "same on both sides", best / 20.0);
		}
	}

	std::printf("state\n");
	{
		RatProcessor p;
		setParam(p, "distortion", 7.3f);
		setParam(p, "filter", 2.1f);
		setParam(p, "volume", 6.6f);
		setParam(p, "on", 0.0f);
		setParam(p, "input", 6.5f);
		juce::MemoryBlock state;
		p.getStateInformation(state);
		RatProcessor q;
		q.setStateInformation(state.getData(), int(state.getSize()));
		auto plain = [&](RatProcessor &x, const char *id) {
			auto *par = x.params().getParameter(id);
			return par->convertFrom0to1(par->getValue());
		};
		check(std::abs(plain(q, "distortion") - 7.3f) < 0.02f && std::abs(plain(q, "filter") - 2.1f) < 0.02f &&
		          std::abs(plain(q, "volume") - 6.6f) < 0.02f && plain(q, "on") < 0.5f && std::abs(plain(q, "input") - 6.5f) < 0.06f,
		      "parameters restore");
		check(q.params().getParameter("level") == nullptr && q.params().getParameter("calibration") == nullptr &&
		          q.getParameters().size() == 5,
		      "five parameters: the three knobs, the footswitch and INPUT");
		auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
		check(xml != nullptr && xml->getIntAttribute("model") == RatProcessor::kModelVersion && q.modelVersion() == RatProcessor::kModelVersion,
		      "the session keeps the model version");
	}

	if (outDir != juce::File()) {
		std::printf("view\n");
		RatProcessor p;
		p.prepareToPlay(rate, 512);
		p.setTestSignal(true);
		run(p, 1.0, rate, 512, [](int) { return 0.0f; });
		HardwareView view(p);
		view.setBounds(0, 0, HardwareView::kWidth, HardwareView::kHeight);
		const auto img = view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f);
		auto f = outDir.getChildFile("view-hardware.png");
		f.deleteFile();
		juce::FileOutputStream s(f);
		juce::PNGImageFormat().writeImageToStream(img, s);
		check(true, "rendered the view");
	}

	std::printf(g_failures ? "%d FAILED\n" : "all passed\n", g_failures);
	return g_failures ? 1 : 0;
}
