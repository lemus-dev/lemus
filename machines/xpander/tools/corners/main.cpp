// xpandercorners: the controls' extremes, checked for what the other checks can't see. Every other
// check compares against something (SPICE, the recordings, golden renders) only where that exists;
// this one renders the corners nobody recorded and fails on:
//   - any solve that didn't converge (xpander::Solvers, core/solver_stats.h: the CEM3372's Newton,
//     the CEM3374's event times, event loop and bulk-resistance solve): an unconverged step is
//     silent in every other check (docs/improvements.md, 2026-11-16);
//   - broadband noise where nothing should make it: with no noise source, a case whose median
//     spectral flatness (40 Hz-16 kHz, windows above -70 dBFS) passes kNoiseFlatness. Tones,
//     chirps, buzzes and the self-oscillation sit far below it;
//   - output that isn't finite, or peaks past its bound.
// Two groups of cases, at 44.1 and 48 kHz (the voices at 176.4 and 192 kHz):
//   - "unit": the plugin's processor, from the factory state, set up on its panel as a player would
//     (the firmware writes the CVs): all 15 filter modes at FREQ 127 and RES 63, FREQ and RES swept,
//     the VCF FM and the linear FM at 63, the VCOs at the top with sync, the pulse widths' ends,
//     MIDI 0-127, a chord, and Oberheim's 100 factory patches (FACTORY1.SYX, when it is in the ROM
//     folder) at a low and a high note;
//   - "voice": one voice alone, its S&Hs written directly over the DAC's whole range (-5 to +5 V
//     where the board allows it), past what the firmware writes: every mode and FD, FREQ and RES at
//     their ends, both VCOs at their ends, the MDAC at 255 into the VCF and into VCO 1, sync, the
//     pulse widths, FREQ jumping between its ends with FAST writes.
// It also reports how many solves needed a retry and the most iterations one took.
//
//   xpandercorners [--rate 44100|48000] [--only TEXT] [--list] [--jobs N] [--wav DIR] [--show]
//
// --wav writes each case's render to DIR/<rate>-<case>.wav, to listen to one; --show prints the
// panel's displays as each unit case starts to play (to see that its setup took). Needs the ROMs; the
// factory state comes from the plugin's cache (made first if missing, which takes a while).
// Exit status 0 when every case passes. 10-30 s on an M5 Max (half its cores), as busy as it is.
#include "PluginProcessor.h"
#include "xpander/core/voice.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdio>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kNoiseFlatness = 0.2;
constexpr float kMaxPeak = 1.5f;    // the outputs clip at 10.5 V = 1.05 full scale
constexpr double kMaxVoice = 12.0;  // VOICE X, volts: its op-amps stop at 11 V
constexpr int kBlock = 256;
bool g_show = false;

juce::MemoryBlock rom(const char *name)
{
	juce::MemoryBlock mb;
	juce::File(XPANDER_ROM_DIR).getChildFile(name).loadFileAsData(mb);
	return mb;
}

struct Roms {
	juce::MemoryBlock r[6] = {rom("exp1.2-0.u8"), rom("exp1.2-1.u7"), rom("exp1.2-2.u6"), rom("exp1.2-3.u5"), rom("ca1.4-0.u912"), rom("ca1.4-1.u914")};
	juce::MemoryBlock bank = rom("FACTORY1.SYX");
	bool ok() const
	{
		for (const auto &b : r)
			if (b.getSize() != 8192)
				return false;
		return true;
	}
};
const Roms &roms()
{
	static const Roms r;
	return r;
}

std::unique_ptr<XpanderProcessor> makeUnit(bool patches, double rate)
{
	const auto &r = roms();
	auto p = std::make_unique<XpanderProcessor>(std::vector<const juce::MemoryBlock *>{&r.r[0], &r.r[1], &r.r[2], &r.r[3]},
	                                            std::vector<const juce::MemoryBlock *>{&r.r[4], &r.r[5]}, lemus::Artwork{},
	                                            patches ? &r.bank : nullptr);
	p->setRateAndBufferSizeDetails(rate, kBlock);
	p->prepareToPlay(rate, kBlock);
	return p;
}

// ---- The unit: the panel and MIDI, at times in seconds from the factory state's start.

