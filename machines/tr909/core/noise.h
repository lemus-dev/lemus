#pragma once

#include <cstdint>

namespace tr909 {

// The noise generator, voicing board (service notes p.11 grid L-M 14-16, crop
// p11-noise-tom-noise.png; docs/circuit/noise.md). IC32 and IC33, HD14006BP 18-bit static shift
// registers, are chained to 36 stages (each wired 1 > 13 > 4 > 12 > 5 > 10 > 6 > 9: 4 + 5 + 4 + 5
// stages, IC32 pin 9 into IC33 pin 1) and clocked together by the IC31a/b (HD14070BP) astable.
// IC31c XORs stage 13 (IC32 pin 10) and stage 31 (IC33 pin 10) into stage 1 through R187, so stages
// 1-31 are a maximal-length feedback shift register, x^31 + x^13 + 1, period 2^31 - 1 clocks, and
// stages 32-36 a delay. The output, IC33 pin 9 (stage 36), swings 0 / +15 V into the BD (R10), SD
// (R286), tom noise (R190) and clap (R208) inputs. At power-on IC31d (C46, R182, R183, R186) holds
// stage 1 high through D48 for about 37 ms: the register fills with ones and starts from there.
class NoiseGenerator {
public:
	static constexpr int kStages = 36;
	// The astable's frequency as its parts give it: R184 10k and C47 100p, slowed by R185 33k into
	// IC31b's input capacitance and the gates' delays at +15 V (docs/circuit/noise.md: 320 kHz for
	// typical parts, 280-340 kHz over their spreads; tools/noise/astable.py).
	static constexpr double kClockHz = 320e3;
	static constexpr double kFillSeconds = 0.037; // IC31d's hold at power-on (C46 4.7u, R182 10k)
	static constexpr double kHigh = 15.0;         // volts: the 4006s run from +15 V
	// The high level at the loaded pin: the 4006's P-channel (about 430 ohms typ at 15 V) into the
	// four voices' inputs (6.6k at DC) drops it to about 14 V; the low level stays at 0 V.
	static constexpr double kHighLoaded = 14.0;

	NoiseGenerator() { reset(); }

	void setClock(double hz);
	double clock() const { return m_hz; }
	void setRate(double hz); // the board's sample rate
	double rate() const { return m_rate; }
	void setHigh(double volts) { m_high = volts; } // the output's high level (default kHigh)
	double high() const { return m_high; }

	// Running, the register as the start-up fill leaves it (all ones), at a clock edge.
	void reset();
	// Power-on: the same, with IC31d's hold still to run (the output stays high through it).
	void powerOn();

	// The output over the next sample, averaged: the high level times the share of the sample it
	// is high (exact for its zero-order-hold bit stream).
	double next();
	// The same, and the output's average over each of n equal parts of the sample in sub[] (for a
	// partition run at n times the board's rate); the sample's value is next()'s to the bit.
	double next(int n, double *sub);

	// One clock edge of the register.
	void clockOnce()
	{
		uint64_t in;
		if (m_fill > 0) {
			in = 1;
			--m_fill;
		} else {
			in = ((m_bits >> 12) ^ (m_bits >> 30)) & 1; // stages 13 and 31
		}
		m_bits = ((m_bits << 1) | in) & kMask;
	}
	bool output() const { return (m_bits >> (kStages - 1)) & 1; } // stage 36, IC33 pin 9

	// The state: stage k is bit k - 1; phase is the share of the clock period gone since the last
	// edge; fill the clocks of the power-on hold still to run.
	struct State {
		uint64_t bits = 0;
		double phase = 0;
		long fill = 0;
	};
	State state() const { return {m_bits, m_phase, m_fill}; }
	void setState(const State &s)
	{
		m_bits = s.bits & kMask;
		m_phase = s.phase;
		m_fill = s.fill;
	}

private:
	static constexpr uint64_t kMask = (uint64_t(1) << kStages) - 1;
	uint64_t m_bits = kMask;
	double m_hz = kClockHz;
	double m_high = kHigh;
	double m_rate = 96000;
	double m_perSample = kClockHz / 96000; // clock periods a sample
	double m_phase = 0;
	long m_fill = 0;
};

} // namespace tr909
