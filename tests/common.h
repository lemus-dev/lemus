// Shared helpers for the headless host checks: each host's own (tests/<host>.cpp) and each
// machine's through its host (machines/<id>/tests/host.cpp). Inline, so every file of a test
// program shares one count of failures.
#pragma once

#include "host/Artwork.h"
#include "host/LemusEditor.h"
#include "host/LemusProcessor.h"
#include "host/Updates.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace lemus::test {

inline int g_failures = 0;

inline void check(bool ok, const juce::String &what)
{
	std::printf("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8());
	if (!ok)
		++g_failures;
}

constexpr double kRate = 48000.0;
constexpr int kBlock = 480;

// A riff on the low strings at about -12 dBFS, slightly different in each channel.
inline juce::AudioBuffer<float> testSignal(int samples)
{
	static const double notes[] = {82.41, 82.41, 98.0, 82.41, 110.0, 82.41, 123.47, 98.0};
	juce::AudioBuffer<float> b(2, samples);
	for (int i = 0; i < samples; ++i) {
		const double time = i / kRate, step = time / 0.25, t = (step - std::floor(step)) * 0.25;
		const double f = notes[int(step) % 8];
		double v = 0;
		for (int h = 1; h <= 6; ++h)
			v += std::sin(2 * juce::MathConstants<double>::pi * f * h * t) / (h * h);
		v *= 0.25 * std::exp(-t * 9);
		b.setSample(0, i, float(v));
		b.setSample(1, i, float(v * 0.9));
	}
	return b;
}

using MidiEvents = std::vector<std::pair<int, juce::MidiMessage>>; // sample position, message

inline juce::AudioBuffer<float> render(juce::AudioProcessor &p, const juce::AudioBuffer<float> &input, const MidiEvents &events = {})
{
	p.setRateAndBufferSizeDetails(kRate, kBlock);
	p.prepareToPlay(kRate, kBlock);
	juce::AudioBuffer<float> out(input);
	juce::MidiBuffer midi;
	for (int pos = 0; pos < out.getNumSamples(); pos += kBlock) {
		const int n = std::min(kBlock, out.getNumSamples() - pos);
		juce::AudioBuffer<float> view(out.getArrayOfWritePointers(), out.getNumChannels(), pos, n);
		for (const auto &[at, m] : events)
			if (at >= pos && at < pos + n)
				midi.addEvent(m, at - pos);
		const juce::ScopedLock sl(p.getCallbackLock());
		p.processBlock(view, midi);
		midi.clear();
	}
	p.releaseResources();
	return out;
}

inline int differences(const juce::AudioBuffer<float> &a, const juce::AudioBuffer<float> &b)
{
	if (a.getNumChannels() != b.getNumChannels() || a.getNumSamples() != b.getNumSamples())
		return -1;
	int n = 0;
	for (int ch = 0; ch < a.getNumChannels(); ++ch)
		for (int i = 0; i < a.getNumSamples(); ++i)
			n += a.getSample(ch, i) != b.getSample(ch, i) ? 1 : 0;
	return n;
}

inline float peak(const juce::AudioBuffer<float> &b)
{
	return b.getMagnitude(0, b.getNumSamples());
}

// Records what the host would be told.
struct HostSpy final : juce::AudioProcessorListener {
	struct Change {
		int index;
		float value;
	};
	std::vector<Change> changes;
	int gestureBegins = 0, gestureEnds = 0, infoChanges = 0;

	void audioProcessorParameterChanged(juce::AudioProcessor *, int index, float v) override { changes.push_back({index, v}); }
	void audioProcessorChanged(juce::AudioProcessor *, const ChangeDetails &d) override { infoChanges += d.parameterInfoChanged ? 1 : 0; }
	void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor *, int) override { ++gestureBegins; }
	void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor *, int) override { ++gestureEnds; }
};

inline void pump(int ms = 100)
{
	juce::MessageManager::getInstance()->runDispatchLoopUntil(ms);
}

inline void finish()
{
	std::printf("\n%s\n", g_failures == 0 ? "All checks passed." : (juce::String(g_failures) + " checks failed.").toRawUTF8());
}

// A ROM folder of our own, so the user's settings are never touched.
struct RomFolder {
	juce::File dir = juce::File::getSpecialLocation(juce::File::tempDirectory).getNonexistentChildFile("lemustest-roms", "");
	RomFolder()
	{
		dir.createDirectory();
		lemus::romfolder::setOverride(dir);
	}
	~RomFolder()
	{
		lemus::romfolder::setOverride({});
		dir.deleteRecursively();
	}
	void add(const juce::String &path, const juce::MemoryBlock &data) const
	{
		auto f = dir.getChildFile(path);
		f.getParentDirectory().createDirectory();
		f.replaceWithData(data.getData(), data.getSize());
	}
	void clear() const
	{
		dir.deleteRecursively();
		dir.createDirectory();
	}
};

