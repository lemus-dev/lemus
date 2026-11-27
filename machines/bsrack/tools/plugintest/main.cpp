// Headless test of the Bass Station Rack's processor: the firmware boots and shows a program, MIDI
// plays it, AUDITION sounds, MODE steps the mode LEDs, the session keeps the controls, FILTER INPUT (the side-chain)
// and the EEPROM, it sleeps with exact zeros and wakes on a note, long runs and random settings stay
// finite, and the panel (--panel [DIR]: snapshots of it). These check that the voice sounds, sleeps and
// stays finite, not how it sounds (tools/compare does that). Needs the ROM from the private folder
// (BSRACK_ROM). --panel [DIR] [--artwork FOLDER] runs them all, then draws the panel to DIR
// (./bsrack-panel), with the marks FOLDER (a ROM folder) has for the artwork places.
#include "BSRackPanel.h"
#include "HardwareView.h"
#include "PluginProcessor.h"
#include "host/Roms.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>

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

juce::MemoryBlock g_rom;

using Events = std::vector<std::pair<int, juce::MidiMessage>>;

// What the firmware hands the voice, block by block during the last run(): GATE OUT, and the
// range of envelope 1's VCA code. It shows the firmware took a note whatever the voice makes of it.
struct Trace {
	bool gate = false;
	int vcaMin = 4096, vcaMax = -1;
	int volMax = 0; // the mixer's oscillator levels (on-chip registers)
	long edges = 0; // the DCOs' sync edges, in the blocks' last samples
	std::vector<std::pair<int, juce::MidiMessage>> midiOut; // what the plugin sent, at its sample
} g_trace;

std::vector<float> run(BassStationProcessor &p, int samples, const Events &events = {})
{
	g_trace = {};
	std::vector<float> out(size_t(samples), 0.0f);
	juce::MidiBuffer midi;
	for (int at = 0; at < samples; at += kBlock) {
		const int len = std::min(kBlock, samples - at);
		juce::AudioBuffer<float> block(2, len);
		block.clear();
		midi.clear();
		for (const auto &[pos, m] : events)
			if (pos >= at && pos < at + len)
				midi.addEvent(m, pos - at);
		p.processBlock(block, midi);
		for (const auto m : midi)
			g_trace.midiOut.emplace_back(at + m.samplePosition, m.getMessage());
		for (int i = 0; i < len; ++i)
			out[size_t(at + i)] = block.getSample(0, i);
		const auto &f = p.machineForTests().frame();
		g_trace.gate = g_trace.gate || f.gateOut;
		g_trace.vcaMin = std::min(g_trace.vcaMin, int(f.dac[bsrack::kVcaCv]));
		g_trace.vcaMax = std::max(g_trace.vcaMax, int(f.dac[bsrack::kVcaCv]));
		g_trace.volMax = std::max({g_trace.volMax, int(f.volA), int(f.volB)});
		g_trace.edges += f.nEdgesA + f.nEdgesB;
	}
	return out;
}

double rmsDb(const std::vector<float> &x, int from, int to)
{
	double s = 0;
	for (int i = from; i < to; ++i)
		s += double(x[size_t(i)]) * x[size_t(i)];
	return 10 * std::log10(s / std::max(1, to - from) + 1e-30);
}

bool finite(const std::vector<float> &x)
{
	for (float v : x)
		if (!std::isfinite(v) || std::fabs(v) > 4)
			return false;
	return true;
}

// A new unit with the firmware, prepared and past its boot.
std::unique_ptr<BassStationProcessor> unit(lemus::Artwork artwork = {})
{
	auto p = std::make_unique<BassStationProcessor>(&g_rom, std::move(artwork));
	p->setRateAndBufferSizeDetails(kRate, kBlock);
	p->prepareToPlay(kRate, kBlock);
	run(*p, int(kRate * 1.0));
	return p;
}

void set(BassStationProcessor &p, const char *id, float v01)
{
	p.params().getParameter(id)->setValueNotifyingHost(v01);
}

