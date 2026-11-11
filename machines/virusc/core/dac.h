// The Virus C's DACs: three Cirrus Logic CS4390s, one per stereo output (OUT 1, 2, 3), seen in a
// repair shop's photos of the board (docs/firmware.md, "Audio in and out"). What the datasheet
// (DS264F1, 1998) says one does to the DSP's samples, modelled here:
// - A linear-phase low-pass, its digital and analog filters together: flat within 0.001 dB to
//   0.4535 Fs, -6 dB at Fs / 2, 70-90 dB down from 0.5465 Fs, 25 samples of delay. It is the
//   resampler's kernel (`filter`): a Kaiser-windowed sinc of 25 samples a side, beta 7.25, fitted to
//   the datasheet's plots of the transition (Figures 10 and 11) within 0.16 dB rms; its stopband is
//   74-80 dB down where the plots show 72-90 dB.
// - Its noise: a dynamic range of 103 dB (typical, 24-bit, unweighted, 10 Hz-20 kHz), so noise
//   103 dB under a full-scale sine, white in the audio band (Figure 13).
// - Its auto-mute: after 8,192 samples of all 0s or all 1s on both channels it mutes until the data
//   moves again. **Assumption:** that the board enables it (the AUTO_MUTE pin low); the photos don't
//   show. Muted, the output here is exactly zero (the datasheet's idle noise is 115 dB down).
// Not modelled: the op-amp stage between the DACs and the jacks, which the photos don't show.
#pragma once

#include "resampler.h"

#include <cstdint>

namespace virusc {

class Dac {
public:
	static constexpr int kMuteAfter = 8192;
	// Noise RMS at the DSP's rate, white up to 23.4 kHz: 103 dB under a full-scale sine (0.7071) in the
	// 20 kHz the datasheet measures over.
	static constexpr float kNoise = 0.70710678f * 7.0794578e-6f / 0.9237604f; // sqrt(20000 / 23437.5)

	// The resampler from the DSP's rate to the host's, with the DAC's response where the host's rate
	// carries it (48 kHz and up). Below the DSP's rate (44.1 kHz) the host's band ends first, at
	// 22.05 kHz, so the resampler keeps its own band edge there.
	static void filter(Resampler &r, double dspRate, double hostRate)
	{
		if (hostRate >= dspRate)
			r.setup(dspRate, hostRate, 25, 1.0, 7.25);
		else
			r.setup(dspRate, hostRate);
	}

	void reset(uint64_t seed)
	{
		m_rng = seed | 1;
		m_static = 0;
	}

	bool muted() const { return m_static >= kMuteAfter; }

	// One frame of the DSP's output, as the DAC puts it out before its filter: with its noise, or
	// silent once muted.
	void frame(float &l, float &r)
	{
		constexpr float kLsb = 1.0f / 8388608.0f; // a 24-bit word of all 1s is -1 LSB
		const bool still = (l == 0.0f || l == -kLsb) && (r == 0.0f || r == -kLsb);
		m_static = still ? (m_static < kMuteAfter ? m_static + 1 : kMuteAfter) : 0;
		if (muted()) {
			l = r = 0.0f;
			return;
		}
		l += noise();
		r += noise();
	}

private:
	// Near-Gaussian (the sum of four 16-bit uniforms), from one xorshift step.
	float noise()
	{
		m_rng ^= m_rng << 13;
		m_rng ^= m_rng >> 7;
		m_rng ^= m_rng << 17;
		const uint64_t x = m_rng;
		const int32_t sum = int32_t(x & 0xffff) + int32_t((x >> 16) & 0xffff) + int32_t((x >> 32) & 0xffff) +
		                    int32_t(x >> 48);
		constexpr float kScale = kNoise / 37837.23f; // the sum's own RMS: 65536 * sqrt(4 / 12)
		return float(sum - 131070) * kScale;
	}

	uint64_t m_rng = 1;
	int m_static = 0;
};

} // namespace virusc