// --snapshot <machine>|picker <png> [about|addrom|prompt|service] [noroms|noart] [2x]: the machine's
// editor in Lemus (or the machine picker), with the private ROM folder (for a look at the panel with
// its ROMs and artwork pack), an empty one, or the machine's ROMs without its artwork pack (noart: the
// panel as users get it, for screenshots), with the About box, the Add ROM pop-up, the first-start
// ROM prompt or the machine's SERVICE view open if asked, at twice the size if asked.
// LEMUS_SNAPSHOT_PARAMS sets machine parameters first: id=value pairs separated by commas, each
// value as the parameter shows it (mode=Pattern Write, tempo=120). LEMUS_SNAPSHOT_CLICKS clicks the
// editor before the shot: x,y points in the editor (at 1x) separated by semicolons, each with ",right"
// for a right-click (a held panel key, say). Returns the exit code.
inline juce::Button *findButton(juce::Component &c, const juce::String &text)
{
	if (auto *b = dynamic_cast<juce::Button *>(&c); b != nullptr && b->getButtonText().startsWith(text))
		return b;
	for (auto *child : c.getChildren())
		if (auto *b = findButton(*child, text))
			return b;
	return nullptr;
}

inline int snapshot(const juce::String &machine, const juce::String &png, const juce::String &open = {}, bool noRoms = false,
                    bool noArt = false, float scale = 1.0f)
{
	// LEMUS_SNAPSHOT_ROMS names the folder to use instead of a temporary one, so a screenshot can show
	// the real default path (run with HOME set to a scratch folder).
	const auto named = juce::SystemStats::getEnvironmentVariable("LEMUS_SNAPSHOT_ROMS", {});
	const auto empty = named.isNotEmpty() ? juce::File(named) : juce::File::createTempFile("lemus-roms");
	if (noRoms || noArt)
		empty.createDirectory();
	if (noArt) // the machine's ROMs, without its artwork pack
		for (const auto &f : juce::File(juce::String(LEMUS_PRIVATE_DIR) + "/roms/" + machine).findChildFiles(juce::File::findFiles, true))
			if (!f.hasFileExtension("svg"))
				f.copyFileTo(empty.getChildFile(f.getFileName()));
	lemus::romfolder::setOverride(noRoms || noArt ? empty : juce::File(juce::String(LEMUS_PRIVATE_DIR) + "/roms"));
	LemusProcessor p;
	if (machine != "picker" && !p.selectMachine(machine))
		return 1;
	lemus::setRomPromptAnswered(machine, open != "prompt"); // kept in memory: the ROM folder is overridden
	for (const auto &pair : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("LEMUS_SNAPSHOT_PARAMS", {}), ",", "")) {
		const auto id = pair.upToFirstOccurrenceOf("=", false, false).trim();
		bool found = false;
		if (auto *m = p.machineProcessor())
			for (auto *param : m->getParameters())
				if (auto *ranged = dynamic_cast<juce::RangedAudioParameter *>(param); ranged != nullptr && ranged->getParameterID() == id) {
					ranged->setValueNotifyingHost(ranged->getValueForText(pair.fromFirstOccurrenceOf("=", false, false).trim()));
					found = true;
				}
		if (!found) {
			std::fprintf(stderr, "LEMUS_SNAPSHOT_PARAMS: no parameter %s\n", id.toRawUTF8());
			return 1;
		}
	}
	std::unique_ptr<juce::AudioProcessorEditor> e(p.createEditorIfNeeded());
	pump(500);
	e->setVisible(true); // off screen, but found under a point
	for (const auto &click : juce::StringArray::fromTokens(juce::SystemStats::getEnvironmentVariable("LEMUS_SNAPSHOT_CLICKS", {}), ";", "")) {
		const auto parts = juce::StringArray::fromTokens(click, ",", "");
		const juce::Point<float> at(parts[0].getFloatValue(), parts[1].getFloatValue());
		auto *target = e->getComponentAt(at.toInt());
		if (target == nullptr)
			continue;
		const auto local = target->getLocalPoint(e.get(), at);
		const juce::ModifierKeys mods(parts[2].trim() == "right" ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier);
		const auto now = juce::Time::getCurrentTime();
		const juce::MouseEvent down(juce::Desktop::getInstance().getMainMouseSource(), local, mods, juce::MouseInputSource::defaultPressure, 0, 0, 0, 0,
		                            target, target, now, local, now, 1, false);
		target->mouseDown(down);
		target->mouseUp(down.withNewPosition(local));
		pump(50);
	}
	if (open == "about" || open == "addrom" || open == "service") {
		if (auto *b = findButton(*e, open == "about" ? "About" : open == "service" ? "SERVICE" : "Add "))
			b->triggerClick();
		pump(50); // a call-out box closes itself within 200 ms when the process isn't in front

	}
	const auto image = e->createComponentSnapshot(e->getLocalBounds(), true, scale);
	const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(png);
	out.deleteFile();
	juce::FileOutputStream stream(out);
	const bool ok = juce::PNGImageFormat().writeImageToStream(image, stream);
	if (named.isEmpty())
		empty.deleteRecursively(); // never a folder it was given
	return ok ? 0 : 1;
}