Events note(int n, double on, double off, int channel = 1)
{
	return {{int(on * kRate), juce::MidiMessage::noteOn(channel, n, juce::uint8(100))}, {int(off * kRate), juce::MidiMessage::noteOff(channel, n)}};
}

int digitsLit(const BassStationProcessor::Display &d)
{
	int n = 0;
	for (const uint8_t v : d.digits)
		for (int b = 0; b < 7; ++b)
			n += (v >> b) & 1;
	return n;
}

void testBoot()
{
	std::printf("Boot\n");
	auto p = unit();
	check(p->romLoaded(), "the firmware loads");
	const auto d = p->display();
	std::printf("    display segments %02x %02x, LEDs program %d rx %d tx %d utility %d save %d\n", d.digits[0], d.digits[1], d.program, d.midiRx,
	            d.midiTx, d.utility, d.save);
	check(digitsLit(d) >= 4, "the display shows a program number");
	check(d.program && !d.midiRx && !d.midiTx && !d.utility && !d.save, "in PROGRAM mode, its LED alone lit");
}

void testMidi()
{
	std::printf("MIDI\n");
	auto p = unit();
	const auto out = run(*p, int(kRate * 2.0), note(48, 0.2, 1.0));
	std::printf("    GATE OUT %s, VCA code %d..%d, oscillator levels up to %d, DCO edges %ld\n", g_trace.gate ? "high" : "low", g_trace.vcaMin,
	            g_trace.vcaMax, g_trace.volMax, g_trace.edges);
	check(g_trace.gate && g_trace.vcaMax - g_trace.vcaMin > 200, "the firmware takes a note on channel 1: GATE OUT, envelope 1 moves");
	const double before = rmsDb(out, 0, int(0.19 * kRate)), during = rmsDb(out, int(0.4 * kRate), int(0.9 * kRate));
	std::printf("    before %.1f dB, during %.1f dB, end %.1f dB\n", before, during, rmsDb(out, int(1.9 * kRate), int(2.0 * kRate)));
	check(during > -40, "a note on channel 1 sounds");
	check(during > before + 30, "well over what comes before it");
	check(finite(out), "finite and in range");
	auto q = unit();
	run(*q, int(kRate * 1.0), note(48, 0.2, 0.8, 5));
	std::printf("    (a note on channel 5: GATE OUT %s; the EEPROM's MIDI RX channel is 1)\n", g_trace.gate ? "high" : "low");
}

void testButtons()
{
	std::printf("Buttons\n");
	auto p = unit();
	p->setButton(bsrack::kAudition, true);
	auto out = run(*p, int(kRate * 0.6));
	const bool gate = g_trace.gate;
	p->setButton(bsrack::kAudition, false);
	run(*p, int(kRate * 0.1));
	check(gate, "AUDITION held plays a note: GATE OUT");
	check(rmsDb(out, int(0.2 * kRate), int(0.5 * kRate)) > -40, "and it sounds");
	p->setButton(bsrack::kMode, true);
	run(*p, int(kRate * 0.1));
	p->setButton(bsrack::kMode, false);
	run(*p, int(kRate * 0.3));
	const auto d = p->display();
	check(d.midiRx && !d.program, "MODE SELECT steps from PROGRAM to MIDI RX CH");
	auto q = unit();
	const auto start = q->display();
	q->setButton(bsrack::kKeyPlus, true);
	run(*q, int(kRate * 0.1));
	q->setButton(bsrack::kKeyPlus, false);
	run(*q, int(kRate * 0.3));
	const auto after = q->display();
	check(after.digits != start.digits, "+ steps the program: the display changes");
}

