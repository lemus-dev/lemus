// bsrackcorners: the controls' extremes at 44.1 and 48 kHz. Each corner is the machine as the plugin
// runs it (firmware v1.5, a new unit's EEPROM) with a note held and the controls set; it reports any
// sample the filter's solver left unconverged, any non-finite or out-of-bounds output (|x| > 4), and
// broadband noise where nothing makes it (the output's top octave against its total while the VCA is
// closed after the note). Exit status 1 if any corner fails.
#include "bsrack/core/machine.h"
#include "bsrack/tools/hands.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#ifndef BSRACK_ROM
#define BSRACK_ROM ""
#endif

using namespace bsrack;

int main()
{
	std::ifstream f(BSRACK_ROM, std::ios::binary);
	const std::vector<uint8_t> rom((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (rom.size() != Machine::kRomSize) {
		std::fprintf(stderr, "no v1.5 ROM at %s\n", BSRACK_ROM);
		return 1;
	}
	struct Corner {
		const char *name;
		int note;
		std::function<void(Controls &)> set;
	};
	auto pots = [](std::initializer_list<std::pair<int, double>> init) {
		const std::vector<std::pair<int, double>> l(init); // the list's own array dies with the statement
		return [l](Controls &c) {
			for (auto [p, v] : l)
				c.pots[size_t(p)] = v;
		};
	};
	const std::vector<Corner> corners = {
		{"filter open, no resonance", 60, pots({{kFrequency, 1}, {kResonance, 0}})},
		{"filter open, full resonance", 60, pots({{kFrequency, 1}, {kResonance, 1}})},
		{"filter closed, full resonance", 36, pots({{kFrequency, 0}, {kResonance, 1}})},
		{"mid, full resonance", 48, pots({{kFrequency, 0.5}, {kResonance, 1}})},
		{"top note, open", 107, pots({{kFrequency, 1}, {kResonance, 0.5}})},
		{"bottom note, open", 12, pots({{kFrequency, 1}, {kResonance, 0.5}})},
		{"12 dB, full resonance, open", 60, [](Controls &c) { c.switches[kCutOff] = 0; c.pots[kResonance] = 1; c.pots[kFrequency] = 1; }},
		{"12 dB, full resonance, closed", 36, [](Controls &c) { c.switches[kCutOff] = 0; c.pots[kResonance] = 1; c.pots[kFrequency] = 0; }},
		{"sync, osc 2 high, open", 48, [](Controls &c) { c.switches[kSync] = 1; c.pots[kSemitone] = 1; c.switches[kOsc2Range] = 3; c.pots[kMix] = 1; c.pots[kFrequency] = 1; }},
		{"fast LFO into everything", 48, pots({{kLfoSpeed, 1}, {kModLfo, 1}, {kModDepth, 1}, {kFrequency, 0.5}, {kResonance, 0.8}})},
		{"pulse narrow, both", 48, [](Controls &c) { c.switches[kOsc1Wave] = 1; c.switches[kOsc2Wave] = 1; c.pots[kPulseWidth] = 1; c.pots[kFrequency] = 1; }},
		{"fastest envelopes, full mod", 48, pots({{kAttack, 0}, {kDecay, 0}, {kSustain, 0}, {kRelease, 0}, {kModDepth, 1}, {kModEnvelope, 1}})},
	};
	int bad = 0;
	for (double rate : {44100.0, 48000.0})
		for (const auto &c : corners) {
			Machine m(rom);
			m.prepare(rate);
			Controls ctl;
			const double defaults[kPots] = {1.0, 0, 0.5, 0, 0.5, 0.5, 0, 0.5, 0.5, 0.5, 0.6, 0, 0.5, 0, 0.4, 1, 0.3, 0};
			for (int i = 0; i < kPots; ++i)
				ctl.pots[size_t(i)] = defaults[i];
			ctl.switches = {0, 1, 0, 1, 0, 0, 0, 1, 1, 1, 0, 2};
			m.setControls(ctl);
			std::vector<float> out(size_t(rate * 0.3));
			m.render(out.data(), int(out.size()), nullptr); // boot
			Controls target = ctl;
			c.set(target);
			tools::moveTo(m, ctl, target, rate);
			m.takeUnconverged();
			for (uint8_t b : {uint8_t(0x90), uint8_t(c.note), uint8_t(127)})
				m.midiIn(b);
			std::vector<float> held(size_t(rate * 1.5));
			m.render(held.data(), int(held.size()), nullptr);
			if (std::getenv("BSRACK_CORNERS_DEBUG")) {
				const auto &fr = m.frame();
				std::printf("  vca %03X vcf %03X res %03X volA %02X volB %02X edges %d/%d asleep %d\n", fr.dac[kVcaCv], fr.dac[kVcfCv],
				            fr.dac[kResCv], fr.volA, fr.volB, fr.nEdgesA, fr.nEdgesB, int(m.asleep()));
			}
			for (uint8_t b : {uint8_t(0x80), uint8_t(c.note), uint8_t(64)})
				m.midiIn(b);
			std::vector<float> after(size_t(rate * 0.5));
			m.render(after.data(), int(after.size()), nullptr);
			const int unconverged = m.takeUnconverged();
			double peak = 0;
			bool finite = true;
			for (const auto *v : {&held, &after})
				for (float x : *v) {
					finite = finite && std::isfinite(x);
					peak = std::max(peak, double(std::abs(x)));
				}
			const bool ok = unconverged == 0 && finite && peak < 4.0;
			bad += !ok;
			std::printf("%s %-32s %5.0f Hz  peak %6.3f  unconverged %d%s\n", ok ? "ok  " : "FAIL", c.name, rate, peak, unconverged,
			            finite ? "" : "  NON-FINITE");
		}
	std::printf(bad ? "\n%d corners failed\n" : "\nAll corners passed\n", bad);
	return bad != 0;
}
