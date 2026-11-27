// ms20corners: the controls' extremes, checked for what the other checks can't see. Every other
// check compares against something (SPICE, the service manual, recordings, golden renders) only
// where that something exists; this one renders the corners nobody recorded (both KORG35s' CUTOFF
// and PEAK at their tops, both ends of the keyboard and the scales, every waveform, the EGs and the
// MG at full depth, fast sweeps and jumps, the patch panel's common routings, the ESP and EXT
// SIGNAL IN taking a sound from outside) through the plugin's processor at 44.1 and 48 kHz, and
// fails on:
//   - any circuit solve that didn't converge (ms20::SolverStats::failures, every block): an
//     unconverged sample is taken as it stands and is silent in every other check;
//   - an operating point that didn't converge (prepare: the block would start from where it was);
//   - broadband noise where nothing should make it: with no noise source patched or selected and
//     no feedback patch, a case whose median spectral flatness (40 Hz-16 kHz, 2048-sample windows
//     whose band holds more than -70 dBFS) passes kNoiseFlatness. Tones, chirps and thumps sit far
//     below it; a KORG35 self-oscillating at the top of CUTOFF measured 0.44-0.48 before the
//     solver's holds (docs/improvements.md, 2026-11-16; MS20_NOHOLD=1 brings that solver back).
//     The cases in knownNoise() are reported as KNOWN instead: the 4x rate's aliasing, logged;
//   - output that isn't finite, or peaks past kMaxPeak.
// It also reports the samples taken in shorter steps and those held in quarters.
//
//   ms20corners [--rate 44100|48000] [--only TEXT] [--list] [--jobs N] [--wav DIR]
//
// --wav writes each case's render to DIR/<rate>-<case>.wav, to listen to one.
//
// Exit status 0 when every case passes. About a minute on an M5 Max (300 renders, 9 threads).
#include "PluginProcessor.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <vector>