void testSession()
{
	std::printf("Session\n");
	auto a = unit();
	set(*a, "frequency", 0.25f);
	set(*a, "osc2_range", a->params().getParameter("osc2_range")->convertTo0to1(3));
	std::vector<uint8_t> eeprom(bsrack::Machine::kEepromSize);
	for (size_t i = 0; i < eeprom.size(); ++i)
		eeprom[i] = uint8_t(i * 7);
	a->machineForTests().loadEeprom(eeprom);
	run(*a, kBlock * 4);
	juce::MemoryBlock state;
	a->getStateInformation(state);
	BassStationProcessor b(&g_rom);
	b.setRateAndBufferSizeDetails(kRate, kBlock);
	b.prepareToPlay(kRate, kBlock);
	b.setStateInformation(state.getData(), int(state.getSize()));
	run(b, kBlock * 4);
	check(std::fabs(b.params().getParameter("frequency")->getValue() - 0.25f) < 1e-6f, "the knobs come back");
	check(juce::roundToInt(b.params().getParameter("osc2_range")->convertFrom0to1(b.params().getParameter("osc2_range")->getValue())) == 3,
	      "the switches come back");
	check(b.machineForTests().saveEeprom() == eeprom, "and the EEPROM (the user programs, the MIDI channels)");
	check(b.modelVersion() == BassStationProcessor::kModelVersion, "with the model version");
}

// FILTER INPUT is the side-chain: routed, it plays through the filter; not routed, nothing is in it.
void testFilterInput()
{
	std::printf("FILTER INPUT\n");
	auto level = [](bool routed) {
		auto p = unit();
		if (routed)
			p->enableAllBuses();
		std::vector<float> out;
		juce::MidiBuffer midi;
		const int blocks = int(kRate * 0.6) / kBlock;
		for (int b = 0; b < blocks; ++b) {
			juce::AudioBuffer<float> block(2, kBlock);
			for (int ch = 0; ch < 2; ++ch)
				for (int i = 0; i < kBlock; ++i)
					block.setSample(ch, i, 0.5f * std::sin(2 * juce::MathConstants<float>::pi * 220.0f * float(b * kBlock + i) / float(kRate)));
			midi.clear();
			if (b == 2)
				midi.addEvent(juce::MidiMessage::noteOn(1, 48, juce::uint8(100)), 0);
			p->processBlock(block, midi);
			for (int i = 0; i < kBlock; ++i)
				out.push_back(block.getSample(0, i));
		}
		return std::make_pair(p->filterInRouted(), rmsDb(out, int(kRate * 0.3), int(out.size())));
	};
	const auto [offRouted, offDb] = level(false);
	const auto [onRouted, onDb] = level(true);
	std::printf("    a note with a 220 Hz sine at the side-chain: %.1f dB not routed, %.1f dB routed\n", offDb, onDb);
	check(!offRouted && onRouted, "plugged in exactly while the host routes the side-chain");
	check(std::fabs(onDb - offDb) > 1.0, "and what it carries plays through the filter");
}

// MIDI OUT: the firmware's bytes leave the plugin as messages, a knob as its controller.
void testMidiOut()
{
	std::printf("MIDI OUT\n");
	auto p = unit();
	set(*p, "frequency", 0.15f);
	run(*p, int(kRate * 0.5));
	int ccs = 0, notes = 0;
	for (const auto &[at, m] : g_trace.midiOut) {
		ccs += m.isController() ? 1 : 0;
		notes += m.isNoteOnOrOff() ? 1 : 0;
	}
	std::printf("    %d messages, %d controllers%s\n", int(g_trace.midiOut.size()), ccs,
	            g_trace.midiOut.empty() ? "" : (", first " + g_trace.midiOut.front().second.getDescription()).toRawUTF8());
	check(ccs > 0, "turning FREQUENCY sends its controller");
	check(notes == 0, "and no notes of its own");
	run(*p, int(kRate * 1.0), note(48, 0.1, 0.6));
	bool echoed = false;
	for (const auto &[at, m] : g_trace.midiOut)
		echoed = echoed || m.isNoteOn();
	std::printf("    a note played in: %s\n", echoed ? "sent on" : "not sent on");
}

