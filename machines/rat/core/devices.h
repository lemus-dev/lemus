// Nonlinear device equations for the RAT: the op amp's input stage and output stage, the
// clipping diodes and the output JFET. Each returns a current and its derivatives, and
// stays finite for any input (Newton's first iterates can be far from the answer).
#pragma once

#include <cmath>

namespace rat {

constexpr double kVt = 0.025852; // thermal voltage at 27 C

// exp() continued linearly above x = 80, so a wild Newton iterate cannot overflow.
inline double limexp(double x, double &d)
{
	constexpr double kLim = 80.0;
	if (x < kLim) {
		d = std::exp(x);
		return d;
	}
	constexpr double e = 5.54062238439351e34; // exp(80)
	d = e;
	return e * (1.0 + x - kLim);
}

// Smooth ramp: 0 below -s, x above s, and the quadratic (x + s)^2 / 4s between, so it and
// its slope are continuous. No transcendental functions: it runs in the inner loop.
inline double softplus(double x, double s, double &d)
{
	if (x <= -s) {
		d = 0;
		return 0;
	}
	if (x >= s) {
		d = 1;
		return x;
	}
	const double t = x + s;
	d = t / (2 * s);
	return t * t / (4 * s);
}

// A pair of identical junctions in anti-parallel (the 1N914s to ground):
// i = Is (e^(v/nVt) - e^(-v/nVt)).
inline double diodePair(double v, double is, double nVt, double &di)
{
	double dp;
	const double x = std::fabs(v) / nVt;
	const double ep = limexp(x, dp);
	const double en = 1.0 / dp; // e^-x (x >= 0, so no overflow)
	di = is * (dp + en) / nVt;
	return std::copysign(is * (ep - en), v);
}

// N-channel JFET, Shichman-Hodges (the SPICE level 1 NJF model): drain current for vgs, vds
// (vds >= 0 here: the drain is on the 9 V rail), with its partial derivatives.
struct Jfet {
	double vto = -2.0;    // pinch-off (V)
	double beta = 1e-3;   // A/V^2
	double lambda = 0.0;  // 1/V

	double current(double vgs, double vds, double &dg, double &dd) const
	{
		const double vov = vgs - vto;
		if (vov <= 0) {
			dg = dd = 0;
			return 0;
		}
		if (vds < 0) {
			// Not expected in this circuit; mirror into reverse operation crudely.
			dg = dd = 0;
			return 0;
		}
		const double cl = 1 + lambda * vds;
		if (vds >= vov) {
			const double i0 = beta * vov * vov;
			dg = 2 * beta * vov * cl;
			dd = i0 * lambda;
			return i0 * cl;
		}
		const double i0 = beta * vds * (2 * vov - vds);
		dg = 2 * beta * vds * cl;
		dd = beta * (2 * vov - 2 * vds) * cl + i0 * lambda;
		return i0 * cl;
	}
};

// Op amp macromodel: a transconductance input stage (a bipolar differential pair,
// i = It tanh(vd / 2Vte)) charging the compensation capacitor at the internal node Y, and a
// unity-gain output stage that follows Y between the output swing limits.
//  - Slew rate = It / Cc. Gain-bandwidth = It / (2 Vte) / (2 pi Cc). Both come from the same
//    two numbers, as in the real part, so the large-signal and small-signal behaviour agree.
//  - DC gain = It / (2 Vte) * Ry.
//  - Y is clamped a little beyond the output limits, as the second stage saturates near the
//    rails; recovering from that clamp is the op amp "sticking" after hard overdrive.
struct OpAmpModel {
	double it = 9e-6;      // input stage tail current (A)
	double asym = 0;       // slew asymmetry: rising It(1 + asym), falling It(1 - asym), |asym| < 0.5
	double vte = 0.0239;   // effective thermal voltage of the pair (V); > Vt with degeneration
	double outLo = 1.0;    // output swing limits (V, above ground)
	double outHi = 8.0;
	double knee = 0.08;    // roundness of the output limiting (V)
	double clampMargin = 0.3; // Y clamp beyond the output limits (V)
	double clampG = 1e-3;     // clamp conductance (S)

	// Current into Y for differential input vd and Y voltage vy.
	double inputCurrent(double vd, double vy, double &dvd, double &dvy) const
	{
		const double x = vd / (2 * vte);
		double th;
		if (x > 20)
			th = 1;
		else if (x < -20)
			th = -1;
		else
			th = std::tanh(x);
		// tanh (1 + a tanh) keeps the small-signal gm at the origin and moves only the two
		// saturated currents apart (asymmetric slew), smoothly and monotonically.
		dvd = it * (1 - th * th) * (1 + 2 * asym * th) / (2 * vte);
		double dh, dl;
		const double hi = softplus(vy - (outHi + clampMargin), 0.02, dh);
		const double lo = softplus((outLo - clampMargin) - vy, 0.02, dl);
		dvy = -clampG * (dh + dl);
		return it * th * (1 + asym * th) - clampG * (hi - lo);
	}

	// Open-circuit output voltage for Y voltage vy. A slight slope beyond the limits (1e-4,
	// a few tens of microvolts across the clamp range) keeps the loop gain from vanishing
	// when the output is pinned, which Newton needs to find its way back.
	double output(double vy, double &d) const
	{
		constexpr double kLeak = 1e-4;
		double d1, d2;
		const double a = softplus(vy - outLo, knee, d1);
		const double b = softplus(vy - outHi, knee, d2);
		d = d1 - d2 + kLeak;
		return outLo + a - b + kLeak * (vy - 0.5 * (outLo + outHi));
	}
};

} // namespace rat
