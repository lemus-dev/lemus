#include "tr909/core/noise.h"

#include <cmath>

namespace tr909 {

void NoiseGenerator::setClock(double hz)
{
	m_hz = hz > 0 ? hz : kClockHz;
	m_perSample = m_hz / m_rate;
}

void NoiseGenerator::setRate(double hz)
{
	m_rate = hz;
	m_perSample = m_hz / m_rate;
}

void NoiseGenerator::reset()
{
	m_bits = kMask;
	m_phase = 0;
	m_fill = 0;
}

void NoiseGenerator::powerOn()
{
	reset();
	m_fill = std::lround(kFillSeconds * m_hz);
}

double NoiseGenerator::next()
{
	// In clock periods: the sample's length, and the time to the next edge. The output changes
	// only on the edges, so the time it is high within the sample is a sum of whole runs.
	double left = m_perSample, high = 0;
	double toEdge = 1 - m_phase;
	while (toEdge <= left) {
		if (output())
			high += toEdge;
		left -= toEdge;
		clockOnce();
		toEdge = 1;
	}
	if (output())
		high += left;
	m_phase = 1 - (toEdge - left);
	return m_high * high / m_perSample;
}

double NoiseGenerator::next(int n, double *sub)
{
	// One pass: the sample as next() has it (the same sums, to the bit), each run of the output
	// also shared out over the parts it spans.
	const double part = m_perSample / n;
	double left = m_perSample, high = 0;
	double toEdge = 1 - m_phase;
	double t = 0, partHigh = 0; // time into the sample; the current part's time high
	int k = 0;
	auto spend = [&](double d, bool on) {
		for (double end = part * (k + 1); k < n - 1 && t + d > end; end = part * (k + 1)) {
			if (on)
				partHigh += end - t;
			d -= end - t;
			t = end;
			sub[k++] = m_high * partHigh / part;
			partHigh = 0;
		}
		if (on)
			partHigh += d;
		t += d;
	};
	while (toEdge <= left) {
		const bool on = output();
		if (on)
			high += toEdge;
		spend(toEdge, on);
		left -= toEdge;
		clockOnce();
		toEdge = 1;
	}
	const bool on = output();
	if (on)
		high += left;
	spend(left, on);
	sub[k] = m_high * partHigh / part;
	for (++k; k < n; ++k) // (only if rounding left a part unreached)
		sub[k] = sub[k - 1];
	m_phase = 1 - (toEdge - left);
	return m_high * high / m_perSample;
}

} // namespace tr909