// A plugin load isn't a power-on: a session reopens the unit as it was, the knob just turned (not
// written to a program) and the mode included, and it runs on as the unit it was saved from.
void testWarmSession()
{
	std::printf("Warm session\n");
	auto a = unit();
	const int vcfBefore = a->machineForTests().frame().dac[bsrack::kVcfCv];
	set(*a, "frequency", 0.15f); // moved more than the soft takeover's few codes: the firmware takes it
	run(*a, int(kRate * 0.5));
	const int vcfEdited = a->machineForTests().frame().dac[bsrack::kVcfCv];
	a->setButton(bsrack::kMode, true);
	run(*a, int(kRate * 0.1));
	a->setButton(bsrack::kMode, false);
	run(*a, int(kRate * 0.3));
	std::printf("    VCF code %03X before the knob, %03X after\n", vcfBefore, vcfEdited);
	juce::MemoryBlock state;
	a->getStateInformation(state);
	BassStationProcessor b(&g_rom);
	b.setRateAndBufferSizeDetails(kRate, kBlock);
	b.prepareToPlay(kRate, kBlock);
	b.setStateInformation(state.getData(), int(state.getSize()));
	check(b.machineForTests().snapshot() == a->machineForTests().snapshot(), "a session brings back the whole unit (CPU, RAM, timers, board)");
	run(*a, int(kRate * 0.5));
	run(b, int(kRate * 0.5));
	const auto da = a->display(), db = b.display();
	check(db.midiRx && !db.program && db.digits == da.digits, "in the mode it was in (MIDI RX CH), the display as it was");
	check(vcfEdited != vcfBefore && int(b.machineForTests().frame().dac[bsrack::kVcfCv]) == vcfEdited,
	      "the knob's edit still sounding: not booted from the EEPROM");
	check(b.machineForTests().snapshot() == a->machineForTests().snapshot(), "and it runs on exactly as the unit it was saved from");

	// saved without its ROM (nothing ran): the session boots when the ROM is there
	BassStationProcessor none(nullptr);
	none.setRateAndBufferSizeDetails(kRate, kBlock);
	none.prepareToPlay(kRate, kBlock);
	run(none, kBlock * 4);
	juce::MemoryBlock bare;
	none.getStateInformation(bare);
	auto c = unit();
	c->setStateInformation(bare.getData(), int(bare.getSize()));
	run(*c, int(kRate * 1.0));
	const auto dc = c->display();
	check(dc.program && digitsLit(dc) >= 4, "a session saved without the ROM boots once the ROM is there");
}

void testSleep()
{
	std::printf("Sleep\n");
	auto p = unit();
	set(*p, "release", 0.0f);
	const auto out = run(*p, int(kRate * 4.0), note(48, 0.1, 0.5));
	check(p->asleep(), "asleep once the note has died away");
	bool zeros = true;
	for (int i = int(3.5 * kRate); i < int(4.0 * kRate); ++i)
		zeros = zeros && out[size_t(i)] == 0.0f;
	check(zeros, "with exact zeros");
	const auto again = run(*p, int(kRate * 0.3), note(48, 0.05, 0.4));
	check(!p->asleep(), "a note wakes it");
	check(rmsDb(again, int(0.15 * kRate), int(0.3 * kRate)) > -40, "and it sounds");
	check(finite(again), "finite after waking");
}

void testLong()
{
	std::printf("Long run\n");
	auto p = unit();
	Events ev;
	for (int i = 0; i < 40; ++i)
		for (auto &e : note(30 + (i * 7) % 40, i * 0.5, i * 0.5 + 0.3))
			ev.push_back(e);
	set(*p, "resonance", 1.0f);
	set(*p, "frequency", 0.9f);
	const auto out = run(*p, int(kRate * 22), ev);
	check(finite(out), "22 s of notes at full resonance: finite and in range");
	check(rmsDb(out, 0, int(out.size())) > -60, "and it sounds");
}

