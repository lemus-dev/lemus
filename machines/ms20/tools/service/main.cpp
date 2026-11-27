// ms20service: the service manual's adjustment procedure (§7) on the model.
//
//   ms20service [check]     every check, as the procedure's figures 1 and 2 set the panel
//   ms20service calibrate [kbd|vco1|vco2|vcf]   sets the trims (all, or one part's) the way §7 does (each pair high/low, repeated;
//                           a VCO whose C-1..C-4 then miss ±2 cents gets its high trim moved to split the error), prints the
//                           Calibration to paste into core/machine.h, then checks
//
// §7-1 (supply ripple) has no counterpart: the supplies are ideal sources here.
// §7-2: VCO 1 (then VCO 2) alone at 8', pulse (VCO 1) or square (VCO 2), MASTER TUNE and PITCH 0:
//       C-1..C-4 within ±2 cents of C2..C5 at the procedure's tuning (the trims set C-1 and C-4);
//       each C at 32', 16', 8', 4' (VCO 2: 16'..2') within ±10 cents.
// §7-3: KBD CV OUT: C-3 exactly half of C-4, C-1 half of C-2.
// §7-4: VCO levels 0, CUTOFF 5 on both filters; HPF PEAK 10 with LPF PEAK 0 (and the other way
//       round): the filter oscillates at 500 Hz at PHONES (here SIGNAL OUT).
#include "ms20/core/machine.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