// Switch matrix positions (column, row): docs/panel.md 6.
constexpr int kVcfVca[2] = {7, 0}, kFmLag[2] = {4, 6}, kVco1[2] = {3, 6}, kVco2[2] = {3, 7}, kPage2[2] = {1, 5};
constexpr int kP1[6][2] = {{3, 0}, {3, 1}, {3, 2}, {3, 3}, {3, 4}, {3, 5}};
constexpr int kP2[6][2] = {{4, 0}, {4, 1}, {4, 2}, {4, 3}, {4, 4}, {4, 5}};

struct Action {
	double at;
	enum Kind { Tap, Enc, On, Off } kind;
	int a = 0, b = 0;
};

struct UnitCase {
	std::string name;
	bool patches = false;  // with the factory patches
	bool noisy = false;    // a noise source: no flatness check
	double seconds = 0;
	std::vector<Action> acts;
};

// A script: setup at the start (each page or key 0.15 s, an encoder 10 ms a detent), then play.
struct Script {
	UnitCase c;
	double t = 0.3; // the firmware's first blocks
	explicit Script(std::string name, bool patches = false) { c.name = std::move(name), c.patches = patches; }
	Script &tap(const int k[2])
	{
		c.acts.push_back({t, Action::Tap, k[0], k[1]});
		t += 0.15;
		return *this;
	}
	Script &enc(int n, int steps)
	{
		c.acts.push_back({t, Action::Enc, n, steps});
		t += 0.01 * std::abs(steps) + 0.05;
		return *this;
	}
	// The filter mode, from the default's 4 POLE LOW (3): the firmware takes one detent of MODE at
	// a time, so a slower turn.
	Script &mode(int m)
	{
		for (int i = 0; i < std::abs(m - 3); ++i) {
			c.acts.push_back({t, Action::Enc, 2, m > 3 ? 1 : -1});
			t += 0.12;
		}
		return *this;
	}
	Script &patch(int nn)
	{
		for (const int d : {nn / 10 % 10, nn % 10}) {
			const int k[2] = {d < 8 ? 0 : 1, d < 8 ? d : d - 8};
			tap(k);
		}
		t += 0.3;
		return *this;
	}
	Script &note(int key, double len, double gap = 0.02)
	{
		c.acts.push_back({t, Action::On, key, 100});
		c.acts.push_back({t + len, Action::Off, key, 0});
		t += len + gap;
		return *this;
	}
	Script &chord(std::vector<int> keys, double len)
	{
		for (int k : keys) {
			c.acts.push_back({t, Action::On, k, 100});
			c.acts.push_back({t + len, Action::Off, k, 0});
		}
		t += len + 0.02;
		return *this;
	}
	// The usual run: both ends of the keyboard and the middle, short and legato.
	Script &play()
	{
		note(24, 0.25).note(108, 0.25).note(60, 0.25, -0.05).note(127, 0.25).note(0, 0.25);
		return *this;
	}
	UnitCase done(double tail = 0.3)
	{
		c.seconds = t + tail;
		return c;
	}
};

// The default patch (two saws, 4 POLE LOW, FREQ 48, RES 0) with FREQ and RES turned to their tops.
Script edgeFilter(std::string name, int mode)
{
	Script s(std::move(name));
	s.tap(kVcfVca).enc(0, 80).enc(1, 63).mode(mode);
	return s;
}

const char *kModes[15] = {"1P LOW", "2P LOW", "3P LOW", "4P LOW", "1P HIGH", "2P HIGH", "3P HIGH", "2P BAND",
                          "4P BAND", "2P NOTCH", "3P PHASE", "2P HIGH+1P LOW", "3P HIGH+1P LOW", "NOTCH+1P LOW", "PHASE+1P LOW"};

