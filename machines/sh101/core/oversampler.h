#pragma once

#include <vector>

namespace sh101 {

// Decimates by 2^stages with a chain of half-band FIR filters (Kaiser-windowed sinc). The
// voice runs at the oversampled rate and only needs the way down.
class Decimator {
public:
	// stages 0-3 (factor 1, 2, 4 or 8).
	void setStages(int stages);
	int factor() const { return 1 << m_stages; }
	void reset();

	// Consumes factor() input samples, returns one output sample.
	float process(const float *in);

	// Delay at the output rate, in output samples.
	double latency() const;

private:
	struct HalfBand {
		std::vector<double> taps; // odd-indexed taps of the half-band filter (the even ones are 0 except the centre)
		// The window's two phases, each 2 * taps.size() samples, doubled for wrap-free reads: the
		// first sample of each pair (every tap but the centre's) and the second (the centre's).
		std::vector<double> histA, histB;
		int pos = 0;
		int len = 0; // window length in input samples (4 * taps.size())
		void design(int halfLength, double transition, double attenuationDb);
		void reset();
		double process(double a, double b); // two input samples in, one out
	};
	int m_stages = 0;
	std::vector<HalfBand> m_bands;
	std::vector<double> m_work;
};

} // namespace sh101
