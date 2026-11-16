#pragma once

#include "lines.h"

namespace sh101 {

// The MODULATOR's oscillator on the synth board (docs/circuit-osc.md 5): IC3a integrates the
// RATE slider's share of IC3b's output into C9, IC3b compares the triangle against the TR5/TR6
// collector node, whose high level depends on what the waveform switch hangs on R31. Its square is
// also the CPU's clock (CLK, through TR8, when nothing is in EXT CLK IN). CLK RST (P24 low) turns
// on TR6, which holds the node low, so CLK goes high at once, and TR4, which lets the square
// discharge C9 through D9 and R16 towards the bottom of the triangle. Runs at the host rate.
class Lfo {
public:
	struct Out {
		float tri = 0;       // the triangle (IC3a's output) relative to the +5 V bus
		bool square = false; // CLK: high while IC3b's output is high, and throughout CLK RST
	};

	void prepare(double rate);
	void setRate(double knob); // RATE slider travel 0..1
	void setWave(LfoWave w);   // the waveform switch: it moves the Schmitt's upper threshold
	// CLK RST, the P24 line itself (active = low on the CPU). Prefer this to reset().
	void setClkRst(bool active);
	void reset();              // a CLK RST of one CPU loop (1.5 ms), for callers that see only its start
	Out tick();                // advance one sample
	Out current() const { return m_out; }
	double hz() const;         // the free-running frequency at the current settings
	double triangleVolts() const { return m_v; } // absolute, for tests

private:
	double m_dt = 1 / 44100.0;
	double m_knob = 0.5;
	double m_k = 0, m_rIn = 120e3;  // the RATE divider's ratio and the integrator's input resistance
	double m_vh = 10.3;             // the node's high level (upper threshold)
	double m_v = 5.2;               // C9: IC3a's output, absolute volts
	bool m_high = false;            // IC3b's output
	bool m_rstLevel = false;
	double m_rstTimer = 0;
	Out m_out;
	void update();
};

} // namespace sh101