void testStress()
{
	std::printf("Random settings\n");
	std::mt19937 rng(1);
	std::uniform_real_distribution<float> u(0, 1);
	bool ok = true;
	for (int trial = 0; trial < 16 && ok; ++trial) {
		auto p = unit();
		for (auto *param : p->getParameters())
			param->setValueNotifyingHost(u(rng));
		Events ev;
		for (int k = 0; k < 6; ++k) {
			const int n = 24 + int(u(rng) * 60);
			const double on = u(rng) * 1.5;
			for (auto &e : note(n, on, on + 0.05 + u(rng)))
				ev.push_back(e);
		}
		ev.push_back({int(0.5 * kRate), juce::MidiMessage::pitchWheel(1, int(u(rng) * 16383))});
		ev.push_back({int(0.7 * kRate), juce::MidiMessage::controllerEvent(1, 1, int(u(rng) * 127))});
		ok = finite(run(*p, int(kRate * 2.5), ev));
	}
	check(ok, "16 random patches with notes, bends and the mod wheel stay finite");
}

// The brand marks a ROM folder has for the places panel.json lists (artwork/bsrack/<place>.svg),
// as the host finds them.
lemus::Artwork artworkFrom(const juce::File &folder)
{
	const auto v = juce::JSON::parse(juce::String::fromUTF8(BSRackPanel::panel_json, BSRackPanel::panel_jsonSize));
	lemus::ArtworkSpec spec;
	spec.name = "bsrack.svg";
	spec.plugin = "no-mono-bsr";
	for (const auto &l : *v["logos"].getArray())
		spec.places.push_back({l["place"].toString(), l["mark"].toString(), l["label"].toString(), float(double(l["box"][2])), float(double(l["box"][3]))});
	lemus::RomSet roms;
	lemus::romfolder::addArtwork(roms, spec, lemus::romfolder::findAllArtwork(folder, spec));
	return lemus::Artwork::load(roms, spec);
}

