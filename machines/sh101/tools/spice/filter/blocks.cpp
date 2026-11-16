// sh101filterblocks: one block of the filter section's model on its own, for the SPICE
// comparisons in this folder (compare_*.py). Writes plain text to stdout.
//
//   sh101filterblocks [--fs HZ] vcf-ac IABC RES F0 F1 N        |H| from the mixer input (470k)
//   sh101filterblocks [--fs HZ] vcf-tran IABC RES WAVE HZ VPK SECONDS [DECIM]
//                                  WAVE: sine, saw (0..2 VPK ramp), step; writes t vin y1..y4 d e pin3
//   sh101filterblocks [--fs HZ] vcf-osc IABC RES SECONDS        steady self-oscillation: Hz, OUT4 pk
//   sh101filterblocks [--fs HZ] vcf-bench IABC RES WAVE HZ VPK SECONDS   Newton iterations per sample
//   sh101filterblocks [--fs HZ] env A D S R MODE GATE_S TOTAL_S [TRIG_AT ... | LFO_HZ]
//                                  writes t env gate57 reset (MODE gatetrig|gate|lfo); the gate
//                                  opens at 10 ms
//   sh101filterblocks cv FREQ ENVPOT ENVPEAK MOD MODV KYBD KCV BENDPOT BEND
//                                  prints pin 9 (V), Iabc, the pole Hz, the ENV (V)
//   sh101filterblocks vca-ctl VOLTS OHMS                          TR28's current
//   sh101filterblocks [--fs HZ] vca-gain IABC VIN_MV              pin 8 / pin 3 at 1 kHz
//   sh101filterblocks [--fs HZ] kybd-track F4KCV F5KCV FREQ       self-oscillation at two KCVs
//   sh101filterblocks [--fs HZ] vca-out VOL HZ                    pin 8 to the jack
//   sh101filterblocks [--fs HZ] gate-vca GATE_S TOTAL_S           GATE mode: t, I_ABC, C57's node
//   sh101filterblocks voice-level SAW PULSE SUB NOISE FREQ VOLUME  the whole voice: level at the jack
//   Before the command: --cal NAME=VALUE (FilterCalibration) for kybd-track and cv.
#include "sh101/core/env_gen.h"
#include "sh101/core/filter.h"
#include "sh101/core/vca_ba662.h"
#include "sh101/core/vcf_ir3109.h"
#include "sh101/core/voice.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace sh101;