std::vector<UnitCase> unitCases()
{
	std::vector<UnitCase> c;
	for (int m = 0; m < 15; ++m)
		c.push_back(edgeFilter(std::string("unit ") + kModes[m] + ", FREQ 127, RES 63", m).play().done());
	c.push_back(edgeFilter("unit 4P LOW, FREQ 0, RES 63", 3).enc(0, -127).play().done());
	{
		Script s("unit 4P LOW, FREQ 127, RES 0");
		s.tap(kVcfVca).enc(0, 80);
		c.push_back(s.play().done());
	}
	// FREQ swept 0-127-0 and RES 0-63-0 under held notes (the encoders turned while they play).
	for (int m : {3, 4}) {
		Script s(std::string("unit ") + kModes[m] + ", RES 63, FREQ swept 0-127-0");
		s.tap(kVcfVca).enc(1, 63).mode(m).enc(0, -48);
		s.c.acts.push_back({s.t, Action::On, 36, 100});
		s.c.acts.push_back({s.t, Action::On, 96, 100});
		s.enc(0, 127).enc(0, -127);
		s.c.acts.push_back({s.t, Action::Off, 36, 0});
		s.c.acts.push_back({s.t, Action::Off, 96, 0});
		c.push_back(s.done());
	}
	for (int m : {3, 7}) {
		Script s(std::string("unit ") + kModes[m] + ", FREQ 127, RES swept 0-63-0");
		s.tap(kVcfVca).enc(0, 80).mode(m);
		s.c.acts.push_back({s.t, Action::On, 48, 100});
		s.enc(1, 63).enc(1, -63);
		s.c.acts.push_back({s.t, Action::Off, 48, 0});
		c.push_back(s.done());
	}
	// The VCF FM at 63 from VCO 2 at its bottom and its top, at the filter's top.
	for (int m : {3, 4, 8})
		for (int f2 : {-63, 63}) {
			Script s = edgeFilter(std::string("unit ") + kModes[m] + ", FREQ 127, RES 63, VCF FM 63, VCO2 FREQ " + (f2 > 0 ? "63" : "0"), m);
			s.tap(kFmLag).enc(0, 63).enc(1, 1).tap(kVco2).enc(0, f2);
			c.push_back(s.play().done());
		}
	// The linear FM at 63 into VCO 1, VCO 2 at both ends; both VCOs at the top with sync.
	for (int f2 : {-63, 63}) {
		Script s(std::string("unit VCO FM 63, VCO2 FREQ ") + (f2 > 0 ? "63" : "0") + ", FREQ 127");
		s.tap(kVcfVca).enc(0, 80).tap(kFmLag).enc(0, 63).tap(kVco2).enc(0, f2);
		c.push_back(s.play().done());
	}
	{
		Script s("unit VCOs FREQ 63, all waves, sync, FREQ 127, RES 63");
		s.tap(kVcfVca).enc(0, 80).enc(1, 63);
		s.tap(kVco1).enc(0, 63).tap(kPage2).tap(kP2[1]).tap(kP2[3]);
		s.tap(kVco2).enc(0, 63).tap(kPage2).tap(kP1[5]).tap(kP2[1]).tap(kP2[3]);
		c.push_back(s.play().done());
	}
	for (int pw : {-63, 63}) {
		Script s(std::string("unit pulses only, PW ") + (pw > 0 ? "63" : "0") + ", FREQ 127");
		s.tap(kVcfVca).enc(0, 80);
		s.tap(kVco1).enc(3, pw).tap(kPage2).tap(kP2[2]).tap(kP2[3]);
		s.tap(kVco2).enc(3, pw).tap(kPage2).tap(kP2[2]).tap(kP2[3]);
		c.push_back(s.play().done());
	}
	{
		Script s = edgeFilter("unit six-note chord, 4P LOW, FREQ 127, RES 63", 3);
		s.chord({24, 43, 60, 79, 96, 115}, 0.8);
		c.push_back(s.done());
	}
	{
		Script s = edgeFilter("unit noise, FREQ 127, RES 63", 3);
		s.tap(kVco2).tap(kPage2).tap(kP2[4]);
		UnitCase k = s.play().done();
		k.noisy = true;
		c.push_back(k);
	}
	// The factory patches. Six switch the noise source on (their voices' UX01 bit 4, read with
	// xpanderrender's show): 36 ROCK TOM, 49 E. HIHAT, 63 SF-FLUTE, 73 BASSREED, 89 AIR REZZ, 93 FLUTE 3.
	if (roms().bank.getSize() > 0)
		for (int p = 0; p < 100; ++p) {
			char n[64];
			std::snprintf(n, sizeof n, "unit factory patch %02d", p);
			Script s(n, true);
			UnitCase k = s.patch(p).note(36, 0.35).note(96, 0.35).done(0.4);
			k.noisy = p == 36 || p == 49 || p == 63 || p == 73 || p == 89 || p == 93;
			c.push_back(k);
		}
	return c;
}