// The reference artwork pack for a machine, from the private ROM folder (empty if it isn't there).
inline juce::MemoryBlock privatePack(const juce::String &machine)
{
	juce::MemoryBlock data;
	juce::File(juce::String(LEMUS_PRIVATE_DIR) + "/roms/" + machine + "/" + machine + ".svg").loadFileAsData(data);
	return data;
}

// The driver names the machine's artwork pack, and the private pack, if there, holds the marks the
// panel looks for.
inline void checkPack(const lemus::MachineInfo &info, const juce::StringArray &marks)
{
	const auto &spec = info.artwork;
	check(spec.name == info.id + ".svg", info.id + ": the driver names its artwork pack " + info.id + ".svg");

	const auto data = privatePack(info.id);
	if (data.isEmpty()) {
		std::printf("      no %s in the private ROM folder: pack contents not checked\n", spec.name.toRawUTF8());
		return;
	}
	const auto artwork = lemus::Artwork::parse(&data);
	bool all = true;
	for (const auto &id : marks)
		all = all && artwork.find(id) != nullptr && !artwork.find(id)->outline.isEmpty();
	check(all && int(artwork.marks().size()) == marks.size(), info.id + ": it holds the marks " + marks.joinIntoString(", "));
}

// A file from the private ROM folder (empty if it isn't there), e.g. privateRom("tb303", "d650c-133.ic8").
inline juce::MemoryBlock privateRom(const juce::String &machine, const juce::String &file)
{
	juce::MemoryBlock data;
	juce::File(juce::String(LEMUS_PRIVATE_DIR) + "/roms/" + machine + "/" + file).loadFileAsData(data);
	return data;
}

// --save-state <machine> <file>: a session state with the machine loaded, from the private ROM
// folder (for checking the built plugins). Returns the exit code.
inline int saveState(const juce::String &machine, const juce::String &file)
{
	lemus::romfolder::setOverride(juce::File(juce::String(LEMUS_PRIVATE_DIR) + "/roms"));
	LemusProcessor p;
	juce::MemoryBlock state;
	if (p.selectMachine(machine))
		p.getStateInformation(state);
	lemus::romfolder::setOverride({});
	return state.getSize() > 0 && juce::File::getCurrentWorkingDirectory().getChildFile(file).replaceWithData(state.getData(), state.getSize()) ? 0 : 1;
}

// Each machine's checks through its host register themselves (machines/<id>/tests/host.cpp):
//   LEMUS_HOST_TEST(tb303) { ... }
// and the host's test program runs them in machine order (runHostTests).
struct HostTest {
	const char *machine;
	void (*run)();
};
inline std::vector<HostTest> &hostTests()
{
	static std::vector<HostTest> tests;
	return tests;
}
struct HostTestRegistration {
	HostTestRegistration(const char *machine, void (*run)()) { hostTests().push_back({machine, run}); }
};
inline void runHostTests()
{
	auto tests = hostTests();
	std::sort(tests.begin(), tests.end(), [](const HostTest &a, const HostTest &b) { return juce::String(a.machine) < juce::String(b.machine); });
	for (const auto &t : tests)
		t.run();
}

} // namespace lemus::test

using namespace lemus::test;

#define LEMUS_HOST_TEST(machine)                                                                         \
	static void lemusHostTest_##machine();                                                               \
	static const lemus::test::HostTestRegistration lemusHostTestRegistration_##machine{#machine,        \
	                                                                                   lemusHostTest_##machine}; \
	static void lemusHostTest_##machine()
