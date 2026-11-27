// The QuadraVerb's linear analog stages (service manual sheet 1, rev F) as discrete filters: their
// designs from the parts' values, shared by the unit (core/unit.cpp) and the check against ngspice
// (tools/analog, tools/spice/ac.py), which compares each with the same stage as a SPICE deck.
// Each is the stage's s-domain transfer by the bilinear transform.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>

namespace qv::analog {

constexpr double kPi = 3.14159265358979323846;

// y = b0 x + b1 x1 - a1 y1
struct FirstOrder {
	double b0 = 1, b1 = 0, a1 = 0, x1 = 0, y1 = 0;
	double operator()(double x)
	{
		const double y = b0 * x + b1 * x1 - a1 * y1;
		x1 = x;
		y1 = y;
		return y;
	}
	void reset() { x1 = y1 = 0; }
	std::complex<double> response(double f, double rate) const
	{
		const auto z1 = std::polar(1.0, -2 * kPi * f / rate);
		return (b0 + b1 * z1) / (1.0 + a1 * z1);
	}
};

// transposed direct form II
struct Biquad {
	double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
	double operator()(double x)
	{
		const double y = b0 * x + z1;
		z1 = b1 * x - a1 * y + z2;
		z2 = b2 * x - a2 * y;
		return y;
	}
	void reset() { z1 = z2 = 0; }
	std::complex<double> response(double f, double rate) const
	{
		const auto z = std::polar(1.0, -2 * kPi * f / rate);
		return (b0 + b1 * z + b2 * z * z) / (1.0 + a1 * z + a2 * z * z);
	}
};

// (n0 + n1 s) / (d0 + d1 s), by the bilinear transform with k = 2 / T (or prewarped)
inline void bilinear(FirstOrder &f, double n0, double n1, double d0, double d1, double k)
{
	const double d = d0 + d1 * k;
	f.b0 = (n0 + n1 * k) / d;
	f.b1 = (n0 - n1 * k) / d;
	f.a1 = (d0 - d1 * k) / d;
}

// k for the bilinear transform, prewarped to be exact at f (0: none)
inline double warp(double rate, double f) { return f > 0 ? 2 * kPi * f / std::tan(kPi * f / rate) : 2 * rate; }

// U1's inverting stages (R1, R4 1K; the INPUT pot, a 10K B dual; C1, C2 4.7u): for rotation t the
// track from the input end to the wiper is R_a = 10K (1 - t), the rest R_b = 10K t the feedback (the
// gain rises clockwise). H(s) = -R_b s C / (1 + s (1K + R_a) C).
constexpr double kInR = 1e3, kInPot = 10e3, kInC = 4.7e-6;
inline void inputStage(FirstOrder &f, double t, double rate)
{
	t = std::clamp(t, 0.0, 1.0);
	const double ra = kInPot * (1 - t), rb = kInPot * t;
	bilinear(f, 0, -rb * kInC, 1, (kInR + ra) * kInC, warp(rate, 0));
}
// the stage's + input noise to its output: 1 + R_b / (1K + R_a) in its band
inline double inputNoiseGain(double t)
{
	t = std::clamp(t, 0.0, 1.0);
	return 1 + kInPot * t / (kInR + kInPot * (1 - t));
}

// U4 pins 1-3: non-inverting, R23 75K from the output to the - input, R24 20K and C43 330p from it
// to ground: (1 + s (R23 + R24) C43) / (1 + s R24 C43), prewarped at 10 kHz.
inline void preEmphasis(FirstOrder &f, double rate)
{
	const double c = 330e-12;
	bilinear(f, 1, 95e3 * c, 1, 20e3 * c, warp(rate, 10e3));
}

// U4's Sallen-Key low-passes after U3's buffers: R30, R29 (R31, R28) 1.1K in series, C50 (C46)
// .01u from their junction to the output, C49 (C48) 3300p from the + input to ground:
// 1 / (1 + s 2R C2 + s^2 R^2 C1 C2), prewarped at 15 kHz.
inline void reconstruction(Biquad &b, double rate)
{
	const double r = 1.1e3, c1 = 0.01e-6, c2 = 3300e-12;
	const double k = warp(rate, 15e3);
	const double a1s = 2 * r * c2, a2s = r * r * c1 * c2;
	const double d = 1 + a1s * k + a2s * k * k;
	b.b0 = 1 / d;
	b.b1 = 2 / d;
	b.b2 = 1 / d;
	b.a1 = (2 - 2 * a2s * k * k) / d;
	b.a2 = (1 - a1s * k + a2s * k * k) / d;
}

// U2's summers: R7, R8 20K with C26, C27 150p across them, inverting; their gain to the output
// from a current in is -20K / (1 + s 20K 150p) (a pole at 53 kHz), here without the 20K and the sign,
// prewarped at 15 kHz.
inline void summerPole(FirstOrder &f, double rate) { bilinear(f, 1, 0, 1, 20e3 * 150e-12, warp(rate, 15e3)); }

// C4, C5 4.7u into the OUTPUT pot's 10K: s 10K C / (1 + s 10K C).
inline void outputCoupling(FirstOrder &f, double rate) { bilinear(f, 0, 10e3 * 4.7e-6, 1, 10e3 * 4.7e-6, warp(rate, 0)); }

// U2's output stages, non-inverting: R10, R13 2K feedback, R11, R12 2K and C8, C6 4.7u to ground:
// 1 + R10 / (R11 + 1 / s C) = (1 + s (R10 + R11) C) / (1 + s R11 C).
inline void outputStage(FirstOrder &f, double rate) { bilinear(f, 1, 4e3 * 4.7e-6, 1, 2e3 * 4.7e-6, warp(rate, 0)); }

// The current through a series R-C from a driven node into a virtual ground, I = V sC / (1 + sRC):
// R32, R33 20K and C9, C10 4.7u into the CEM3381's inputs.
struct SeriesRc {
	double b = 0, a = 0, x1 = 0, y1 = 0;
	void setup(double r, double c, double rate)
	{
		const double k = 2 * rate * r * c; // 2 RC / T
		b = 2 * rate * c / (1 + k);
		a = (1 - k) / (1 + k);
		x1 = y1 = 0;
	}
	double operator()(double v)
	{
		const double y = b * (v - x1) - a * y1;
		x1 = v;
		y1 = y;
		return y;
	}
	std::complex<double> response(double f, double rate) const
	{
		const auto z1 = std::polar(1.0, -2 * kPi * f / rate);
		return b * (1.0 - z1) / (1.0 + a * z1);
	}
};
constexpr double kDryR = 20e3, kDryC = 4.7e-6;

} // namespace qv::analog