namespace {
constexpr double kPi = 3.14159265358979323846;
double g_fs = 176400;
FilterCalibration g_cal = FilterCalibration::serviceNotes();

double arg(char **argv, int i) { return std::atof(argv[i]); }

// Steady-state amplitude and phase of x at frequency f, by quadrature over whole periods.
double amplitude(const std::vector<double> &x, double f, double fs)
{
	const int per = int(std::round(fs / f));
	const int n = int(x.size()) / per * per;
	if (n <= 0)
		return 0;
	double c = 0, s = 0;
	const int start = int(x.size()) - n;
	for (int i = start; i < int(x.size()); ++i) {
		const double ph = 2 * kPi * f * i / fs;
		c += x[size_t(i)] * std::cos(ph);
		s += x[size_t(i)] * std::sin(ph);
	}
	return 2 * std::sqrt(c * c + s * s) / n;
}

int vcfAc(char **argv)
{
	const double iabc = arg(argv, 0), res = arg(argv, 1), f0 = arg(argv, 2), f1 = arg(argv, 3);
	const int nf = std::atoi(argv[4]);
	const double vpk = 1e-3; // small signal at the mixer's far end
	for (int k = 0; k < nf; ++k) {
		const double f = f0 * std::pow(f1 / f0, nf > 1 ? double(k) / (nf - 1) : 0);
		Ir3109Vcf v;
		v.prepare(g_fs);
		// Settle the couplings, then long enough for the slowest pole and whole periods.
		const double settle = std::max(0.3, std::min(6.0, 300.0 / f));
		const int n0 = int(settle * g_fs), n1 = int(std::max(0.05, 20.0 / f) * g_fs);
		std::vector<double> out, y4;
		out.reserve(size_t(n1));
		for (int i = 0; i < n0 + n1; ++i) {
			const double x = vpk * std::sin(2 * kPi * f * i / g_fs);
			const double p3 = v.tick(iabc, res, x, 470e3);
			if (i >= n0) {
				out.push_back(p3);
				y4.push_back(v.out4());
			}
		}
		const double a3 = amplitude(out, f, g_fs) / vpk, a4 = amplitude(y4, f, g_fs) / vpk;
		std::printf("%.6g %.6f %.6f\n", f, 20 * std::log10(a3 + 1e-30), 20 * std::log10(a4 + 1e-30));
	}
	return 0;
}

double wave(const std::string &w, double hz, double vpk, double t)
{
	if (w == "sine")
		return vpk * std::sin(2 * kPi * hz * t);
	if (w == "saw") {
		const double ph = hz * t - std::floor(hz * t);
		return 2 * vpk * ph; // 0 .. 2 VPK, as the CEM3340's saw
	}
	if (w == "step")
		return t >= 1 / hz ? vpk : 0.0;
	if (w == "square")
		return (hz * t - std::floor(hz * t)) < 0.5 ? vpk : -vpk;
	return 0;
}

int vcfTran(char **argv, int argc)
{
	const double iabc = arg(argv, 0), res = arg(argv, 1);
	const std::string w = argv[2];
	const double hz = arg(argv, 3), vpk = arg(argv, 4), secs = arg(argv, 5);
	const int decim = argc > 6 ? std::atoi(argv[6]) : 1;
	Ir3109Vcf v;
	v.prepare(g_fs);
	const int n = int(secs * g_fs);
	for (int i = 0; i < n; ++i) {
		const double t = i / g_fs;
		const double x = wave(w, hz, vpk, t);
		const double p3 = v.tick(iabc, res, x, 470e3);
		if (i % decim == 0)
			std::printf("%.9g %.6g %.6g %.6g %.6g %.6g %.6g %.6g %.6g\n", t, x, v.stage(0), v.stage(1), v.stage(2),
			            v.stage(3), v.diode(), v.tr27(), p3);
	}
	return 0;
}

int vcfBench(char **argv)
{
	// Iterations and time per sample, for a waveform at a setting.
	const double iabc = arg(argv, 0), res = arg(argv, 1);
	const std::string w = argv[2];
	const double hz = arg(argv, 3), vpk = arg(argv, 4), secs = arg(argv, 5);
	Ir3109Vcf v;
	v.prepare(g_fs);
	const int n = int(secs * g_fs);
	long iters = 0;
	int worst = 0;
	double sink = 0;
	for (int i = 0; i < n; ++i) {
		sink += v.tick(iabc, res, wave(w, hz, vpk, i / g_fs), 470e3);
		iters += v.lastIterations();
		worst = std::max(worst, v.lastIterations());
		static int shown = 0;
		if (std::getenv("SH101_BENCH_DUMP") && v.lastIterations() >= 12 && shown++ < 12)
			std::fprintf(stderr, "t %.6f y %.3f %.3f %.3f %.3f d %.3f e %.3f\n", i / g_fs, v.stage(0), v.stage(1),
			             v.stage(2), v.stage(3), v.diode(), v.tr27());
	}
	std::printf("mean iterations %.3f, worst %d (%g)\n", double(iters) / n, worst, sink * 0);
	if (std::getenv("SH101_BENCH_DUMP")) {
		Ir3109Vcf v2;
		v2.prepare(g_fs);
		std::vector<int> hist(16);
		for (int i = 0; i < n; ++i) {
			v2.tick(iabc, res, wave(w, hz, vpk, i / g_fs), 470e3);
			hist[size_t(std::min(v2.lastIterations(), 15))]++;
		}
		for (size_t k = 0; k < hist.size(); ++k)
			if (hist[k])
				std::fprintf(stderr, "  %zu iterations: %d\n", k, hist[k]);
	}
	return 0;
}

int vcfOsc(char **argv)
{
	const double iabc = arg(argv, 0), res = arg(argv, 1), secs = arg(argv, 2);
	Ir3109Vcf v;
	v.prepare(g_fs);
	const int n = int(secs * g_fs);
	std::vector<double> y;
	y.reserve(size_t(n));
	for (int i = 0; i < n; ++i) {
		// A 1 mV kick through the mixer starts it, as noise would.
		const double x = i < 20 ? 1e-3 : 0.0;
		v.tick(iabc, res, x, 470e3);
		y.push_back(v.out4());
	}
	// The last quarter: zero crossings and peak.
	const size_t from = y.size() * 3 / 4;
	double pk = 0, mean = 0;
	for (size_t i = from; i < y.size(); ++i)
		mean += y[i];
	mean /= double(y.size() - from);
	int ups = 0;
	double first = -1, last = -1;
	for (size_t i = from + 1; i < y.size(); ++i) {
		pk = std::max(pk, std::abs(y[i] - mean));
		if (y[i - 1] - mean < 0 && y[i] - mean >= 0) {
			const double frac = (mean - y[i - 1]) / (y[i] - y[i - 1]);
			const double t = (double(i - 1) + frac) / g_fs;
			if (first < 0)
				first = t;
			last = t;
			++ups;
		}
	}
	const double hz = ups > 1 ? (ups - 1) / (last - first) : 0;
	std::printf("%.6g %.6g %.6g\n", hz, pk, mean);
	return 0;
}

int env(char **argv, int argc)
{
	const double a = arg(argv, 0), d = arg(argv, 1), s = arg(argv, 2), r = arg(argv, 3);
	const std::string mode = argv[4];
	const double gateS = arg(argv, 5), total = arg(argv, 6);
	// GATE+TRIG: trigger times (KCV steps); LFO: the square's rate, phase reset at the gate.
	std::vector<double> trigs;
	double lfoHz = 0;
	if (mode == "lfo" && argc > 7)
		lfoHz = arg(argv, 7);
	else
		for (int i = 7; i < argc; ++i)
			trigs.push_back(arg(argv, i));
	EnvGen e;
	e.prepare(g_fs);
	const EnvGen::Mode m = mode == "gate" ? EnvGen::Mode::Gate : mode == "lfo" ? EnvGen::Mode::Lfo : EnvGen::Mode::GateTrig;
	e.setControls(a, d, s, r, m);
	const int n = int(total * g_fs);
	double kcv = 2.0;
	size_t ti = 0;
	const int decim = std::max(1, int(g_fs / 20000));
	for (int i = 0; i < n; ++i) {
		const double t = i / g_fs;
		// Each trigger time steps the KCV by a semitone (the TRIG detector's input).
		while (ti < trigs.size() && t >= trigs[ti]) {
			kcv += 1.0 / 12;
			++ti;
		}
		const double tl = t - 0.01;
		const bool sq = lfoHz <= 0 || tl < 0 || (lfoHz * tl - std::floor(lfoHz * tl)) < 0.5;
		const double v = e.tick(t >= 0.01 && t < 0.01 + gateS, kcv, sq);
		if (i % decim == 0)
			std::printf("%.7g %.6g %.6g %d\n", t, v, e.gateLevel(), e.resetting() ? 1 : 0);
	}
	return 0;
}

int cv(char **argv)
{
	Controls c;
	c.vcfFreq = arg(argv, 0);
	c.vcfEnv = arg(argv, 1);
	const bool envPeak = arg(argv, 2) > 0.5; // 0: ENV at rest (gate off), 1: held at its peak
	c.vcfMod = arg(argv, 3);
	const double modV = arg(argv, 4);
	c.vcfKybd = arg(argv, 5);
	const double kcv = arg(argv, 6);
	const double bendPot = arg(argv, 7), bend = arg(argv, 8);
	Performance p;
	FilterSection f;
	f.prepare(g_fs);
	f.setCalibration(g_cal);
	f.setControls(c);
	f.setPerformance(p);
	// The ENV at rest (gate off), or held at its peak: attack 0, sustain 10, gate on.
	c.attack = 0;
	c.sustain = 1;
	c.trigger = Trigger::Gate;
	f.setControls(c);
	OscFrame o;
	o.modVolts = modV;
	o.kybdVolts = kcv;
	// The bender's VCF BEND as compare_cv.py has it: the lever at 0.43 V a side through VR5 100k B.
	o.vcfBend = {0.43 * bend * bendPot, 100e3 * bendPot * (1 - bendPot) + 1e-3};
	CpuLines l;
	l.gate = envPeak;
	f.setLines(l);
	ModFrame mf;
	double env = 0;
	for (int i = 0; i < int(0.05 * g_fs); ++i) {
		env = f.tickEnv(mf);
		f.tick(o);
	}
	std::printf("%.6g %.6g %.6g %.6g\n", f.pin9(), f.iAbc(), f.vcfHz(), env);
	return 0;
}

int vcaCtl(char **argv)
{
	Ba662Vca v;
	v.prepare(g_fs);
	std::printf("%.6g\n", v.controlCurrent(arg(argv, 0), arg(argv, 1)));
	return 0;
}

int vcaGain(char **argv)
{
	const double iabc = arg(argv, 0), vin = arg(argv, 1) * 1e-3;
	Ba662Vca v;
	v.prepare(g_fs);
	v.setVolume(1.0);
	std::vector<double> y;
	const int n = int(0.05 * g_fs);
	for (int i = 0; i < n; ++i) {
		v.tick(vin * std::sin(2 * kPi * 1000 * i / g_fs), iabc);
		y.push_back(v.vcaOut());
	}
	std::printf("%.6g\n", amplitude(y, 1000, g_fs) / vin);
	return 0;
}

int vcaOut(char **argv)
{
	// The output stage alone: pin 8 to the OUTPUT jack, at a frequency and VOLUME.
	const double vol = arg(argv, 0), f = arg(argv, 1);
	Ba662Vca v;
	v.prepare(g_fs);
	v.setVolume(vol);
	const double iabc = 0.3e-3, vin = 1e-3;
	std::vector<double> y8, yj;
	const int n0 = int(std::max(2.0, 20 / f) * g_fs), n1 = int(std::max(0.2, 20 / f) * g_fs);
	for (int i = 0; i < n0 + n1; ++i) {
		const double o = v.tick(vin * std::sin(2 * kPi * f * i / g_fs), iabc);
		if (i >= n0) {
			y8.push_back(v.vcaOut());
			yj.push_back(o);
		}
	}
	std::printf("%.6g\n", amplitude(yj, f, g_fs) / amplitude(y8, f, g_fs));
	return 0;
}

int gateVca(char **argv)
{
	// The VCA's GATE mode: the C57 node (TR30, R142, R41) loaded by R133 into TR28.
	const double gateS = arg(argv, 0), total = arg(argv, 1);
	EnvGen e;
	e.prepare(g_fs);
	Ba662Vca v;
	v.prepare(g_fs);
	const int n = int(total * g_fs);
	const int decim = std::max(1, int(g_fs / 100000));
	for (int i = 0; i < n; ++i) {
		const double t = i / g_fs;
		e.tick(t >= 0.001 && t < 0.001 + gateS, 2.0, true);
		const double ie = v.controlCurrent(e.gateLevel(), Ba662Vca::kR133);
		e.setGateLoad(ie);
		if (i % decim == 0)
			std::printf("%.8g %.6g %.6g\n", t, ie * 300 / 301, e.gateLevel());
	}
	return 0;
}

int voiceLevel(char **argv)
{
	// The whole voice (both sections, oversampled): a held note at KCV code 29 (F4 at M), 8',
	// GATE VCA, sources and VCF as given; RMS and peak at the OUTPUT jack after it settles.
	Controls c;
	c.sawLevel = arg(argv, 0);
	c.pulseLevel = arg(argv, 1);
	c.subLevel = arg(argv, 2);
	c.noiseLevel = arg(argv, 3);
	c.vcfFreq = arg(argv, 4);
	c.vcfRes = 0;
	c.vcfEnv = 0;
	c.vcfKybd = 0;
	c.volume = arg(argv, 5);
	c.vcaMode = VcaMode::Gate;
	c.pulseWidth = 0;
	c.pwmSource = PwmSource::Manual;
	Voice v;
	const double host = 48000;
	v.prepare(host);
	v.setControls(c);
	v.setCalibration(Calibration::serviceNotes());
	CpuLines l;
	l.kcv = 29;
	l.range = 12;
	l.gate = true;
	v.setLines(l);
	const int n = int(host * 2);
	std::vector<float> out(static_cast<size_t>(n));
	std::vector<ModFrame> mod(static_cast<size_t>(n));
	v.render(out.data(), n, mod.data());
	double sum = 0, pk = 0;
	const int from = n / 2;
	for (int i = from; i < n; ++i) {
		const double x = out[size_t(i)] / Voice::kOutputScale;
		sum += x * x;
		pk = std::max(pk, std::abs(x));
	}
	const double rms = std::sqrt(sum / (n - from));
	std::printf("rms %.4f V (%+.2f dBu)  peak %.4f V (%+.2f dBFS)\n", rms, 20 * std::log10(rms / 0.7746), pk,
	            20 * std::log10(pk * Voice::kOutputScale));
	return 0;
}

int kybdTrack(char **argv)
{
	const double k4 = arg(argv, 0), k5 = arg(argv, 1);
	Controls c;
	c.vcfFreq = arg(argv, 2);
	c.vcfRes = 1;
	c.vcfKybd = 1;
	c.vcfEnv = 0;
	c.volume = 1;
	c.vcaMode = VcaMode::Gate;
	double hz[2] = {};
	for (int k = 0; k < 2; ++k) {
		FilterSection f;
		f.prepare(g_fs);
		f.setCalibration(g_cal);
		f.setControls(c);
		CpuLines l;
		l.gate = true;
		f.setLines(l);
		OscFrame o;
		o.kybdVolts = k == 0 ? k4 : k5;
		ModFrame mf;
		const int n = int(0.6 * g_fs);
		std::vector<double> y;
		for (int i = 0; i < n; ++i) {
			o.mix[3] = {i < 20 ? 1e-3 : 0.0, 150e3};
			f.tickEnv(mf);
			f.tick(o);
			y.push_back(f.out4());
		}
		int ups = 0;
		double first = -1, last = -1;
		for (size_t i = y.size() / 2 + 1; i < y.size(); ++i)
			if (y[i - 1] < 0 && y[i] >= 0) {
				const double t = (double(i - 1) + y[i - 1] / (y[i - 1] - y[i])) / g_fs;
				if (first < 0)
					first = t;
				last = t;
				++ups;
			}
		hz[k] = ups > 1 ? (ups - 1) / (last - first) : 0;
	}
	std::printf("%.6g %.6g %.6f\n", hz[0], hz[1], std::log2(hz[1] / hz[0]) / ((k5 - k4)));
	return 0;
}
} // namespace

