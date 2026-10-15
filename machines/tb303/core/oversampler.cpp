#include "oversampler.h"

#include <cmath>

namespace tb303 {

namespace {
constexpr double kPi = 3.14159265358979323846; // M_PI isn't standard (MSVC lacks it)

double besselI0(double x)
{
	double sum = 1, term = 1;
	for (int k = 1; k < 50; ++k) {
		term *= (x / (2 * k)) * (x / (2 * k));
		sum += term;
		if (term < 1e-12 * sum)
			break;
	}
	return sum;
}
} // namespace

// Half-band low-pass: h[n] = 0 for even n != 0, h[0] = 0.5. Only the odd taps are stored:
// taps[k] multiplies x[centre - (2k+1)] + x[centre + (2k+1)].
void Decimator::HalfBand::design(int halfLength, double transition, double attenuationDb)
{
	const int n = halfLength; // number of odd taps per side
	const double beta = attenuationDb > 50 ? 0.1102 * (attenuationDb - 8.7)
	                                       : 0.5842 * std::pow(attenuationDb - 21, 0.4) + 0.07886 * (attenuationDb - 21);
	(void)transition;
	const double span = 2.0 * n; // window half width in samples
	taps.assign(size_t(n), 0.0);
	double sum = 0.5;
	for (int k = 0; k < n; ++k) {
		const double m = 2 * k + 1;
		const double sinc = std::sin(kPi * m / 2) / (kPi * m);
		const double w = besselI0(beta * std::sqrt(std::max(0.0, 1 - (m / span) * (m / span)))) / besselI0(beta);
		taps[size_t(k)] = sinc * w;
		sum += 2 * taps[size_t(k)];
	}
	// Normalise DC gain to 1.
	const double scale = 1.0 / sum;
	for (double &t : taps)
		t *= scale;
	len = 2 * n;
	weights.assign(size_t(len), 0.0);
	for (int k = 0; k < n; ++k)
		weights[size_t(n - 1 - k)] = weights[size_t(n + k)] = taps[size_t(k)];
	even.assign(size_t(2 * len), 0.0);
	odd.assign(size_t(2 * len), 0.0);
	pos = 0;
}

void Decimator::HalfBand::reset()
{
	std::fill(even.begin(), even.end(), 0.0);
	std::fill(odd.begin(), odd.end(), 0.0);
	pos = 0;
}

double Decimator::HalfBand::process(double a, double b)
{
	// Push the pair; each phase's window is its last len samples, oldest first, at [pos, pos+len).
	even[size_t(pos)] = even[size_t(pos + len)] = a;
	odd[size_t(pos)] = odd[size_t(pos + len)] = b;
	if (++pos == len)
		pos = 0;
	const double *x = &even[size_t(pos)];
	const double *w = weights.data();
	// The centre tap meets the second sample of the pair len/2 pairs back.
	const double centre = 0.5 * odd[size_t(pos + len / 2 - 1)];
	// Eight partial sums, so the adds don't wait on each other (len is a multiple of 8); adjacent
	// pairs vectorise.
	double s[8] = {0, 0, 0, 0, 0, 0, 0, 0};
	for (int k = 0; k < len; k += 8)
		for (int j = 0; j < 8; ++j)
			s[j] += w[k + j] * x[k + j];
	return centre + (((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7])));
}

void Decimator::setStages(int stages)
{
	m_stages = stages < 0 ? 0 : stages > 3 ? 3 : stages;
	m_bands.assign(size_t(m_stages), {});
	// The first stage (highest rate) sees the widest guard band relative to its rate, so it
	// can be short; the last one sets the final pass band edge (~20 kHz at 44.1k) and is long.
	for (int s = 0; s < m_stages; ++s) {
		const bool last = s == m_stages - 1;
		m_bands[size_t(s)].design(last ? 32 : 12, 0.0, last ? 110.0 : 100.0);
	}
	m_work.assign(size_t(1 << m_stages), 0.0);
}

void Decimator::reset()
{
	for (auto &b : m_bands)
		b.reset();
}

float Decimator::process(const float *in)
{
	const int n = factor();
	if (n == 1)
		return in[0];
	for (int i = 0; i < n; ++i)
		m_work[size_t(i)] = in[i];
	int count = n;
	for (auto &b : m_bands) {
		for (int i = 0; i < count / 2; ++i)
			m_work[size_t(i)] = b.process(m_work[size_t(2 * i)], m_work[size_t(2 * i + 1)]);
		count /= 2;
	}
	return float(m_work[0]);
}

double Decimator::latency() const
{
	// Each stage delays by (window length / 2 - 1) samples at its input rate, roughly half of
	// its window; accumulate in output samples.
	double lat = 0;
	double rateDiv = 1.0 / factor();
	for (const auto &b : m_bands) {
		rateDiv *= 2;
		lat += (2.0 * double(b.taps.size()) - 1) * rateDiv / 2.0;
	}
	return lat;
}

} // namespace tb303
