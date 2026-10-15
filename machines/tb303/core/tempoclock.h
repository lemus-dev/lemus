#pragma once

#include <cmath>

namespace tb303 {

// The internal tempo clock (main board, service notes p.4): the IC6 oscillator, divided by two in
// IC1a into the 24 ppqn square wave at TP1. Nothing on the board stops it while the 303 is
// stopped, so the firmware sees clock edges then too (that is what flashes the selected pattern's
// LED). Pressing RUN (IC1b's Q rising) fires the C3/R1 monostable, the block diagram's "MONO
// MULTI": C2 clears IC1a and the pulse holds the oscillator; when the pulse ends, C4 sets IC1a. So
// the first clock rising edge comes one pulse length after RUN, and the clock runs in phase from
// there. STOP does nothing to the clock (D1 clamps the falling edge).
class TempoClock {
public:
	// C3 (0.01 uF) into R1 (1 M), 10 ms, until the 4584's input falls below its lower threshold,
	// about 2.3 V on 6 V: 10 ms x ln(6 / 2.3).
	static constexpr double kMonoSeconds = 0.0096;

	// Advances by dt seconds at `bpm` with the RUN flip-flop at `run`. Returns the clock level.
	bool advance(double dt, double bpm, bool run)
	{
		if (run && !m_run)
			m_mono = kMonoSeconds;
		m_run = run;
		if (m_mono > 0) {
			m_mono -= dt;
			if (m_mono > 0)
				return false;
			m_phase = 0; // IC1a set: the clock rises
			return true;
		}
		m_phase += bpm / 60.0 * 24.0 * dt;
		m_phase -= std::floor(m_phase);
		return m_phase < 0.5;
	}

private:
	double m_phase = 0; // in clock periods; high for the first half
	double m_mono = 0;  // seconds left of the RUN monostable
	bool m_run = false;
};

} // namespace tb303