namespace {

using ms20::Jack;

constexpr double kNoiseFlatness = 0.2;
constexpr float kMaxPeak = 4.0f; // well past anything SIGNAL OUT reaches (about 1)
constexpr double kPi = 3.14159265358979;

void set(Ms20Processor &p, const std::string &id, float plain)
{
	auto *param = p.params().getParameter(id);
	if (param == nullptr) {
		std::fprintf(stderr, "no parameter %s\n", id.c_str());
		std::abort();
	}
	param->setValueNotifyingHost(param->convertTo0to1(plain));
}

// What the plugin's input carries (the cable from outside).
enum class Input { None, Sweep, Saw };

struct Case {
	std::string name;
	std::vector<std::pair<std::string, float>> params;
	std::vector<std::pair<Jack, int>> cables; // input jack, output jack (or ms20::kHostInput)
	Input input = Input::None;
	bool noisy = false; // a noise source, a random S&H, or a feedback patch: no flatness check
	double seconds = 1.0;
	double step = 0.12; // between notes
	// Per block, given the time: automation.
	std::function<void(Ms20Processor &, double)> automate;
	bool bend = false; // pitch bend swept (the control wheel) full down and up
	std::string known; // why its noise is known (reported, not failed): see knownNoise()
};

// Notes: both ends of the keyboard (MIDI C3 and C6 are its ends) and between, short and legato.
juce::MidiBuffer notes(double rate, const Case &k)
{
	juce::MidiBuffer m;
	const int keys[] = {48, 84, 60, 79, 48, 72, 55, 84};
	int i = 0;
	for (double t = 0.02; t < k.seconds - 0.25; t += k.step, ++i) {
		const int n = keys[i % 8];
		const bool legato = i % 3 == 2;
		m.addEvent(juce::MidiMessage::noteOn(1, n, juce::uint8(100)), int(t * rate));
		m.addEvent(juce::MidiMessage::noteOff(1, n), int((t + (legato ? 1.3 : 0.6) * k.step) * rate));
	}
	if (k.bend)
		for (double t = 0; t < k.seconds; t += 0.01) {
			const double v = std::sin(t * 2 * kPi * 3);
			m.addEvent(juce::MidiMessage::pitchWheel(1, std::clamp(int(8192 + 8191 * v), 0, 16383)), int(t * rate));
		}
	return m;
}

// The other filter out of the way, the VCOs at the ends of their scales, a fast EG 2.
std::vector<std::pair<std::string, float>> base()
{
	return {{"vco1_wave", 1},   {"vco1_scale", 0}, {"vco1_level", 10}, {"vco2_wave", 1},   {"vco2_scale", 3},
	        {"vco2_level", 10}, {"hpf_cutoff", 0}, {"hpf_peak", 0},    {"lpf_cutoff", 10}, {"lpf_peak", 0},
	        {"eg2_attack", 0},  {"eg2_decay", 2},  {"eg2_sustain", 5}, {"eg2_release", 1}, {"volume", 10}};
}

Case with(Case k, std::vector<std::pair<std::string, float>> more)
{
	if (k.params.empty())
		k.params = base();
	for (auto &q : more) {
		bool found = false;
		for (auto &p : k.params)
			if (p.first == q.first) {
				p.second = q.second;
				found = true;
			}
		if (!found)
			k.params.push_back(q);
	}
	return k;
}

// Cases whose noise is known and logged (docs/improvements.md, "Aliasing at the top of the
// KORG35s"): with both KORG35s self-oscillating near 40 kHz, their products with each other and the
// input alias at the 4x rate between the filters (every block's solves converge; at 16x the first
// two are clean). One filter at the top alone is clean since its outputs are filtered from the held
// quarters (Circuit::setDecimation, 2026-11-22); "HPF cutoff 10.0 peak 10 VCOs", "MG into both EXT
// 10, PEAKs 10" and "EG 1 into TOTAL EXT, MG/T.EXT 10 on the VCOs and both filters" left the list
// then. Reported as KNOWN when they trip the noise flag; the solver and bounds checks still apply.
const std::vector<std::pair<std::string, std::string>> &knownNoise()
{
	static const std::string alias = "the filters' aliasing (clean at 16x)", open = "noisy at 16x too: aliasing or the circuit's own";
	static const std::vector<std::pair<std::string, std::string>> k = {
		{"HPF 10 / LPF 10, both PEAK 10, VCOs", alias},
		// at 44.1 kHz since the A pots' power law (2026-11-23; its default EG 2 moved it over the
		// limit): flatness 0.233 at the filters' 8x, clean at 16x
		{"LPF cutoff 10.0 peak 10 no input", alias},
		// at 44.1 kHz since the MG's VR25 law (2026-11-24; the MG's rate moves its alias lines):
		// flatness 0.222 with the adaptive rates, 0.125 at 8x throughout, 0.000 at 16x
		// (MS20_FILTER_OVER=4)
		{"HPF cutoff 10.0 peak 10 no input", alias},
		// since the KORG35's BR 4.35 (2026-11-23): flatness 0.22-0.24 with the filters at 8x, 0.25-0.31
		// at 16x: two self-oscillating filters and the VCOs intermodulate, more so the finer the steps
		{"HPF 9 / LPF 10, both PEAK 10, VCOs", open},
		{"EG 2 10 into both, sustain 10, CUTOFFs 10, PEAKs 10", alias},
		{"EG 2 10 into both, EG 2 all 0, PEAKs 10, CUTOFFs 5", open},
		{"input (sweep) into EXT SIGNAL IN, LPF 10 PEAK 10, HPF 10 PEAK 10", open},
	};
	return k;
}

std::vector<Case> cases()
{
	std::vector<Case> c;
	char n[128];
	// Each KORG35: CUTOFF and PEAK over their range, the VCOs into it or nothing (self-oscillation).
	const float cutoffs[] = {0, 4, 7, 9, 9.5f, 10};
	const float peaks[] = {0, 5, 8, 10};
	for (int hpf = 0; hpf < 2; ++hpf)
		for (float f : cutoffs)
			for (float r : peaks)
				for (int src = 0; src < 2; ++src) {
					const std::string flt = hpf ? "hpf" : "lpf";
					std::snprintf(n, sizeof n, "%s cutoff %.1f peak %.0f %s", hpf ? "HPF" : "LPF", f, r, src ? "VCOs" : "no input");
					Case k{n};
					c.push_back(with(k, {{flt + "_cutoff", f}, {flt + "_peak", r}, {"vco1_level", src ? 10.f : 0.f},
					                     {"vco2_level", src ? 10.f : 0.f}}));
				}
	// Both at once.
	for (float h : {9.f, 10.f})
		for (float l : {9.f, 10.f}) {
			std::snprintf(n, sizeof n, "HPF %.0f / LPF %.0f, both PEAK 10, VCOs", h, l);
			c.push_back(with(Case{n}, {{"hpf_cutoff", h}, {"hpf_peak", 10}, {"lpf_cutoff", l}, {"lpf_peak", 10}}));
		}
	// Every waveform at the ends of its scale, into the LPF near its top and ringing.
	static const char *w1[] = {"triangle", "saw", "pulse", "noise"};
	static const char *w2[] = {"saw", "square", "narrow", "ring"};
	for (int w = 0; w < 4; ++w)
		for (int s : {0, 3}) {
			std::snprintf(n, sizeof n, "VCO 1 %s %s, LPF 9 PEAK 8", w1[w], s ? "4'" : "32'");
			Case k = with(Case{n}, {{"vco1_wave", float(w)}, {"vco1_scale", float(s)}, {"vco2_level", 0}, {"lpf_cutoff", 9},
			                        {"lpf_peak", 8}, {"vco1_pw", w == 2 ? (s ? 10.f : 0.f) : 5.f}});
			k.noisy = w == 3;
			c.push_back(k);
		}
	for (int w = 0; w < 4; ++w)
		for (int s : {0, 3}) {
			std::snprintf(n, sizeof n, "VCO 2 %s %s PITCH %+d, LPF 9 PEAK 8", w2[w], s ? "2'" : "16'", s ? 10 : -10);
			c.push_back(with(Case{n}, {{"vco2_wave", float(w)}, {"vco2_scale", float(s)}, {"vco2_pitch", s ? 10.f : -10.f},
			                           {"vco1_level", w == 3 ? 10.f : 0.f}, {"lpf_cutoff", 9}, {"lpf_peak", 8}}));
		}
	// The EGs, the MG and the tune controls at full depth.
	c.push_back(with(Case{"EG 2 10 into both, EG 2 all 0, PEAKs 10, CUTOFFs 5"},
	                 {{"hpf_cutoff", 5}, {"hpf_peak", 10}, {"hpf_eg2", 10}, {"lpf_cutoff", 5}, {"lpf_peak", 10}, {"lpf_eg2", 10},
	                  {"eg2_hold", 0}, {"eg2_attack", 0}, {"eg2_decay", 0}, {"eg2_sustain", 0}, {"eg2_release", 0}}));
	c.push_back(with(Case{"EG 2 10 into both, sustain 10, CUTOFFs 10, PEAKs 10"},
	                 {{"hpf_cutoff", 10}, {"hpf_peak", 10}, {"hpf_eg2", 10}, {"lpf_cutoff", 10}, {"lpf_peak", 10}, {"lpf_eg2", 10},
	                  {"eg2_sustain", 10}}));
	{
		Case k = with(Case{"EG 2 10 into LPF, fast notes, PEAK 10"},
		              {{"lpf_cutoff", 3}, {"lpf_peak", 10}, {"lpf_eg2", 10}, {"eg2_decay", 0}, {"eg2_sustain", 0}});
		k.step = 0.03;
		c.push_back(k);
	}
	c.push_back(with(Case{"EG 1 10 into the VCOs, EG 1 all 0"},
	                 {{"fm_eg1", 10}, {"eg1_delay", 0}, {"eg1_attack", 0}, {"eg1_release", 0}, {"lpf_cutoff", 8}, {"lpf_peak", 8}}));
	c.push_back(with(Case{"EG 1 10 into the VCOs, delay and attack 10"},
	                 {{"fm_eg1", 10}, {"eg1_delay", 10}, {"eg1_attack", 10}, {"eg1_release", 10}}));
	for (float wave : {0.f, 10.f}) {
		std::snprintf(n, sizeof n, "MG wave %.0f at FREQ 10 into the VCOs and both filters, PEAKs 10", wave);
		c.push_back(with(Case{n}, {{"mg_wave", wave}, {"mg_freq", 10}, {"fm_mg", 10}, {"hpf_cutoff", 3}, {"hpf_peak", 10}, {"hpf_mg", 10},
		                           {"lpf_cutoff", 7}, {"lpf_peak", 10}, {"lpf_mg", 10}}));
	}
	c.push_back(with(Case{"MG 10 into LPF 10 PEAK 10, MG FREQ 0"}, {{"mg_freq", 0}, {"lpf_cutoff", 10}, {"lpf_peak", 10}, {"lpf_mg", 10}}));
	c.push_back(with(Case{"portamento 10, LPF 9 PEAK 10"}, {{"portamento", 10}, {"lpf_cutoff", 9}, {"lpf_peak", 10}}));
	c.push_back(with(Case{"MASTER TUNE +10, 4' and 2', LPF 10 PEAK 8"},
	                 {{"master_tune", 10}, {"vco1_scale", 3}, {"lpf_peak", 8}}));
	c.push_back(with(Case{"MASTER TUNE -10, 32' and 16'"}, {{"master_tune", -10}, {"vco2_scale", 0}}));
	// Sweeps and jumps, PEAK 10.
	{
		Case k = with(Case{"LPF CUTOFF swept 0-10-0, PEAK 10"}, {{"lpf_peak", 10}});
		k.seconds = 2;
		k.automate = [](Ms20Processor &p, double t) { set(p, "lpf_cutoff", float(5 - 5 * std::cos(t * kPi))); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"LPF PEAK swept 0-10-0 at CUTOFF 10"}, {{"lpf_cutoff", 10}});
		k.seconds = 2;
		k.automate = [](Ms20Processor &p, double t) { set(p, "lpf_peak", float(5 - 5 * std::cos(t * kPi))); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"LPF CUTOFF jumps 0 / 10 each block, PEAK 10"}, {{"lpf_peak", 10}});
		k.automate = [](Ms20Processor &p, double t) { set(p, "lpf_cutoff", int(t * 94) % 2 ? 10.f : 0.f); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"HPF CUTOFF swept 0-10-0, PEAK 10"}, {{"hpf_peak", 10}});
		k.seconds = 2;
		k.automate = [](Ms20Processor &p, double t) { set(p, "hpf_cutoff", float(5 - 5 * std::cos(t * kPi))); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"HPF PEAK swept 0-10-0 at CUTOFF 10"}, {{"hpf_cutoff", 10}});
		k.seconds = 2;
		k.automate = [](Ms20Processor &p, double t) { set(p, "hpf_peak", float(5 - 5 * std::cos(t * kPi))); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"HPF CUTOFF jumps 0 / 10 each block, PEAK 10"}, {{"hpf_peak", 10}});
		k.automate = [](Ms20Processor &p, double t) { set(p, "hpf_cutoff", int(t * 94) % 2 ? 10.f : 0.f); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"VCO 2 PITCH and VCO 1 PW swept, ring"}, {{"vco2_wave", 3}, {"vco1_wave", 2}, {"lpf_peak", 8}});
		k.seconds = 2;
		k.automate = [](Ms20Processor &p, double t) {
			set(p, "vco2_pitch", float(10 * std::sin(t * kPi * 2)));
			set(p, "vco1_pw", float(5 - 5 * std::cos(t * kPi * 3)));
		};
		c.push_back(k);
	}
	// The patch panel.
	{
		Case k = with(Case{"KBD CV into LPF EXT 10, LPF 8 PEAK 10"}, {{"lpf_cutoff", 8}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::LpfExtCutoff, int(Jack::KbdCvOut)}};
		c.push_back(k);
	}
	{
		Case k = with(Case{"MG into both EXT 10, PEAKs 10"},
		              {{"mg_freq", 10}, {"hpf_cutoff", 5}, {"hpf_peak", 10}, {"hpf_eg2", 10}, {"lpf_cutoff", 7}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::HpfExtCutoff, int(Jack::MgTriangle)}, {Jack::LpfExtCutoff, int(Jack::MgRectangle)}};
		c.push_back(k);
	}
	{
		Case k = with(Case{"EG 1 into TOTAL EXT, MG/T.EXT 10 on the VCOs and both filters"},
		              {{"fm_mg", 10}, {"hpf_mg", 10}, {"lpf_mg", 10}, {"lpf_cutoff", 6}, {"lpf_peak", 10}});
		k.cables = {{Jack::TotalExtMod, int(Jack::Eg1Out)}};
		c.push_back(k);
	}
	{
		Case k = with(Case{"EG 2 REV into VCO 1+2 CV IN, EG 1 REV into LPF EXT"},
		              {{"lpf_eg2", 10}, {"lpf_cutoff", 8}, {"lpf_peak", 9}, {"eg2_decay", 0}, {"eg2_sustain", 0}});
		k.cables = {{Jack::VcoCvIn, int(Jack::Eg2RevOut)}, {Jack::LpfExtCutoff, int(Jack::Eg1RevOut)}};
		c.push_back(k);
	}
	{
		Case k = with(Case{"control wheel into LPF EXT 10, wheel swept, LPF 7 PEAK 10"}, {{"lpf_cutoff", 7}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::LpfExtCutoff, int(Jack::Wheel)}};
		k.bend = true;
		c.push_back(k);
	}
	{
		Case k = with(Case{"momentary switch into TRIG IN, pressed every 50 ms"}, {{"lpf_cutoff", 5}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::TrigIn, int(Jack::Momentary)}};
		k.automate = [](Ms20Processor &p, double t) { p.setManual(0, int(t * 20) % 2 == 1); };
		c.push_back(k);
	}
	{
		Case k = with(Case{"S&H: white noise, MG clock, into LPF EXT 10, PEAK 10"}, {{"mg_freq", 9}, {"lpf_cutoff", 5}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::ShIn, int(Jack::WhiteNoise)}, {Jack::ShClock, int(Jack::MgRectangle)}, {Jack::LpfExtCutoff, int(Jack::ShOut)}};
		k.noisy = true;
		c.push_back(k);
	}
	{
		Case k = with(Case{"white noise into EXT SIGNAL IN, filters at the top, PEAKs 10"},
		              {{"vco1_level", 0}, {"vco2_level", 0}, {"hpf_cutoff", 10}, {"hpf_peak", 10}, {"lpf_cutoff", 10}, {"lpf_peak", 10}});
		k.cables = {{Jack::ExtSigIn, int(Jack::WhiteNoise)}};
		k.noisy = true;
		c.push_back(k);
	}
	{
		Case k = with(Case{"pink noise into LPF EXT 10, PEAK 10"}, {{"lpf_cutoff", 6}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::LpfExtCutoff, int(Jack::PinkNoise)}};
		k.noisy = true;
		c.push_back(k);
	}
	{
		Case k = with(Case{"MVCA: white noise by EG 1 into EXT SIGNAL IN"}, {{"eg1_release", 3}, {"lpf_cutoff", 8}, {"lpf_peak", 8}});
		k.cables = {{Jack::MvcaIn, int(Jack::WhiteNoise)}, {Jack::ExtSigIn, int(Jack::MvcaOut)}};
		k.noisy = true;
		c.push_back(k);
	}
	{
		Case k = with(Case{"SIGNAL OUT into EXT SIGNAL IN (feedback), filters at the top"},
		              {{"hpf_cutoff", 10}, {"hpf_peak", 10}, {"lpf_cutoff", 10}, {"lpf_peak", 10}});
		k.cables = {{Jack::ExtSigIn, int(Jack::SignalOut)}};
		k.noisy = true;
		c.push_back(k);
	}
	// A sound from outside: into EXT SIGNAL IN, and through the ESP.
	{
		Case k = with(Case{"input (sweep) into EXT SIGNAL IN, LPF 10 PEAK 10, HPF 10 PEAK 10"},
		              {{"vco1_level", 0}, {"vco2_level", 0}, {"hpf_cutoff", 10}, {"hpf_peak", 10}, {"lpf_cutoff", 10}, {"lpf_peak", 10}});
		k.cables = {{Jack::ExtSigIn, ms20::kHostInput}};
		k.input = Input::Sweep;
		c.push_back(k);
	}
	{
		Case k = with(Case{"input (saw) into EXT SIGNAL IN, LPF 6 PEAK 10"}, {{"lpf_cutoff", 6}, {"lpf_peak", 10}});
		k.cables = {{Jack::ExtSigIn, ms20::kHostInput}};
		k.input = Input::Saw;
		c.push_back(k);
	}
	for (float lvl : {5.f, 10.f}) {
		std::snprintf(n, sizeof n, "ESP at level %.0f: F-V to VCO CV, ENV to LPF EXT, TRIG to TRIG IN, sweep in", lvl);
		Case k = with(Case{n}, {{"esp_level", lvl}, {"esp_lowcut", 10}, {"esp_highcut", 0}, {"esp_threshold", 0}, {"esp_cvadj", 10},
		                        {"lpf_cutoff", 5}, {"lpf_peak", 10}, {"lpf_eg2", 10}});
		k.cables = {{Jack::EspSignalIn, ms20::kHostInput},
		            {Jack::VcoCvIn, int(Jack::EspFvOut)},
		            {Jack::LpfExtCutoff, int(Jack::EspEnvOut)},
		            {Jack::TrigIn, int(Jack::EspTrigOut)}};
		k.input = Input::Sweep;
		k.step = 10; // the ESP plays it
		c.push_back(k);
	}
	{
		Case k = with(Case{"ESP AMP and BAND-PASS out into EXT SIGNAL IN and HPF EXT, saw in"},
		              {{"esp_level", 10}, {"esp_lowcut", 0}, {"esp_highcut", 10}, {"hpf_eg2", 10}, {"hpf_peak", 10}, {"lpf_peak", 8}});
		k.cables = {{Jack::EspSignalIn, ms20::kHostInput}, {Jack::ExtSigIn, int(Jack::EspAmpOut)}, {Jack::HpfExtCutoff, int(Jack::EspFilterOut)}};
		k.input = Input::Saw;
		c.push_back(k);
	}
	for (auto &k : c)
		for (const auto &[name, why] : knownNoise())
			if (k.name == name)
				k.known = why;
	return c;
}