namespace {

using ms20::Controls;
using ms20::Machine;

constexpr double kRate = 48000;
int g_fail = 0;

void line(bool ok, const char *what, const char *value)
{
	std::printf("  %s  %-58s %s\n", ok ? "ok  " : "FAIL", what, value);
	if (!ok)
		++g_fail;
}

// The fundamental of x (autocorrelation, parabolic peak), between 15 Hz and 5 kHz.
double frequency(const std::vector<float> &x)
{
	const size_t n = x.size();
	std::vector<double> y(x.begin(), x.end());
	double mean = 0;
	for (double v : y)
		mean += v;
	mean /= double(n);
	for (double &v : y)
		v -= mean;
	const int lo = int(kRate / 5000), hi = int(kRate / 15);
	std::vector<double> r(size_t(hi + 2), 0.0);
	for (int lag = lo - 1; lag <= hi + 1; ++lag) {
		double s = 0;
		for (size_t i = 0; i + size_t(lag) < n; ++i)
			s += y[i] * y[i + size_t(lag)];
		r[size_t(lag)] = s / double(n - size_t(lag));
	}
	// the first peak above 80 % of the best: the period, not a multiple of it
	double best = 0;
	for (int lag = lo; lag <= hi; ++lag)
		best = std::max(best, r[size_t(lag)]);
	int at = lo;
	for (int lag = lo; lag <= hi; ++lag)
		if (r[size_t(lag)] > 0.8 * best && r[size_t(lag)] >= r[size_t(lag - 1)] && r[size_t(lag)] >= r[size_t(lag + 1)]) {
			at = lag;
			break;
		}
	const double a = r[size_t(at - 1)], b = r[size_t(at)], c = r[size_t(at + 1)];
	const double p = at + 0.5 * (a - c) / (a - 2 * b + c);
	return kRate / p;
}

// A key held for `seconds`, the output's last `measure` seconds.
std::vector<float> play(Machine &m, int key, double seconds, double measure)
{
	m.prepare(kRate);
	if (key >= 0)
		m.setKey(key, true);
	const int n = int(seconds * kRate);
	std::vector<float> out(static_cast<size_t>(n));
	m.render(out.data(), n, nullptr);
	if (key >= 0)
		m.setKey(key, false);
	return std::vector<float>(out.end() - int(measure * kRate), out.end());
}

double cents(double f, double target) { return 1200 * std::log2(f / target); }

// Figure 1's panel for VCO `vco` (1 or 2) alone at `scale` (the switch position, 0-3).
Controls tuning(int vco, int scale)
{
	Controls c;
	c.vco1Wave = ms20::Vco1Wave::Pulse;
	c.vco1Pw = 0;
	c.vco2Wave = ms20::Vco2Wave::Square;
	c.vco1Level = vco == 1 ? 10 : 0;
	c.vco2Level = vco == 2 ? 10 : 0;
	c.vco1Scale = vco == 1 ? scale : 2;
	c.vco2Scale = vco == 2 ? scale : 1;
	c.hpfCutoff = 0;
	c.lpfCutoff = 10;
	c.eg2Sustain = 10;
	c.volume = 10;
	return c;
}

// C-1..C-4 are keys 0, 12, 24, 36; at 8' they are C2..C5 (130.81 Hz ... 1046.5 Hz).
double target(int key, int vco, int scale)
{
	const int eight = vco == 1 ? 2 : 1; // the 8' position
	return 130.8128 * std::pow(2.0, key / 12.0) * std::pow(2.0, scale - eight);
}

Machine &shared()
{
	static Machine m; // parsing the netlists once: each evaluation starts it again from its operating point
	return m;
}

double vcoFreq(Machine::Calibration cal, int vco, int scale, int key)
{
	Machine &m = shared();
	for (int k = 0; k < Machine::kKeys; ++k)
		m.setKey(k, false);
	m.setCalibration(cal);
	m.setControls(tuning(vco, scale));
	return frequency(play(m, key, 0.6, 0.4));
}

double kbdCv(Machine::Calibration cal, int key)
{
	Machine &m = shared();
	for (int k = 0; k < Machine::kKeys; ++k)
		m.setKey(k, false);
	m.setControls(Controls());
	m.setCalibration(cal);
	m.prepare(kRate);
	m.setKey(key, true);
	std::vector<float> out(size_t(0.1 * kRate));
	m.render(out.data(), int(out.size()), nullptr);
	return m.board().signal("kbdcv");
}

// Fig. 2: VCO levels 0, both CUTOFF 5, one PEAK 10.
double filterFreq(Machine::Calibration cal, bool hpf, bool rev2 = false)
{
	Controls c;
	c.vco1Level = c.vco2Level = 0;
	c.hpfCutoff = c.lpfCutoff = 5;
	c.hpfPeak = hpf ? 10 : 0;
	c.lpfPeak = hpf ? 0 : 10;
	c.eg2Sustain = 10;
	c.volume = 10;
	Machine &m = shared();
	for (int k = 0; k < Machine::kKeys; ++k)
		m.setKey(k, false);
	m.setFilterRev(rev2 ? Machine::FilterRev::Rev2 : Machine::FilterRev::Rev1);
	m.setCalibration(cal);
	m.setControls(c);
	const double f = frequency(play(m, 12, 0.8, 0.4));
	m.setFilterRev(Machine::FilterRev::Rev1);
	return f;
}

// Solves f(x) = 0 for x in [0, 1] by bisection on a monotonic function. A trim that can't reach
// (no sign change between its ends) stays at the end nearer the target, as a technician's would.
double solve(const std::function<double(double)> &f)
{
	double a = 0, b = 1, fa = f(a);
	const double fb = f(b);
	if ((fa < 0) == (fb < 0))
		return std::abs(fa) <= std::abs(fb) ? 0.0 : 1.0;
	for (int i = 0; i < 24; ++i) {
		const double mid = 0.5 * (a + b), fm = f(mid);
		if ((fm < 0) == (fa < 0)) {
			a = mid;
			fa = fm;
		} else
			b = mid;
	}
	return 0.5 * (a + b);
}

Machine::Calibration calibrate(Machine::Calibration cal, const std::string &only)
{
	auto want = [&](const char *part) { return only.empty() || only == part; };
	// §7-3: KBD CV high (VR4) so C-3 is half of C-4, low (VR3) so C-1 is half of C-2, repeated
	for (int round = 0; round < 3 && want("kbd"); ++round) {
		cal.vr4 = solve([&](double x) { auto c = cal; c.vr4 = x; return kbdCv(c, 24) / kbdCv(c, 36) - 0.5; });
		cal.vr3 = solve([&](double x) { auto c = cal; c.vr3 = x; return kbdCv(c, 0) / kbdCv(c, 12) - 0.5; });
	}
	// §7-2: each VCO's high trim at C-4, low at C-1, repeated
	for (int vco = 1; vco <= 2; ++vco)
		for (int round = 0; round < 3 && want(vco == 1 ? "vco1" : "vco2"); ++round) {
			const int eight = vco == 1 ? 2 : 1;
			double &hi = vco == 1 ? cal.vr1 : cal.vr6;
			double &low = vco == 1 ? cal.vr2 : cal.vr5;
			hi = solve([&](double x) {
				auto c = cal;
				(vco == 1 ? c.vr1 : c.vr6) = x;
				return cents(vcoFreq(c, vco, eight, 36), target(36, vco, eight));
			});
			low = solve([&](double x) {
				auto c = cal;
				(vco == 1 ? c.vr2 : c.vr5) = x;
				return cents(vcoFreq(c, vco, eight, 0), target(0, vco, eight));
			});
		}
	// §7-2's check after the trims: C-1..C-4 within ±2 cents. Where it fails (VCO 1 with the sheets'
	// typical parts: C-2 / C-3 +2.0 / +2.2 cents, the 2SC1583's Early effect through the cascode,
	// docs/improvements.md), a technician moves the high trim until the errors split evenly about
	// zero, as the check's tuner shows them: the bow stays, centred
	for (int vco = 1; vco <= 2; ++vco) {
		if (!want(vco == 1 ? "vco1" : "vco2"))
			continue;
		const int eight = vco == 1 ? 2 : 1;
		auto spread = [&](const Machine::Calibration &c, double &lo, double &hi) {
			lo = 1e9;
			hi = -1e9;
			for (int key = 0; key <= 36; key += 12) {
				const double e = cents(vcoFreq(c, vco, eight, key), target(key, vco, eight));
				lo = std::min(lo, e);
				hi = std::max(hi, e);
			}
		};
		double lo = 0, hi = 0;
		spread(cal, lo, hi);
		if (lo >= -2 && hi <= 2)
			continue;
		double &trim = vco == 1 ? cal.vr1 : cal.vr6;
		trim = solve([&](double x) {
			auto c = cal;
			(vco == 1 ? c.vr1 : c.vr6) = x;
			double l = 0, h = 0;
			spread(c, l, h);
			return l + h;
		});
	}
	// §7-4: FC ADJ to 500 Hz
	if (want("vcf")) {
		cal.vr7 = solve([&](double x) { auto c = cal; c.vr7 = x; return filterFreq(c, true) - 500; });
		cal.vr8 = solve([&](double x) { auto c = cal; c.vr8 = x; return filterFreq(c, false) - 500; });
		// Rev 2 (KLM-307): the same check, KLM-307's VR1/VR2 left where they are (mid)
		cal.vr7b = solve([&](double x) { auto c = cal; c.vr7b = x; return filterFreq(c, true, true) - 500; });
		cal.vr8b = solve([&](double x) { auto c = cal; c.vr8b = x; return filterFreq(c, false, true) - 500; });
	}
	return cal;
}

void check(const Machine::Calibration &cal)
{
	char v[96];
	std::printf("§7-3 KBD CV\n");
	const double c1 = kbdCv(cal, 0), c2 = kbdCv(cal, 12), c3 = kbdCv(cal, 24), c4 = kbdCv(cal, 36);
	std::snprintf(v, sizeof v, "%.4f / %.4f / %.4f / %.4f V", c1, c2, c3, c4);
	line(std::abs(c3 / c4 - 0.5) < 1e-3, "C-1 .. C-4 at KBD CV OUT", v);
	std::snprintf(v, sizeof v, "C-3/C-4 %.5f, C-1/C-2 %.5f", c3 / c4, c1 / c2);
	line(std::abs(c3 / c4 - 0.5) < 1e-3 && std::abs(c1 / c2 - 0.5) < 1e-3, "each C half the next (0.1 %)", v);

	for (int vco = 1; vco <= 2; ++vco) {
		std::printf("§7-2 VCO %d\n", vco);
		const int eight = vco == 1 ? 2 : 1;
		for (int key = 0; key <= 36; key += 12) {
			const double f = vcoFreq(cal, vco, eight, key), e = cents(f, target(key, vco, eight));
			char what[64];
			std::snprintf(what, sizeof what, "8' C-%d within ±2 cents", key / 12 + 1);
			std::snprintf(v, sizeof v, "%.2f Hz, %+.2f cents", f, e);
			line(std::abs(e) <= 2, what, v);
		}
		static const char *names1[] = {"32'", "16'", "8'", "4'"};
		static const char *names2[] = {"16'", "8'", "4'", "2'"};
		for (int scale = 0; scale < 4; ++scale)
			for (int key = 0; key <= 36; key += 12) {
				const double f = vcoFreq(cal, vco, scale, key), e = cents(f, target(key, vco, scale));
				char what[64];
				std::snprintf(what, sizeof what, "%s C-%d within ±10 cents", (vco == 1 ? names1 : names2)[scale], key / 12 + 1);
				std::snprintf(v, sizeof v, "%.2f Hz, %+.2f cents", f, e);
				line(std::abs(e) <= 10, what, v);
			}
	}

	std::printf("§7-4 VCF fc\n");
	const double h = filterFreq(cal, true), l = filterFreq(cal, false);
	std::snprintf(v, sizeof v, "%.2f Hz", h);
	line(std::abs(h - 500) < 5, "HPF at CUTOFF 5, PEAK 10: 500 Hz", v);
	std::snprintf(v, sizeof v, "%.2f Hz", l);
	line(std::abs(l - 500) < 5, "LPF at CUTOFF 5, PEAK 10: 500 Hz", v);
	std::printf("§7-4 VCF fc, Rev 2 (KLM-307)\n");
	const double h2 = filterFreq(cal, true, true), l2 = filterFreq(cal, false, true);
	std::snprintf(v, sizeof v, "%.2f Hz", h2);
	line(std::abs(h2 - 500) < 5, "HPF at CUTOFF 5, PEAK 10: 500 Hz", v);
	std::snprintf(v, sizeof v, "%.2f Hz", l2);
	line(std::abs(l2 - 500) < 5, "LPF at CUTOFF 5, PEAK 10: 500 Hz", v);
}

} // namespace

int main(int argc, char **argv)
{
	Machine::Calibration cal = shared().calibration();
	if (argc > 1 && std::strcmp(argv[1], "calibrate") == 0) {
		cal = calibrate(cal, argc > 2 ? argv[2] : "");
		std::printf("Calibration: vr4 %.4f vr3 %.4f | vr1 %.4f vr2 %.4f | vr6 %.4f vr5 %.4f | vr7 %.4f vr8 %.4f | Rev 2 vr7 %.4f vr8 %.4f\n", cal.vr4, cal.vr3,
		            cal.vr1, cal.vr2, cal.vr6, cal.vr5, cal.vr7, cal.vr8, cal.vr7b, cal.vr8b);
	}
	check(cal);
	std::printf("\n%s\n", g_fail == 0 ? "All service checks pass." : "Some service checks FAIL.");
	return g_fail == 0 ? 0 : 1;
}
