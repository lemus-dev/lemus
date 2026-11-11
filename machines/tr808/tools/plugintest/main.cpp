// Headless plugin test: runs TR808Processor without a host. Checks that the firmware sequencer
// follows a fake host transport (DIN sync) and START/STOP on the internal clock, that MIDI plays
// the voices (velocity 100 and up accents), that the idle board sleeps and wakes as before, that
// voice mode works without the ROM, and that state (parameters + RAM) round-trips.
//   tr808plugintest [OUT_DIR]   (writes sync.wav, midi.wav, internal.wav when given; TR808_QUICK=1:
//                               the stress and long runs cut short)
//   tr808plugintest --panel OUT.png [SCALE] [running|off] [service] [zoom=Z@X,Y] [setup=N]
//     (draws the hardware view; running: in 1st PART, BD selected, a rhythm playing; service:
//     SERVICE, zoomed Z times onto panel mm X, Y; setup: trimmer N's procedure on the scope)
//   tr808plugintest --factory DIR   (the factory rhythms from the sequencer, as WAVs)
//   tr808plugintest --service [DIR]   (the SERVICE checks alone; DIR: the view's shots, whole and
//                                     zoomed)
#include "HardwareView.h"
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace {
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

double rms(const juce::AudioBuffer<float> &b, int from, int n)
{
	double s = 0;
	int k = 0;
	for (int i = std::max(0, from); i < from + n && i < b.getNumSamples(); ++i, ++k)
		s += double(b.getSample(0, i)) * b.getSample(0, i);
	return std::sqrt(s / std::max(1, k));
}

juce::AudioBuffer<float> run(TR808Processor &p, FakePlayHead &ph, double seconds, double rate, juce::MidiBuffer *midiAll = nullptr)
{
	const int block = 512;
	const int total = int(seconds * rate);
	juce::AudioBuffer<float> out(1, total);
	juce::AudioBuffer<float> buf(2, block);
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		juce::MidiBuffer midi;
		if (midiAll)
			for (const auto m : *midiAll)
				if (m.samplePosition >= pos && m.samplePosition < pos + n)
					midi.addEvent(m.getMessage(), m.samplePosition - pos);
		p.processBlock(buf, midi);
		out.copyFrom(0, pos, buf, 0, 0, n);
		if (ph.playing)
			ph.ppq += n / rate * ph.bpm / 60.0;
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
	const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
	    juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	auto w = wav.createWriterFor(s, opts);
	w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

// Onsets: the starts of the sound's 1 ms windows that jump 12 dB over the window before.
std::vector<double> onsets(const juce::AudioBuffer<float> &b, double rate)
{
	std::vector<double> t;
	const int w = int(0.001 * rate);
	double last = 1e-9;
	for (int i = 0; i + w < b.getNumSamples(); i += w) {
		const double r = rms(b, i, w);
		if (r > 1e-3 && r > 4 * last && (t.empty() || i / rate - t.back() > 0.1))
			t.push_back(i / rate);
		last = std::max(r, 1e-9);
	}
	return t;
}

int failures = 0;
void check(bool ok, const char *what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++failures;
}

void set(TR808Processor &p, const char *id, float v)
{
	p.params().getParameter(id)->setValueNotifyingHost(v);
}
void choose(TR808Processor &p, const char *id, int index)
{
	auto *c = dynamic_cast<juce::AudioParameterChoice *>(p.params().getParameter(id));
	c->setValueNotifyingHost(c->convertTo0to1(float(index)));
}
} // namespace

// --panel OUT.png [SCALE] [running|off] [--artwork PACK.svg]: draws the hardware view (no window)
// into a PNG and quits; running: a pattern playing; off: switched off; with an artwork pack, its marks.
int panelShot(const char *path, float scale, bool running, bool off, const char *artworkPath, bool service = false, float zoom = 1,
              juce::Point<float> at = {}, int setup = -1, bool rear = false)
{
	juce::MemoryBlock rom, artwork;
	juce::File(TR808_ROM_PATH).loadFileAsData(rom);
	if (artworkPath != nullptr)
		juce::File::getCurrentWorkingDirectory().getChildFile(artworkPath).loadFileAsData(artwork);
	TR808Processor p(&rom, lemus::Artwork::parse(artworkPath != nullptr ? &artwork : nullptr));
	p.prepareToPlay(48000, 512);
	if (running) {
		// BASIC RHYTHM 1 in 1st PART with BD selected: its steps lit, the running light on them
		tr808::Machine m;
		std::vector<uint16_t> steps(16, 0);
		for (int s = 0; s < 16; ++s)
			steps[size_t(s)] = uint16_t((s % 4 == 0 ? tr808::kBitBD : 0) | (s % 2 == 0 ? tr808::kBitCH : 0));
		m.writePattern(0, 0, steps, 2);
		p.setRam(std::vector<uint8_t>(m.ram().begin(), m.ram().end()));
		p.params().getParameter(TR808Processor::kModeId)->setValueNotifyingHost(1.0f / 5);
		p.params().getParameter(TR808Processor::kInstrumentId)->setValueNotifyingHost(1.0f / 11);
		juce::AudioBuffer<float> buf(2, 512);
		juce::MidiBuffer midi;
		for (int b = 0; b < 40; ++b) {
			if (b == 4)
				p.setKey(TR808Processor::Step1, true); // pick BASIC RHYTHM 1
			if (b == 20)
				p.setKey(TR808Processor::Step1, false);
			if (b == 24)
				p.setKey(TR808Processor::StartStop, true);
			if (b == 25)
				p.setKey(TR808Processor::StartStop, false);
			p.processBlock(buf, midi);
		}
		for (int b = 0; b < 60; ++b)
			p.processBlock(buf, midi);
	}
	if (off) {
		p.setPower(false);
		juce::AudioBuffer<float> buf(2, 512);
		juce::MidiBuffer midi;
		for (int b = 0; b < 4; ++b)
			p.processBlock(buf, midi);
	}
	tr808::HardwareView view(p);
	view.setBounds(0, 0, tr808::HardwareView::kWidth, tr808::HardwareView::kHeight);
	if (rear) { // the rear panel, with cables in BD and SD
		p.setCable(tr808::Voice::BD, true);
		p.setCable(tr808::Voice::SD, true);
		view.showRear(true);
	}
	if (service) {
		view.showService(true);
		if (zoom > 1)
			view.serviceView(zoom, at);
		if (setup >= 0) { // the procedure on the scope, the board run past it (TM3's: a clap played)
			view.serviceSetUp(setup);
			juce::AudioBuffer<float> buf(2, 512);
			for (int b = 0; b < 100; ++b) {
				juce::MidiBuffer midi;
				if (b % 50 == 4)
					midi.addEvent(juce::MidiMessage::noteOn(10, 39, juce::uint8(50)), 0);
				p.processBlock(buf, midi);
			}
			view.serviceSweep();
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

// SERVICE: each trimmer's procedure reads the unit's own target on its test point; the unit's serial
// number and portion and the trims round-trip with the session; the 000300-010599 board's clap is
// louder by what R346 and R332 give; every netlist pin can be probed; the board's voices still run as jobs. With DIR: the
// view's shots, whole and zoomed.
void service(const juce::File &dir)
{
	const double rate = 48000;
	juce::MemoryBlock rom;
	juce::File(TR808_ROM_PATH).loadFileAsData(rom);
	char what[300];
	auto runFor = [&](TR808Processor &p, double seconds, juce::MidiBuffer *m = nullptr) {
		FakePlayHead ph;
		p.setPlayHead(&ph);
		return run(p, ph, seconds, rate, m);
	};
	// 1. The procedures (SN p.14), through the view's SET UP
	{
		TR808Processor p(&rom);
		p.prepareToPlay(rate, 512);
		set(p, "tempo", 1.0f); // TM2's: TEMPO and FINE fully clockwise
		set(p, "fine", 1.0f);
		tr808::HardwareView view(p);
		view.setBounds(0, 0, tr808::HardwareView::kWidth, tr808::HardwareView::kHeight);
		view.showService(true);
		const auto unit = tr808::unitCalibration();
		struct Want {
			int trimmer;
			const char *tp;
			double target, tolerance; // the reading, and the share it may be off
			const char *unit;
		};
		const Want wants[] = {
			{0, "TP2", 1.9, 0.002, "ms"},
			{1, "TP3", 1000.0 / (24 * 300.0) * 60, 0.002, "ms"},
			{3, "TP4", p.tp4PerNoiseRms() * unit.noiseRms * 1000, 0.05, "mV rms"},
			{4, "TP6", 1000 / unit.cbHz1, 0.003, "ms"},
			{5, "TP7", 1000 / unit.cbHz2, 0.003, "ms"},
		};
		for (const auto &w : wants) {
			view.serviceSetUp(w.trimmer);
			runFor(p, 1.0);
			view.serviceSweep();
			const double got = view.serviceReading();
			std::snprintf(what, sizeof what, "service: the unit's %s reads %.4g %s by its procedure (%.4g wanted)", w.tp, got, w.unit, w.target);
			check(std::abs(got / w.target - 1) < w.tolerance, what);
		}
		// TM3's: CP with LEVEL fully clockwise, its peak to peak at TP5 (the notes' picture: 6 Vp-p)
		set(p, "cp_level", 1.0f);
		view.serviceSetUp(2);
		juce::MidiBuffer clap;
		clap.addEvent(juce::MidiMessage::noteOn(10, 39, juce::uint8(50)), int(0.3 * rate));
		runFor(p, 0.6, &clap);
		view.serviceSweep();
		std::printf("      service: the unit's TP5 with CP LEVEL fully clockwise: %.2f Vp-p (the notes' picture: 6)\n", view.serviceReading());
		if (dir != juce::File()) {
			// the shots: the whole view with TM1's procedure (the cowbell's square), and zoomed onto it
			view.serviceSetUp(4);
			runFor(p, 0.5);
			view.serviceSweep();
			for (const auto &[name, zoom] : {std::pair<const char *, float>{"service.png", 1.0f}, {"service-zoom.png", 4.0f}}) {
				if (zoom > 1)
					view.serviceView(zoom, {445, 236});
				const auto image = view.createComponentSnapshot(view.getLocalBounds(), true, 2.0f);
				const auto f = dir.getChildFile(name);
				f.deleteFile();
				juce::FileOutputStream stream(f);
				juce::PNGImageFormat().writeImageToStream(image, stream);
				std::printf("      %s\n", f.getFullPathName().toRawUTF8());
			}
		}
	}
	// 2. The unit and its trims round-trip with the session
	{
		TR808Processor a(&rom);
		a.setSerial(300);
		a.setPortionChanged(false);
		set(a, "tm_cb1", 0.123f);
		set(a, "tm_noise", 0.456f);
		juce::MemoryBlock st;
		a.getStateInformation(st);
		TR808Processor b(&rom);
		b.setStateInformation(st.getData(), int(st.getSize()));
		bool trims = true;
		for (const char *id : TR808Processor::kTrimIds)
			trims = trims && b.params().getParameter(id)->getValue() == a.params().getParameter(id)->getValue();
		check(b.serial() == 300 && !b.portionChanged() && trims
		          && std::abs(b.params().getParameter("tm_cb1")->getValue() - 0.123f) < 1e-6,
		      "service: the serial number, the portion and the trims round-trip with the session");
		TR808Processor c(&rom);
		check(c.serial() == tr808::kFittedSerial && c.portionChanged(),
		      "service: a new instance is the fitted unit, SN 103852, the portion changed");
	}
	// 3. The 000300-010599 board's clap. Its R332 22k feeds IC21b's band-pass more noise, for the
	// bursts (the OTA, IC22) and the reverb (Q70); its R346 1k gives the reverb's VCA a larger share
	// of it (R342 15k over R346). On the board itself, R332 and R346 alone (the same noise), each
	// path at its coupling capacitor into IC19b (C150, C152), against what the band-pass (its
	// admittances, over the noise's flat band) and the divider give; then the whole board, with its
	// 22k gate (R106: less gate drive, a weaker trig into the envelopes), at the clap's jack, TM4 at
	// the late board's gain of IC24a (the first noise generator has R127 4.7k with it).
	{
		auto paths = [&](bool early, double &ota, double &reverb) {
			tr808::VoiceBoard b;
			auto cal = tr808::unitCalibration();
			if (early)
				cal.parts["cp.r332"] = 22e3, cal.parts["cp.r346"] = 1e3;
			b.setCalibration(cal);
			b.prepare(rate);
			std::vector<float> out(512), a, c;
			for (int k = 0; k < 40; ++k)
				b.render(out.data(), 512);
			b.setProbe(0, "cp.c150");
			b.setProbe(1, "cp.c152");
			auto take = [&](int n) {
				b.render(out.data(), n);
				const int f = n * b.factor();
				a.insert(a.end(), b.probeSamples(0), b.probeSamples(0) + f);
				c.insert(c.end(), b.probeSamples(1), b.probeSamples(1) + f);
			};
			b.setLines(true, tr808::kBitCP);
			take(58); // the trigger's 1.2 ms
			b.setLines(false, 0);
			for (int k = 0; k < 32; ++k)
				take(512);
			auto acRms = [](const std::vector<float> &x) {
				double mean = 0, sq = 0;
				for (float v : x)
					mean += v;
				mean /= double(x.size());
				for (float v : x)
					sq += (v - mean) * (v - mean);
				return std::sqrt(sq / double(x.size()));
			};
			ota = acRms(a);
			reverb = acRms(c);
		};
		double ol, rl, oe, re;
		paths(false, ol, rl);
		paths(true, oe, re);
		const auto cal = tr808::unitCalibration();
		auto bandPower = [&](double r332) { // IC21b's multiple-feedback band-pass, |H|^2 over the band
			const double c127 = 1.8e-9, r333 = 10e3, c128 = cal.parts.at("cp.c128"), c129 = cal.parts.at("cp.c129"), r334 = 100e3;
			double sum = 0;
			for (double f = 20; f < 20000; f *= 1.01) {
				const std::complex<double> s(0, 2 * 3.14159265358979 * f);
				const auto y1 = 1 / r332 + s * c127, y2 = std::complex<double>(1 / r333), y3 = s * c128, y4 = s * c129, y5 = std::complex<double>(1 / r334);
				sum += std::norm(y1 * y3 / (y5 * (y1 + y2 + y3 + y4) + y3 * y4)) * f * 0.01;
			}
			return sum;
		};
		const double r332 = 10 * std::log10(bandPower(22e3) / bandPower(27e3)), r346 = 20 * std::log10((1000.0 / 16000) / (680.0 / 15680));
		const double dOta = 20 * std::log10(oe / ol), dReverb = 20 * std::log10(re / rl);
		std::snprintf(what, sizeof what, "service: R332 22k and R346 1k: the clap's bursts %+.2f dB (R332 gives %+.2f), its reverb %+.2f dB (with R346: %+.2f)", dOta,
		              r332, dReverb, r332 + r346);
		check(std::abs(dOta - r332) < 0.5 && std::abs(dReverb - (r332 + r346)) < 0.6, what);
		auto clap = [&](int serial) {
			TR808Processor p(&rom);
			if (serial != tr808::kFittedSerial) {
				const float late = p.params().getParameter("tm_noise")->getValue();
				p.setSerial(serial);
				set(p, "tm_noise", late + 4.7f / 22);
			}
			p.prepareToPlay(rate, 512);
			runFor(p, 0.5);
			juce::MidiBuffer m;
			m.addEvent(juce::MidiMessage::noteOn(10, 39, juce::uint8(50)), int(0.05 * rate));
			const auto out = runFor(p, 0.45, &m);
			return rms(out, int(0.05 * rate), int(0.35 * rate));
		};
		const double dJack = 20 * std::log10(clap(300) / clap(tr808::kFittedSerial));
		std::snprintf(what, sizeof what, "service: SN 000300-010599's clap, its gate 22k too: %+.2f dB at the master out against the late board's", dJack);
		check(dJack > 0.5, what);
	}
	// 4. Every pin of every part the netlists draw is a node the scope can probe; the scope's probe
	// on the clap's netlist node "out" reads what the board hands its jack (jack.cp)
	{
		tr808::VoiceBoard board;
		board.setCalibration(tr808::unitCalibration());
		board.prepare(48000);
		const auto &parts = tr808::VoiceBoard::parts();
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
		std::snprintf(what, sizeof what, "service: %d parts (%d R, %d C, %d D, %d Q, %d IC sections, %d VR, %d TM), %d pins, %d not probeable %s",
		              int(parts.size()), kinds["R"], kinds["C"], kinds["D"], kinds["Q"], kinds["IC"], kinds["VR"], kinds["TM"], pins, missing, first.c_str());
		check(parts.size() > 600 && missing == 0 && kinds["TM"] == 2, what);
	}
	{
		TR808Processor p(&rom);
		p.prepareToPlay(rate, 512);
		runFor(p, 0.5);
		p.setProbe(0, "cp.out");
		p.setProbe(1, "jack.cp");
		juce::MidiBuffer m;
		m.addEvent(juce::MidiMessage::noteOn(10, 39, juce::uint8(127)), int(0.05 * rate));
		runFor(p, 0.3, &m);
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
			trig = trig || (uint32_t(t[size_t(i)]) & tr808::kBitCP);
		}
		std::snprintf(what, sizeof what, "service: the scope's probe on cp.out is the clap's jack (%.2f V peak, %.1e V apart), its gate seen", peak, worst);
		check(p.probeFound(0) && p.probeFound(1) && peak > 0.5 && worst < 1e-4 && trig, what);
	}
	// 5. Each unit's board runs its voices as jobs (the probes and the revisions leave the jobs'
	// grouping alone), and a probe in a voice keeps it so; threaded faster than on one thread
	{
		bool jobs = true;
		for (int serial : {1, 300, tr808::kFittedSerial})
			for (bool late : {false, true}) {
				TR808Processor p(&rom);
				p.setSerial(serial);
				p.setPortionChanged(late);
				p.prepareToPlay(rate, 512);
				jobs = jobs && p.boardRunsJobs();
			}
		check(jobs, "service: every serial number's board, either portion, runs its voices as jobs");
		auto busy = [&](const char *workers) {
			setenv("TR808_WORKERS", workers, 1);
			TR808Processor p(&rom);
			unsetenv("TR808_WORKERS");
			p.setSerial(300);
			p.prepareToPlay(rate, 512);
			p.setProbe(0, "cy.out");
			p.setProbe(1, "tp2");
			juce::MidiBuffer m;
			for (double s = 0.1; s < 3; s += 0.125)
				for (int note : {36, 38, 39, 42, 49, 56, 47})
					m.addEvent(juce::MidiMessage::noteOn(10, note, juce::uint8(100)), int(s * rate));
			const double t0 = juce::Time::getMillisecondCounterHiRes();
			runFor(p, 3.0, &m);
			return juce::Time::getMillisecondCounterHiRes() - t0;
		};
		const double one = busy("0"), threaded = busy("4");
		std::printf("      service: a busy 3 s on SN 000300 with two probes on: %.0f ms on one thread, %.0f ms threaded\n", one, threaded);
		check(threaded < 0.85 * one, "service: probed, the voices' jobs still run on the worker threads");
	}
}

// Every drum note the voices answer to (the congas, claves and maracas as rows of their own).
const int kDrumNotes[] = {36, 37, 38, 39, 42, 43, 46, 47, 49, 50, 56, 62, 63, 64, 70, 75};

// Random automation of every parameter (the trimmers too), MIDI, panel keys, cables and the unit's
// serial number and block sizes from 1 to 1024: nothing but finite, bounded output, and once it all
// stops the board goes back to sleep.
void stress(const juce::MemoryBlock &rom, double rate)
{
	std::printf("\nStress: random settings, MIDI, keys, cables, units and block sizes\n");
	TR808Processor p(&rom);
	auto layout = p.getBusesLayout();
	for (int b = 1; b < layout.outputBuses.size(); ++b)
		layout.outputBuses.getReference(b) = juce::AudioChannelSet::mono();
	check(p.setBusesLayout(layout), "stress: every MULTI OUT on");
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 1024);
	juce::Random rng(808);
	auto &params = p.getParameters();
	const auto serials = tr808::boardSerials();
	const int channels = p.getTotalNumOutputChannels(), total = int((std::getenv("TR808_QUICK") ? 6 : 30) * rate);
	bool ok = true;
	float peak = 0;
	int blocks = 0, nextUnit = 0;
	for (int pos = 0; pos < total && ok;) {
		const int n = rng.nextInt(4) == 0 ? 1 + rng.nextInt(16) : 1 + rng.nextInt(1024);
		for (int k = 0; k < 3; ++k) {
			auto *param = dynamic_cast<juce::AudioProcessorParameterWithID *>(params[rng.nextInt(params.size())]);
			if (param != nullptr && param->paramID != "sync")
				param->setValueNotifyingHost(rng.nextInt(3) == 0 ? (rng.nextBool() ? 1.0f : 0.0f) : rng.nextFloat());
		}
		if (rng.nextInt(40) == 0)
			p.setCable(tr808::Voice(rng.nextInt(tr808::kVoices)), rng.nextBool());
		if (rng.nextInt(8) == 0)
			p.setKey(TR808Processor::Step1 + rng.nextInt(16), rng.nextBool());
		if (rng.nextInt(60) == 0)
			p.setKey(TR808Processor::StartStop, rng.nextBool());
		if (rng.nextInt(60) == 0)
			p.setKey(TR808Processor::Tap, rng.nextBool());
		if (pos >= nextUnit) {
			nextUnit = pos + int(4 * rate);
			p.setSerial(rng.nextBool() ? tr808::kFittedSerial : serials[size_t(rng.nextInt(int(serials.size())))]);
		}
		juce::AudioBuffer<float> buf(channels, n);
		buf.clear();
		juce::MidiBuffer midi;
		for (int k = rng.nextInt(4); k > 0; --k)
			midi.addEvent(juce::MidiMessage::noteOn(10, kDrumNotes[rng.nextInt(int(std::size(kDrumNotes)))], uint8_t(1 + rng.nextInt(127))), rng.nextInt(n));
		p.processBlock(buf, midi);
		for (int ch = 0; ch < channels; ++ch)
			for (int i = 0; i < n; ++i) {
				const float v = buf.getSample(ch, i);
				ok = ok && std::isfinite(v) && std::abs(v) < 8.0f;
				peak = std::max(peak, std::abs(v));
			}
		pos += n;
		++blocks;
	}
	char what[160];
	std::snprintf(what, sizeof what, "stress: finite and bounded (%d blocks, peak %.2f)", blocks, peak);
	check(ok, what);
	// let go of everything, stop the sequencer, a plain setting, and wait
	for (int k = 0; k < 16; ++k)
		p.setKey(TR808Processor::Step1 + k, false);
	p.setKey(TR808Processor::Tap, false);
	p.setKey(TR808Processor::StartStop, false);
	juce::MidiBuffer none;
	run(p, ph, 0.2, rate, &none);
	if (p.snapshot().running) {
		p.setKey(TR808Processor::StartStop, true);
		run(p, ph, 0.2, rate, &none);
		p.setKey(TR808Processor::StartStop, false);
	}
	auto tail = run(p, ph, 20.0, rate, &none);
	bool finite = true;
	for (int i = 0; i < tail.getNumSamples(); ++i)
		finite = finite && std::isfinite(tail.getSample(0, i));
	check(finite && !p.snapshot().running && p.voiceAsleep(), "stress: afterwards the board stops and sleeps");
}

// The voices hit for a long time at the knobs' extremes on each board: once the long decays have
// built up (about 12 s at full), the level holds to the end, and the board goes back to sleep.
void longRun(const juce::MemoryBlock &rom, double rate)
{
	const bool quick = std::getenv("TR808_QUICK") != nullptr;
	const double seconds = quick ? 30 : 60;
	std::printf("\nLong run (%.0f s each, the knobs' extremes, each board)\n", seconds);
	const auto serials = tr808::boardSerials();
	std::vector<int> units(serials.begin(), serials.end());
	units.push_back(tr808::kFittedSerial);
	if (quick)
		units = {tr808::kFittedSerial};
	for (int unit : units)
		for (float value : {1.0f, 0.0f}) {
			TR808Processor p(&rom);
			p.setSerial(unit);
			for (auto *raw : p.getParameters())
				if (auto *param = dynamic_cast<juce::AudioParameterFloat *>(raw)) {
					const auto &id = param->paramID;
					if (!id.startsWith("tm_") && id != "tempo" && id != "fine") // the trimmers stay at the unit's own
						param->setValueNotifyingHost(id.containsIgnoreCase("level") || id == "volume" ? 1.0f : value);
				}
			FakePlayHead ph;
			p.setPlayHead(&ph);
			p.prepareToPlay(rate, 512);
			juce::MidiBuffer midi;
			const int count = int(std::size(kDrumNotes));
			for (int step = 0; step * 0.125 < seconds; ++step) { // 16ths at 120 BPM, each voice in turn, on a kick
				const int at = int(step * 0.125 * rate);
				midi.addEvent(juce::MidiMessage::noteOn(10, kDrumNotes[step % count], uint8_t(step % 4 == 0 ? 127 : 90)), at);
				if (step % 4 == 0)
					midi.addEvent(juce::MidiMessage::noteOn(10, 36, uint8_t(110)), at);
			}
			const auto x = run(p, ph, seconds, rate, &midi);
			bool finite = true;
			for (int i = 0; i < x.getNumSamples(); ++i)
				finite = finite && std::isfinite(x.getSample(0, i)) && std::abs(x.getSample(0, i)) < 8.0f;
			const int window = int(4 * rate);
			const double early = rms(x, int(20 * rate), window), late = rms(x, x.getNumSamples() - window, window);
			juce::MidiBuffer none;
			run(p, ph, 20.0, rate, &none);
			char what[200];
			std::snprintf(what, sizeof what, "SN %06d, knobs %s: level holds (rms %.3f at 20 s, %.3f at the end), then sleeps", unit, value > 0.5f ? "full" : "zero", early, late);
			check(finite && early > 1e-3 && late < 1.05 * early && late > 0.95 * early && p.voiceAsleep(), what);
		}
}

// --factory DIR: ROCK, BOSSA NOVA, MAMBO and SAMBA from a new instance's own sequencer, every knob at
// its default, BASIC-VARIATION at AB: four bars (A, B, A, B) at the default tempo, then the tail.
int factoryClips(const char *path)
{
	const double rate = 48000;
	const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile(path);
	dir.createDirectory();
	juce::MemoryBlock rom;
	juce::File(TR808_ROM_PATH).loadFileAsData(rom);
	const std::pair<const char *, int> rhythms[] = {{"rock", 2}, {"bossa-nova", 3}, {"mambo", 4}, {"samba", 5}};
	for (const auto &[name, key] : rhythms) {
		TR808Processor p(&rom);
		FakePlayHead ph;
		p.setPlayHead(&ph);
		p.prepareToPlay(rate, 512);
		choose(p, TR808Processor::kModeId, 3); // MANUAL PLAY
		choose(p, "variation", 1);             // AB
		run(p, ph, 0.2, rate);
		p.setKey(TR808Processor::Step1 + key, true);
		run(p, ph, 0.15, rate);
		p.setKey(TR808Processor::Step1 + key, false);
		run(p, ph, 0.15, rate);
		p.setKey(TR808Processor::StartStop, true);
		const auto start = run(p, ph, 0.1, rate);
		p.setKey(TR808Processor::StartStop, false);
		const auto bars = run(p, ph, 4 * 2.0 - 0.1, rate);
		p.setKey(TR808Processor::StartStop, true); // stopped at the end of the fourth bar: the tail rings on
		const auto stop = run(p, ph, 0.1, rate);
		p.setKey(TR808Processor::StartStop, false);
		const auto tail = run(p, ph, 1.4, rate);
		juce::AudioBuffer<float> all(1, start.getNumSamples() + bars.getNumSamples() + stop.getNumSamples() + tail.getNumSamples());
		int at = 0;
		for (const auto *b : {&start, &bars, &stop, &tail}) {
			all.copyFrom(0, at, *b, 0, 0, b->getNumSamples());
			at += b->getNumSamples();
		}
		juce::WavAudioFormat wav;
		const auto file = dir.getChildFile(juce::String(name) + ".wav");
		file.deleteFile();
		std::unique_ptr<juce::AudioFormatWriter> writer(wav.createWriterFor(new juce::FileOutputStream(file), rate, 1, 24, {}, 0));
		writer->writeFromAudioSampleBuffer(all, 0, all.getNumSamples());
		std::printf("%s: peak %.3f\n", file.getFullPathName().toRawUTF8(), all.getMagnitude(0, 0, all.getNumSamples()));
	}
	return 0;
}

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
	if (argc > 2 && std::string(argv[1]) == "--factory")
		return factoryClips(argv[2]);
	if (argc > 2 && std::string(argv[1]) == "--panel") {
		const char *art = nullptr;
		bool running = false, off = false, svc = false, rear = false;
		float scale = 1, zoom = 1;
		juce::Point<float> at;
		int setup = -1;
		for (int a = 3; a < argc; ++a) {
			const juce::String s(argv[a]);
			if (s == "--artwork" && a + 1 < argc)
				art = argv[++a];
			else if (s == "running")
				running = true;
			else if (s == "rear")
				rear = true;
			else if (s == "off")
				off = true;
			else if (s == "service")
				svc = true;
			else if (s.startsWith("zoom=")) { // zoom=Z@X,Y
				svc = true;
				zoom = s.fromFirstOccurrenceOf("=", false, false).upToFirstOccurrenceOf("@", false, false).getFloatValue();
				const auto xy = s.fromFirstOccurrenceOf("@", false, false);
				at = {xy.upToFirstOccurrenceOf(",", false, false).getFloatValue(), xy.fromFirstOccurrenceOf(",", false, false).getFloatValue()};
			} else if (s.startsWith("setup=")) {
				svc = true;
				setup = s.fromFirstOccurrenceOf("=", false, false).getIntValue();
			} else if (s.containsOnly("0123456789."))
				scale = s.getFloatValue();
		}
		return panelShot(argv[2], scale > 0 ? scale : 1.0f, running, off, art, svc, zoom, at, setup, rear);
	}
	if (argc > 1 && std::string(argv[1]) == "--service") { // the SERVICE checks alone, and the view's shots
		const juce::File shots = argc > 2 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]) : juce::File();
		if (shots != juce::File())
			shots.createDirectory();
		service(shots);
		std::printf("%s\n", failures ? "FAILED" : "all passed");
		return failures ? 1 : 0;
	}
	const juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
	if (dir != juce::File())
		dir.createDirectory();
	const double rate = 48000;

	juce::MemoryBlock rom;
	juce::File(TR808_ROM_PATH).loadFileAsData(rom);
	TR808Processor p(&rom);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	check(p.romLoaded(), "ROM loaded");

	// 1. Stopped: silence, and the idle board asleep (exact zeros).
	auto quiet = run(p, ph, 1.0, rate);
	check(quiet.getMagnitude(0, 0, quiet.getNumSamples()) < 1e-4, "silent while stopped");
	check(p.voiceAsleep(), "the idle board sleeps");

	// 2. A new instance holds the factory rhythms. BASIC RHYTHM 1 (selected at power on) is a
	// practice program, empty: the sequencer runs with the host and plays nothing.
	{
		tr808::Machine m;
		m.writeFactoryRhythms();
		check(std::equal(m.ram().begin(), m.ram().end(), p.ram().begin()), "a new instance has the factory rhythms");
	}
	set(p, "sync", 1.0f);
	ph.playing = true;
	ph.ppq = 0;
	auto blank = run(p, ph, 1.0, rate);
	check(blank.getMagnitude(0, 0, blank.getNumSamples()) < 1e-3, "BASIC RHYTHM 1 (empty) plays nothing");
	ph.playing = false;
	run(p, ph, 0.5, rate);

	// BASIC RHYTHM 3, ROCK A, plays: 16ths at 120 BPM, BD, SD and CH on 14 of them. (Onsets are
	// counted by level steps, which miss the quieter hats under the BD; tr808seq's "factory"
	// shows each step's voices.)
	{
		choose(p, TR808Processor::kModeId, 3);
		p.setKey(TR808Processor::Step1 + 2, true);
		run(p, ph, 0.15, rate);
		p.setKey(TR808Processor::Step1 + 2, false);
		run(p, ph, 0.15, rate);
		ph.playing = true;
		ph.ppq = 0;
		auto rock = run(p, ph, 2.0, rate);
		save(dir, "rock.wav", rock, rate);
		ph.playing = false;
		run(p, ph, 1.0, rate);
		const auto ron = onsets(rock, rate);
		std::printf("      rock: %zu onsets in the first bar, the first %.1f ms after the host's start\n", ron.size(),
		            ron.empty() ? -1.0 : ron.front() * 1000);
		check(ron.size() >= 6 && ron.size() <= 14 && ron.front() < 0.012, "factory ROCK plays");
		p.setKey(TR808Processor::Step1, true); // back to BASIC RHYTHM 1
		run(p, ph, 0.15, rate);
		p.setKey(TR808Processor::Step1, false);
		run(p, ph, 0.15, rate);
	}

	// 3. BASIC RHYTHM 1 = the rim shot (short and sharp) on the 8ths: with SYNC on, the host's
	// clock runs it, in time with the host's grid.
	{
		tr808::Machine m;
		std::vector<uint16_t> steps(16, 0);
		for (int s = 0; s < 16; s += 2)
			steps[size_t(s)] = tr808::kBitRS;
		m.writePattern(0, 0, steps, 2);
		p.setRam(std::vector<uint8_t>(m.ram().begin(), m.ram().end()));
	}
	choose(p, TR808Processor::kModeId, 3); // MANUAL PLAY, BASIC RHYTHM 1 (selected at power on)
	run(p, ph, 0.5, rate);                 // (the CPU restarted with the new RAM)
	ph.playing = true;
	ph.ppq = 0;
	auto sync = run(p, ph, 4.0, rate);
	save(dir, "sync.wav", sync, rate);
	const auto on = onsets(sync, rate);
	int nearBeat = 0;
	for (double t : on) {
		const double eighths = t / (60.0 / ph.bpm / 2);
		nearBeat += std::abs(eighths - std::round(eighths)) * (60.0 / ph.bpm / 2) < 0.012 ? 1 : 0;
	}
	std::printf("      sync: %zu onsets, %d within 12 ms of an 8th of the host's grid\n", on.size(), nearBeat);
	if (!on.empty())
		std::printf("      first onset %.1f ms after the host's start\n", on.front() * 1000);
	check(on.size() >= 15 && nearBeat >= int(on.size()) - 1, "the sequencer plays in time with the host's transport");
	ph.playing = false;
	run(p, ph, 1.0, rate);

	// 4. Internal clock: START/STOP with SYNC off, at the TEMPO knob's 120 BPM.
	set(p, "sync", 0.0f);
	p.setKey(TR808Processor::StartStop, true);
	p.setKey(TR808Processor::StartStop, false);
	auto internal = run(p, ph, 3.0, rate);
	save(dir, "internal.wav", internal, rate);
	const auto ion = onsets(internal, rate);
	double period = ion.size() > 4 ? (ion.back() - ion.front()) / double(ion.size() - 1) : 0;
	std::printf("      internal: %zu onsets, %.1f ms apart on average (%.1f BPM on the knob)\n", ion.size(), period * 1000,
	            p.snapshot().bpm);
	check(ion.size() >= 11 && std::abs(period - 0.25) < 0.002, "START/STOP runs the internal clock (8ths at 120 BPM)");
	p.setKey(TR808Processor::StartStop, true);
	p.setKey(TR808Processor::StartStop, false);
	run(p, ph, 3.0, rate);

	// 5. MIDI: a note plays its voice with the sequencer stopped; velocity 127 accents.
	set(p, "ac_level", 1.0f);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 38, uint8_t(64)), int(0.1 * rate));
	midi.addEvent(juce::MidiMessage::noteOn(1, 38, uint8_t(127)), int(0.6 * rate));
	auto m = run(p, ph, 1.2, rate, &midi);
	save(dir, "midi.wav", m, rate);
	const double plain = m.getMagnitude(0, int(0.1 * rate), int(0.1 * rate)), acc = m.getMagnitude(0, int(0.6 * rate), int(0.1 * rate));
	std::printf("      midi: SD plain peak %.4f, accented %.4f (%+.1f dB)\n", plain, acc, 20 * std::log10(acc / std::max(plain, 1e-9)));
	check(plain > 1e-2, "a MIDI note plays its voice");
	check(acc > plain * 1.5, "velocity 127 accents (louder with ACCENT at max)");

	// 6. Idle again: asleep, exact zeros; a note wakes it and sounds as before.
	{
		run(p, ph, 4.0, rate);
		auto idle = run(p, ph, 1.0, rate);
		check(p.voiceAsleep() && idle.getMagnitude(0, 0, idle.getNumSamples()) == 0.0f, "idle: the board sleeps, exact zeros");
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		run(p, ph, 5.0, rate);
		const double asleepMs = juce::Time::getMillisecondCounterHiRes() - t0;
		// the bass drum (no noise in it): a note after the sleep peaks as one before it
		juce::MidiBuffer before;
		before.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.1 * rate));
		auto b = run(p, ph, 2.5, rate, &before);
		const double bdBefore = b.getMagnitude(0, int(0.1 * rate), int(0.1 * rate));
		run(p, ph, 3.0, rate);
		juce::MidiBuffer again;
		again.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.1 * rate));
		auto w = run(p, ph, 0.5, rate, &again);
		const double woken = w.getMagnitude(0, int(0.1 * rate), int(0.1 * rate));
		std::printf("      idle: 5 s asleep took %.0f ms; a BD after it peaks %.4f (before %.4f)\n", asleepMs, woken, bdBefore);
		const double acc = bdBefore;
		check(asleepMs < 100, "asleep costs next to nothing");
		check(std::abs(woken / acc - 1) < 0.05, "a note wakes it and sounds as before (peak within 5 %)");
	}

	// 7. State round trip: parameters, RAM and the model version.
	set(p, "bd_decay", 0.123f);
	juce::MemoryBlock state;
	p.getStateInformation(state);
	TR808Processor q(&rom);
	q.setStateInformation(state.getData(), int(state.getSize()));
	check(std::abs(q.params().getParameter("bd_decay")->getValue() - 0.123f) < 1e-3, "parameters restore");
	check(q.ram() == p.ram(), "the RAM restores");
	const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	check(xml != nullptr && xml->getIntAttribute("model") == TR808Processor::kModelVersion, "the session records the model version");

	// 9. POWER: off is silent and dark (a MIDI note does nothing, the LEDs are out); on again, the
	// note plays and the patterns are still there.
	{
		const auto ramBefore = p.ram();
		p.setPower(false);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.1 * rate));
		auto off = run(p, ph, 0.5, rate, &note);
		const auto snap = p.snapshot();
		const bool dark = !snap.power && std::all_of(snap.leds.begin(), snap.leds.end(), [](float l) { return l == 0.0f; });
		p.setPower(true);
		auto muted = run(p, ph, 0.5, rate, &note); // within the power-on detector's 1.5 s
		juce::MidiBuffer later;
		later.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(1.3 * rate));
		auto on = run(p, ph, 2.0, rate, &later);
		const int fade = int(0.01 * rate); // after the switch's 5 ms fade
		const float offPeak = off.getMagnitude(0, fade, off.getNumSamples() - fade);
		const float mutedPeak = muted.getMagnitude(0, 0, muted.getNumSamples());
		const float onPeak = on.getMagnitude(0, int(1.2 * rate), int(0.5 * rate));
		std::printf("      power: off peak %.6f (after the fade); on: %.5f while Q13 mutes, a BD after it %.4f\n", offPeak, mutedPeak, onPeak);
		check(offPeak == 0.0f && dark, "POWER off: silent, LEDs dark, MIDI ignored");
		check(mutedPeak < 0.01f * onPeak, "POWER on: the master muted while the board charges (Q13, 1.5 s)");
		check(onPeak > 1e-2 && p.ram() == ramBefore, "POWER on: plays again after it, patterns kept");
	}

	// 10. Voice mode: no ROM, MIDI still plays.
	{
		TR808Processor v(nullptr);
		FakePlayHead vh;
		v.setPlayHead(&vh);
		v.prepareToPlay(rate, 512);
		juce::MidiBuffer mb;
		mb.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(100)), int(0.05 * rate));
		auto b = run(v, vh, 0.5, rate, &mb);
		check(!v.romLoaded() && b.getMagnitude(0, 0, b.getNumSamples()) > 1e-2, "voice mode: no ROM, MIDI plays the voices");
	}

	// 11. MIDI control changes move the knobs (the TR-8's numbers).
	{
		TR808Processor v(nullptr);
		FakePlayHead vh;
		v.setPlayHead(&vh);
		v.prepareToPlay(rate, 512);
		juce::MidiBuffer mb;
		mb.addEvent(juce::MidiMessage::controllerEvent(1, 26, 127), 0); // SD SNAPPY
		mb.addEvent(juce::MidiMessage::controllerEvent(1, 46, 0), 1);   // LT TUNING
		run(v, vh, 0.02, rate, &mb);
		check(v.params().getParameter("sd_snappy")->getValue() == 1.0f && v.params().getParameter("lt_tuning")->getValue() == 0.0f,
		      "MIDI CC 26 and 46 set SD SNAPPY and LT TUNING");
	}

	// 12. MULTI OUT (the rear panel): a cable in a jack takes its voice out of the mix (the jack's
	// normal contact) to that output, if the host has it on; without one the voice stays in the mix
	// and the output is silent, whatever the host turns on. Every jack, and the session.
	{
		using tr808::Voice;
		const std::pair<Voice, int> notes[tr808::kVoices] = {{Voice::BD, 36}, {Voice::SD, 38}, {Voice::LT, 43}, {Voice::MT, 47}, {Voice::HT, 50}, {Voice::RS, 37},
		                                                     {Voice::CP, 39}, {Voice::CB, 56}, {Voice::CY, 49}, {Voice::OH, 46}, {Voice::CH, 42}};
		struct Heard { double mix, other, jack, sides; };
		// `v` hit at 0.05 s and the next voice at 0.55 s; 0.3 s of each; `v`'s output on or off
		auto hits = [&](int k, bool cable, bool output, bool stereo) {
			TR808Processor p2(nullptr);
			const Voice v = notes[k].first;
			if (output) {
				auto layout = p2.getBusesLayout();
				layout.outputBuses.getReference(1 + int(v)) = stereo ? juce::AudioChannelSet::stereo() : juce::AudioChannelSet::mono();
				check(p2.setBusesLayout(layout), "MULTI OUT: the host can turn the output on");
			}
			p2.setCable(v, cable);
			FakePlayHead vh;
			p2.setPlayHead(&vh);
			p2.prepareToPlay(rate, 512);
			const int channels = p2.getTotalNumOutputChannels(), total = int(1.0 * rate) / 512 * 512;
			juce::AudioBuffer<float> out(channels, total);
			out.clear();
			for (int pos = 0; pos < total; pos += 512) {
				juce::AudioBuffer<float> buf(channels, 512);
				buf.clear();
				juce::MidiBuffer mb;
				for (const auto &[t, note] : {std::pair<double, int>{0.05, notes[k].second}, {0.55, notes[(k + 1) % tr808::kVoices].second}}) {
					const int at = int(t * rate);
					if (at >= pos && at < pos + 512)
						mb.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(100)), at - pos);
				}
				p2.processBlock(buf, mb);
				for (int ch = 0; ch < channels; ++ch)
					out.copyFrom(ch, pos, buf, ch, 0, 512);
			}
			const int a0 = int(0.05 * rate), a1 = int(0.55 * rate), len = int(0.3 * rate);
			double diff = 0;
			if (output && stereo)
				for (int i = 0; i < total; ++i)
					diff = std::max(diff, double(std::abs(out.getSample(2, i) - out.getSample(3, i))));
			return Heard{out.getRMSLevel(0, a0, len), out.getRMSLevel(0, a1, len), output ? out.getRMSLevel(2, a0, len) : 0.0, diff};
		};
		auto db = [](double a, double b) { return 20 * std::log10((a + 1e-12) / (b + 1e-12)); };
		char what[220];
		bool every = true;
		double worst = -999;
		for (int k = 0; k < tr808::kVoices; ++k) {
			const auto none = hits(k, false, true, false), in = hits(k, true, true, false);
			worst = std::max(worst, db(in.mix, none.mix));
			// the next voice stays: within 0.25 dB (the mix resistors load C17, so one taken out moves the others' low end, as on the unit), the
			// metal voices' free-running squares meet it at another phase (1.1 dB on the open hat)
			every = every && none.jack == 0 && none.mix > 1e-3 && in.jack > 1e-4 && db(in.mix, none.mix) < -30 && std::abs(db(in.other, none.other)) < 1.5;
		}
		std::snprintf(what, sizeof what, "MULTI OUT: on each of the eleven jacks, a cable moves the voice from the mix (at most %+.1f dB) to its output; without one the output is silent", worst);
		check(every, what);
		const auto none = hits(0, false, true, true), in = hits(0, true, true, true), nowhere = hits(0, true, false, false);
		check(in.sides == 0 && in.jack > 1e-3, "MULTI OUT: a jack on a stereo output is the same on both sides");
		check(db(nowhere.mix, none.mix) < -30 && std::abs(db(nowhere.other, none.other)) < 0.2, "MULTI OUT: a cable into an output the host has off goes nowhere");
		TR808Processor a2(nullptr), b2(nullptr);
		a2.setCable(Voice::SD, true);
		juce::MemoryBlock state;
		a2.getStateInformation(state);
		b2.setStateInformation(state.getData(), int(state.getSize()));
		check(b2.cable(Voice::SD) && !b2.cable(Voice::BD), "MULTI OUT: the cables are saved with the session");
		// The view, by the mouse: REAR on the bar, then a click on the BD jack plugs a cable, another pulls it out.
		TR808Processor p3(nullptr);
		tr808::HardwareView view(p3);
		view.setBounds(0, 0, tr808::HardwareView::kWidth, tr808::HardwareView::kHeight);
		auto click = [&](juce::Point<float> at) {
			const auto now = juce::Time::getCurrentTime();
			const juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(), at, {}, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &view, &view, now, at, now, 1, false);
			view.mouseDown(e);
			view.mouseUp(e);
		};
		for (float x = 0; x < float(tr808::HardwareView::kWidth) && !view.rearShown(); x += 10)
			click({x, float(tr808::HardwareView::kPanelHeight) + float(tr808::HardwareView::kBarHeight) / 2});
		check(view.rearShown(), "MULTI OUT: REAR on the bar shows the rear panel");
		const juce::Point<float> bd((tr808::HardwareView::kPanelW - 222.3f) * tr808::HardwareView::kPxPerMm, (56.0f + 36.2f) * tr808::HardwareView::kPxPerMm);
		click(bd);
		const bool plugged = p3.cable(Voice::BD);
		click(bd);
		check(plugged && !p3.cable(Voice::BD), "MULTI OUT: a click on a jack plugs a cable in, another pulls it out");
	}

	// 13. Random settings, and long runs at the extremes
	stress(rom, rate);
	longRun(rom, rate);

	// 14. SERVICE
	service(juce::File());

	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