// Median spectral flatness over 40 Hz-16 kHz, 2048-sample Hann windows whose band holds more than
// -70 dBFS (a strong ultrasonic tone with nothing in the band is not judged by its rounding).
double medianFlatness(const std::vector<float> &x, double rate)
{
	const int n = 2048;
	std::vector<double> flat;
	std::vector<std::complex<double>> a(static_cast<size_t>(n));
	const int lo = int(std::ceil(40.0 * n / rate)), hi = int(16000.0 * n / rate);
	for (size_t start = 0; start + size_t(n) <= x.size(); start += size_t(n)) {
		for (int i = 0; i < n; ++i)
			a[size_t(i)] = x[start + size_t(i)] * (0.5 - 0.5 * std::cos(2 * kPi * i / n));
		// Radix-2 FFT.
		for (int i = 1, j = 0; i < n; ++i) {
			int bit = n >> 1;
			for (; j & bit; bit >>= 1)
				j ^= bit;
			j ^= bit;
			if (i < j)
				std::swap(a[size_t(i)], a[size_t(j)]);
		}
		for (int len = 2; len <= n; len <<= 1) {
			const std::complex<double> w = std::polar(1.0, -2 * kPi / len);
			for (int i = 0; i < n; i += len) {
				std::complex<double> wk = 1;
				for (int k = 0; k < len / 2; ++k, wk *= w) {
					const auto u = a[size_t(i + k)], v = a[size_t(i + k + len / 2)] * wk;
					a[size_t(i + k)] = u + v;
					a[size_t(i + k + len / 2)] = u - v;
				}
			}
		}
		double lg = 0, ar = 0;
		for (int k = lo; k <= hi; ++k) {
			const double p = std::norm(a[size_t(k)]) + 1e-30;
			lg += std::log(p);
			ar += p;
		}
		// the band's power as a mean square (Hann: sum w^2 = 3n/8; one-sided, so twice)
		const double ms = 2 * ar / (double(n) * 0.375 * n);
		if (ms < 1e-7) // -70 dBFS
			continue;
		const int m = hi - lo + 1;
		flat.push_back(std::exp(lg / m) / (ar / m));
	}
	if (flat.empty())
		return 0;
	std::nth_element(flat.begin(), flat.begin() + long(flat.size() / 2), flat.end());
	return flat[flat.size() / 2];
}

