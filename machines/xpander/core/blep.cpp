// The band-limited step and corner (BLEP, BLAMP) of machines/proone/core/vco.cpp: a cubic B-spline
// kernel over four samples, outputs two samples late.
#include "blep.h"

#include <cmath>

namespace xpander {

// ---------------------------------------------------------------------------------------------
// BLEP and BLAMP (the cubic B-spline kernel, support -2..2)

double Blep::residual(double x)
{
	// B is the integral of the kernel; the residual is B minus the unit step.
	double b;
	if (x <= -2)
		b = 0;
	else if (x <= -1) {
		const double u = 2 + x;
		b = u * u * u * u / 24;
	} else if (x <= 0)
		b = 1.0 / 24 + (4 * x - 2 * x * x * x - 0.75 * x * x * x * x + 2.75) / 6;
	else if (x <= 1) {
		const double y = -x;
		b = 1 - (1.0 / 24 + (4 * y - 2 * y * y * y - 0.75 * y * y * y * y + 2.75) / 6);
	} else if (x < 2) {
		const double u = 2 - x;
		b = 1 - u * u * u * u / 24;
	} else
		b = 1;
	return b - (x >= 0 ? 1 : 0);
}

double Blep::residualRamp(double x)
{
	// The integral of residual(): even, 7/30 at 0, zero outside -2..2.
	const double y = -std::fabs(x);
	if (y <= -2)
		return 0;
	if (y <= -1) {
		const double u = 2 + y;
		return u * u * u * u * u / 120;
	}
	const double f = y / 24 + (2 * y * y - 0.5 * y * y * y * y - 0.15 * y * y * y * y * y + 2.75 * y) / 6;
	return 1.0 / 120 + f + 0.225;
}

void Blep::reset(double level)
{
	m_pos = 0;
	m_buf[0] = m_buf[1] = level;
	m_buf[2] = m_buf[3] = 0;
}

void Blep::step(double h, double frac)
{
	m_buf[m_pos] += h * residual(-1 - frac);
	m_buf[(m_pos + 1) & 3] += h * residual(-frac);
	m_buf[(m_pos + 2) & 3] += h * residual(1 - frac);
	m_buf[(m_pos + 3) & 3] += h * residual(2 - frac);
}

void Blep::corner(double s, double frac)
{
	m_buf[m_pos] += s * residualRamp(-1 - frac);
	m_buf[(m_pos + 1) & 3] += s * residualRamp(-frac);
	m_buf[(m_pos + 2) & 3] += s * residualRamp(1 - frac);
	m_buf[(m_pos + 3) & 3] += s * residualRamp(2 - frac);
}

double Blep::push(double naive)
{
	m_buf[(m_pos + 2) & 3] += naive;
	const double out = m_buf[m_pos];
	m_buf[m_pos] = 0;
	m_pos = (m_pos + 1) & 3;
	return out;
}

} // namespace xpander