void testPanel(const juce::File &dir, lemus::Artwork artwork)
{
	std::printf("Panel\n");
	if (!artwork.marks().empty())
		std::printf("    artwork: %d marks\n", int(artwork.marks().size()));
	auto p = unit(std::move(artwork));
	{
		std::unique_ptr<juce::AudioProcessorEditor> ed(p->createEditor());
		check(ed != nullptr && ed->getWidth() > 0, "the editor opens");
	}
	bsrack::HardwareView v(*p);
	v.setBounds(0, 0, bsrack::HardwareView::kWidth, bsrack::HardwareView::kHeight);
	check(v.controlCount() == 44 && v.unmappedControls() == 0, "44 controls (18 knobs, 12 switches, 14 keys), each with its parameter or button");

	// the computer keys: the keypad by place, AUDITION and MODE by letter, Space left to the host
	check(v.typeForTests(juce::KeyPress('7')) && v.keyDown(bsrack::kKey7), "7 presses the keypad's 7");
	check(v.typeForTests(juce::KeyPress('a', {}, 'a')) && v.keyDown(bsrack::kAudition), "A presses AUDITION/DEMO");
	check(!v.typeForTests(juce::KeyPress(juce::KeyPress::spaceKey)), "Space goes on to the host");
	v.typeForTests(juce::KeyPress(juce::KeyPress::escapeKey));
	check(!v.keyDown(bsrack::kKey7) && !v.keyDown(bsrack::kAudition), "Escape lets go of them");
	run(*p, int(kRate * 0.3));

	dir.createDirectory();
	auto shot = [&](const char *name) {
		v.timerCallbackForTests();
		juce::Image img(juce::Image::ARGB, v.getWidth() * 2, v.getHeight() * 2, true);
		{
			juce::Graphics g(img);
			g.addTransform(juce::AffineTransform::scale(2.0f));
			v.paintEntireComponent(g, true);
		}
		const auto file = dir.getChildFile(name);
		file.deleteFile();
		juce::FileOutputStream out(file);
		juce::PNGImageFormat().writeImageToStream(img, out);
	};
	shot("panel.png"); // as it boots: program 00 shown, every switch where a new unit has it
	v.hoverForTests("frequency");
	shot("tip-frequency.png");
	v.hoverForTests("lfo_shape");
	shot("tip-lfo-shape.png");
	v.hoverForTests("audition");
	shot("tip-audition.png");
	v.hoverForTests("");
	// the display on another program: + three times
	for (int i = 0; i < 3; ++i) {
		p->setButton(bsrack::kKeyPlus, true);
		run(*p, int(kRate * 0.1));
		p->setButton(bsrack::kKeyPlus, false);
		run(*p, int(kRate * 0.2));
	}
	shot("program-03.png");
	// every switch in another position
	for (int s = 0; s < bsrack::kSwitches; ++s) {
		auto *param = p->params().getParameter(BassStationProcessor::switchId(s));
		const int n = bsrack::kSwitchPositions[size_t(s)];
		const int pos = juce::roundToInt(param->convertFrom0to1(param->getValue()));
		param->setValueNotifyingHost(param->convertTo0to1(float((pos + 1) % n)));
	}
	// and the knobs turned, so their pointers show
	for (int k = 0; k < bsrack::kPots; ++k)
		p->params().getParameter(BassStationProcessor::potId(k))->setValueNotifyingHost(float(k % 6) / 5.0f);
	run(*p, int(kRate * 0.3));
	shot("switches-moved.png");
	// MODE: MIDI RX CH, its LED and the channel
	p->setButton(bsrack::kMode, true);
	run(*p, int(kRate * 0.1));
	p->setButton(bsrack::kMode, false);
	run(*p, int(kRate * 0.3));
	shot("mode-midi-rx.png");
	{
		// the cost of a whole repaint at 1x (the timer repaints only the display and LEDs while they
		// alone change)
		juce::Image img(juce::Image::ARGB, v.getWidth(), v.getHeight(), true);
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		for (int i = 0; i < 20; ++i) {
			juce::Graphics g(img);
			v.paintEntireComponent(g, true);
		}
		std::printf("    a whole repaint: %.1f ms\n", (juce::Time::getMillisecondCounterHiRes() - t0) / 20);
	}
	v.showHelpForTests(true);
	shot("keys.png");
	v.showHelpForTests(false);
	// the rear: no cable while nothing is routed to the side-chain, one in EXTERNAL AUDIO INPUT once it is
	v.showRear(true);
	check(v.rearShown() && !p->filterInRouted(), "REAR turns it round, EXTERNAL AUDIO INPUT empty while nothing is routed");
	shot("rear.png");
	p->enableAllBuses();
	run(*p, kBlock * 2);
	check(p->filterInRouted(), "the side-chain routed: a cable in EXTERNAL AUDIO INPUT");
	shot("rear-cable.png");
	v.showRear(false);
	std::printf("  snapshots in %s\n", dir.getFullPathName().toRawUTF8());
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (!juce::File(BSRACK_ROM).loadFileAsData(g_rom)) {
		std::printf("No ROM at %s: skipped.\n", BSRACK_ROM);
		return 0;
	}
	const bool panel = argc > 1 && std::strcmp(argv[1], "--panel") == 0;
	testBoot();
	testMidi();
	testButtons();
	testSession();
	testWarmSession();
	testMidiOut();
	testFilterInput();
	testSleep();
	testLong();
	testStress();
	if (panel) {
		// --panel [DIR] [--artwork FOLDER]
		juce::String dirName = "bsrack-panel";
		lemus::Artwork artwork;
		for (int i = 2; i < argc; ++i) {
			if (std::strcmp(argv[i], "--artwork") == 0 && i + 1 < argc)
				artwork = artworkFrom(juce::File::getCurrentWorkingDirectory().getChildFile(argv[++i]));
			else
				dirName = argv[i];
		}
		testPanel(juce::File::getCurrentWorkingDirectory().getChildFile(dirName), std::move(artwork));
	}
	std::printf("\n%s\n", g_failures == 0 ? "All checks passed." : "Some checks FAILED.");
	return g_failures == 0 ? 0 : 1;
}
