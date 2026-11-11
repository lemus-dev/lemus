#include "trimmers.h"

#include "machine.h"
#include "revisions.h"

#include <algorithm>
#include <cmath>

namespace tr808 {

namespace {
// IC1 (HD14584) on the CPU board runs on 5 V ("P14 = 5V"); the voicing board's IC1 on about 4.7 V.
// The squares use Werner's typical thresholds (docs/circuit.md §3.2: T = 0.483 RC).
constexpr double kVdd = 5.0;
const double kSquareK = std::log((kVdd - 2.1) / (kVdd - 2.7)) + std::log(2.7 / 2.1);

// The INT clock: IC1's input charges through R49 and TM1 from its lower threshold to its upper
// (the output high), then discharges through them and D13 + R48 10k to the output's 0 V (low:
// /INT). The diode's drop taken as 0.6 V.
double intPeriodWith(double rc, double c, double up, double down, double *low)
{
	const double high = rc * c * std::log((kVdd - down) / (kVdd - up));
	const double par = rc * 10e3 / (rc + 10e3), vinf = 0.6 * rc / (rc + 10e3);
	const double lo = c * par * std::log((up - vinf) / (down - vinf));
	if (low)
		*low = lo;
	return high + lo;
}

// No unit's HD14584 thresholds are known, and the notes fit C203 or leave it out by the chip's
// hysteresis (SN p.15 change 1): with the typical part's 0.6 V the late board couldn't reach 1.9 ms
// at all. The model takes a chip centred on 2.4 V whose hysteresis lets the late board's TM1 set
// 1.9 ms at its mid travel: 0.35 V, near the data sheet's least.
struct Thresholds {
	double up, down;
};
const Thresholds &intThresholds()
{
	static const Thresholds t = [] {
		double lo = 0.01, hi = 2.0;
		for (int k = 0; k < 100; ++k) {
			const double h = (lo + hi) / 2;
			(intPeriodWith(150e3, 0.086e-6, 2.4 + h / 2, 2.4 - h / 2, nullptr) < 0.0019 ? lo : hi) = h;
		}
		const double h = (lo + hi) / 2;
		return Thresholds{2.4 + h / 2, 2.4 - h / 2};
	}();
	return t;
}

double intRc(double r, const TrimParts &p) { return p.r49 + (1 - std::clamp(r, 0.0, 1.0)) * 100e3; }

// TM4 in IC24a's gain leg: 22k(B) as a rheostat, at least its end's resistance (1 % of the track);
// the first generator has R127 4.7k in series with it.
double noiseGain(double r, const TrimParts &p)
{
	const double tm = std::max(220.0, (1 - std::clamp(r, 0.0, 1.0)) * 22e3);
	return 1 + 1e6 / (tm + (p.earlyNoise ? 4.7e3 : 0.0));
}
} // namespace

TrimParts TrimParts::forSerial(int serial)
{
	TrimParts p;
	const auto parts = revisionParts(serial);
	auto get = [&](const char *k, double &v) {
		if (const auto it = parts.find(k); it != parts.end())
			v = it->second;
	};
	get("int.r49", p.r49);
	get("int.c11", p.c11);
	get("int.c203", p.c203);
	get("cbosc.r44", p.r44);
	get("cbosc.c6", p.c6);
	get("cbosc.r45", p.r45);
	p.earlyNoise = parts.count("noise.r131") > 0;
	return p;
}

double trimmerValue(Trimmer t, double r, const TrimParts &p)
{
	r = std::clamp(r, 0.0, 1.0);
	switch (t) {
	case Trimmer::IntClock: {
		const auto &th = intThresholds();
		return intPeriodWith(intRc(r, p), p.c11 + p.c203, th.up, th.down, nullptr);
	}
	case Trimmer::TempoClock:
		return 60.0 / (24.0 * TempoClock::bpm(1, 1, r));
	case Trimmer::CpOffset:
		return r;
	case Trimmer::Noise:
		return 0.130 * noiseGain(r, p) / noiseGain(0.5, p); // the circuit itself has its mid travel's gain
	case Trimmer::Cowbell1:
		return 1 / (kSquareK * (p.r44 + (1 - r) * 220e3) * p.c6);
	case Trimmer::Cowbell2:
		return 1 / (kSquareK * (p.r45 + (1 - r) * 220e3) * p.c5);
	}
	return 0;
}

double trimmerRotation(Trimmer t, double value, const TrimParts &p)
{
	if (t == Trimmer::CpOffset)
		return std::clamp(value, 0.0, 1.0);
	const double v0 = trimmerValue(t, 0, p), v1 = trimmerValue(t, 1, p);
	const bool rising = v1 > v0;
	if (rising ? value <= v0 : value >= v0)
		return 0;
	if (rising ? value >= v1 : value <= v1)
		return 1;
	double lo = 0, hi = 1;
	for (int k = 0; k < 60; ++k) {
		const double mid = (lo + hi) / 2;
		((trimmerValue(t, mid, p) < value) == rising ? lo : hi) = mid;
	}
	return (lo + hi) / 2;
}

double intClockLow(double r, const TrimParts &p)
{
	const auto &th = intThresholds();
	double low = 0;
	intPeriodWith(intRc(r, p), p.c11 + p.c203, th.up, th.down, &low);
	return low;
}

} // namespace tr808
