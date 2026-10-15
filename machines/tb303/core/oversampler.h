#pragma once

#include <vector>

namespace tb303 {

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

} // namespace tb303