// ---- One voice alone, its S&Hs written directly (Rev D's order: 0 PW1, 1 VCA, 2 VOL A, 3 VCOF1,
// 4 VOL B, 5 VCFF, 6 PW2, 7 VCOF2, 8 RES), as tools/circuitrun does.

struct VoiceCase {
	std::string name;
	int mode = 6; // UX01's filter bits: FD x 8 + the 4051's channel
	double vcff = 0, res = 0, volA = 4.86, volB = 4.86, vca = 4.86, pw1 = -0.2, pw2 = -0.2, vcof1 = 0, vcof2 = 0;
	uint8_t waves = 0, mdac = 0; // UX02: 1 FM to VCO 1, 2 SAW2, 4 TRI1, 8 SYNC, 16 TRI2, 32 SAW1
	int jump = 0;                // samples between FAST writes of VCFF alternating with jumpTo
	double jumpTo = 0;
	double seconds = 0.12;
	// A saw or a pulse under 50 Hz: its edges are clicks, each window's spectrum flat whatever the
	// solves did, so no flatness check. (The VCOs' frequency at VCOF, before the tune: 620 Hz at
	// 0 V, 4.22K/100R into 17.9 mV an octave.)
	bool clicks() const
	{
		auto hz = [](double vcof) { return 620 * std::exp2(vcof * 100 / 4320 / 0.0179); };
		const bool edges1 = (waves & 32) || (pw1 > 0 && pw1 < 4), edges2 = (waves & 2) || (pw2 > 0 && pw2 < 4);
		return (edges1 && hz(vcof1) < 50) || (edges2 && hz(vcof2) < 50);
	}
};

std::vector<VoiceCase> voiceCases()
{
	std::vector<VoiceCase> c;
	char n[128];
	for (int mode = 0; mode < 16; ++mode)
		for (double f : {-5.0, 2.0, 4.5, 5.0})
			for (double r : {0.0, 3.5, 4.67, 4.86})
				for (int src = 0; src < 2; ++src)
					for (int hi = 0; hi < 2; ++hi) {
						VoiceCase k;
						k.mode = mode, k.vcff = f, k.res = r;
						k.vcof1 = hi ? 5.0 : -5.0;
						k.vcof2 = hi ? 4.0 : -3.0;
						if (src == 0)
							k.waves = 2 | 32, k.pw1 = k.pw2 = 2.0; // saws and pulses
						else
							k.waves = 4 | 16; // triangles
						std::snprintf(n, sizeof n, "voice mode %d%s, VCFF %+.1f V, RES %.2f V, %s, VCOs %s", mode & 7, mode & 8 ? " FD" : "", f,
						              r, src ? "triangles" : "saws and pulses", hi ? "high" : "low");
						k.name = n;
						c.push_back(k);
					}
	for (int mode = 0; mode < 16; ++mode) {
		const char *fd = mode & 8 ? " FD" : "";
		{
			VoiceCase k;
			k.mode = mode, k.res = 4.86, k.vcff = -5, k.jump = 97, k.jumpTo = 5, k.waves = 2 | 32, k.pw1 = k.pw2 = 2, k.vcof1 = 3, k.vcof2 = 4;
			std::snprintf(n, sizeof n, "voice mode %d%s, RES 4.86 V, VCFF jumping -5/+5 V (FAST)", mode & 7, fd);
			k.name = n;
			c.push_back(k);
		}
		for (double f2 : {5.0, -1.0}) {
			VoiceCase k;
			k.mode = mode, k.res = 4.86, k.vcff = f2 > 0 ? 5 : 4, k.waves = 2 | 16, k.mdac = 255, k.vcof2 = f2, k.vcof1 = f2 > 0 ? 5 : 0;
			std::snprintf(n, sizeof n, "voice mode %d%s, RES 4.86 V, VCFF %+.0f V, VCF FM 255, VCOF2 %+.0f V", mode & 7, fd, k.vcff, f2);
			k.name = n;
			c.push_back(k);
		}
	}
	for (double f1 : {-5.0, 5.0})
		for (double f2 : {-5.0, 5.0})
			for (int sync = 0; sync < 2; ++sync)
				for (double pw : {-0.2, 0.1, 3.9, 4.86}) {
					VoiceCase k;
					k.vcof1 = f1, k.vcof2 = f2, k.mdac = 255, k.waves = uint8_t(1 | 2 | 4 | 16 | 32 | (sync ? 8 : 0));
					k.pw1 = k.pw2 = pw, k.vcff = 3, k.res = 2;
					std::snprintf(n, sizeof n, "voice VCOF1 %+.0f V, VCOF2 %+.0f V, VCO FM 255,%s PW %.1f V", f1, f2, sync ? " sync," : "", pw);
					k.name = n;
					c.push_back(k);
				}
	return c;
}

