// Sample-rate conversion from the Virus C's 46875 Hz to the host's
// a polyphase Kaiser-windowed sinc, its phases interpolated linearly.
#pragma once

#include <vector>

namespace virusc {

class Resampler {
public:
	// The cutoff sits at `passband` of the lower Nyquist frequency; `zeroCrossings` per side, in
	// output samples when decimating; `beta` is the Kaiser window's. 24, 0.92 and 9 give about 90 dB
	// of stopband from 0.92 x Nyquist to Nyquist and up. The DACs' own response is another set
	// (dac.h).
	void setup(double inRate, double outRate, int zeroCrossings = 24, double passband = 0.92, double beta = 9.0);
	void reset();

	// Pull model: while needsInput(), push() the next input sample; then pop() one output sample.
	bool needsInput() const { return m_written <= m_n + m_half; }
	void push(float x)
	{
		const int i = int(m_written & m_mask);
		m_ring[size_t(i)] = m_ring[size_t(i + m_size)] = x;
		++m_written;
	}
	float pop();

	// The delay through the filter, in output samples.
	double latency() const { return m_half * m_out / m_in; }

private:
	double m_in = 1, m_out = 1, m_step = 1;
	int m_half = 1, m_taps = 2, m_phases = 256;
	std::vector<float> m_table; // (phases + 1) rows of `taps`
	std::vector<float> m_ring;  // twice `size`, so a window is always contiguous
	int m_size = 0;
	long long m_mask = 0;
	long long m_written = 0; // input samples pushed
	long long m_n = 0;       // the integer part of the next output's time, in input samples
	double m_frac = 0;
};

} // namespace virusc
