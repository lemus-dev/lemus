#pragma once

#include "solver_stats.h"

namespace sh101 {

// The SH-101's VCA and output (docs/circuit-filter.md 6-7): TR28, a PNP in common base (base on
// the +5 V bus), turns the ENV (through R197 3.3k and R133 12k, with R196 120k from +14 V on S6's
// side of R197) or the GATE level (R133) into the BA662A's I_ABC; the BA662's output current into R140 47k, its buffer, VOLUME (100k A) to
// the bus, IC1a (M5218 on +9 V), C2 2.2 uF and jack board A (R30 100k, R29 2.2k, C12 1 nF,
// R28 1k) into the load on the OUTPUT jack.
class Ba662Vca {
public:
	struct Params {
		double vRef = 5.0;
		double vt = 0.025693;
		double tr28Is = 3.3e-14, tr28Beta = 300; // 2SA1015GR (tools/spice/filter/models.lib)
		double km = 0.985;       // output saturates at 98.5 % of I_ABC (ba662.lib)
		double lossAmps = 5.5e-3; // input loss: vin / (1 + I_ABC / lossAmps)
		// The input pair's base current loads pin 3's source: R139 1k with R130/R132 (75k) through
		// C53, 987 ohm. The pair's differential input resistance is 4 beta V_T / I_ABC, beta of
		// ba662.lib's NPN at the pair's V_CE (BF 200 and its Early effect: 220, as ngspice gives it).
		double pin3Ohms = 1 / (1 / 1e3 + 1 / 75e3);
		double inputBeta = 220;
		double vosVolts = 0.1e-3; // input offset (BA662A, "offset selected": 0.03-0.25 mV)
		double loadOhms = 47e3;   // whatever the OUTPUT jack drives
		// Moves TR28's turn-on down by this much (volts), for tools; 0 in every calibration
		// (FilterCalibration::vcaKneeShiftVolts).
		double kneeShiftVolts = 0;
	};
	enum class Mode { Env, Gate };

	void setParams(const Params &p);
	void prepare(double fs);
	void reset();
	void setVolume(double travel);

	// TR28's emitter current for a control voltage through a resistance (amps).
	double controlCurrent(double volts, double ohms);
	// The same in ENV mode: the ENV line through R197 into S6's ENV contact, which R196 pulls
	// towards +14 V (the board diagram's placement, docs/circuit-filter.md 5.7), then R133.
	double envControlCurrent(double envVolts, double v14);
	// One sample: in = the BA662's input (pin 3 against the bus), iAbc its control current.
	// Returns the OUTPUT jack's voltage.
	double tick(double in, double iAbc);

	double vcaOut() const { return m_v8; } // pin 8 against the bus

	// Switched off (JackBoard::railVolts()): +9 V falling. IC1a's output can't rise above about
	// 1.2 V under it, so it falls from the bus to ground; once the +15 V regulator has dropped out
	// (+9 V under kVoiceDiesBelow, the DC-DC converter's 21 V then under about 16 V), the rest of
	// the voice is taken as silent. 9 V (reset()) is switched on.
	void setSupply(double v9) { m_v9 = v9; }
	static constexpr double kVoiceDiesBelow = 6.9;

	static constexpr double kR196 = 120e3, kR197 = 3.3e3, kR133 = 12e3, kR140 = 47e3, kR138 = 1e3;

	// What controlCurrent()'s Newton did since prepare() (solver_stats.h; no retries).
	const SolverStats &stats() const { return m_stats; }

private:
	Params m_p;
	double m_fs = 176400, m_t = 1 / 176400.0;
	double m_vol = 0.5;
	double m_ie = 0; // TR28, warm start
	// controlCurrent()'s last inputs, the current it started from and its result: repeated while that
	// current is a fixed point.
	double m_ccVolts = -1e300, m_ccOhms = 0, m_ccStart = -1, m_ccOut = -2;
	double m_v8 = 0;
	double m_v9 = 9;
	// C2 high-pass and C12 low-pass (bilinear one-poles).
	double m_hpA = 0, m_hpX = 0, m_hpY = 0;
	double m_lpA = 0, m_lpB = 0, m_lpX = 0, m_lpY = 0;
	double m_jackGain = 1;
	SolverStats m_stats;
};

} // namespace sh101
