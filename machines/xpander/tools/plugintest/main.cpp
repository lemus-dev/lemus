// xpanderplugintest: the Xpander's processor headless. The first start (memory reset and TUNE ALL,
// cached), MIDI notes in tune, the panel (a press reaches the firmware), the session state.
// Needs the ROMs from the private folder. --site <dir> writes the website's panel picture. --panel <dir> also opens the editor, presses a key through
// it, and writes PNGs of it (1x, 2x, the rear, the key list) to <dir>.
#include "HardwareView.h"
#include "PluginProcessor.h"

#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
int g_fails = 0;
void check(bool ok, const juce::String &what)
{
	std::printf("%s %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
	g_fails += !ok;
}

juce::MemoryBlock rom(const char *name)
{
	juce::MemoryBlock mb;
	juce::File(XPANDER_ROM_DIR).getChildFile(name).loadFileAsData(mb);
	return mb;
}

constexpr double kRate = 48000;
constexpr int kBlock = 256;

struct Rig {
	juce::MemoryBlock r[6] = {rom("exp1.2-0.u8"), rom("exp1.2-1.u7"), rom("exp1.2-2.u6"), rom("exp1.2-3.u5"), rom("ca1.4-0.u912"), rom("ca1.4-1.u914")};
	juce::MemoryBlock bank; // the factory patches
	std::unique_ptr<XpanderProcessor> p;
	explicit Rig(bool patches = false)
	{
		if (patches)
			bank = rom("FACTORY1.SYX");
		p = std::make_unique<XpanderProcessor>(std::vector<const juce::MemoryBlock *>{&r[0], &r[1], &r[2], &r[3]},
		                                       std::vector<const juce::MemoryBlock *>{&r[4], &r[5]}, lemus::Artwork{},
		                                       bank.getSize() > 0 ? &bank : nullptr);
		p->setRateAndBufferSizeDetails(kRate, kBlock);
		p->prepareToPlay(kRate, kBlock);
	}
	// Runs `seconds`, MIDI at the start; returns L + R (the pan law keeps it the same for every voice).
	std::vector<float> run(double seconds, std::vector<juce::MidiMessage> midiAtStart = {})
	{
		std::vector<float> out;
		juce::AudioBuffer<float> buf(2, kBlock);
		const int blocks = int(seconds * kRate / kBlock);
		for (int b = 0; b < blocks; ++b) {
			juce::MidiBuffer midi;
			if (b == 0)
				for (const auto &m : midiAtStart)
					midi.addEvent(m, 0);
			buf.clear();
			p->processBlock(buf, midi);
			for (int i = 0; i < kBlock; ++i)
				out.push_back(buf.getSample(0, i) + buf.getSample(1, i));
		}
		return out;
	}
};

// The fundamental by counting upward zero crossings of a low-passed copy over the middle half.
double pitch(const std::vector<float> &x)
{
	std::vector<double> y(x.size());
	double s = 0, s2 = 0;
	for (size_t i = 0; i < x.size(); ++i) {
		s += (x[i] - s) * 0.05;
		s2 += (s - s2) * 0.05;
		y[i] = s2;
	}
	double mean = 0;
	for (size_t i = x.size() / 4; i < 3 * x.size() / 4; ++i)
		mean += y[i];
	mean /= double(x.size() / 2);
	double first = -1, last = -1;
	int n = 0;
	for (size_t i = x.size() / 4 + 1; i < 3 * x.size() / 4; ++i)
		if (y[i - 1] < mean && y[i] >= mean) {
			const double t = double(i - 1) + (mean - y[i - 1]) / (y[i] - y[i - 1]);
			if (first < 0)
				first = t;
			last = t;
			++n;
		}
	return n > 1 ? kRate * (n - 1) / (last - first) : 0;
}

// The editor: it opens at its size, a click on TUNE PAGE reaches the firmware, and its pictures.
void testPanel(Rig &rig, const juce::File &dir)
{
	std::printf("Panel\n");
	std::unique_ptr<juce::AudioProcessorEditor> ed(rig.p->createEditor());
	auto *view = ed != nullptr ? dynamic_cast<xpander::HardwareView *>(ed->getChildComponent(0)) : nullptr;
	check(ed != nullptr && view != nullptr && ed->getWidth() == xpander::HardwareView::kWidth && ed->getHeight() == xpander::HardwareView::kHeight,
	      "the editor opens at " + juce::String(xpander::HardwareView::kWidth) + " x " + juce::String(xpander::HardwareView::kHeight));
	if (view == nullptr)
		return;
	check(view->buttonCount() == 59, "it has the 59 keys (" + juce::String(view->buttonCount()) + ")");
	view->pressForTests("btn_tune", true);
	rig.run(0.1);
	view->timerCallbackForTests();
	view->pressForTests("btn_tune", false);
	rig.run(0.3);
	check(rig.p->displayText(0).find("TUNE") != std::string::npos, "TUNE PAGE pressed through the view opens the tune page");
	// Encoder 2 (DETUNE on the VCO1 page): five detents of a quick drag all count.
	view->pressForTests("btn_vco1", true);
	rig.run(0.1);
	view->pressForTests("btn_vco1", false);
	rig.run(0.3);
	auto detune = [&] { return juce::String(rig.p->displayText(2)).substring(8, 14).removeCharacters("+").trim().getIntValue(); };
	const int before = detune();
	auto turn = [&](int detents) {
		view->turnForTests(1, detents);
		for (int n = 0; n < std::abs(detents) + 2; ++n) {
			rig.run(0.02); // the view's pacer
			view->drainForTests();
		}
		rig.run(0.3);
	};
	turn(5);
	const int up = detune();
	turn(-5);
	check(up == before + 5 && detune() == before, "five detents of encoder 2 move DETUNE by five and back (" + juce::String(before) + ", " + juce::String(up) + ", " + juce::String(detune()) + ")");
	view->pressForTests("btn_vco1", true); // held for the picture
	rig.run(0.2);
	view->timerCallbackForTests();
	std::printf("     VFD1 %s\n     VFD2 %s\n     VFD3 %s\n", rig.p->displayText(0).c_str(), rig.p->displayText(1).c_str(), rig.p->displayText(2).c_str());

	dir.createDirectory();
	auto shoot = [&](const char *name, float scale) {
		const auto t0 = juce::Time::getMillisecondCounterHiRes();
		const auto image = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
		const auto t1 = juce::Time::getMillisecondCounterHiRes();
		ed->createComponentSnapshot(ed->getLocalBounds(), true, scale); // the panel image is cached now
		const auto t2 = juce::Time::getMillisecondCounterHiRes();
		const auto file = dir.getChildFile(name);
		file.deleteFile();
		juce::FileOutputStream out(file);
		const bool ok = out.openedOk() && juce::PNGImageFormat().writeImageToStream(image, out);
		check(ok, juce::String(name) + ": first paint " + juce::String(t1 - t0, 0) + " ms, then " + juce::String(t2 - t1, 1) + " ms a frame");
	};
	shoot("xpander-panel-1x.png", 1.0f);
	shoot("xpander-panel-2x.png", 2.0f);
	view->pressForTests("btn_vco1", false);
	rig.run(0.2);
	view->showRear(true);
	shoot("xpander-rear-1x.png", 1.0f);
	shoot("xpander-rear-2x.png", 2.0f);
	view->showRear(false);
	view->showHelp(true);
	shoot("xpander-keys-1x.png", 1.0f);
	view->showHelp(false);

	// The artwork places, filled from a pack of plain boxes where the marks go (Lemus ships none).
	const juce::String pack = "<svg xmlns=\"http://www.w3.org/2000/svg\">"
	                          "<rect id=\"oberheim\" x=\"54.65\" y=\"242.05\" width=\"59.2\" height=\"10.5\"/>"
	                          "<rect id=\"xpander\" x=\"54.55\" y=\"254.55\" width=\"78.8\" height=\"20.3\"/></svg>";
	const juce::MemoryBlock packData(pack.toRawUTF8(), pack.getNumBytesAsUTF8());
	rig.p->setArtwork(lemus::Artwork::parse(&packData));
	ed.reset(rig.p->createEditor());
	view = dynamic_cast<xpander::HardwareView *>(ed->getChildComponent(0));
	shoot("xpander-places-1x.png", 1.0f);
	view->showRear(true);
	shoot("xpander-places-rear-1x.png", 1.0f);
	ed.reset();
	rig.p->setArtwork({});
}

} // namespace

