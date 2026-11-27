// Headless test of the QuadraVerb GT's processor: it boots with the factory programs, the effects
// sound, the session keeps the RAM, the unit sleeps with exact zeros and wakes, POWER, and the
// same input gives the same output; an offline render waits for the boot, the latency is the
// host's rate's, NaN input passes as silence, a save with no audio running is quick, and a
// decimal-comma locale changes nothing. Needs the ROM from the private folder (QVGT_ROM).
#include "HardwareView.h"
#include "PluginProcessor.h"
#include "QVGTPanel.h"
#include "host/Roms.h"

#include <atomic>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <thread>

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

bool allFinite(const juce::AudioBuffer<float> &b)
{
	for (int ch = 0; ch < b.getNumChannels(); ++ch)
		for (int i = 0; i < b.getNumSamples(); ++i)
			if (!std::isfinite(b.getSample(ch, i)))
				return false;
	return true;
}

// The RAM a saved session holds.
juce::MemoryBlock savedRam(const juce::MemoryBlock &state)
{
	const auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	if (xml == nullptr)
		return {};
	const auto tree = juce::ValueTree::fromXml(*xml);
	const auto *b = tree.getProperty("ram").getBinaryData();
	return b != nullptr ? *b : juce::MemoryBlock();
}

bool sameRam(const juce::MemoryBlock &saved, QVGTProcessor &p)
{
	const auto &ram = p.unitForTests().board().ram();
	return saved.getSize() == ram.size() && std::memcmp(saved.getData(), ram.data(), ram.size()) == 0;
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
		// The parts' own noise always, as on the unit: no setting turns it off
		check(p.params().getParameter("hiss") == nullptr, "no HISS setting: the hiss is always there");
		// asleep once the tail has settled into the hiss, which goes on
		const auto quiet = run(p, silence(int(kRate * 20)));
		check(p.asleep(), "asleep once the tail has gone");
		const float after = rmsDb(quiet, int(kRate * 18), int(kRate * 20));
		std::printf("    idle %.1f dBFS awake (the tail gone), %.1f dBFS asleep\n", rmsDb(quiet, int(kRate * 8), int(kRate * 9)), after);
		check(after > -140 && std::abs(after - rmsDb(quiet, int(kRate * 8), int(kRate * 9))) < 3, "and the hiss goes on at its level");
		run(p, riff(kBlock * 4));
		check(!p.asleep(), "input wakes it");
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

	std::printf("Offline renders\n");
	{
		// A bounce right after loading, a session load or POWER on: the host can wait for the boot
		// (it used to be cleared until the boot was done, about 120 ms of wall time: a short bounce
		// came out silent). Booted, the output starts at the latency (the hiss alone is nonzero).
		const auto firstSound = [](const juce::AudioBuffer<float> &o) {
			for (int i = 0; i < o.getNumSamples(); ++i)
				if (o.getSample(0, i) != 0.0f || o.getSample(1, i) != 0.0f)
					return i;
			return -1;
		};
		const auto bounce = [&](QVGTProcessor &x, const char *what) {
			const auto t0 = juce::Time::getMillisecondCounterHiRes();
			const auto o = run(x, in);
			const int first = firstSound(o);
			const float level = rmsDb(o, 0, int(kRate * 0.25));
			std::printf("    %s: first sound at sample %d, %.1f dB over the first 0.25 s (%.0f ms to render 2 s)\n", what, first, level,
			            juce::Time::getMillisecondCounterHiRes() - t0);
			return first >= 0 && first <= x.getLatencySamples() && level > -40;
		};
		QVGTProcessor x(&rom, &font);
		x.setNonRealtime(true);
		x.setRateAndBufferSizeDetails(kRate, kBlock);
		x.prepareToPlay(kRate, kBlock);
		check(bounce(x, "right after loading"), "a bounce right after loading has the effects from the start (the latency)");
		x.setStateInformation(state.getData(), int(state.getSize()));
		check(bounce(x, "right after a session load"), "and right after a session load");
		x.setPowered(false);
		x.setPowered(true);
		check(bounce(x, "right after POWER on"), "and right after POWER on");
		QVGTProcessor live(&rom, &font);
		live.setRateAndBufferSizeDetails(kRate, kBlock);
		live.prepareToPlay(kRate, kBlock);
		juce::AudioBuffer<float> blk(2, kBlock);
		blk.clear();
		juce::MidiBuffer none;
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		live.processBlock(blk, none);
		const auto ms = juce::Time::getMillisecondCounterHiRes() - t0;
		check(ms < 20 && blk.getMagnitude(0, kBlock) == 0.0f, "playing live, a block during the boot doesn't wait: silent");
	}

	std::printf("Latency\n");
	for (const double rate : {44100.0, 96000.0}) {
		// prepared while the firmware boots (at 48 kHz unless told otherwise): the host is told this
		// rate's latency, and the unit runs at this rate once booted
		QVGTProcessor x(&rom, &font);
		x.setRateAndBufferSizeDetails(rate, kBlock);
		const bool during = !x.booted();
		x.prepareToPlay(rate, kBlock);
		const int told = x.getLatencySamples();
		x.waitForBoot();
		const int unit = x.unitForTests().latency();
		std::printf("    %.0f Hz: told %d (prepared %s the boot), the unit's %d\n", rate, told, during ? "during" : "after", unit);
		check(x.booted() && told == unit && told == x.unitForTests().latencyAt(rate), "a prepare during the boot tells the host the unit's latency at its rate");
		// a session load (a boot at this rate) and a prepare at another rate during it
		const double other = rate == 44100.0 ? 96000.0 : 44100.0;
		x.setStateInformation(state.getData(), int(state.getSize()));
		x.setRateAndBufferSizeDetails(other, kBlock);
		x.prepareToPlay(other, kBlock);
		const int told2 = x.getLatencySamples();
		x.waitForBoot();
		std::printf("    then %.0f Hz during a session load: told %d, the unit's %d\n", other, told2, x.unitForTests().latency());
		check(x.booted() && told2 == x.unitForTests().latency() && told2 != told, "and a rate change during a session load: the new rate's, told and run");
	}

	std::printf("Bad input\n");
	{
		// NaN and Inf from the host are silence: they used to reach the A/D (undefined behaviour in
		// its conversion) and stay in the DSP's memory and the filters, NaN out until a reload
		juce::AudioBuffer<float> bad(2, kBlock);
		for (int ch = 0; ch < 2; ++ch)
			for (int i = 0; i < kBlock; ++i)
				bad.setSample(ch, i, i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : i % 3 == 1 ? std::numeric_limits<float>::infinity() : -std::numeric_limits<float>::infinity());
		const auto during = run(a, bad);
		const auto after = run(a, riff(int(kRate)));
		float pk = 0;
		for (int ch = 0; ch < 2; ++ch)
			pk = std::max(pk, after.getMagnitude(ch, 0, after.getNumSamples()));
		check(allFinite(during) && allFinite(after) && pk > 0.01f, "a block of NaN and Inf, then the riff: finite output, with the effects");
	}

	std::printf("Saving\n");
	{
		// with no audio running the save copies the RAM itself (it used to wait 400 ms for the
		// audio thread)
		QVGTProcessor x(&rom, &font);
		x.setRateAndBufferSizeDetails(kRate, kBlock);
		x.waitForBoot();
		auto timedSave = [&](juce::MemoryBlock &m) {
			const auto t0 = juce::Time::getMillisecondCounterHiRes();
			x.getStateInformation(m);
			return juce::Time::getMillisecondCounterHiRes() - t0;
		};
		juce::MemoryBlock s1;
		const double ms1 = timedSave(s1);
		x.prepareToPlay(kRate, kBlock);
		juce::MidiBuffer m;
		m.addEvent(juce::MidiMessage::programChange(1, 5), 0);
		run(x, silence(int(kRate * 1.0)), 0, &m);
		juce::Thread::sleep(300); // the audio thread stopped
		juce::MemoryBlock s2;
		const double ms2 = timedSave(s2);
		std::printf("    booted, never played: %.1f ms; played, then stopped: %.1f ms\n", ms1, ms2);
		check(ms1 < 50 && ms2 < 50, "a save with no audio running returns at once");
		check(sameRam(savedRam(s2), x) && savedRam(s1) != savedRam(s2), "with the unit's RAM as it is");
		// with audio running, the audio thread hands it over
		std::atomic<bool> stop{false};
		std::thread audio([&] {
			juce::AudioBuffer<float> blk(2, kBlock);
			juce::MidiBuffer none;
			while (!stop) {
				blk.clear();
				x.processBlock(blk, none);
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		});
		juce::Thread::sleep(100);
		juce::MemoryBlock s3;
		const double ms3 = timedSave(s3);
		stop = true;
		audio.join();
		std::printf("    playing: %.1f ms\n", ms3);
		check(ms3 < 100 && sameRam(savedRam(s3), x), "and with audio running, the RAM from the audio thread");
	}

	std::printf("Decimal-comma locale\n");
	{
		// A host or another plugin in its process may set a C locale with a decimal comma; the
		// netlists' values (".01u") must read the same (std::stod used to throw: a crash from POWER)
		QVGTProcessor ref(&rom, &font);
		ref.setRateAndBufferSizeDetails(kRate, kBlock);
		ref.setStateInformation(state.getData(), int(state.getSize()));
		ref.waitForBoot();
		ref.prepareToPlay(kRate, kBlock);
		const auto want = run(ref, in);
		const std::string was = std::setlocale(LC_NUMERIC, nullptr);
		std::string set;
		for (const char *name : {"de_DE.UTF-8", "de_DE", "fr_FR.UTF-8", "fr_FR"})
			if (std::setlocale(LC_NUMERIC, name) != nullptr) {
				set = name;
				break;
			}
		const bool comma = !set.empty() && std::strtod("0.5", nullptr) == 0.0;
		std::string error;
		juce::AudioBuffer<float> got;
		bool booted = false;
		try {
			QVGTProcessor x(&rom, &font);
			x.setRateAndBufferSizeDetails(kRate, kBlock);
			x.setStateInformation(state.getData(), int(state.getSize()));
			x.waitForBoot();
			x.prepareToPlay(kRate, kBlock);
			got = run(x, in);
			booted = x.booted();
			x.setPowered(false);
			x.setPowered(true); // a power-on builds a new unit
			x.waitForBoot();
			booted = booted && x.booted();
		} catch (const std::exception &e) {
			error = e.what();
		}
		std::setlocale(LC_NUMERIC, was.c_str());
		if (!comma)
			std::printf("    no decimal-comma locale here: checked in the C locale only\n");
		else
			std::printf("    built in %s%s%s\n", set.c_str(), error.empty() ? "" : ": ", error.c_str());
		bool same = booted && got.getNumSamples() == want.getNumSamples();
		int differ = 0, firstDiff = -1;
		for (int ch = 0; same && ch < 2; ++ch)
			for (int i = 0; i < want.getNumSamples(); ++i)
				if (got.getSample(ch, i) != want.getSample(ch, i)) {
					++differ;
					if (firstDiff < 0)
						firstDiff = i;
				}
		std::printf("    booted %d, %d samples differ (first at %d)\n", int(booted), differ, firstDiff);
		same = same && differ == 0;
		check(error.empty() && same, "built and powered on under a decimal-comma locale: the same output, bit for bit");
	}

	std::printf(g_failures ? "\n%d FAILED\n" : "\nAll passed.\n", g_failures);
	return g_failures ? 1 : 0;
}
