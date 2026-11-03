#pragma once

#include <vector>

namespace tr909 {

// Decimates by 2^stages with a chain of half-band FIR filters (Kaiser-windowed sinc). The
// voice runs at the oversampled rate and only needs the way down.
class Decimator {
public:
	// stages 0-3 (factor 1, 2, 4 or 8). The last stage's half length (odd taps a side) sets the
	// final pass band: 32 for the host rate; a short one (8) for a block run at twice the board's
	// rate, whose content above the host's band the board's own decimator takes out.
	void setStages(int stages, int lastHalfLength = 32);
	int factor() const { return 1 << m_stages; }
	void reset();

	// Consumes factor() input samples, returns one output sample.
	float process(const float *in);

	// Delay at the output rate, in output samples.
	double latency() const;
	// The last stage's odd taps (Interpolator's design).
	std::vector<double> halfBandTaps() const { return m_bands.empty() ? std::vector<double>{} : m_bands.back().taps; }

private:
	struct HalfBand {
		std::vector<double> taps; // odd-indexed taps of the half-band filter (the even ones are 0 except the centre)
		// Polyphase: the odd taps only ever meet the first sample of each input pair, so those
		// are kept apart (doubled for wrap-free reads) and weighted by `weights`, the symmetric
		// odd taps written out in full; the second samples only feed the centre tap.
		std::vector<double> weights; // 2n
		std::vector<double> even;    // 2 * 2n first-of-pair samples
		std::vector<double> odd;     // 2 * 2n second-of-pair samples
		int pos = 0;
		int len = 0; // 2n
		void design(int halfLength, double transition, double attenuationDb);
		void reset();
		double process(double a, double b); // two input samples in, one out
	};
	int m_stages = 0;
	std::vector<HalfBand> m_bands;
	std::vector<double> m_work;
};

// Doubles the rate with a half-band FIR (the decimator's design): one sample in, two out, the
// pass band to about 0.13 of the output rate at halfLength 8. Delay: delay() output samples.
class Interpolator {
public:
	void design(int halfLength, double attenuationDb = 100);
	void reset();
	void process(double x, double *out); // out[0], out[1]
	int delay() const { return 2 * m_n - 1; }

private:
	std::vector<double> m_taps; // the odd taps, times 2 (the zero-stuffed samples)
	std::vector<double> m_hist; // the last 2n inputs, doubled for wrap-free reads
	int m_n = 0, m_pos = 0;
};

} // namespace tr909
