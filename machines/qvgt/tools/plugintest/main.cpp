// Headless test of the QuadraVerb GT's processor: it boots with the factory programs, the effects
// sound, the session keeps the RAM, the unit sleeps with exact zeros and wakes, POWER, and the
// same input gives the same output. Needs the ROM from the private folder (QVGT_ROM).
#include "HardwareView.h"
#include "PluginProcessor.h"
#include "QVGTPanel.h"
#include "host/Roms.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

int g_failures = 0;
void check(bool ok, const char *what)
{
	std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
	if (!ok)
		++g_failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

juce::AudioBuffer<float> riff(int samples)
{
	static const double notes[] = {82.41, 98.0, 110.0, 123.47};
	juce::AudioBuffer<float> b(2, samples);
	for (int i = 0; i < samples; ++i) {
		const double t = i / kRate, step = t / 0.25, u = (step - std::floor(step)) * 0.25;
		double v = 0;
		for (int h = 1; h <= 6; ++h)
			v += std::sin(2 * juce::MathConstants<double>::pi * notes[int(step) % 4] * h * u) / (h * h);
		v *= 0.25 * std::exp(-u * 9);
		b.setSample(0, i, float(v));
		b.setSample(1, i, float(v));
	}
	return b;
}

// Silence: juce::AudioBuffer doesn't clear what it allocates.
juce::AudioBuffer<float> silence(int samples)
{
	juce::AudioBuffer<float> b(2, samples);
	b.clear();
	return b;
}

juce::AudioBuffer<float> run(QVGTProcessor &p, const juce::AudioBuffer<float> &in, int extra = 0, juce::MidiBuffer *firstMidi = nullptr)
{
	const int n = in.getNumSamples() + extra;
	juce::AudioBuffer<float> out(2, n);
	out.clear();
	juce::MidiBuffer midi;
	for (int at = 0; at < n; at += kBlock) {
		midi.clear();
		if (at == 0 && firstMidi != nullptr)
			midi = *firstMidi;
		const int len = std::min(kBlock, n - at);
		juce::AudioBuffer<float> block(2, len);
		for (int ch = 0; ch < 2; ++ch)
			for (int i = 0; i < len; ++i)
				block.setSample(ch, i, at + i < in.getNumSamples() ? in.getSample(ch, at + i) : 0.0f);
		p.processBlock(block, midi);
		for (int ch = 0; ch < 2; ++ch)
			out.copyFrom(ch, at, block, ch, 0, len);
	}
	return out;
}

float rmsDb(const juce::AudioBuffer<float> &b, int from, int to)
{
	double s = 0;
	for (int ch = 0; ch < 2; ++ch)
		for (int i = from; i < to; ++i)
			s += double(b.getSample(ch, i)) * b.getSample(ch, i);
	return float(10 * std::log10(s / (2.0 * (to - from)) + 1e-30));
}

juce::String lcd(const QVGTProcessor &p, int line)
{
	const auto d = p.display();
	juce::String s;
	for (int i = 0; i < 16; ++i)
		s += juce::String::charToString(juce::juce_wchar(d.text[line][i] >= 0x20 && d.text[line][i] < 0x7f ? d.text[line][i] : '#'));
	return s;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	// --snapshot out.png [rear | --artwork FOLDER] [--hover X Y] [--help]: the panel (or the rear)
	// after boot, drawn headless, with the brand marks the ROM folder FOLDER has for the places
	// panel.json lists, and nothing else; with the mouse over X, Y (view pixels) for a tooltip, or
	// the key list (?) open
	const juce::String snapshot = argc > 2 && juce::String(argv[1]) == "--snapshot" ? juce::String(argv[2]) : juce::String();
	lemus::Artwork artwork;
	if (argc > 4 && juce::String(argv[3]) == "--artwork") {
		const auto v = juce::JSON::parse(juce::String::fromUTF8(QVGTPanel::panel_json, QVGTPanel::panel_jsonSize));
		lemus::ArtworkSpec spec;
		spec.name = "qvgt.svg";
		spec.plugin = "al-fx-quadgt";
		for (const auto &l : *v["logos"].getArray())
			spec.places.push_back({l["place"].toString(), l["mark"].toString(), l["label"].toString(), float(double(l["box"][2])), float(double(l["box"][3]))});
		lemus::RomSet roms;
		lemus::romfolder::addArtwork(roms, spec, lemus::romfolder::findAllArtwork(juce::File(argv[4]), spec));
		artwork = lemus::Artwork::load(roms, spec);
		std::printf("    artwork: %d marks from %s\n", int(artwork.marks().size()), argv[4]);
	}
	juce::MemoryBlock rom, font;
	if (!juce::File(QVGT_ROM).loadFileAsData(rom) || !juce::File(QVGT_LCD_ROM).loadFileAsData(font)) {
		std::printf("No ROMs at %s, %s: skipped.\n", QVGT_ROM, QVGT_LCD_ROM);
		return 0;
	}

	std::printf("Boot\n");
	QVGTProcessor p(&rom, &font, artwork);
	p.setRateAndBufferSizeDetails(kRate, kBlock);
	p.prepareToPlay(kRate, kBlock);
	p.waitForBoot();
	check(p.romLoaded() && p.booted(), "the firmware boots");
	p.prepareToPlay(kRate, kBlock);
	run(p, silence(kBlock * 10)); // a block or two for the display
	std::printf("    LCD |%s|%s|\n", lcd(p, 0).toRawUTF8(), lcd(p, 1).toRawUTF8());
	check(lcd(p, 1).contains("GUITARBYALESIS"), "program 00 is the factory's GUITARBYALESIS");
	if (snapshot.isNotEmpty()) {
		qvgt::HardwareView view(p);
		view.setBounds(0, 0, qvgt::HardwareView::kWidth, qvgt::HardwareView::kHeight);
		view.timerCallbackForTests();
		view.showRear(argc > 3 && juce::String(argv[3]) == "rear");
		for (int i = 3; i + 2 < argc; ++i) // --hover X Y: the mouse over that point (view pixels)
			if (juce::String(argv[i]) == "--hover")
				view.hoverForTests({juce::String(argv[i + 1]).getFloatValue(), juce::String(argv[i + 2]).getFloatValue()});
		for (int i = 3; i < argc; ++i)
			if (juce::String(argv[i]) == "--help")
				view.showHelpForTests();
		juce::Image img(juce::Image::ARGB, view.getWidth() * 2, view.getHeight() * 2, true);
		juce::Graphics g(img);
		g.addTransform(juce::AffineTransform::scale(2.0f));
		view.paintEntireComponent(g, true);
		juce::FileOutputStream os{juce::File(snapshot)};
		os.setPosition(0);
		os.truncate();
		juce::PNGImageFormat().writeImageToStream(img, os);
		std::printf("    panel drawn to %s\n", snapshot.toRawUTF8());
		return 0;
	}
	check(p.getLatencySamples() > 0, "it reports its latency");

	std::printf("Sound\n");
	const auto in = riff(int(kRate * 2));
	const auto out = run(p, in, int(kRate * 4));
	const float wet = rmsDb(out, 0, int(kRate * 2)), tail = rmsDb(out, int(kRate * 2.5), int(kRate * 3));
	std::printf("    level %.1f dB, tail 0.5-1 s after %.1f dB\n", wet, tail);
	check(wet > -40, "the effects sound");
	check(tail > -120 && tail < wet, "with a tail after the input stops");

	std::printf("Sleep\n");
	{
		// HISS on (the default): asleep once the tail has settled into the hiss, which goes on
		const auto quiet = run(p, silence(int(kRate * 20)));
		check(p.asleep(), "HISS on: asleep once the tail has gone");
		const float before = rmsDb(quiet, int(kRate * 1), int(kRate * 2)), after = rmsDb(quiet, int(kRate * 18), int(kRate * 20));
		std::printf("    idle %.1f dBFS awake (the tail gone), %.1f dBFS asleep\n", rmsDb(quiet, int(kRate * 8), int(kRate * 9)), after);
		check(after > -140 && std::abs(after - rmsDb(quiet, int(kRate * 8), int(kRate * 9))) < 3, "and the hiss goes on at its level");
		(void)before;
		run(p, riff(kBlock * 4));
		check(!p.asleep(), "input wakes it");
		// HISS off: noiseless, asleep with exact zeros
		auto *hiss = p.params().getParameter("hiss");
		hiss->setValueNotifyingHost(0.0f);
		const auto still = run(p, silence(int(kRate * 20)));
		check(p.asleep(), "HISS off: asleep once the tail has gone");
		bool zeros = true;
		for (int ch = 0; ch < 2; ++ch)
			for (int i = int(kRate * 19); i < still.getNumSamples(); ++i)
				zeros = zeros && still.getSample(ch, i) == 0.0f;
		check(zeros, "with exact zeros out");
		run(p, riff(kBlock * 4));
		check(!p.asleep(), "input wakes it");
		hiss->setValueNotifyingHost(1.0f);
	}

	std::printf("Session\n");
	juce::MemoryBlock state;
	p.getStateInformation(state);
	QVGTProcessor q(&rom, &font);
	q.setRateAndBufferSizeDetails(kRate, kBlock);
	q.setStateInformation(state.getData(), int(state.getSize()));
	q.waitForBoot();
	q.prepareToPlay(kRate, kBlock);
	run(q, silence(kBlock * 10));
	check(q.booted() && lcd(q, 1).contains("GUITARBYALESIS"), "a session load boots from its RAM");

	std::printf("Determinism\n");
	QVGTProcessor a(&rom, &font), b(&rom, &font);
	for (auto *x : {&a, &b}) {
		x->setRateAndBufferSizeDetails(kRate, kBlock);
		x->setStateInformation(state.getData(), int(state.getSize()));
		x->waitForBoot();
		x->prepareToPlay(kRate, kBlock);
	}
	const auto ra = run(a, in), rb = run(b, in);
	bool same = true;
	for (int ch = 0; ch < 2; ++ch)
		same = same && std::memcmp(ra.getReadPointer(ch), rb.getReadPointer(ch), sizeof(float) * size_t(ra.getNumSamples())) == 0;
	check(same, "the same session and input give the same output, bit for bit");

	std::printf("Layouts\n");
	{
		// the jacks: one plug in the inputs feeds both sides, one plug in the outputs gets the average
		const auto mono = juce::AudioChannelSet::mono(), stereo = juce::AudioChannelSet::stereo();
		auto layout = [](juce::AudioChannelSet in, juce::AudioChannelSet out) {
			juce::AudioProcessor::BusesLayout l;
			l.inputBuses.add(in);
			l.outputBuses.add(out);
			return l;
		};
		bool all = true;
		for (const auto &i : {mono, stereo})
			for (const auto &o : {mono, stereo})
				all = all && a.checkBusesLayoutSupported(layout(i, o));
		check(all, "mono or stereo in, mono or stereo out");
		// the guitar in channel 0 of a block `channels` wide (the rest silent), as a host hands it
		auto runAs = [&](QVGTProcessor &x, int channels, bool both) {
			const int n = in.getNumSamples();
			juce::AudioBuffer<float> out(channels, n);
			juce::MidiBuffer midi;
			for (int at = 0; at < n; at += kBlock) {
				const int len = std::min(kBlock, n - at);
				juce::AudioBuffer<float> block(channels, len);
				block.clear();
				for (int ch = 0; ch < (both ? channels : 1); ++ch)
					block.copyFrom(ch, 0, in, 0, at, len);
				x.processBlock(block, midi);
				for (int ch = 0; ch < channels; ++ch)
					out.copyFrom(ch, at, block, ch, 0, len);
			}
			return out;
		};
		QVGTProcessor st(&rom, &font), mm(&rom, &font), ms(&rom, &font);
		const bool set = mm.setBusesLayout(layout(mono, mono)) && ms.setBusesLayout(layout(mono, stereo));
		for (auto *x : {&st, &mm, &ms}) {
			x->setRateAndBufferSizeDetails(kRate, kBlock);
			x->setStateInformation(state.getData(), int(state.getSize()));
			x->waitForBoot();
			x->prepareToPlay(kRate, kBlock);
		}
		const auto rs = runAs(st, 2, true), rm = runAs(mm, 1, false), rms = runAs(ms, 2, false);
		bool average = set, spread = set;
		for (int i = 0; i < rs.getNumSamples(); ++i) {
			average = average && rm.getSample(0, i) == 0.5f * (rs.getSample(0, i) + rs.getSample(1, i));
			spread = spread && rms.getSample(0, i) == rs.getSample(0, i) && rms.getSample(1, i) == rs.getSample(1, i);
		}
		check(spread, "mono in, stereo out: the guitar into both inputs, both outputs");
		check(average, "mono out: the average of the two outputs");
	}

	std::printf("MIDI\n");
	{
		juce::MidiBuffer m;
		m.addEvent(juce::MidiMessage::programChange(1, 5), 0);
		run(a, silence(int(kRate * 1.0)), 0, &m);
		std::printf("    LCD |%s|%s|\n", lcd(a, 0).toRawUTF8(), lcd(a, 1).toRawUTF8());
		check(lcd(a, 1).contains("WATERFALLS"), "a program change by MIDI selects program 05 (WATERFALLS)");
		juce::MidiBuffer back;
		m.clear();
		m.addEvent(juce::MidiMessage::programChange(1, 0), 0);
		juce::AudioBuffer<float> blk(2, kBlock);
		blk.clear();
		a.processBlock(blk, m);
		bool thru = false;
		for (const auto e : m)
			thru = thru || e.getMessage().isProgramChange();
		for (int k = 0; k < 20 && !thru; ++k) {
			blk.clear();
			juce::MidiBuffer out;
			a.processBlock(blk, out);
			for (const auto e : out)
				thru = thru || e.getMessage().isProgramChange();
		}
		check(thru, "MIDI THRU: the program change comes back out");
		run(a, silence(int(kRate * 1.0)));
		check(lcd(a, 1).contains("GUITARBYALESIS"), "and program 00 is back");
	}

	std::printf("Session, mid-edit\n");
	{
		// a session is not a power-on: it reopens on the program, the edit page and the edit
		const auto press = [&](int key) {
			a.setKey(key, true, false);
			run(a, silence(int(kRate * 0.15)));
			a.setKey(key, false, false);
			run(a, silence(int(kRate * 0.3)));
		};
		juce::MidiBuffer m;
		m.addEvent(juce::MidiMessage::programChange(1, 5), 0);
		run(a, silence(int(kRate * 1.0)), 0, &m);
		for (const int k : {15, 7, 3, 3}) // REVERB, PAGE up, VALUE up twice: REVERB INPUT 1 moved
			press(k);
		const auto before = lcd(a, 0) + "|" + lcd(a, 1);
		juce::MemoryBlock s2;
		a.getStateInformation(s2);
		QVGTProcessor r(&rom, &font);
		r.setRateAndBufferSizeDetails(kRate, kBlock);
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		r.setStateInformation(s2.getData(), int(s2.getSize()));
		r.waitForBoot();
		const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
		r.prepareToPlay(kRate, kBlock);
		run(r, silence(int(kRate * 1.0)));
		const auto after = lcd(r, 0) + "|" + lcd(r, 1);
		std::printf("    before |%s|, after |%s|, ready in %.0f ms\n", before.toRawUTF8(), after.toRawUTF8(), ms);
		check(before.startsWith("REVERB INPUT 1") && after == before, "a session reopens on its edit page with the edit");
		juce::MidiBuffer back;
		back.addEvent(juce::MidiMessage::programChange(1, 0), 0);
		run(a, silence(int(kRate * 1.0)), 0, &back);
	}

	std::printf("Footswitch\n");
	{
		const auto ledBypass = [&] { return (a.display().latch0400 & 0x40) == 0; };
		const bool before = ledBypass();
		a.setFootswitch(0, true);
		run(a, silence(int(kRate * 0.3)));
		a.setFootswitch(0, false);
		run(a, silence(int(kRate * 0.3)));
		check(ledBypass() != before, "the BYPASS footswitch toggles bypass (its LED)");
		a.setFootswitch(0, true);
		run(a, silence(int(kRate * 0.3)));
		a.setFootswitch(0, false);
		run(a, silence(int(kRate * 0.3)));
		check(ledBypass() == before, "and back");
	}

	std::printf("Stress\n");
	{
		juce::Random rng(7);
		bool finite = true;
		float peak = 0;
		const auto in = riff(int(kRate * 2));
		for (int round = 0; round < 12; ++round) {
			for (auto *param : a.getParameters())
				param->setValueNotifyingHost(rng.nextFloat());
			const int key = rng.nextInt(16);
			a.setKey(key, true, rng.nextBool());
			auto chunk = run(a, in, 0);
			a.setKey(key, false);
			for (int ch = 0; ch < 2; ++ch)
				for (int i = 0; i < chunk.getNumSamples(); ++i) {
					const float x = chunk.getSample(ch, i);
					finite = finite && std::isfinite(x);
					peak = std::max(peak, std::abs(x));
				}
		}
		std::printf("    peak %.1f dBFS\n", 20 * std::log10(peak + 1e-30f));
		check(finite, "random knobs and keys for 24 s: the output stays finite");
		check(peak <= 6.05f, "and within the output stages' swing (10.5 V, +15.6 dBFS)");
		for (auto *param : a.getParameters())
			param->setValueNotifyingHost(param->getDefaultValue());
	}

	std::printf("Long run\n");
	{
		bool finite = true;
		float peak = 0;
		const auto in = riff(int(kRate * 2));
		for (int k = 0; k < 30; ++k) {
			const auto chunk = run(a, in);
			for (int ch = 0; ch < 2; ++ch)
				for (int i = 0; i < chunk.getNumSamples(); ++i) {
					finite = finite && std::isfinite(chunk.getSample(ch, i));
					peak = std::max(peak, std::abs(chunk.getSample(ch, i)));
				}
		}
		check(finite && peak <= 6.05f, "60 s of playing: finite, no runaway");
	}

	std::printf("Power\n");
	b.setPowered(false);
	const auto off = run(b, in);
	check(off.getMagnitude(0, off.getNumSamples()) == 0.0f, "POWER off: silent");
	b.setPowered(true);
	b.waitForBoot();
	b.prepareToPlay(kRate, kBlock);
	run(b, silence(kBlock * 10));
	check(b.booted() && lcd(b, 1).contains("GUITARBYALESIS"), "POWER on: boots from the RAM the battery kept");

	std::printf(g_failures ? "\n%d FAILED\n" : "\nAll passed.\n", g_failures);
	return g_failures ? 1 : 0;
}
