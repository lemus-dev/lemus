// Headless test of the MS-20's processor: MIDI plays it, settings at random never blow up, long runs
// stay finite, the session keeps the controls, the cables and the wheel and loads settled, the values
// read as printed, it sleeps with exact zeros and wakes sounding the same, POWER (a boot from cold, a
// run-down to exact zeros, a session load neither), the panel (--panel).
#include "HardwareView.h"
#include "PluginProcessor.h"

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

using Events = std::vector<std::pair<int, juce::MidiMessage>>;

std::vector<float> run(Ms20Processor &p, int samples, const Events &events = {})
{
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
		for (int i = 0; i < len; ++i)
			out[size_t(at + i)] = block.getSample(0, i);
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

void prepare(Ms20Processor &p)
{
	p.setRateAndBufferSizeDetails(kRate, kBlock);
	p.prepareToPlay(kRate, kBlock);
}

void set(Ms20Processor &p, const char *id, float plain)
{
	auto *param = p.params().getParameter(id);
	param->setValueNotifyingHost(param->convertTo0to1(plain));
}

Events note(int n, double on, double off)
{
	return {{int(on * kRate), juce::MidiMessage::noteOn(1, n, juce::uint8(100))}, {int(off * kRate), juce::MidiMessage::noteOff(1, n)}};
}

void testMidi()
{
	std::printf("MIDI\n");
	Ms20Processor p;
	prepare(p);
	const auto out = run(p, int(1.0 * kRate), note(48, 0.1, 0.5));
	check(rmsDb(out, int(0.2 * kRate), int(0.4 * kRate)) > -40, "a note sounds");
	check(rmsDb(out, 0, int(0.09 * kRate)) < -120, "and nothing before it");
	check(finite(out), "finite");
}

void testStress()
{
	std::printf("Stress\n");
	Ms20Processor p;
	prepare(p);
	std::mt19937 rng(20);
	std::uniform_real_distribution<float> u(0, 1);
	bool ok = true;
	for (int round = 0; round < 40; ++round) {
		for (auto *param : p.getParameters())
			param->setValueNotifyingHost(u(rng));
		ok = ok && finite(run(p, int(0.25 * kRate), note(36 + int(u(rng) * 36), 0.0, 0.2)));
	}
	check(ok, "40 rounds of random settings stay finite and in range");
	ok = finite(run(p, int(20 * kRate), note(60, 0.0, 19.0)));
	check(ok, "a 20 s note stays finite");
}

void testSession()
{
	std::printf("Session\n");
	Ms20Processor a;
	set(a, "lpf_cutoff", 3.5f);
	set(a, "vco2_pitch", -4.0f);
	ms20::Patch cables;
	cables.connect(ms20::Jack::LpfExtCutoff, int(ms20::Jack::MgTriangle));
	a.setPatch(cables);
	juce::MemoryBlock state;
	a.getStateInformation(state);
	Ms20Processor b;
	b.setStateInformation(state.getData(), int(state.getSize()));
	check(std::abs(b.params().getRawParameterValue("lpf_cutoff")->load() - 3.5f) < 1e-4f, "the controls come back");
	check(std::abs(b.params().getRawParameterValue("vco2_pitch")->load() + 4.0f) < 1e-4f, "a centre-zero knob too");
	check(b.patch() == cables, "the cables come back");
	check(b.modelVersion() == Ms20Processor::kModelVersion, "with the model version");

	// the control wheel has no spring: it stays where it was left
	a.setWheel(0.5);
	a.getStateInformation(state);
	Ms20Processor w;
	w.setStateInformation(state.getData(), int(state.getSize()));
	check(std::abs(w.manual().wheel - 0.5) < 1e-9, "the control wheel's place comes back");
	auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	xml->removeAttribute("wheel");
	juce::MemoryBlock old;
	juce::AudioProcessor::copyXmlToBinary(*xml, old);
	w.setWheel(-0.3);
	w.setStateInformation(old.getData(), int(old.getSize()));
	check(w.manual().wheel == 0.0, "a session from before it was saved: the wheel at its centre");

	// the values as the panel prints them
	auto text = [&](const char *id, float plain) {
		auto *param = a.params().getParameter(id);
		return param->getText(param->convertTo0to1(plain), 32);
	};
	check(text("vco2_pitch", -4.0f) == "-2.0" && text("master_tune", 3.0f) == "+1.5" && text("master_tune", 0.0f) == "0.0",
	      "VCO 2 PITCH and MASTER TUNE read -5 to +5, as printed");
	auto *tune = a.params().getParameter("master_tune");
	check(std::abs(tune->convertFrom0to1(tune->getValueForText("+2.5")) - 5.0f) < 1e-4f &&
	          std::abs(tune->convertFrom0to1(tune->getValueForText("-1")) + 2.0f) < 1e-4f,
	      "and type in the same way");
	auto *volume = a.params().getParameter("volume");
	check(text("volume", 0.2f) == "POWER OFF" && text("volume", 4.0f) == "4.0" && volume->getValueForText("POWER OFF") == 0.0f,
	      "VOLUME's counter-clockwise click reads POWER OFF");
}

float peakOf(const std::vector<float> &x, int from, int to)
{
	float m = 0;
	for (int i = from; i < to; ++i)
		m = std::max(m, std::fabs(x[size_t(i)]));
	return m;
}

// A session loads warm: the circuit settled at its controls and cables, as a plugin load is, not
// stepping there from the defaults' operating point in the first blocks. KBD TRIG OUT's idle +5 V
// into VCA INITIAL GAIN holds the VCA open, so a step would be heard (it was a 0.7 peak thump).
void testSessionWarm()
{
	std::printf("Session load\n");
	Ms20Processor a;
	set(a, "lpf_cutoff", 2.0f);
	set(a, "hpf_cutoff", 6.0f);
	set(a, "lpf_peak", 8.0f);
	set(a, "vco2_pitch", -4.0f);
	set(a, "master_tune", 3.0f);
	set(a, "portamento", 5.0f);
	set(a, "volume", 9.0f);
	ms20::Patch cables;
	cables.connect(ms20::Jack::LpfExtCutoff, int(ms20::Jack::MgTriangle));
	cables.connect(ms20::Jack::InitialGain, int(ms20::Jack::KbdTrigOut));
	a.setPatch(cables);
	juce::MemoryBlock state;
	a.getStateInformation(state);
	// the reference: the machine handed the session's settings before it is prepared (a plugin load)
	Ms20Processor r;
	r.setStateInformation(state.getData(), int(state.getSize()));
	r.machineForTests().setControls(r.controls());
	r.machineForTests().setPatch(r.patch());
	prepare(r);
	const auto ref = run(r, int(0.5 * kRate));
	// the host's order for a new instance: the session, then prepare
	Ms20Processor c;
	c.setStateInformation(state.getData(), int(state.getSize()));
	prepare(c);
	check(run(c, int(0.5 * kRate)) == ref, "loaded before prepare: the same as a plugin load at its settings, bit for bit");
	// into one that is running at the defaults
	Ms20Processor b;
	prepare(b);
	run(b, int(1.0 * kRate));
	b.setStateInformation(state.getData(), int(state.getSize()));
	const auto into = run(b, int(0.5 * kRate));
	const float pi = peakOf(into, 0, int(0.2 * kRate)), pr = peakOf(ref, 0, int(0.2 * kRate));
	std::printf("        first 0.2 s peak %.4f (a plugin load's %.4f)\n", double(pi), double(pr));
	check(pi <= pr * 1.1f && std::abs(rmsDb(into, int(0.4 * kRate), int(0.5 * kRate)) - rmsDb(ref, int(0.4 * kRate), int(0.5 * kRate))) < 1,
	      "loaded into a running one: no thump, settled as a plugin load is");
}

void testSleep()
{
	std::printf("Sleep\n");
	Ms20Processor p;
	prepare(p);
	set(p, "eg2_release", 0.0f);
	// after the note, SIGNAL OUT's coupling lets a few microvolts of the VCA's step go over about
	// 1.5 s: the machine sleeps once that is under -120 dB for 100 ms
	const auto first = run(p, int(3.0 * kRate), note(48, 0.0, 0.3));
	check(p.asleep(), "asleep within 3 s of the note");
	bool zeros = true;
	for (int i = int(2.9 * kRate); i < int(3.0 * kRate); ++i)
		zeros = zeros && first[size_t(i)] == 0.0f;
	check(zeros, "with exact zeros");
	const auto again = run(p, int(1.0 * kRate), note(48, 0.0, 0.3));
	check(!p.asleep() && rmsDb(again, int(0.1 * kRate), int(0.3 * kRate)) > -40, "a key wakes it");
	// the VCOs and the MG run free: after a sleep they take up where they stopped, so the note is
	// the same note at another phase
	const double a = rmsDb(first, int(0.1 * kRate), int(0.3 * kRate)), b = rmsDb(again, int(0.1 * kRate), int(0.3 * kRate));
	check(std::abs(a - b) < 0.5, "and it sounds as it did (level within 0.5 dB)");
}

double maxDiff(const std::vector<float> &a, const std::vector<float> &b, int from, int to)
{
	double m = 0;
	for (int i = from; i < to; ++i)
		m = std::max(m, double(std::fabs(a[size_t(i)] - b[size_t(i)])));
	return m;
}

bool zerosFrom(const std::vector<float> &x, int from)
{
	for (size_t i = size_t(from); i < x.size(); ++i)
		if (x[i] != 0.0f)
			return false;
	return true;
}

// POWER (VOLUME's click) is a switch-on from cold and a run-down (core/machine.h: switchOn); a
// plugin or session load is neither: warm, settled at its settings.
void testPower()
{
	std::printf("Power\n");
	// off with a note sounding: it runs down with the rails, then exact zeros whatever is played
	Ms20Processor p;
	prepare(p);
	run(p, int(0.3 * kRate), {{0, juce::MidiMessage::noteOn(1, 60, juce::uint8(100))}});
	p.setPowered(false);
	const auto down = run(p, int(1.0 * kRate), {{int(0.5 * kRate), juce::MidiMessage::noteOff(1, 60)}, {int(0.6 * kRate), juce::MidiMessage::noteOn(1, 48, juce::uint8(100))}});
	int last = 0;
	for (int i = 0; i < int(down.size()); ++i)
		if (down[size_t(i)] != 0.0f)
			last = i;
	std::printf("        run-down: sounding %.1f dB in its first 10 ms, last non-zero sample at %.3f s\n", rmsDb(down, 0, int(0.01 * kRate)),
	            last / kRate);
	check(rmsDb(down, 0, int(0.01 * kRate)) > -40 && rmsDb(down, int(0.05 * kRate), int(0.1 * kRate)) < rmsDb(down, 0, int(0.01 * kRate)) - 10,
	      "POWER off: the note runs down with the rails");
	check(zerosFrom(down, int(0.4 * kRate)) && finite(down), "then exact zeros, whatever is played");
	check(!p.machineForTests().powered(), "and the machine is off");
	run(p, int(0.2 * kRate), {{0, juce::MidiMessage::noteOff(1, 48)}}); // the key let go

	// on again: a boot from cold, beside a warm start (a new instance at the same settings)
	p.setPowered(true);
	const auto boot = run(p, int(2.5 * kRate), {{int(2.0 * kRate), juce::MidiMessage::noteOn(1, 48, juce::uint8(100))}});
	Ms20Processor w;
	prepare(w);
	const auto warm = run(w, int(2.5 * kRate), {{int(2.0 * kRate), juce::MidiMessage::noteOn(1, 48, juce::uint8(100))}});
	const float bootPeak = peakOf(boot, 0, int(0.5 * kRate));
	std::printf("        boot: peak %.4f in its first 0.5 s (a warm start's %.4f), %.4f from 0.5 s to 2 s\n", double(bootPeak),
	            double(peakOf(warm, 0, int(0.5 * kRate))), maxDiff(boot, warm, int(0.5 * kRate), int(2.0 * kRate)));
	check(maxDiff(boot, warm, 0, int(0.001 * kRate)) > 0 || maxDiff(boot, warm, 0, int(0.05 * kRate)) > 1e-3,
	      "POWER on: a boot from cold, not a warm start");
	check(bootPeak < 2 && finite(boot), "its transient stays in range");
	check(maxDiff(boot, warm, int(0.5 * kRate), int(2.0 * kRate)) < 1e-3, "and comes within 1 mV of a warm start's by 0.5 s");
	const double nb = rmsDb(boot, int(2.1 * kRate), int(2.4 * kRate)), nw = rmsDb(warm, int(2.1 * kRate), int(2.4 * kRate));
	std::printf("        a note 2 s after: %.2f dB (warm %.2f)\n", nb, nw);
	check(nb > -40 && std::abs(nb - nw) < 0.5, "a note 2 s after the switch sounds as on a warm start (level within 0.5 dB)");
	const auto settled = run(p, int(4.0 * kRate), {{0, juce::MidiMessage::noteOff(1, 48)}});
	check(p.asleep() && zerosFrom(settled, int(3.9 * kRate)), "booted and settled, it sleeps with exact zeros");
	const auto st = p.display().solver;
	check(st.failures == 0 && st.dcFailures == 0, "every solve converged through the run-down and the boot");

	// a session load is neither: into one that is off, a session saved on loads warm, settled
	Ms20Processor a;
	set(a, "lpf_cutoff", 4.0f);
	juce::MemoryBlock on;
	a.getStateInformation(on);
	Ms20Processor r;
	r.setStateInformation(on.getData(), int(on.getSize()));
	prepare(r);
	const auto ref = run(r, int(0.5 * kRate));
	Ms20Processor b;
	prepare(b);
	b.setPowered(false);
	run(b, int(0.5 * kRate));
	b.setStateInformation(on.getData(), int(on.getSize()));
	const auto loaded = run(b, int(0.5 * kRate));
	check(b.machineForTests().powered() && maxDiff(loaded, ref, 0, int(0.5 * kRate)) < 1e-4,
	      "a session saved on, loaded into one that is off: on and settled as a plugin load, no boot");
	// and into one that is on, a session saved off: off at once, no run-down
	juce::MemoryBlock off;
	set(a, "volume", 0.0f);
	a.setPowered(false);
	a.getStateInformation(off);
	b.setStateInformation(off.getData(), int(off.getSize()));
	const auto gone = run(b, int(0.2 * kRate), note(48, 0.0, 0.1));
	check(!b.powered() && zerosFrom(gone, 0), "a session saved off: off at once, exact zeros, no run-down");
	// turned out of the click again: a switch-on, from cold
	set(b, "volume", 5.0f);
	const auto again = run(b, int(0.1 * kRate));
	check(b.powered() && b.machineForTests().powered() && peakOf(again, 0, int(0.1 * kRate)) > 0, "VOLUME out of its click: on, from cold");
}

} // namespace