// ---- Checks.

// Median spectral flatness over 40 Hz-16 kHz, Hann windows of n samples above `floor` rms.
double medianFlatness(const std::vector<float> &x, double rate, int n, double floor)
{
	std::vector<double> flat;
	std::vector<std::complex<double>> a(static_cast<size_t>(n));
	for (size_t start = 0; start + size_t(n) <= x.size(); start += size_t(n)) {
		double e = 0;
		for (int i = 0; i < n; ++i)
			e += double(x[start + size_t(i)]) * x[start + size_t(i)];
		if (std::sqrt(e / n) < floor)
			continue;
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
		const int lo = int(std::ceil(40.0 * n / rate)), hi = int(16000.0 * n / rate);
		double lg = 0, ar = 0;
		for (int k = lo; k <= hi; ++k) {
			const double p = std::norm(a[size_t(k)]) + 1e-30;
			lg += std::log(p);
			ar += p;
		}
		const int m = hi - lo + 1;
		flat.push_back(std::exp(lg / m) / (ar / m));
	}
	if (flat.empty())
		return 0;
	std::nth_element(flat.begin(), flat.begin() + long(flat.size() / 2), flat.end());
	return flat[flat.size() / 2];
}

struct Result {
	xpander::Solvers solver;
	double flatness = 0;
	double peak = 0, maxPeak = 0;
	bool finite = true;
};