// --state RAM OUT: a plugin state (as Lemus saves it, for lemus-render and Lemus AB scenes) holding
// a battery RAM written by xpanderrender's saveram: a unit set up on its panel.
int makeState(const char *ramPath, const char *outPath)
{
	juce::MemoryBlock ram;
	if (!juce::File(ramPath).loadFileAsData(ram) || ram.getSize() != size_t(xpander::MainBoard::kRamSize)) {
		std::fprintf(stderr, "can't read a %d-byte RAM from %s\n", xpander::MainBoard::kRamSize, ramPath);
		return 1;
	}
	Rig rig;
	auto state = rig.p->params().copyState();
	state.setProperty("model", XpanderProcessor::kModelVersion, nullptr);
	state.setProperty("protect", false, nullptr);
	state.setProperty("power", true, nullptr);
	state.setProperty("ram", ram.toBase64Encoding(), nullptr);
	juce::MemoryBlock inner;
	if (auto xml = state.createXml())
		juce::AudioProcessor::copyXmlToBinary(*xml, inner);
	// Lemus's own wrapper (host/LemusProcessor.cpp): the machine and its state.
	juce::ValueTree outer("LEMUS");
	outer.setProperty("version", 1, nullptr);
	outer.setProperty("savedBy", "xpanderplugintest", nullptr);
	outer.setProperty("machine", "xpander", nullptr);
	outer.setProperty("state", inner, nullptr);
	juce::MemoryBlock out;
	juce::MemoryOutputStream stream(out, false);
	outer.writeToStream(stream);
	stream.flush();
	return juce::File(outPath).replaceWithData(out.getData(), out.getSize()) ? 0 : 1;
}

