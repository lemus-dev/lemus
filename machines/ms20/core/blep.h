#pragma once

#include <array>
#include <cmath>
#include <vector>

namespace ms20 {

// Band-limited corrections for waveforms built from events at exact times: a step (BLEP) or a
// change of slope (BLAMP) at a fraction of a sample. Each is the difference between the
// band-limited event (a windowed-sinc impulse integrated once or twice: linear phase, pass band to
// 0.45 of the rate) and the naive one, spread over kHalf samples before and after the event, so a
// waveform sampled naively plus these corrections is band-limited. The output is delayed by kHalf
// samples (Corrector::latency()).
class BlepTable {
public:
	static constexpr int kHalf = 8;          // samples each side of the event
	static constexpr int kOver = 128;        // table points a sample
	static constexpr int kLen = 2 * kHalf * kOver + 1;
	static const BlepTable &get();
	// The residuals at time t (in samples) from the event: t in [-kHalf, kHalf].
	// (the band-limited step and ramp are smooth and interpolate well; the naive ones' corner at 0
	// is subtracted exactly, so an event just before a sample gets no interpolation across it)
	double step(double t) const
	{
		if (t <= -kHalf || t >= kHalf)
			return 0;
		return lookup(m_step, t) - (t >= 0 ? 1.0 : 0.0);
	}
	double ramp(double t) const
	{
		if (t <= -kHalf || t >= kHalf)
			return 0;
		return lookup(m_ramp, t) - (t >= 0 ? t : 0.0);
	}
	double impulse(double t) const { return lookup(m_imp, t); } // per unit area (volt-samples)

private:
	BlepTable();
	static double lookup(const std::vector<double> &tab, double t)
	{
		const double x = (t + kHalf) * kOver;
		if (x <= 0 || x >= kLen - 1)
			return 0;
		const int i = int(x);
		const double f = x - i;
		return tab[size_t(i)] + (tab[size_t(i) + 1] - tab[size_t(i)]) * f;
	}
	std::vector<double> m_step, m_ramp, m_imp;
};

// A waveform's corrections, pending: add events as they happen (each at `ago` samples before the
// current sample, 0 <= ago < 1: it happened between the last sample and this one), then take each
// sample's naive value through process(), which returns the band-limited value kHalf samples late.
class Corrector {
public:
	static constexpr int kHalf = BlepTable::kHalf;
	static int latency() { return kHalf; }
	void reset(double value = 0)
	{
		m_buf.fill(0);
		m_delay.fill(value);
		m_pos = 0;
	}
	// A step of height h, `ago` samples before the current sample (the one process() takes next).
	void step(double h, double ago)
	{
		const auto &t = BlepTable::get();
		// the current sample is index kHalf in the window (its delayed output kHalf later)
		for (int k = -kHalf; k < kHalf; ++k)
			m_buf[size_t((m_pos + kHalf + k) & kMask)] += h * t.step(double(k) + ago);
	}
	// An impulse (a glitch too short to resolve) of `area` volt-samples, `ago` samples before the
	// current sample: the naive waveform has nothing of it.
	void impulse(double area, double ago)
	{
		const auto &t = BlepTable::get();
		for (int k = -kHalf; k < kHalf; ++k)
			m_buf[size_t((m_pos + kHalf + k) & kMask)] += area * t.impulse(double(k) + ago);
	}
	// A change of slope by d (per sample), `ago` samples before the current sample.
	void ramp(double d, double ago)
	{
		const auto &t = BlepTable::get();
		for (int k = -kHalf; k < kHalf; ++k)
			m_buf[size_t((m_pos + kHalf + k) & kMask)] += d * t.ramp(double(k) + ago);
	}
	// The naive value of the current sample in; the corrected value kHalf samples ago out.
	double process(double naive)
	{
		m_delay[size_t((m_pos + kHalf) & kMask)] = naive;
		const double out = m_delay[size_t(m_pos)] + m_buf[size_t(m_pos)];
		m_buf[size_t(m_pos)] = 0;
		m_pos = (m_pos + 1) & kMask;
		return out;
	}

private:
	static constexpr int kSize = 32; // a power of two above 2 kHalf
	static constexpr int kMask = kSize - 1;
	std::array<double, kSize> m_buf{}, m_delay{};
	int m_pos = 0;
};

inline const BlepTable &BlepTable::get()
{
	static const BlepTable t;
	return t;
}

inline BlepTable::BlepTable()
{
	// A windowed sinc (cut-off 0.45 of the rate, Blackman-Harris over +-kHalf), integrated: the
	// band-limited step; integrated again: the band-limited ramp. Each minus its naive event.
	const double fc = 0.45;
	std::vector<double> h(kLen);
	const double pi = 3.14159265358979323846;
	double sum = 0;
	for (int i = 0; i < kLen; ++i) {
		const double t = double(i) / kOver - kHalf;
		const double x = 2 * fc * t;
		const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(pi * x) / (pi * x);
		const double w = double(i) / (kLen - 1);
		const double win = 0.35875 - 0.48829 * std::cos(2 * pi * w) + 0.14128 * std::cos(4 * pi * w) - 0.01168 * std::cos(6 * pi * w);
		h[size_t(i)] = sinc * win;
		sum += h[size_t(i)];
	}
	m_step.resize(kLen);
	m_ramp.resize(kLen);
	m_imp.resize(kLen);
	for (int i = 0; i < kLen; ++i)
		m_imp[size_t(i)] = h[size_t(i)] / sum * kOver; // the step's derivative, per sample
	double acc = 0, acc2 = 0;
	// the band-limited step (0 -> 1) and ramp, by the trapezoidal rule (centred on the points)
	for (int i = 0; i < kLen; ++i) {
		const double prevAcc = acc;
		acc += (i > 0 ? (h[size_t(i - 1)] + h[size_t(i)]) / 2 : h[0] / 2) / sum;
		m_step[size_t(i)] = acc;
		acc2 += (prevAcc + acc) / 2 / kOver;
		m_ramp[size_t(i)] = acc2;
	}
	// the ends exact: the step from 0 to 1, the ramp's residual 0 at the far end
	const double s0 = m_step.front(), s1 = m_step.back();
	for (int i = 0; i < kLen; ++i)
		m_step[size_t(i)] = (m_step[size_t(i)] - s0) / (s1 - s0);
	const double drift = m_ramp.back() - kHalf;
	for (int i = 0; i < kLen; ++i)
		m_ramp[size_t(i)] -= drift * double(i) / (kLen - 1);
}

} // namespace ms20