void writeWav(const std::string &dir, const std::string &name, double rate, const std::vector<float> &x)
{
	const juce::File f = juce::File::getCurrentWorkingDirectory().getChildFile(dir).getChildFile(
	    juce::String(int(rate)) + "-" + juce::String(name).replaceCharacters(" /,.+'", "______") + ".wav");
	f.getParentDirectory().createDirectory();
	f.deleteFile();
	juce::AudioBuffer<float> b(1, int(x.size()));
	b.copyFrom(0, 0, x.data(), int(x.size()));
	juce::WavAudioFormat wav;
	std::unique_ptr<juce::OutputStream> st(f.createOutputStream());
	const auto opts = juce::AudioFormatWriterOptions{}.withSampleRate(rate).withNumChannels(1).withBitsPerSample(32).withSampleFormat(
	    juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
	if (auto w = wav.createWriterFor(st, opts))
		w->writeFromAudioSampleBuffer(b, 0, b.getNumSamples());
}

Result runUnit(const UnitCase &k, double rate, const std::string &wavDir)
{
	auto p = makeUnit(k.patches, rate);
	const int total = int(k.seconds * rate);
	std::vector<float> out(static_cast<size_t>(total));
	juce::AudioBuffer<float> buf(2, kBlock);
	Result r;
	r.maxPeak = kMaxPeak;
	std::vector<Action> acts = k.acts;
	std::stable_sort(acts.begin(), acts.end(), [](const Action &a, const Action &b) { return a.at < b.at; });
	size_t next = 0;
	bool shown = false;
	std::vector<std::pair<double, std::array<int, 2>>> releases;
	for (int pos = 0; pos < total; pos += kBlock) {
		const int n = std::min(kBlock, total - pos);
		const double t1 = (pos + n) / rate;
		juce::MidiBuffer midi;
		for (auto it = releases.begin(); it != releases.end();)
			if (it->first < t1) {
				p->setSwitch(it->second[0], it->second[1], false);
				it = releases.erase(it);
			} else
				++it;
		for (; next < acts.size() && acts[next].at < t1; ++next) {
			const auto &a = acts[next];
			const int at = std::clamp(int(a.at * rate) - pos, 0, n - 1);
			switch (a.kind) {
				case Action::Tap:
					p->setSwitch(a.a, a.b, true);
					releases.push_back({a.at + 0.05, {a.a, a.b}});
					break;
				case Action::Enc: p->turnEncoder(a.a, a.b); break;
				case Action::On:
					if (g_show && !shown)
						std::printf("%s @%.0f\n  |%s|\n  |%s|\n  |%s|\n", k.name.c_str(), rate, p->displayText(0).c_str(),
						            p->displayText(1).c_str(), p->displayText(2).c_str());
					shown = true;
					midi.addEvent(juce::MidiMessage::noteOn(1, a.a, juce::uint8(a.b)), at);
					break;
				case Action::Off: midi.addEvent(juce::MidiMessage::noteOff(1, a.a), at); break;
			}
		}
		buf.setSize(2, n, false, false, true);
		buf.clear();
		p->processBlock(buf, midi);
		for (int i = 0; i < n; ++i) {
			const float s = 0.5f * (buf.getSample(0, i) + buf.getSample(1, i));
			r.finite = r.finite && std::isfinite(buf.getSample(0, i)) && std::isfinite(buf.getSample(1, i));
			r.peak = std::max({r.peak, double(std::fabs(buf.getSample(0, i))), double(std::fabs(buf.getSample(1, i)))});
			out[size_t(pos + i)] = s;
		}
	}
	r.solver = p->solverStats();
	if (!wavDir.empty())
		writeWav(wavDir, k.name, rate, out);
	r.flatness = k.noisy ? 0 : medianFlatness(out, rate, 2048, 3.2e-4);
	return r;
}

void write(xpander::Voice &v, int ch, double volts, bool fast, uint32_t cycles)
{
	v.select(ch, volts, fast);
	v.charge(cycles);
	v.deselect();
}

Result runVoice(const VoiceCase &k, double hostRate, const std::string &wavDir)
{
	const double rate = 4 * hostRate; // the voices' own rate
	xpander::Voice v;
	v.prepare(rate);
	v.setRevD(true);
	v.reset();
	v.seedNoise(0x9e3779b9u);
	const double cv[8] = {k.pw1, k.vca, k.volA, k.vcof1, k.volB, k.vcff, k.pw2, k.vcof2};
	for (int ch = 0; ch < 8; ++ch)
		write(v, ch, cv[ch], true, uint32_t(xpander::Voice::kBusHz));
	v.latch(0, k.mdac);
	v.latch(1, k.waves);
	v.latch(2, uint8_t(k.mode & 15));
	for (int w = 0; w < 16; ++w) // RES has no FAST path: written again and again, as each IRQ does
		write(v, 8, k.res, false, uint32_t(xpander::Voice::kBusHz));
	const long total = long(k.seconds * rate);
	std::vector<float> out(static_cast<size_t>(total));
	Result r;
	r.maxPeak = kMaxVoice;
	for (long i = 0; i < total; ++i) {
		if (k.jump > 0 && i % k.jump == 0)
			write(v, 5, (i / k.jump) % 2 ? k.jumpTo : k.vcff, true, 20); // a 10 us FAST write
		const double x = v.tick(0.0);
		r.finite = r.finite && std::isfinite(x);
		r.peak = std::max(r.peak, std::fabs(x));
		out[size_t(i)] = float(x / 10); // full scale 10 V, as at the outputs
	}
	r.solver = v.solverStats();
	if (!wavDir.empty())
		writeWav(wavDir, k.name, rate, out);
	// The same 23 Hz resolution as 2048 samples at the host's rate.
	r.flatness = k.clicks() ? 0 : medianFlatness(out, rate, 8192, 3.2e-4);
	return r;
}

} // namespace

