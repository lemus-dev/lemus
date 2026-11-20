// Oversampling by 2, 4 or 8 with cascaded polyphase IIR half-band filters (two chains of
// first-order allpasses at the low rate). The coefficients come from the elliptic design in
// Laurent de Soras' HIIR. Minimum phase, no latency; the passband stays flat to 20 kHz.
#pragma once

#include <array>
#include <vector>

namespace rat {

class HalfBand {
public:
	static constexpr int kMaxSections = 12; // per chain

	// Coefficients for a half-band filter with the given stopband attenuation (dB) and
	// transition half-width (fraction of the high sample rate, 0..0.25).
	static std::vector<double> design(double attenuationDb, double transition);

	void setCoefs(const std::vector<double> &c);
	void reset();

	// Low rate in, two high-rate samples out.
	void up(double x, double &out0, double &out1)
	{
		const V2 y = run(V2{x, x});
		out0 = y[0];
		out1 = y[1];
	}
	// Two high-rate samples in, low rate out.
	double down(double in0, double in1)
	{
		const V2 y = run(V2{in1, in0});
		return 0.5 * (y[0] + y[1]);
	}

	// Group delay at DC, in high-rate samples.
	double delay() const;

private:
	// Both allpass chains side by side, one per vector lane. The shorter chain is padded with
	// a = 1, which passes its input straight through. 8-byte alignment, as Pedal's pairs
	// (pedal.h).
	typedef double V2 __attribute__((vector_size(16), aligned(8)));

	V2 run(V2 x)
	{
		// (a + z^-1) / (1 + a z^-1) per section, at the low rate.
		for (int i = 0; i < m_n; ++i) {
			const V2 y = m_a[i] * (x - m_y1[i]) + m_x1[i];
			m_x1[i] = x;
			m_y1[i] = y;
			x = y;
		}
		return x;
	}

	V2 m_a[kMaxSections]{}, m_x1[kMaxSections]{}, m_y1[kMaxSections]{};
	int m_n = 0;
	int m_count[2] = {0, 0};
};

class Oversampler {
public:
	// factor: 1, 2, 4 or 8. passHz: top of the band that must stay clean.
	void prepare(double baseRate, int factor, double passHz);
	void reset();
	int factor() const { return m_factor; }

	// x: one base-rate sample; out: factor() samples.
	void up(double x, double *out);
	// in: factor() samples (overwritten); returns one base-rate sample.
	double down(double *in);

	// Low-frequency delay of up() alone, and of up() followed by down(), in base-rate samples.
	double upDelay() const;
	double roundTripDelay() const;

private:
	int m_factor = 1;
	int m_stages = 0;
	std::array<HalfBand, 3> m_up, m_down;
};

} // namespace rat
