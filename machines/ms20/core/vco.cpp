#include "vco.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>

namespace ms20 {

namespace {

// The buffer and coupling (vco.md §9): the JFET follower's gain, C1 10 µF with R13 10K.
constexpr double kBufferGain = 0.906;
constexpr double kCouplingRc = 10e3 * 10e-6;

// The shapers' sources as Thevenin equivalents into the switch common (vco.md §1, §5).
// VCO 1: triangle IC2B through R6 10K onto R7 100K from +15 V; saw node 10 through R4 100K; pulse
// IC2A through R2 100K onto R1 6.8K. VCO 2: saw node 11 from the buffer (its output resistance,
// about 190 ohm, from vco.md §5's loaded 5.44 against unloaded 5.46 Vpp at LEVEL 10) through R62
// 100K, which the circuit diagrams leave out (they take node 11 straight to SW-4) but the manual's
// board layout places beside R63 and board photos show fitted (brown-black-yellow, Rev 1 and 2:
// vco.md §1). As drawn VCO 2's saw stood 4.7 dB over its square at the mixer; three real units
// have it 3.6-4.6 dB under and two level with VCO 1's saw (docs/compare.md §1.4); with R62 -3.8 dB;
// square/narrow IC6A through R63 100K onto R64 6.8K; ring the 4011's pin 4 through R65 47K onto
// R66 100K to -15 V and R67 4.7K.
constexpr double kTriA = 100.0 / 110.0, kTriB = 15.0 * 10.0 / 110.0, kTriR = 1.0 / (1 / 10e3 + 1 / 100e3);
constexpr double kSawR = 100e3;
constexpr double kPulseA = 6.8 / 106.8, kPulseR = 1.0 / (1 / 100e3 + 1 / 6.8e3);
constexpr double kSaw2R = 100e3 + 190;
constexpr double kRingG = 1 / 47e3 + 1 / 100e3 + 1 / 4.7e3;
constexpr double kRingR = 1 / kRingG;
double ringVth(double pin4) { return (pin4 / 47e3 - 15.0 / 100e3) / kRingG; }
constexpr double kNoiseR = 1e3; // KLM-128's white output: to be read from klm128.md

// PW (VR9 1MB) between node 56 (R10 300K from +15 V, R11 100K to ground) and node 57 (R12 1K to
// ground): its wiper, IC2A's + input.
double pwWiper(double knob)
{
	const double r11 = 1.0 / (1 / 100e3 + 1 / (1e6 + 1e3));
	const double v56 = 15.0 * r11 / (300e3 + r11);
	const double v57 = v56 * 1e3 / (1e6 + 1e3);
	return v57 + std::clamp(knob, 0.0, 10.0) / 10 * (v56 - v57);
}
// The narrow pulse's threshold: R70 100K from +15 V over R69 22K.
constexpr double kNarrow = 15.0 * 22.0 / 122.0;
// The 4011's input threshold (half its 15 V supply) plus D1/D8's drop: where a comparator's output
// counts as logic high.
constexpr double kLogic = 7.5 + 0.6;

// VCO LEVEL's load on the switch common: the pot (a = common, b = ground) with its wiper into
// R75/R77 100K at IC8A's virtual ground. `share` is the wiper-to-ground part of the track.
double loadOf(double share)
{
	const double bottom = share * 100e3, top = (1 - share) * 100e3;
	const double par = bottom <= 0 ? 0 : 1.0 / (1 / bottom + 1 / 100e3);
	return top + par;
}
// A pot's share at rotation x: linear, or Japanese A (audio) as the netlists' a0.15 (two segments
// through 15 % at mid rotation).
double shareOf(double x, bool audio)
{
	x = std::clamp(x, 0.0, 1.0);
	if (!audio)
		return x;
	return x < 0.5 ? x / 0.5 * 0.15 : 0.15 + (x - 0.5) / 0.5 * 0.85;
}

// polyBLEP: the correction for a step of height h at fraction f of the sample interval (0-1) to the
// sample before it (`before`) and the one after (`after`).
void blep(double h, double f, double &before, double &after)
{
	before += h * (1 - f) * (1 - f) / 2;
	after -= h * f * f / 2;
}

// polyBLAMP, its integral: the correction for a change of slope by d (volts a sample) at fraction f
// (Esqueda, Valimaki and Bilbao 2016, the two-point residual): a slewing comparator's corners.
void blamp(double d, double f, double &before, double &after)
{
	const double g = 1 - f;
	before += d * g * g * g / 6;
	after += d * f * f * f / 6;
}

} // namespace

// =========================================================================================== core

void Vco::setRate(double rate)
{
	m_dt = 1.0 / rate;
	m_hpA = 1 - std::exp(-m_dt / kCouplingRc);
}

void Vco::reset()
{
	m_v = m_core.top;
	m_dead = -1;
	m_hp = kBufferGain * (m_core.top + m_core.trip) / 2; // C1 charged to the ramp's middle
	m_nodeStart = m_nodeEnd = m_nodeLow = kBufferGain * m_v - m_hp;
	m_resetAt = -1;
}

double Vco::resetStep() const { return kBufferGain * (m_core.top - m_core.trip); }

void Vco::step(double amps)
{
	const double slope = std::max(0.0, amps) / m_core.c; // volts a second, falling
	m_nodeStart = kBufferGain * m_v - m_hp;
	m_resetAt = -1;
	double t = 0; // seconds into the sample
	if (m_dead >= 0) {
		if (m_dead < m_dt) {
			t = m_dead;
			m_resetAt = t / m_dt;
			m_nodeLow = kBufferGain * m_v - m_hp;
			m_v = m_core.top;
			m_dead = -1;
		} else {
			m_dead -= m_dt;
			t = m_dt;
		}
	}
	if (m_dead < 0 && t < m_dt) {
		double left = m_dt - t;
		while (left > 0) {
			const double toTrip = slope > 0 ? (m_v - m_core.trip) / slope : 1e9;
			if (toTrip >= left) {
				m_v -= slope * left;
				break;
			}
			// the trip within the sample: the latch holds the ramp for the dead time, then it's at the top
			left -= toTrip;
			m_v = m_core.trip;
			if (m_core.dead >= left) {
				m_dead = m_core.dead - left;
				break;
			}
			left -= m_core.dead;
			m_resetAt = 1 - left / m_dt;
			m_nodeLow = kBufferGain * m_v - m_hp;
			m_v = m_core.top;
		}
	}
	// the buffer and C1's high-pass (the coupling's own step over a sample is negligible: 1.6 Hz)
	const double x = kBufferGain * m_v;
	m_hp += (x - m_hp) * m_hpA;
	m_nodeEnd = x - m_hp;
}

void Vco::advance(double amps, double seconds)
{
	const double slope = std::max(0.0, amps) / m_core.c;
	if (slope <= 0 || seconds <= 0)
		return;
	const double fall = (m_core.top - m_core.trip) / slope; // seconds from the top to the trip
	const double period = fall + m_core.dead;
	// seconds since the ramp last left the top, then that much later within the cycle
	const double since = m_dead >= 0 ? fall + (m_core.dead - m_dead) : std::max(0.0, m_core.top - m_v) / slope;
	const double t = std::fmod(since + seconds, period);
	if (t < fall) {
		m_v = m_core.top - slope * t;
		m_dead = -1;
	} else {
		m_v = m_core.trip;
		m_dead = period - t;
	}
}

// ===================================================================================== comparator

double SlewComparator::step(bool high, double at)
{
	m_yStart = m_y;
	m_turnAt = -1;
	m_cornerCount = 0;
	// the slope (volts a sample) toward a target from y: m_step that way, or 0 there
	auto slopeTo = [&](double target, double y) { return y == target ? 0.0 : (target > y ? m_step : -m_step); };
	double t = 0;
	double slope = slopeTo(m_high ? kSat : -kSat, m_y);
	if (at >= 0 && at < 1) {
		// the old decision until `at`
		const double target = m_high ? kSat : -kSat;
		const double reach = std::abs(target - m_y) / m_step;
		if (slope != 0 && reach <= at) { // it reaches the rail first
			addCorner(reach, -slope);
			slope = 0;
		}
		m_y = reach <= at ? target : m_y + (target > m_y ? 1 : -1) * m_step * at;
		t = at;
		m_turnAt = at;
		// the new decision from `at`: the slope changes there
		const double turned = slopeTo(high ? kSat : -kSat, m_y);
		addCorner(at, turned - slope);
		slope = turned;
	}
	m_high = high;
	const double target = m_high ? kSat : -kSat;
	const double span = 1 - t;
	const double reach = std::abs(target - m_y) / m_step;
	if (slope != 0 && reach <= span) // the rail within the rest of the sample
		addCorner(t + reach, -slope);
	m_y = reach <= span ? target : m_y + (target > m_y ? 1 : -1) * m_step * span;
	return m_y;
}

int SlewComparator::crossed(double level, double &at) const
{
	// the output moved linearly (with one turn at m_turnAt): find the level's crossing
	auto seg = [&](double t0, double y0, double t1, double y1) -> int {
		if ((y0 < level) == (y1 < level) || y1 == y0)
			return 0;
		at = t0 + (t1 - t0) * (level - y0) / (y1 - y0);
		return y1 > y0 ? 1 : -1;
	};
	if (m_turnAt < 0)
		return seg(0, m_yStart, 1, m_y);
	// reconstruct the turn's value: the start slewed toward the old side until m_turnAt
	const double dir = m_y > m_yStart ? 1 : -1;
	(void)dir;
	const double mid = std::clamp(m_yStart + (m_high ? -1 : 1) * m_step * m_turnAt, -kSat, kSat);
	if (const int r = seg(0, m_yStart, m_turnAt, mid))
		return r;
	return seg(m_turnAt, mid, 1, m_y);
}

// ========================================================================================= shapers

void Vcos::setRate(double rate)
{
	m_vco1.setRate(rate);
	m_vco2.setRate(rate);
	m_ic2a.setRate(rate);
	m_ic6a.setRate(rate);
}

void Vcos::reset()
{
	m_vco1.reset();
	m_vco2.reset();
	m_ic2a.reset(false);
	m_ic6a.reset(false);
	m_ringHigh = false;
	m_common1 = m_common2 = m_pend1 = m_pend2 = 0;
}

namespace {

// A comparator whose - input is a falling ramp node (with an upward reset at `resetAt`) and whose +
// input is `level`: its decision at the start, and where it changes within the sample.
struct Decision {
	bool start = false, end = false;
	double at = -1;
};
Decision decide(const Vco &v, double level)
{
	Decision d;
	const double x0 = v.nodeStart(), x1 = v.nodeEnd();
	d.start = level > x0;
	d.end = level > x1;
	if (d.start == d.end)
		return d;
	if (v.resetLast()) {
		d.at = v.resetAt(); // the reset carries the node back above the level
		return d;
	}
	d.at = (x0 - level) / (x0 - x1); // the ramp falls through the level
	return d;
}

} // namespace

void Vcos::step(double ia1, double ia2, double white, const Controls &c)
{
	m_vco1.step(ia1);
	m_vco2.step(ia2);

	// the comparators (4558s slewing at 1 V/µs between ±14 V)
	const Decision d2a = decide(m_vco1, pwWiper(c.vco1Pw));
	m_ic2a.step(d2a.end, d2a.at);
	const bool narrow = c.vco2Wave == Vco2Wave::Narrow;
	const Decision d6a = decide(m_vco2, narrow ? kNarrow : 0.0);
	m_ic6a.step(d6a.end, d6a.at);

	// the ring modulator: the XOR of the two outputs as the 4011 reads them, its edges (0 <-> 15 V
	// at pin 4) as steps
	std::array<double, 4> ringEdges{};
	std::array<double, 4> ringHeights{};
	int ringCount = 0;
	{
		double at = 0;
		if (m_ic2a.crossed(kLogic, at) != 0)
			ringEdges[size_t(ringCount++)] = at;
		if (m_ic6a.crossed(kLogic, at) != 0)
			ringEdges[size_t(ringCount++)] = at;
		std::sort(ringEdges.begin(), ringEdges.begin() + ringCount);
		for (int k = 0; k < ringCount; ++k) {
			m_ringHigh = !m_ringHigh;
			ringHeights[size_t(k)] = m_ringHigh ? 15.0 : -15.0;
		}
	}

	// each common as continuous time has it at the sample's end, and its steps within the sample
	auto commit = [](double naive, double &pend, double before, double after) {
		const double out = pend + before;
		pend = naive + after;
		return out;
	};

	// VCO 1's switch common (VR13 100KA)
	{
		const double k = [&] { const double l = loadOf(shareOf(c.vco1Level / 10, true)); return l; }();
		double vth = 0, rth = 1, before = 0, after = 0;
		const double node = m_vco1.nodeEnd();
		switch (c.vco1Wave) {
		case Vco1Wave::Triangle:
			vth = kTriA * -std::abs(node) + kTriB;
			rth = kTriR;
			if (m_vco1.resetLast()) // -|node| from just before the step to just after
				blep(kTriA * (std::abs(m_vco1.nodeBeforeReset()) - std::abs(m_vco1.nodeBeforeReset() + m_vco1.resetStep())),
				     m_vco1.resetAt(), before, after);
			break;
		case Vco1Wave::Saw:
			vth = node;
			rth = kSawR;
			if (m_vco1.resetLast())
				blep(m_vco1.resetStep(), m_vco1.resetAt(), before, after);
			break;
		case Vco1Wave::Pulse:
			vth = kPulseA * m_ic2a.value();
			rth = kPulseR;
			for (int k = 0; k < m_ic2a.corners(); ++k)
				blamp(kPulseA * m_ic2a.corner(k).slope, m_ic2a.corner(k).at, before, after);
			break;
		case Vco1Wave::Noise: vth = white; rth = kNoiseR; break;
		}
		const double scale = k / (rth + k);
		m_common1 = commit(vth * scale, m_pend1, before * scale, after * scale);
	}
	// VCO 2's (VR14 100KB)
	{
		const double k = loadOf(shareOf(c.vco2Level / 10, false));
		double vth = 0, rth = 1, before = 0, after = 0;
		switch (c.vco2Wave) {
		case Vco2Wave::Saw:
			vth = m_vco2.nodeEnd();
			rth = kSaw2R;
			if (m_vco2.resetLast())
				blep(m_vco2.resetStep(), m_vco2.resetAt(), before, after);
			break;
		case Vco2Wave::Square:
		case Vco2Wave::Narrow:
			vth = kPulseA * m_ic6a.value();
			rth = kPulseR;
			for (int k = 0; k < m_ic6a.corners(); ++k)
				blamp(kPulseA * m_ic6a.corner(k).slope, m_ic6a.corner(k).at, before, after);
			break;
		case Vco2Wave::Ring: {
			vth = ringVth(m_ringHigh ? 15.0 : 0.0);
			rth = kRingR;
			const double perVolt = (1 / 47e3) / kRingG;
			for (int e = 0; e < ringCount; ++e)
				blep(ringHeights[size_t(e)] * perVolt, ringEdges[size_t(e)], before, after);
			break;
		}
		}
		const double scale = k / (rth + k);
		m_common2 = commit(vth * scale, m_pend2, before * scale, after * scale);
	}
}

} // namespace ms20
