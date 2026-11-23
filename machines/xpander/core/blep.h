// The band-limited step and corner of machines/proone (a cubic B-spline kernel, support -2..2).
#pragma once

namespace xpander {

class Blep {
public:
	void reset(double level);
	// Adds a step of height h at fraction `frac` (0..1] of the interval ending at the sample about
	// to be pushed, measured from its start.
	void step(double h, double frac);
	// Adds a change of slope of `dSlope` (per sample) at `frac`: a corner (BLAMP, first order).
	void corner(double dSlope, double frac);
	// Pushes the naive value of the current sample; returns the band-limited one, two samples late.
	double push(double naive);
	static double residual(double x); // B(x) - H(x) for the B-spline kernel, x in samples
	static double residualRamp(double x); // its integral: the ramp's residual

private:
	double m_buf[4] = {0, 0, 0, 0};
	int m_pos = 0;
};

} // namespace xpander
