// Sample-rate conversion between the host's rate and the DSP's 41,666.67 Hz: a streaming
// windowed-sinc resampler (mono) with a precomputed polyphase kernel, Hann-windowed.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace qvgt {

class Resampler {
public:
	// cutoffFraction: the passband's edge as a fraction of the lower Nyquist frequency.
	Resampler(double inRate, double outRate, double cutoffFraction = 0.9, int halfTaps = 24)
		: m_step(inRate / outRate), m_half(halfTaps), m_hist(size_t(2 * halfTaps + 2), 0.0f)
	{
		constexpr double kPi = 3.14159265358979323846;
		const double cutoff = std::min(1.0, outRate / inRate) * cutoffFraction;
		m_table.resize(size_t((kPhases + 1) * 2 * m_half));
		for (int p = 0; p <= kPhases; ++p) {
			const double frac = double(p) / kPhases;
			for (int j = 0; j < 2 * m_half; ++j) {
				const double t = (j - m_half + 1) - frac;
				const double s = std::fabs(t) < 1e-12 ? cutoff : std::sin(kPi * cutoff * t) / (kPi * t);
				const double w = 0.5 * (1 + std::cos(kPi * t / (m_half + 1)));
				m_table[size_t(p * 2 * m_half + j)] = float(s * w);
			}
		}
	}

	// The input-side latency in input samples (the kernel's centre).
	int latencyInput() const { return m_half; }

	void reset()
	{
		std::fill(m_hist.begin(), m_hist.end(), 0.0f);
		m_pos = 0;
		m_inCount = 0;
	}

	// Pushes one input sample; appends the output samples it completes to `out`.
	template <typename Vec> void push(float x, Vec &out)
	{
		std::rotate(m_hist.begin(), m_hist.begin() + 1, m_hist.end());
		m_hist.back() = x;
		++m_inCount;
		while (m_pos <= double(m_inCount - m_half - 1)) {
			out.push_back(interp(m_pos - double(m_inCount - long(m_hist.size()))));
			m_pos += m_step;
		}
	}

private:
	static constexpr int kPhases = 512;

	float interp(double p) const
	{
		const int i0 = int(std::floor(p));
		const double ph = (p - i0) * kPhases;
		const int p0 = int(ph);
		const float a = float(ph - p0);
		const float *k0 = &m_table[size_t(p0 * 2 * m_half)];
		const float *k1 = k0 + 2 * m_half;
		const int first = i0 - m_half + 1, taps = 2 * m_half;
		if (first < 0 || first + taps > int(m_hist.size())) { // only ever at the edges
			float acc = 0;
			for (int j = 0; j < taps; ++j) {
				const int idx = first + j;
				if (idx >= 0 && idx < int(m_hist.size()))
					acc += m_hist[size_t(idx)] * (k0[j] + a * (k1[j] - k0[j]));
			}
			return acc;
		}
		// four running sums, for the vector units
		const float *x = &m_hist[size_t(first)];
		float s0 = 0, s1 = 0, s2 = 0, s3 = 0;
		int j = 0;
		for (; j + 4 <= taps; j += 4) {
			s0 += x[j] * (k0[j] + a * (k1[j] - k0[j]));
			s1 += x[j + 1] * (k0[j + 1] + a * (k1[j + 1] - k0[j + 1]));
			s2 += x[j + 2] * (k0[j + 2] + a * (k1[j + 2] - k0[j + 2]));
			s3 += x[j + 3] * (k0[j + 3] + a * (k1[j + 3] - k0[j + 3]));
		}
		for (; j < taps; ++j)
			s0 += x[j] * (k0[j] + a * (k1[j] - k0[j]));
		return (s0 + s1) + (s2 + s3);
	}

	double m_step, m_pos = 0;
	long m_inCount = 0;
	int m_half;
	std::vector<float> m_hist;
	std::vector<float> m_table;
};

} // namespace qvgt