int main(int argc, char **argv)
{
	int i = 1;
	while (i + 1 < argc && argv[i][0] == '-' && argv[i][1] == '-') {
		if (std::strcmp(argv[i], "--fs") == 0)
			g_fs = std::atof(argv[i + 1]);
		else if (std::strcmp(argv[i], "--cal") == 0) {
			const std::string kv = argv[i + 1];
			const auto eq = kv.find('=');
			if (eq == std::string::npos || !g_cal.set(kv.substr(0, eq), std::atof(kv.c_str() + eq + 1))) {
				std::fprintf(stderr, "bad --cal %s\n", kv.c_str());
				return 2;
			}
		} else
			break;
		i += 2;
	}
	if (i >= argc) {
		std::fprintf(stderr, "usage: see the comment at the top of tools/spice/filter/blocks.cpp\n");
		return 2;
	}
	const std::string cmd = argv[i];
	char **a = argv + i + 1;
	const int n = argc - i - 1;
	if (cmd == "vcf-ac" && n >= 5) return vcfAc(a);
	if (cmd == "vcf-tran" && n >= 6) return vcfTran(a, n);
	if (cmd == "vcf-osc" && n >= 3) return vcfOsc(a);
	if (cmd == "vcf-bench" && n >= 6) return vcfBench(a);
	if (cmd == "env" && n >= 7) return env(a, n);
	if (cmd == "cv" && n >= 9) return cv(a);
	if (cmd == "vca-ctl" && n >= 2) return vcaCtl(a);
	if (cmd == "vca-gain" && n >= 2) return vcaGain(a);
	if (cmd == "kybd-track" && n >= 3) return kybdTrack(a);
	if (cmd == "vca-out" && n >= 2) return vcaOut(a);
	if (cmd == "gate-vca" && n >= 2) return gateVca(a);
	if (cmd == "voice-level" && n >= 6) return voiceLevel(a);
	std::fprintf(stderr, "unknown or incomplete command: %s\n", cmd.c_str());
	return 2;
}
