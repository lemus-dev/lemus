// One channel of the plugin: host-rate samples in and out, the pedal at 8x (4x at 88.2/96 kHz,
// 2x above), the dBFS-to-volts convention, a gain in front of the pedal (the plugin's INPUT) and
// the footswitch (true bypass, with a short crossfade instead of the real switch's click).
#pragma once

#include "oversampler.h"
#include "pedal.h"

namespace rat {

// 0 dBFS = +4 dBu by default (1.736 V peak), about a hot humbucker or line-level synth.
constexpr double kDefaultLevelDbu = 4.0;

inline double dbuToPeakVolts(double dbu)
{
	return 0.7745966692 * std::pow(10.0, dbu / 20.0) * 1.41421356237;
}

int oversamplingFor(double hostRate);

class Channel {
public:
	void prepare(double hostRate);
	void reset();
	// Takes on another channel's whole running state, without allocating (Pedal::copyStateFrom).
	void copyStateFrom(const Channel &o);

	Pedal &pedal() { return m_pedal; }
	const Pedal &pedal() const { return m_pedal; }

	void setLevelDbu(double dbu) { m_vfs = dbuToPeakVolts(dbu); }
	double levelVolts() const { return m_vfs; }
	// A gain in front of the pedal: drives it harder or softer. The output isn't made up for it,
	// and the bypass skips it. Changes glide over about 10 ms.
	void setInputGainDb(double db) { m_inTarget = std::pow(10.0, db / 20.0); }
	void setEngaged(bool on) { m_engaged = on; }
	bool engaged() const { return m_engaged; }

	double process(double x);
	// One sample of two channels at once (left and right): exactly process() on each, with the
	// two pedals' solves interleaved so the processor overlaps them.
	static void processPair(Channel &a, Channel &b, double xa, double xb, double &ya, double &yb);

	int oversampling() const { return m_os.factor(); }
	// Base-rate samples of delay through the oversampler (the pedal itself adds none).
	double latency() const { return m_os.roundTripDelay(); }

private:
	double mix(double wet, double x);
	double inputGain()
	{
		if (m_inGain != m_inTarget)
			m_inGain = std::abs(m_inTarget - m_inGain) < 1e-6 ? m_inTarget : m_inGain + (m_inTarget - m_inGain) * m_inGlide;
		return m_inGain;
	}

	Pedal m_pedal;
	Oversampler m_os;
	double m_vfs = dbuToPeakVolts(kDefaultLevelDbu);
	double m_inGain = 1.0, m_inTarget = 1.0, m_inGlide = 0.002;
	bool m_engaged = true;
	double m_mix = 1.0; // 1 = effect, 0 = bypassed
	double m_fadeStep = 1.0 / 240;
	double m_buf[8]{};
};

} // namespace rat