// The filter board (Rev 1 / Rev 2): switched from the bar, it rebuilds at the next block and plays;
// it comes back with the session; a session from before it was saved is Rev 1.
void testRev2()
{
	std::printf("Filter board\n");
	Ms20Processor p;
	prepare(p);
	set(p, "eg2_sustain", 10.0f);
	run(p, int(0.3 * kRate));
	p.setRev2(true);
	const auto out = run(p, int(1.0 * kRate), note(60, 0.1, 0.9));
	check(p.machineForTests().filterRev() == ms20::Machine::FilterRev::Rev2, "REV 2 from the bar: the machine takes it at the next block");
	check(finite(out) && rmsDb(out, int(0.3 * kRate), int(0.8 * kRate)) > -50, "and plays a note");
	juce::MemoryBlock state;
	p.getStateInformation(state);
	Ms20Processor b;
	b.setStateInformation(state.getData(), int(state.getSize()));
	check(b.rev2() && b.machineForTests().filterRev() == ms20::Machine::FilterRev::Rev2, "it comes back with the session");
	auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
	xml->removeAttribute("filterRev");
	juce::MemoryBlock old;
	juce::AudioProcessor::copyXmlToBinary(*xml, old);
	b.setStateInformation(old.getData(), int(old.getSize()));
	check(!b.rev2() && b.machineForTests().filterRev() == ms20::Machine::FilterRev::Rev1, "a session from before it was saved: REV 1");
}

