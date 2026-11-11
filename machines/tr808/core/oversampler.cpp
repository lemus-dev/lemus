#include "oversampler.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace tr808 {

namespace {
constexpr double kPi = 3.14159265358979323846; // M_PI isn't standard (MSVC lacks it)

bool sameBits(double a, double b)
{
	uint64_t x, y;
	std::memcpy(&x, &a, 8);
	std::memcpy(&y, &b, 8);
	return x == y;
}

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
	same = 0;
}

void Decimator::HalfBand::reset()
{
	std::fill(even.begin(), even.end(), 0.0);
	std::fill(odd.begin(), odd.end(), 0.0);
	pos = 0;
	same = 0;
}

double Decimator::HalfBand::process(double a, double b)
{
	// A pair like the last: once every sample in both windows is it, the windows are the same
	// whatever their position, and so is the output (the ring's contents are left as they are).
	if (sameBits(a, lastA) && sameBits(b, lastB)) {
		if (same > len)
			return held;
		++same;
	} else {
		lastA = a;
		lastB = b;
		same = 1;
	}
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
	held = centre + (((s[0] + s[1]) + (s[2] + s[3])) + ((s[4] + s[5]) + (s[6] + s[7])));
	return held;
}

void Decimator::setStages(int stages, int lastHalfLength)
{
	m_stages = stages < 0 ? 0 : stages > 3 ? 3 : stages;
	m_bands.assign(size_t(m_stages), {});
	// The first stage (highest rate) sees the widest guard band relative to its rate, so it
	// can be short; the last one sets the final pass band edge (~20 kHz at 44.1k) and is long.
	for (int s = 0; s < m_stages; ++s) {
		const bool last = s == m_stages - 1;
		m_bands[size_t(s)].design(last ? lastHalfLength : 12, 0.0, last && lastHalfLength >= 32 ? 110.0 : 100.0);
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

void Interpolator::design(int halfLength, double attenuationDb)
{
	// the decimator's half-band, its odd taps doubled: zero-stuffing halves the gain
	Decimator d;
	d.setStages(1, halfLength);
	(void)attenuationDb;
	m_n = halfLength;
	m_taps = d.halfBandTaps();
	for (double &t : m_taps)
		t *= 2;
	m_hist.assign(size_t(4 * m_n), 0.0);
	m_pos = 0;
	m_same = 0;
}

void Interpolator::reset()
{
	std::fill(m_hist.begin(), m_hist.end(), 0.0);
	m_pos = 0;
	m_same = 0;
}

void Interpolator::process(double x, double *out)
{
	// An input like the last: once the whole window is it, so are the outputs (as the decimator's).
	if (sameBits(x, m_last)) {
		if (m_same > 2 * m_n) {
			out[0] = m_held[0];
			out[1] = m_held[1];
			return;
		}
		++m_same;
	} else {
		m_last = x;
		m_same = 1;
	}
	// y[2m] = 2 sum_j h_j (x[m-n+1+j] + x[m-n-j]), y[2m+1] = x[m-n+1] (the centre tap): the window
	// x[m-2n+1 .. m] sits at h[0 .. 2n), oldest first
	const int len = 2 * m_n;
	m_hist[size_t(m_pos)] = m_hist[size_t(m_pos + len)] = x;
	if (++m_pos == len)
		m_pos = 0;
	const double *h = &m_hist[size_t(m_pos)];
	double s = 0;
	for (int j = 0; j < m_n; ++j)
		s += m_taps[size_t(j)] * (h[m_n + j] + h[m_n - 1 - j]);
	out[0] = m_held[0] = s;
	out[1] = m_held[1] = h[m_n];
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

} // namespace tr808