struct Result {
	double seconds = 0; // to render
	ms20::SolverStats solver;
	std::map<std::string, long> failing; // block: unconverged samples
	double flatness = 0;
	float peak = 0;
	bool finite = true;
};

bool g_rev2 = false; // --rev2: every case on the Rev 2 filter board

Result run(const Case &k, double rate, const std::string &wavDir)
{
	const auto t0 = std::chrono::steady_clock::now();
	Ms20Processor p;
	p.setRev2(g_rev2);
	for (const auto &q : k.params)
		set(p, q.first, q.second);
	ms20::Patch patch;
	for (const auto &[in, out] : k.cables)
		patch.connect(in, out);
	p.setPatch(patch);
	if (k.input != Input::None) {
		auto layout = p.getBusesLayout();
		layout.inputBuses.getReference(0) = juce::AudioChannelSet::stereo();
		p.setBusesLayout(layout);
	}
	const int block = 512;
	p.setRateAndBufferSizeDetails(rate, block);
	p.prepareToPlay(rate, block);
	const int total = int(k.seconds * rate);
	const juce::MidiBuffer all = notes(rate, k);
	std::vector<float> out(static_cast<size_t>(total));
	juce::AudioBuffer<float> buf(2, block);
	Result r;
	double phase = 0;
	for (int pos = 0; pos < total; pos += block) {
		const int n = std::min(block, total - pos);
		buf.setSize(2, n, false, false, true);
		buf.clear();
		if (k.input != Input::None)
			for (int i = 0; i < n; ++i) {
				const double t = (pos + i) / rate;
				// on for 0.3 s, off for 0.1 s: the ESP's envelope and trigger move
				const bool on = std::fmod(t, 0.4) < 0.3;
				double v = 0;
				if (k.input == Input::Sweep) {
					const double f = 80 * std::pow(2.0, 6 * t / k.seconds); // 80 Hz to 5 kHz
					phase += 2 * kPi * f / rate;
					v = 0.5 * std::sin(phase);
				} else {
					phase += 110 / rate;
					v = 0.9 * (2 * (phase - std::floor(phase)) - 1);
				}
				buf.setSample(0, i, on ? float(v) : 0.0f);
				buf.setSample(1, i, on ? float(v) : 0.0f);
			}
		juce::MidiBuffer midi;
		for (const auto m : all)
			if (m.samplePosition >= pos && m.samplePosition < pos + n)
				midi.addEvent(m.getMessage(), m.samplePosition - pos);
		if (k.automate)
			k.automate(p, pos / rate);
		p.processBlock(buf, midi);
		const float *s = buf.getReadPointer(0);
		for (int i = 0; i < n; ++i) {
			r.finite = r.finite && std::isfinite(s[i]);
			r.peak = std::max(r.peak, std::abs(s[i]));
			out[size_t(pos + i)] = s[i];
		}
	}
	r.solver = p.display().solver;
	const auto &board = p.machineForTests().board();
	for (const auto &b : board.blockNames())
		if (const long f = board.solverStats(b).failures; f > 0)
			r.failing[b] = f;
	if (!wavDir.empty()) {
		juce::String name = juce::String(int(rate)) + "-" + juce::String(k.name).replaceCharacters(" /,'+:&()", "_________") + ".wav";
		const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(wavDir).getChildFile(name);
		f.getParentDirectory().createDirectory();
		f.deleteFile();
		juce::WavAudioFormat wav;
		std::unique_ptr<juce::OutputStream> st(f.createOutputStream());
		if (st != nullptr)
			if (auto w = wav.createWriterFor(st, juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
			                                         juce::AudioFormatWriterOptions::SampleFormat::floatingPoint))) {
				const float *ch[] = {out.data()};
				w->writeFromFloatArrays(ch, 1, total);
			}
	}
	r.flatness = k.noisy ? 0 : medianFlatness(out, rate);
	r.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
	Ms20Processor::fixedSeeds(true); // (renders repeat)
	std::vector<double> rates = {44100, 48000};
	std::string only, wavDir;
	bool list = false;
	int jobs = int(std::max(1u, std::thread::hardware_concurrency() / 2));
	for (int i = 1; i < argc; ++i) {
		const std::string a = argv[i];
		if (a == "--rate" && i + 1 < argc)
			rates = {std::atof(argv[++i])};
		else if (a == "--only" && i + 1 < argc)
			only = argv[++i];
		else if (a == "--jobs" && i + 1 < argc)
			jobs = std::max(1, std::atoi(argv[++i]));
		else if (a == "--wav" && i + 1 < argc)
			wavDir = argv[++i];
		else if (a == "--list")
			list = true;
		else if (a == "--rev2")
			g_rev2 = true;
		else {
			std::fprintf(stderr, "usage: ms20corners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR] [--rev2]\n");
			return 2;
		}
	}
	std::vector<Case> all;
	for (auto &k : cases())
		if (only.empty() || k.name.find(only) != std::string::npos)
			all.push_back(k);
	if (list) {
		for (auto &k : all)
			std::printf("%s\n", k.name.c_str());
		return 0;
	}

	// Every case at every rate in one pool, the longest first.
	std::vector<std::pair<size_t, size_t>> order; // rate, case
	for (size_t r = 0; r < rates.size(); ++r)
		for (size_t i = 0; i < all.size(); ++i)
			order.push_back({r, i});
	std::stable_sort(order.begin(), order.end(), [&](auto &a, auto &b) { return all[a.second].seconds > all[b.second].seconds; });
	std::vector<std::vector<Result>> res(rates.size(), std::vector<Result>(all.size()));
	std::atomic<size_t> next{0};
	std::vector<std::thread> pool;
	for (int j = 0; j < jobs; ++j)
		pool.emplace_back([&] {
			for (size_t q; (q = next++) < order.size();) {
				const auto [r, i] = order[q];
				res[r][i] = run(all[i], rates[r], wavDir);
			}
		});
	for (auto &t : pool)
		t.join();

	int bad = 0, known = 0;
	for (size_t ri = 0; ri < rates.size(); ++ri) {
		const double rate = rates[ri];
		ms20::SolverStats total;
		size_t worst = 0, slowest = 0;
		double seconds = 0;
		for (size_t i = 0; i < all.size(); ++i) {
			const Result &r = res[ri][i];
			total += r.solver;
			seconds += r.seconds;
			if (all[i].known.empty() && r.flatness > res[ri][worst].flatness)
				worst = i;
			if (r.seconds > res[ri][slowest].seconds)
				slowest = i;
			std::string why;
			if (r.solver.failures > 0) {
				why += " unconverged " + std::to_string(r.solver.failures) + " (";
				for (const auto &[b, f] : r.failing)
					why += b + " " + std::to_string(f) + (b == r.failing.rbegin()->first ? ")" : ", ");
			}
			if (r.solver.dcFailures > 0)
				why += " operating points unconverged " + std::to_string(r.solver.dcFailures);
			const bool noise = r.flatness > kNoiseFlatness;
			if (noise && all[i].known.empty())
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > kMaxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL  %5.0f  %-64s%s\n", rate, all[i].name.c_str(), why.c_str());
			} else if (noise) {
				++known;
				std::printf("  KNOWN %5.0f  %-64s noise (flatness %.3f): %s\n", rate, all[i].name.c_str(), r.flatness,
				            all[i].known.c_str());
			}
		}
		std::printf("%5.0f Hz: %zu cases, %ld solves, %ld samples in shorter steps (%ld held), %ld softened, %ld unconverged, "
		            "%ld operating points unconverged; noisiest not known: %s (median flatness %.3f, limit %.2f); "
		            "%.0f s rendering, slowest %s (%.1f s)\n",
		            rate, all.size(), total.solves, total.substepped, total.held, total.softened, total.failures, total.dcFailures,
		            all[worst].name.c_str(), res[ri][worst].flatness, kNoiseFlatness, seconds, all[slowest].name.c_str(),
		            res[ri][slowest].seconds);
	}
	std::printf("%s: %d failing case(s), %d known\n", bad ? "FAIL" : "PASS", bad, known);
	return bad ? 1 : 0;
}