// Each instance its own unit: two instances' hiss differs; with fixed seeds (the tools) it repeats.
void testSeeds()
{
	std::printf("Seeds\n");
	auto hiss = [](bool fixed) {
		Ms20Processor::fixedSeeds(fixed);
		Ms20Processor p;
		prepare(p);
		set(p, "vco1_wave", 3.0f); // WHITE NOISE
		set(p, "eg2_sustain", 10.0f);
		auto out = run(p, int(0.3 * kRate), note(60, 0.05, 0.28));
		Ms20Processor::fixedSeeds(true);
		return out;
	};
	const auto a = hiss(false), b = hiss(false), c = hiss(true), d = hiss(true);
	auto same = [](const std::vector<float> &x, const std::vector<float> &y) { return x == y; };
	check(!same(a, b) && rmsDb(a, int(0.1 * kRate), int(0.25 * kRate)) > -60, "two instances hiss and wander apart");
	check(same(c, d), "fixed seeds (the tools): the same every time");
}

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI gui;
	Ms20Processor::fixedSeeds(true); // (renders repeat; testSeeds turns it off for its own)
	const bool panel = argc > 1 && std::strcmp(argv[1], "--panel") == 0;
	testMidi();
	testStress();
	testSession();
	testSessionWarm();
	testSleep();
	testPower();
	testRev2();
	testSeeds();
	if (panel) {
		std::printf("Panel\n");
		Ms20Processor p;
		ms20::Patch cables;
		cables.connect(ms20::Jack::LpfExtCutoff, int(ms20::Jack::MgTriangle));
		cables.connect(ms20::Jack::EspSignalIn, ms20::kHostInput);
		cables.connect(ms20::Jack::TotalExtMod, int(ms20::Jack::Eg1Out));
		p.setPatch(cables);
		std::unique_ptr<juce::AudioProcessorEditor> ed(p.createEditor());
		check(ed != nullptr && ed->getWidth() > 0, "the editor opens");
		auto *view = ed != nullptr ? dynamic_cast<ms20::HardwareView *>(ed->getChildComponent(0)) : nullptr;
		check(view != nullptr && view->controlCount() == 36 && view->unmappedControls() == 0, "36 controls, each on its parameter");
		check(view != nullptr && view->jackCount() == 35, "35 jacks");
		if (view != nullptr) {
			// the mouse, as the panel gets it
			auto event = [&](juce::Point<float> at, juce::ModifierKeys mods, juce::Point<float> down) {
				return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), at, mods, juce::MouseInputSource::defaultPressure, 0.0f,
				                        0.0f, 0.0f, 0.0f, view, view, juce::Time::getCurrentTime(), down, juce::Time::getCurrentTime(), 1, false);
			};
			auto click = [&](const juce::String &part, juce::ModifierKeys mods = {}) {
				const auto at = view->placeForTests(part);
				view->mouseDown(event(at, mods, at));
				view->mouseUp(event(at, mods, at));
			};
			auto choice = [&](const char *id) { return dynamic_cast<juce::AudioParameterChoice *>(p.params().getParameter(id))->getIndex(); };
			const auto shift = juce::ModifierKeys(juce::ModifierKeys::shiftModifier | juce::ModifierKeys::leftButtonModifier);
			const auto right = juce::ModifierKeys(juce::ModifierKeys::rightButtonModifier);
			click("vco1_scale");
			const int a = choice("vco1_scale");
			click("vco1_scale");
			const int b = choice("vco1_scale");
			click("vco1_scale", shift);
			check(a == 3 && b == 0 && choice("vco1_scale") == 3, "a rotary switch: a click steps it clockwise and wraps, shift-click steps back");
			const auto c = view->placeForTests("vco1_scale"), up = c + juce::Point<float>(-40, -40);
			view->mouseDown(event(c, {}, c));
			view->mouseDrag(event(up, {}, c));
			view->mouseUp(event(up, {}, c));
			check(choice("vco1_scale") == 0, "and a drag round picks a position");
			auto in = [&] {
				const auto q = p.patch();
				return q.from[size_t(ms20::Jack::EspSignalIn)] == ms20::kHostInput ? 1 : q.from[size_t(ms20::Jack::ExtSigIn)] == ms20::kHostInput ? 2 : 0;
			};
			const int i0 = in();
			if (i0 != 0)
				click(i0 == 1 ? "jack:espin" : "jack:extsig"); // pull it out first
			const int iNone = in();
			click("jack:espin");
			const int i1 = in();
			click("jack:extsig");
			const int i2 = in();
			click("jack:extsig");
			check(iNone == 0 && i1 == 1 && i2 == 2 && in() == 0, "the host's audio: a click on ESP or EXT SIGNAL IN plugs it in there (out of the other), a click pulls it");
			click("service");
			click("circuit:2");
			const bool r2 = p.rev2();
			click("circuit:1");
			const bool r1 = !p.rev2();
			view->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
			check(r2 && r1, "SERVICE: CIRCUIT picks REV 2 and REV 1");
			if (i0 != 0)
				click(i0 == 1 ? "jack:espin" : "jack:extsig"); // as it was
			click("switch", right);
			const bool latched = p.manual().momentary && view->statusForTests().startsWith("MOMENTARY SWITCH held");
			view->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey));
			check(latched && !p.manual().momentary, "the momentary switch: a right-click holds it down, Escape lets go");
		}
		if (view != nullptr && argc > 2) {
			if (argc > 3 && juce::String(argv[3]) == "service") // the snapshot with SERVICE's card open
				view->mouseDown(juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), view->placeForTests("service"), {},
				                                 juce::MouseInputSource::defaultPressure, 0.0f, 0.0f, 0.0f, 0.0f, view, view,
				                                 juce::Time::getCurrentTime(), view->placeForTests("service"), juce::Time::getCurrentTime(), 1, false));
			const auto img = view->createComponentSnapshot(view->getLocalBounds(), true, 1.0f);
			juce::File out(argv[2]);
			out.deleteFile();
			juce::FileOutputStream stream(out);
			juce::PNGImageFormat().writeImageToStream(img, stream);
			std::printf("  wrote %s\n", argv[2]);
		}
	}
	std::printf("\n%s\n", g_failures == 0 ? "All checks passed." : "Some checks FAILED.");
	return g_failures == 0 ? 0 : 1;
}
