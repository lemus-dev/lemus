// Decimation by a whole factor: a Kaiser-windowed sinc low-pass on a circular history, one output
// per `factor` inputs. Takes the preamp's rate down to the DSP's: it stands in for the A/D's 20 kHz
// filter module (7-20-0001, whose parts aren't on the sheet) and keeps what folds below 18.75 kHz
// 90 dB down.
#pragma once

#include <cmath>
#include <vector>

namespace qvgt {

class Decimator {
public:
	// cutoff: the -6 dB point as a fraction of the input rate; taps odd.
	Decimator(int factor, double cutoff, int taps, double beta) : m_factor(factor), m_taps(taps)
	{
		constexpr double kPi = 3.14159265358979323846;
		auto i0 = [](double x) { // the modified Bessel function of the first kind, order 0
			double s = 1, t = 1;
			for (int k = 1; k < 40; ++k) {
				t *= (x / (2 * k)) * (x / (2 * k));
				s += t;
			}
			return s;
		};
		m_h.resize(size_t(taps));
		const double mid = (taps - 1) / 2.0, norm = i0(beta);
		double sum = 0;
		for (int i = 0; i < taps; ++i) {
			const double t = i - mid, r = t / mid;
			const double s = std::abs(t) < 1e-12 ? 2 * cutoff : std::sin(2 * kPi * cutoff * t) / (kPi * t);
			m_h[size_t(i)] = s * i0(beta * std::sqrt(std::max(0.0, 1 - r * r))) / norm;
			sum += m_h[size_t(i)];
		}
		for (auto &h : m_h)
			h /= sum;
		m_hist.assign(size_t(2 * taps), 0.0);
	}

	// The delay in input samples (the kernel's centre).
	double delay() const { return (m_taps - 1) / 2.0; }
	int factor() const { return m_factor; }

	void reset()
	{
		std::fill(m_hist.begin(), m_hist.end(), 0.0);
		m_pos = 0;
		m_phase = 0;
	}

	// Pushes one input; true when an output is ready (in `out`).
	bool push(double x, double &out)
	{
		m_hist[size_t(m_pos)] = m_hist[size_t(m_pos + m_taps)] = x;
		m_pos = m_pos + 1 == m_taps ? 0 : m_pos + 1;
		if (++m_phase < m_factor)
			return false;
		m_phase = 0;
		// the newest input is at m_pos - 1; the kernel is symmetric, so the oldest-first window
		// m_hist[m_pos .. m_pos + taps) takes it as it is, folded about its centre (four running
		// sums, for the vector units)
		const double *x0 = &m_hist[size_t(m_pos)], *h = m_h.data();
		const int half = m_taps / 2;
		double a0 = 0, a1 = 0, a2 = 0, a3 = 0;
		int i = 0;
		for (; i + 4 <= half; i += 4) {
			a0 += h[i] * (x0[i] + x0[m_taps - 1 - i]);
			a1 += h[i + 1] * (x0[i + 1] + x0[m_taps - 2 - i]);
			a2 += h[i + 2] * (x0[i + 2] + x0[m_taps - 3 - i]);
			a3 += h[i + 3] * (x0[i + 3] + x0[m_taps - 4 - i]);
		}
		for (; i < half; ++i)
			a0 += h[i] * (x0[i] + x0[m_taps - 1 - i]);
		out = (a0 + a1) + (a2 + a3) + h[half] * x0[half];
		return true;
	}

private:
	int m_factor, m_taps;
	std::vector<double> m_h, m_hist;
	int m_pos = 0, m_phase = 0;
};

} // namespace qvgt