int main(int argc, char **argv)
{
	juce::ScopedJuceInitialiser_GUI juce;
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
		else if (a == "--show")
			g_show = true;
		else {
			std::fprintf(stderr, "usage: xpandercorners [--rate HZ] [--only TEXT] [--list] [--jobs N] [--wav DIR] [--show]\n");
			return 2;
		}
	}
	if (!roms().ok()) {
		std::fprintf(stderr, "ROMs not found in %s\n", XPANDER_ROM_DIR);
		return 2;
	}
	if (roms().bank.getSize() == 0)
		std::fprintf(stderr, "note: no FACTORY1.SYX in %s: the factory patches are not played\n", XPANDER_ROM_DIR);

	// One case of either group, by index: the unit cases first.
	std::vector<UnitCase> units;
	std::vector<VoiceCase> voices;
	for (auto &k : unitCases())
		if (only.empty() || k.name.find(only) != std::string::npos)
			units.push_back(k);
	for (auto &k : voiceCases())
		if (only.empty() || k.name.find(only) != std::string::npos)
			voices.push_back(k);
	const size_t count = units.size() + voices.size();
	auto nameOf = [&](size_t i) { return i < units.size() ? units[i].name : voices[i - units.size()].name; };
	if (list) {
		for (size_t i = 0; i < count; ++i)
			std::printf("%s\n", nameOf(i).c_str());
		return 0;
	}
	// The factory states (the plugin's first start, cached), made here before the cases start.
	if (!units.empty()) {
		bool withBank = false;
		for (const auto &k : units)
			withBank = withBank || k.patches;
		for (int b = 0; b < (withBank ? 2 : 1); ++b) {
			auto p = makeUnit(b == 1, rates[0]);
			if (p->warmingUp())
				std::fprintf(stderr, "making the factory state%s (reset and TUNE ALL)...\n", b ? " with the patches" : "");
			p->waitForWarmUp();
		}
	}

	int bad = 0;
	for (double rate : rates) {
		std::vector<Result> res(count);
		std::atomic<size_t> next{0};
		std::vector<std::thread> pool;
		for (int j = 0; j < jobs; ++j)
			pool.emplace_back([&] {
				for (size_t i; (i = next++) < count;)
					res[i] = i < units.size() ? runUnit(units[i], rate, wavDir) : runVoice(voices[i - units.size()], rate, wavDir);
			});
		for (auto &t : pool)
			t.join();
		xpander::Solvers total;
		size_t worst = 0;
		for (size_t i = 0; i < count; ++i) {
			const Result &r = res[i];
			total += r.solver;
			if (r.flatness > res[worst].flatness)
				worst = i;
			std::string why;
			if (r.solver.vcf.failures > 0)
				why += " VCF unconverged " + std::to_string(r.solver.vcf.failures);
			if (r.solver.vco.failures > 0)
				why += " VCO unconverged " + std::to_string(r.solver.vco.failures);
			if (r.flatness > kNoiseFlatness)
				why += " noise (flatness " + juce::String(r.flatness, 3).toStdString() + ")";
			if (!r.finite || r.peak > r.maxPeak)
				why += " output " + std::string(r.finite ? "peak " + juce::String(r.peak, 2).toStdString() : "not finite");
			if (!why.empty()) {
				++bad;
				std::printf("  FAIL %5.0f  %-64s%s\n", rate, nameOf(i).c_str(), why.c_str());
			}
		}
		auto line = [](const char *what, const xpander::SolverStats &s) {
			std::printf("    %s %ld solves, %ld retried (%.4f %%), %ld unconverged, at most %d iterations (mean %.2f)\n", what, s.solves,
			            s.retried, s.solves ? 100.0 * double(s.retried) / double(s.solves) : 0.0, s.failures, s.worst,
			            s.solves ? double(s.iterations) / double(s.solves) : 0.0);
		};
		std::printf("%5.0f Hz: %zu cases (%zu unit, %zu voice)\n", rate, count, units.size(), voices.size());
		line("VCF:", total.vcf);
		line("VCO:", total.vco);
		if (count > 0)
			std::printf("    noisiest: %s (median flatness %.3f, limit %.2f)\n", nameOf(worst).c_str(), res[worst].flatness, kNoiseFlatness);
	}
	std::printf("%s: %d failing case(s)\n", bad ? "FAIL" : "PASS", bad);
	return bad ? 1 : 0;
}