// --site DIR: the website's picture, with the factory patches (FACTORY1.SYX) and patch 23 on the
// display, the VCO1 page held; the brand places blank as Lemus ships them.
static int sitePicture(const juce::File &dir)
{
	Rig rig(true);
	if (!rig.p->factoryPatches()) {
		std::printf("no FACTORY1.SYX in the ROM folder\n");
		return 1;
	}
	rig.p->waitForWarmUp();
	rig.run(0.3);
	for (int digit : {2, 3}) { // the keypad: 2, 3 (switches_0 bits 2, 3)
		rig.p->setSwitch(0, digit, true);
		rig.run(0.1);
		rig.p->setSwitch(0, digit, false);
		rig.run(0.4);
	}
	std::unique_ptr<juce::AudioProcessorEditor> ed(rig.p->createEditor());
	auto *view = ed != nullptr ? dynamic_cast<xpander::HardwareView *>(ed->getChildComponent(0)) : nullptr;
	if (view == nullptr)
		return 1;
	view->pressForTests("btn_vco1", true);
	rig.run(0.3);
	view->timerCallbackForTests();
	std::printf("VFD1 %s\nVFD2 %s\nVFD3 %s\n", rig.p->displayText(0).c_str(), rig.p->displayText(1).c_str(), rig.p->displayText(2).c_str());
	dir.createDirectory();
	for (float scale : {1.0f, 2.0f}) {
		const auto image = ed->createComponentSnapshot(ed->getLocalBounds(), true, scale);
		const auto file = dir.getChildFile("xpander-site-" + juce::String(int(scale)) + "x.png");
		file.deleteFile();
		juce::FileOutputStream out(file);
		if (!out.openedOk() || !juce::PNGImageFormat().writeImageToStream(image, out))
			return 1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	if (argc == 4 && std::strcmp(argv[1], "--state") == 0)
		return makeState(argv[2], argv[3]);
	if (argc == 3 && std::strcmp(argv[1], "--site") == 0)
		return sitePicture(juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));
	Rig rig;
	check(rig.p->romsLoaded(), "the six ROMs load");
	const bool fresh = rig.p->warmingUp();
	std::printf("     %s\n", fresh ? "first start: the unit resets and tunes itself (cached afterwards)" : "factory state from the cache");
	rig.p->waitForWarmUp();
	rig.run(0.2);
	std::printf("     VFD3 %s\n", rig.p->displayText(2).c_str());
	check(rig.p->displayText(0).find("BERHEIM") != std::string::npos, "it starts on the single patch page: " + juce::String(rig.p->displayText(0)));

	const auto c4 = rig.run(1.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8(100))});
	rig.run(1.0, {juce::MidiMessage::noteOff(1, 60)});
	float pk = 0;
	for (auto v : c4)
		pk = std::max(pk, std::fabs(v));
	check(pk > 0.001f, "MIDI C4 sounds (peak " + juce::String(20 * std::log10(std::max(pk, 1e-9f)), 1) + " dBFS)");
	const double f = pitch(c4);
	const double cents = 1200 * std::log2(f / 261.6256);
	check(std::fabs(cents) < 10, "C4 in tune after the factory tune: " + juce::String(f, 2) + " Hz (" + juce::String(cents, 1) + " cents)");

	// Sleep: after the release, every voice settles and the outputs are exact zeros; a note wakes it.
	const auto tail = rig.run(2.0);
	bool zeros = true;
	for (size_t i = tail.size() - size_t(0.5 * kRate); i < tail.size(); ++i)
		zeros = zeros && tail[i] == 0.0f;
	check(rig.p->asleep() && zeros, "released, it sleeps: exact zeros");
	const auto again = rig.run(1.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8(100))});
	rig.run(1.0, {juce::MidiMessage::noteOff(1, 60)});
	check(!rig.p->asleep() || std::fabs(1200 * std::log2(pitch(again) / 261.6256)) < 10, "a note wakes it, in tune");
	float pk2 = 0;
	for (auto v : again)
		pk2 = std::max(pk2, std::fabs(v));
	// The next voice plays it: the voices' levels differ by up to 0.83 dB (machine.cpp kVoiceLevelDb).
	check(std::fabs(20 * std::log10(pk2 / pk)) < 1.3, "and as loud as before, within the voices' spread (" + juce::String(20 * std::log10(pk2 / pk), 2) + " dB)");

	// TUNE on the panel: the firmware answers with its page.
	rig.p->setSwitch(1, 6, true);
	rig.run(0.1);
	rig.p->setSwitch(1, 6, false);
	rig.run(0.3);
	check(rig.p->displayText(0).find("TUNE") != std::string::npos, "TUNE on the panel opens the tune page");
	// Out again with VCO1 (SINGLE would start a patch number).
	rig.p->setSwitch(3, 6, true);
	rig.run(0.1);
	rig.p->setSwitch(3, 6, false);
	rig.run(0.3);

	juce::MemoryBlock state;
	rig.p->getStateInformation(state);
	Rig other;
	other.p->waitForWarmUp();
	const auto saved = rig.p->ram();
	other.p->setStateInformation(state.getData(), int(state.getSize()));
	// The firmware's start rewrites its working variables; the patches and settings stay.
	const auto back = other.p->ram();
	int differ = 0;
	for (size_t i = 0; i < saved.size() && i < back.size(); ++i)
		differ += saved[i] != back[i];
	check(back.size() == saved.size() && differ < 64, "the session state brings back the battery RAM (" + juce::String(differ) + " working bytes rewritten at start)");
	other.run(0.5);
	check(other.p->displayText(0).find("BERHEIM") != std::string::npos, "and the unit starts from it");

	// With Oberheim's factory patches (FACTORY1.SYX, optional): a new unit has them in battery RAM,
	// received through MIDI IN, and starts on patch 00. (The firmware marks the buffer edited at
	// every start: the dot after the number, read here as '#'.)
	{
		Rig factory(true);
		if (!factory.p->factoryPatches())
			std::printf("     (no FACTORY1.SYX in the ROM folder: factory patches not checked)\n");
		else {
			factory.p->waitForWarmUp();
			factory.run(0.2);
			const auto shown = factory.p->displayText(0);
			check(shown.rfind("S0", 0) == 0 && shown.find("SYNC0M 8") == 4, "with the factory patches it starts on patch 00, SYNCOM 8: " + juce::String(shown));
			factory.p->setSwitch(1, 2, true); // + : patch 01
			factory.run(0.1);
			factory.p->setSwitch(1, 2, false);
			factory.run(0.3);
			check(factory.p->displayText(0).find("B0XBRASS") == 4, "and patch 01 is BOXBRASS: " + juce::String(factory.p->displayText(0)));
			const auto note = factory.run(1.0, {juce::MidiMessage::noteOn(1, 60, juce::uint8(100))});
			factory.run(0.5, {juce::MidiMessage::noteOff(1, 60)});
			float pk = 0;
			for (auto v : note)
				pk = std::max(pk, std::fabs(v));
			check(pk > 0.001f, "which plays (peak " + juce::String(20 * std::log10(std::max(pk, 1e-9f)), 1) + " dBFS)");
		}
	}

	if (argc > 2 && std::strcmp(argv[1], "--panel") == 0)
		testPanel(rig, juce::File::getCurrentWorkingDirectory().getChildFile(argv[2]));

	std::printf("\n%s\n", g_fails == 0 ? "all passed" : "FAILURES");
	return g_fails == 0 ? 0 : 1;
}
