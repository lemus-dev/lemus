#include "resampler.h"

#include <algorithm>
#include <cmath>

namespace virusc {

namespace {

constexpr double kPi = 3.14159265358979323846; // M_PI isn't standard: Windows' headers leave it out
double besselI0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 60; ++k) {
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
		if (term < sum * 1e-17)
			break;
	}
	return sum;
}
} // namespace

void Resampler::setup(double inRate, double outRate, int zeroCrossings, double passband, double beta)
{
	m_in = inRate;
	m_out = outRate;
	m_step = inRate / outRate;
	// The cutoff in cycles per input sample.
	const double fc = 0.5 * std::min(1.0, outRate / inRate) * passband;
	m_half = int(std::ceil(zeroCrossings / (2.0 * fc) * passband));
	m_taps = 2 * m_half;
	const double i0b = besselI0(beta);
	m_table.assign(size_t((m_phases + 1) * m_taps), 0.0f);
	for (int p = 0; p <= m_phases; ++p) {
		const double frac = double(p) / m_phases;
		double sum = 0;
		std::vector<double> row(static_cast<size_t>(m_taps));
		for (int i = 0; i < m_taps; ++i) {
			const double u = frac + i - m_half; // the output's time minus the input sample's
			const double r = u / m_half;
			double w = 0;
			if (std::abs(r) < 1)
				w = besselI0(beta * std::sqrt(1 - r * r)) / i0b;
			const double x = 2 * fc * u;
			const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(kPi * x) / (kPi * x);
			row[size_t(i)] = 2 * fc * sinc * w;
			sum += row[size_t(i)];
		}
		for (int i = 0; i < m_taps; ++i) // each phase passes DC at exactly unity
			m_table[size_t(p * m_taps + i)] = float(row[size_t(i)] / sum);
	}
	m_size = 1;
	while (m_size < m_taps + 2)
		m_size <<= 1;
	m_mask = m_size - 1;
	m_ring.assign(size_t(2 * m_size), 0.0f);
	reset();
}

void Resampler::reset()
{
	std::fill(m_ring.begin(), m_ring.end(), 0.0f);
	// A window of silence behind the first input pushed, which is the first output's time.
	m_written = m_half;
	m_n = m_half;
	m_frac = 0;
}

float Resampler::pop()
{
	// Output at time n + frac needs inputs n - half + 1 .. n + half: tap i takes input n + half - i.
	const double fp = m_frac * m_phases;
	const int p = std::min(m_phases - 1, int(fp));
	const float a = float(fp - p);
	const float *h0 = &m_table[size_t(p * m_taps)];
	const float *h1 = h0 + m_taps;
	// The ring holds inputs n - half + 1 .. n + half contiguously, oldest first.
	const int start = int((m_n - m_half + 1) & m_mask);
	const float *x = &m_ring[size_t(start)];
	float acc0 = 0, acc1 = 0;
	for (int i = 0; i < m_taps; ++i) {
		const float xi = x[m_taps - 1 - i];
		acc0 += h0[i] * xi;
		acc1 += h1[i] * xi;
	}
	m_frac += m_step;
	const double whole = std::floor(m_frac);
	m_n += (long long)whole;
	m_frac -= whole;
	return acc0 + (acc1 - acc0) * a;
}

} // namespace virusc
