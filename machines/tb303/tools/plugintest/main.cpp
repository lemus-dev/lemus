// Headless plugin test: runs TB303Processor without a host. Checks that the firmware
// sequencer follows a fake host transport (DIN sync), that MIDI plays the voice, that the
// internal RUN/STOP clock works, and that state (parameters + pattern memory) round-trips.
//   plugintest [OUT_DIR]   (writes sync.wav, midi.wav, internal.wav when given)
#include "PluginProcessor.h"
#include "tb303/core/patterns.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <cstdio>

namespace {
struct FakePlayHead : juce::AudioPlayHead {
	bool playing = false;
	double bpm = 125, ppq = 0;
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
	for (int i = from; i < from + n && i < b.getNumSamples(); ++i)
		s += double(b.getSample(0, i)) * b.getSample(0, i);
	return std::sqrt(s / std::max(1, n));
}

juce::AudioBuffer<float> run(TB303Processor &p, FakePlayHead &ph, double seconds, double rate, juce::MidiBuffer *midiAll = nullptr)
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
		for (int i = 0; i < n; ++i)
			if (!std::isfinite(buf.getSample(0, i))) {
				static bool reported = false;
				if (!reported) {
					reported = true;
					const auto snap = p.snapshot();
					std::printf("      first non-finite sample at block %d (+%d): vcf %.3g Hz meg %.3g veg %.3g gate %.3g sweep %.3g\n", pos, i,
					            snap.probe.vcfHz, snap.probe.meg, snap.probe.veg, snap.probe.gate, snap.probe.accentSweep);
				}
				break;
			}
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
	std::unique_ptr<juce::AudioFormatWriter> w(wav.createWriterFor(s.get(), rate, 1, 32, {}, 0));
	s.release();
	w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

// The battery-backed pattern RAM in the processor's saved state.
juce::MemoryBlock patternRam(TB303Processor &p)
{
	juce::MemoryBlock state;
	p.getStateInformation(state);
	auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	juce::MemoryOutputStream ram;
	if (xml != nullptr)
		juce::Base64::convertFromBase64(ram, xml->getStringAttribute("machine"));
	return juce::MemoryBlock(ram.getData(), std::min<size_t>(ram.getDataSize(), 4 + tb303::Machine::kRamNibbles));
}

// The pattern memory proper (groups I-IV), and what it holds with every pattern cleared:
// pitch nibbles F, flag nibbles 0.
juce::MemoryBlock patterns(const juce::MemoryBlock &ram)
{
	return ram.getSize() < 4 + 0xc00 ? juce::MemoryBlock() : juce::MemoryBlock(static_cast<const char *>(ram.getData()) + 4 + 0x400, 0x800);
}
juce::MemoryBlock cleared()
{
	juce::MemoryBlock m(4 + 0xc00, true);
	for (int a = 0; a < 0x800; ++a)
		static_cast<uint8_t *>(m.getData())[4 + 0x400 + a] = (a & 0x80) != 0 ? 0 : 0xf;
	return m;
}

int failures = 0;
void check(bool ok, const char *what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++failures;
}
} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI init;
	const juce::File dir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
	if (dir != juce::File())
		dir.createDirectory();
	const double rate = 44100;

	juce::MemoryBlock rom;
	juce::File(TB303_ROM_PATH).loadFileAsData(rom);
	TB303Processor p(&rom);
	FakePlayHead ph;
	p.setPlayHead(&ph);
	p.prepareToPlay(rate, 512);
	check(p.romLoaded(), "ROM loaded");

	// 1. Host stopped, nothing playing: silence.
	auto quiet = run(p, ph, 1.0, rate);
	check(rms(quiet, 0, quiet.getNumSamples()) < 1e-4, "silent while stopped");

	// 2. A new 303 has no patterns: every one is as PATTERN CLEAR leaves it (16 steps, all
	// rest), so the sequencer runs silently with the host.
	const auto fresh = patternRam(p);
	check(fresh.getSize() == 4 + tb303::Machine::kRamNibbles && patterns(fresh) == patterns(cleared()),
	      "a new instance has every pattern cleared");
	p.params().getParameter("sync")->setValueNotifyingHost(1.0f);
	ph.playing = true;
	ph.ppq = 0;
	auto blank = run(p, ph, 2.0, rate);
	check(rms(blank, 0, blank.getNumSamples()) < 1e-4, "a cleared pattern plays nothing");
	ph.playing = false;
	run(p, ph, 0.5, rate);

	// 2b. With a pattern in A1, host playing with SYNC on: the firmware runs it from the
	// DIN-sync clock.
	p.writePattern(0, 0, 0, tb303::parsePattern(tb303::kTestPattern));
	ph.playing = true;
	ph.ppq = 0;
	auto sync = run(p, ph, 4.0, rate);
	save(dir, "sync.wav", sync, rate);
	int loud = 0;
	for (int i = 0; i < 16; ++i)
		loud += rms(sync, int(i * 0.25 * rate), int(0.25 * rate)) > 1e-3 ? 1 : 0;
	std::printf("      sync: %d of 16 quarter-second windows have sound\n", loud);
	check(loud >= 12, "sequencer plays with host transport");
	ph.playing = false;
	run(p, ph, 0.5, rate);

	// 3. MIDI note with the host stopped plays the voice; velocity 127 accents.
	p.params().getParameter("accent")->setValueNotifyingHost(1.0f);
	juce::MidiBuffer midi;
	midi.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(64)), int(0.1 * rate));
	midi.addEvent(juce::MidiMessage::noteOff(1, 36), int(0.4 * rate));
	midi.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.6 * rate));
	midi.addEvent(juce::MidiMessage::noteOff(1, 36), int(0.9 * rate));
	auto m = run(p, ph, 1.5, rate, &midi);
	save(dir, "midi.wav", m, rate);
	auto peak = [&](double t0, double len) { return double(m.getMagnitude(0, int(t0 * rate), int(len * rate))); };
	const double plain = peak(0.1, 0.1), acc = peak(0.6, 0.1);
	std::printf("      midi: plain peak %.4f, accented peak %.4f (%+.1f dB), after %.6f\n", plain, acc, 20 * std::log10(acc / plain),
	            rms(m, int(1.1 * rate), int(0.3 * rate)));
	check(plain > 1e-3, "MIDI note sounds");
	check(acc > plain * 2.0, "velocity 127 accents (louder by > 6 dB at ACCENT max)");

	// 3b. Idle: the settled voice sleeps (exact zeros, no circuit model) and a note wakes it.
	{
		run(p, ph, 6.0, rate);
		auto idle = run(p, ph, 1.0, rate);
		check(p.voiceAsleep() && idle.getMagnitude(0, 0, idle.getNumSamples()) == 0.0f, "idle: the voice sleeps and puts out exact zeros");
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		run(p, ph, 5.0, rate);
		const double asleepMs = juce::Time::getMillisecondCounterHiRes() - t0;
		juce::MidiBuffer again;
		again.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.1 * rate));
		again.addEvent(juce::MidiMessage::noteOff(1, 36), int(0.4 * rate));
		auto w = run(p, ph, 1.5, rate, &again);
		const double woken = double(w.getMagnitude(0, int(0.1 * rate), int(0.1 * rate)));
		std::printf("      idle: 5 s asleep took %.0f ms to process; the note after it peaks %.4f (before: %.4f)\n", asleepMs, woken, acc);
		check(!p.voiceAsleep() || w.getMagnitude(0, int(0.1 * rate), int(0.3 * rate)) > 0, "a note wakes it");
		check(std::abs(woken / acc - 1) < 0.05, "and sounds as before (peak within 5 %)");
	}

	// 4. Internal clock: RUN/STOP with sync off.
	p.params().getParameter("sync")->setValueNotifyingHost(0.0f);
	p.toggleRun();
	auto internal = run(p, ph, 2.0, rate);
	save(dir, "internal.wav", internal, rate);
	check(rms(internal, 0, internal.getNumSamples()) > 1e-3, "RUN/STOP runs the internal clock");
	p.toggleRun();
	run(p, ph, 0.3, rate);

	// 4b. POWER SW, on VOLUME: at OFF the unit is silent and dark and takes no notes or keys;
	// switched on, the CPU starts afresh, stopped, with its pattern memory.
	{
		auto *vol = p.params().getParameter("volume");
		const float was = vol->getValue();
		p.toggleRun();
		run(p, ph, 0.5, rate);
		vol->setValueNotifyingHost(0.0f);
		juce::MidiBuffer note;
		note.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(127)), int(0.1 * rate));
		auto off = run(p, ph, 1.0, rate, &note);
		const auto s = p.snapshot();
		bool dark = !s.powered && !s.running;
		for (float l : s.leds)
			dark = dark && l == 0.0f;
		check(off.getMagnitude(0, int(0.05 * rate), off.getNumSamples() - int(0.05 * rate)) == 0.0f && dark && vol->getText(0.0f, 16) == "Off",
		      "VOLUME at OFF switches the unit off: silent, every LED dark, MIDI ignored");
		p.toggleRun(); // RUN/STOP while off does nothing
		run(p, ph, 0.1, rate);
		vol->setValueNotifyingHost(was);
		auto on = run(p, ph, 1.0, rate);
		check(p.snapshot().powered && !p.snapshot().running && rms(on, 0, on.getNumSamples()) < 1e-4, "switched on, it starts stopped");
		p.toggleRun();
		auto again = run(p, ph, 2.0, rate);
		check(rms(again, 0, again.getNumSamples()) > 1e-3, "and RUN plays the pattern memory");
		p.toggleRun();
		run(p, ph, 0.3, rate);
	}

	// 5. Stress: random knob settings with accented and sliding MIDI notes stay finite.
	{
		juce::Random rng(303);
		bool finite = true;
		const char *ids[] = {"tuning", "cutoff", "resonance", "envmod", "decay", "accent", "volume", "waveform"};
		for (int round = 0; round < 24 && finite; ++round) {
			for (const auto *id : ids)
				p.params().getParameter(id)->setValueNotifyingHost(rng.nextBool() ? (rng.nextBool() ? 1.0f : 0.0f) : rng.nextFloat());
			juce::MidiBuffer mb;
			int t = 0;
			for (int k = 0; k < 6; ++k) {
				const int note = 24 + rng.nextInt(48);
				mb.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(rng.nextBool() ? 127 : 70)), t);
				t += int(rate * (0.03 + 0.08 * rng.nextFloat()));
				if (rng.nextBool())
					mb.addEvent(juce::MidiMessage::noteOff(1, note), t);
			}
			mb.addEvent(juce::MidiMessage::allNotesOff(1), t + 100);
			auto b = run(p, ph, 0.6, rate, &mb);
			for (int i = 0; i < b.getNumSamples(); ++i)
				finite = finite && std::isfinite(b.getSample(0, i)) && std::abs(b.getSample(0, i)) < 4.0f;
		}
		check(finite, "random settings and notes stay finite and bounded");
	}

	// 6. Calibration: TM3 at the ends of the service notes' tolerance (3.0 and 2.0 ms) moves the ring
	// at cutoff centre 0.5 ms either way from the unit's 2.5 ms.
	{
		namespace tr = tb303::trims;
		const float tm3Unit = p.params().getParameter("tm3")->getDefaultValue();
		const char *knobs[] = {"tuning", "cutoff", "resonance", "envmod", "decay", "accent", "volume", "waveform", "tm3"};
		const float vals[] = {0.5f, 0.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0.6f, 0.0f, tm3Unit};
		for (size_t i = 0; i < std::size(knobs); ++i)
			p.params().getParameter(knobs[i])->setValueNotifyingHost(vals[i]);
		const float trims[] = {float(tr::tm3For(1000 / 3.0)), tm3Unit, float(tr::tm3For(500))};
		double hz[3];
		for (int t = 0; t < 3; ++t) {
			p.params().getParameter("tm3")->setValueNotifyingHost(trims[t]);
			juce::MidiBuffer mb;
			mb.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(64)), 0);
			auto b = run(p, ph, 0.3, rate, &mb);
			hz[t] = p.snapshot().probe.vcfHz;
			mb.clear();
			mb.addEvent(juce::MidiMessage::allNotesOff(1), 0);
			run(p, ph, 0.3, rate, &mb);
		}
		// Anticlockwise lengthens the ring (lower cutoff), clockwise shortens it.
		const double ringMs = 1000.0 / hz[1];
		std::printf("      cutoff at centre %.0f Hz; TM3 at 3.0 / 2.0 ms %.0f / %.0f Hz\n", hz[1], hz[0], hz[2]);
		check(std::abs(hz[0] * (ringMs + 0.5) / 1000 - 1) < 0.03 && std::abs(hz[2] * (ringMs - 0.5) / 1000 - 1) < 0.03,
		      "TM3 moves the ring by 0.5 ms each way across the notes' tolerance");

		// FULL CUT OFF: full CUT OFF reaches the ring it is set to, 1.2 to 3 kHz, while cutoff
		// centre stays where TM3 puts it.
		p.params().getParameter("tm3")->setValueNotifyingHost(tm3Unit);
		auto *top = dynamic_cast<juce::RangedAudioParameter *>(p.params().getParameter("fullcutoff"));
		const float topDefault = top->getDefaultValue();
		const float tops[] = {1200, 1400, 1800, 2200, 2600, 3000};
		const int nTop = int(std::size(tops));
		std::vector<double> full, centre;
		for (int i = 0; i < nTop; ++i) {
			top->setValueNotifyingHost(top->convertTo0to1(tops[i]));
			for (float c : {1.0f, 0.5f}) {
				p.params().getParameter("cutoff")->setValueNotifyingHost(c);
				juce::MidiBuffer mb;
				mb.addEvent(juce::MidiMessage::noteOn(1, 36, uint8_t(64)), 0);
				run(p, ph, 0.3, rate, &mb);
				(c > 0.75f ? full : centre).push_back(p.snapshot().probe.vcfHz);
				mb.clear();
				mb.addEvent(juce::MidiMessage::allNotesOff(1), 0);
				run(p, ph, 0.3, rate, &mb);
			}
		}
		top->setValueNotifyingHost(topDefault);
		p.params().getParameter("cutoff")->setValueNotifyingHost(0.5f);
		bool reaches = true, steady = true;
		for (int i = 0; i < nTop; ++i)
			reaches = reaches && std::abs(full[size_t(i)] / tops[i] - 1) < 0.03,
			steady = steady && std::abs(centre[size_t(i)] / centre[0] - 1) < 0.03;
		std::printf("      full CUT OFF set to");
		for (float f : tops)
			std::printf(" %.0f", double(f));
		std::printf(" Hz reaches");
		for (double f : full)
			std::printf(" %.0f", f);
		std::printf(" Hz; cutoff centre %.0f ... %.0f Hz; default %s\n", centre.front(), centre.back(),
		            top->getText(topDefault, 16).toRawUTF8());
		check(reaches && steady, "FULL CUT OFF reaches what it is set to (within 3 %) and keeps cutoff centre");

		// The unit stays stable over a long run at full cutoff and resonance, at both ends of
		// TM3's tolerance and the unit's: no slow
		// (~6 Hz) oscillation building up in the resonance loop's high-passes. Eight loops of
		// an acid line; the energy under 12 Hz after the first loop stays 30 dB under the total.
		const float acid[] = {0.5f, 1.0f, 1.0f, 0.6f, 0.4f, 0.8f, 0.6f, 0.0f, 0.5f};
		for (size_t i = 0; i < std::size(knobs); ++i)
			p.params().getParameter(knobs[i])->setValueNotifyingHost(acid[i]);
		const struct { int line, note, len; bool acc; } pat[] = {
			{0, 31, 1, false}, {4, 43, 2, false}, {8, 39, 2, true}, {12, 24, 2, true},
			{16, 51, 2, false}, {20, 53, 2, false}, {24, 34, 1, false}, {28, 24, 2, false}};
		const double lineSec = 60.0 / 125 / 8;
		const int loopLen = int(32 * lineSec * rate), loops = 8;
		for (int t = 0; t < 3; ++t) {
			p.params().getParameter("tm3")->setValueNotifyingHost(trims[t]);
			juce::MidiBuffer mb;
			for (int l = 0; l < loops; ++l)
				for (const auto &n : pat) {
					const int on = l * loopLen + int(n.line * lineSec * rate);
					mb.addEvent(juce::MidiMessage::noteOn(1, n.note, uint8_t(n.acc ? 127 : 64)), on);
					mb.addEvent(juce::MidiMessage::noteOff(1, n.note), on + int(n.len * lineSec * rate) - 1);
				}
			auto b = run(p, ph, loops * loopLen / rate, rate, &mb);
			// Two cascaded one-pole low-passes at 12 Hz.
			const double a = 1 - std::exp(-2 * juce::MathConstants<double>::pi * 12 / rate);
			double y1 = 0, y2 = 0, lo = 0, all = 0;
			for (int i = 0; i < b.getNumSamples(); ++i) {
				const double x = b.getSample(0, i);
				y1 += a * (x - y1);
				y2 += a * (y1 - y2);
				if (i >= loopLen)
					lo += y2 * y2, all += x * x;
			}
			const double ratio = 10 * std::log10(lo / std::max(all, 1e-30));
			const double ms = 1e3 / tr::tm3RingHz(trims[t]);
			std::printf("      TM3 at %.1f ms, 8 loops: energy under 12 Hz %+.1f dB\n", ms, ratio);
			check(ratio < -30, ("stays stable over a long run with TM3 at " + juce::String(ms, 1) + " ms").toRawUTF8());
		}
		p.params().getParameter("tm3")->setValueNotifyingHost(tm3Unit);
	}

	// 6b. The other trimmers (SERVICE): at their defaults the unit is on the notes' spec (A at
	// 110 Hz, octaves 2:1, 300 BPM at full TEMPO); turned, each moves what core/trims.h says.
	{
		namespace tr = tb303::trims;
		auto setP = [&](const char *id, float v) { p.params().getParameter(id)->setValueNotifyingHost(v); };
		auto def = [&](const char *id) { return p.params().getParameter(id)->getDefaultValue(); };
		auto hold = [&](int note) {
			juce::MidiBuffer mb;
			mb.addEvent(juce::MidiMessage::noteOn(1, note, uint8_t(64)), 0);
			run(p, ph, 0.15, rate, &mb);
			const auto snap = p.snapshot();
			mb.clear();
			mb.addEvent(juce::MidiMessage::allNotesOff(1), 0);
			run(p, ph, 0.05, rate, &mb);
			return snap;
		};
		setP("tuning", 0.5f);
		const double a = hold(45).probe.vcoHz, c2 = hold(36).probe.vcoHz, c3 = hold(48).probe.vcoHz;
		std::printf("      defaults: A %.4f Hz, C3 : C2 %.5f\n", a, c3 / c2);
		check(std::abs(a - 110) < 1e-3 && std::abs(c3 / c2 - 2) < 1e-5, "default trims: A is 110 Hz and the octave 2:1");

		// TM5 wider, TM4 back to 110 Hz at A as the notes' procedure goes: the octave is the trims'.
		tr::Trims t;
		t.tm5 = 0.5;
		double lo = 0, hi = 1;
		for (int i = 0; i < 60; ++i) {
			t.tm4 = 0.5 * (lo + hi);
			(tr::aHz(t) < 110 ? lo : hi) = t.tm4;
		}
		setP("tm5", float(t.tm5));
		setP("tm4", float(t.tm4));
		t.tm4 = double(float(t.tm4)); // as the parameter holds it
		const double a5 = hold(45).probe.vcoHz, lo5 = hold(36).probe.vcoHz, hi5 = hold(48).probe.vcoHz;
		std::printf("      TM5 %.2f, TM4 %.4f: A %.3f Hz, C3 : C2 %.4f (trims %.4f)\n", t.tm5, t.tm4, a5, hi5 / lo5, tr::octaveRatio(t));
		check(std::abs(a5 - tr::aHz(t)) < 0.01 && std::abs(hi5 / lo5 / tr::octaveRatio(t) - 1) < 1e-4 && hi5 / lo5 > 2.02,
		      "TM5 widens the octave and TM4 brings A back, as the trims say");
		setP("tm4", def("tm4"));
		setP("tm5", def("tm5"));

		// TM6: CV OUT's octave is its reference; the pitch follows.
		setP("tm6", 0.6f);
		const auto s6 = hold(45);
		t = {};
		t.tm6 = double(0.6f);
		std::printf("      TM6 0.6: CV OUT %.4f V per octave, A %.3f Hz (trims %.3f)\n", s6.cvRef, s6.probe.vcoHz, tr::aHz(t));
		check(std::abs(s6.cvRef - 1.01) < 1e-6 && std::abs(s6.probe.vcoHz - tr::aHz(t)) < 0.01, "TM6 scales CV OUT and moves the pitch with it");
		setP("tm6", def("tm6"));

		// TM1: TEMPO full clockwise is 300 BPM trimmed, slower with more R18 + TM1.
		setP("tempo", 1.0f);
		run(p, ph, 0.02, rate);
		const double bpm0 = p.snapshot().bpm;
		setP("tm1", 0.75f);
		run(p, ph, 0.02, rate);
		const double bpm1 = p.snapshot().bpm;
		std::printf("      TEMPO full clockwise: %.1f BPM, TM1 0.75: %.1f BPM\n", bpm0, bpm1);
		check(std::abs(bpm0 - 300) < 1e-6 && std::abs(bpm1 - 300 / tr::tempoRScale(0.75)) < 1e-3, "TM1 sets how fast full TEMPO goes");
		setP("tm1", def("tm1"));
		setP("tempo", def("tempo"));
	}

	// 6c. A new instance is at the factory's trim, and stays there through a session's save and load.
	{
		auto same = [](const tb303::trims::Trims &a, const tb303::trims::Trims &b) {
			return a.tm1 == b.tm1 && a.tm2 == b.tm2 && a.tm3 == b.tm3 && a.tm4 == b.tm4 && a.tm5 == b.tm5 && a.tm6 == b.tm6;
		};
		TB303Processor fresh(&rom);
		juce::MemoryBlock st;
		fresh.getStateInformation(st);
		TB303Processor reloaded(&rom);
		reloaded.setStateInformation(st.getData(), int(st.getSize()));
		const auto t = reloaded.trims();
		std::printf("      reloaded trims %.9f %.9f %.9f %.9f %.9f %.9f\n", t.tm1, t.tm2, t.tm3, t.tm4, t.tm5, t.tm6);
		check(same(fresh.trims(), TB303Processor::trimDefaults()) && same(t, TB303Processor::trimDefaults()), "a new instance is at factory trim, also after save and load");
	}

	// 7. State round trip, including pattern memory.
	p.params().getParameter("cutoff")->setValueNotifyingHost(0.123f);
	juce::MemoryBlock state;
	p.getStateInformation(state);
	TB303Processor q(&rom);
	q.setStateInformation(state.getData(), int(state.getSize()));
	check(std::abs(q.params().getParameter("cutoff")->getValue() - 0.123f) < 1e-3, "parameters restore");
	const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	check(xml != nullptr && xml->getIntAttribute("model") == TB303Processor::kModelVersion &&
	          q.modelVersion() == TB303Processor::kModelVersion,
	      "the session records the model version");
	juce::MemoryBlock state2;
	q.getStateInformation(state2);
	check(state2.getSize() > 3000, "state carries pattern memory");

	std::printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
