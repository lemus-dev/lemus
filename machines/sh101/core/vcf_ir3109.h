#pragma once

#include "solver_stats.h"

namespace sh101 {

// The IR3109 VCF as the SH-101 wires it (docs/circuit-filter.md 2-3): four OTA integrators with
// their 68k/68k/560 summing networks and 240 pF capacitors, the buffers' output limits, the
// mixer's input through C45, and the resonance loop: VR18 into the TR26 phase splitter, the
// TR27 follower, C52, R129 5.6k, the D30/D31 clamp and R113 15k back into stage 1. Also the
// output sum R130/R132 and C53 into the BA662's input with R139.
//
// Voltages are relative to the +5 V reference bus. Each sample is solved implicitly
// (trapezoidal integration, Newton on the four capacitors, the diode node and TR27's emitter).
class Ir3109Vcf {
public:
	struct Params {
		double c = 240e-12;     // C48, C47, C50, C51
		// The CAP pins' own capacitance: the OTA's output transistors' and the buffer's junctions,
		// 2.60-2.68 pF in the decode's devices (ngspice at 25 C, 1-300 uA), so 1.1 % on the 240 pF.
		double capPinF = 2.65e-12;
		double vt = 0.025693;   // kT/q at 25 C
		double gm[4] = {1, 1, 1, 1}; // OTA mismatch (OML: matched to about 5 %)
		// The OTAs' output resistance, as g_m r_o: their Wilson mirrors (lateral PNPs) leak the CAP
		// node's swing back. In the decode's devices (ir3109t.lib, ngspice at 25 C) 33 700 from 1 to
		// 10 uA (31 700 at 100 uA), so each stage's passband gain is about 0.4 % short of 1.
		double otaGmRo = 33.7e3;
		// The OTA's output current against I_ABC tanh(u / 2V_T): the tail mirror's and the Wilson
		// mirrors' Early effects and beta give 1 + 2.2 % at 1 uA, falling 0.143 % per e-fold of
		// I_ABC as their V_BEs rise (the decode's devices, ngspice at 25 C: within 0.04 % from 0.3 to
		// 500 uA together with the input pair's degeneration, which the model leaves out:
		// docs/model-filter.md 2).
		double otaGain1uA = 1.02213, otaGainPerE = 1.433e-3;
		// The OTA outputs near the rails (OML: Wilson mirrors). The stage's current is I_C2, which a
		// PNP Wilson sources into CAP, less I_C1, which a PNP and an NPN Wilson sink from it. Once
		// CAP comes within a V_BE of pin 8, the NPN Wilson's output transistor saturates: its
		// base-collector junction takes the mirror's current as e^(V_CE/V_T) against the forward
		// current and turns it round into CAP, so the sink gives I_C1 tanh((CAP - pin 8 -
		// V_BE(I_C1)) / 2V_T); the PNP Wilson does the same towards pin 16 with V_EB(I_C2). So the
		// CAP pins stop softly a V_BE from the rails, lower the harder the stage is driven, rather
		// than at a fixed compliance. The decode at transistor level (ir3109t.lib, ngspice at 25 C)
		// follows this within a few mV from 0.3 to 210 uA, with its devices' I_S (5e-16 at
		// ngspice's TNOM of 27 C, 3.67e-16 at 25 C). Pin 8 (0 V) is 5 V below the bus.
		double otaIs = 3.67e-16;
		double pin8BelowBus = 5.0;
		// Hard limits on the CAP pins: the rails, a guard that the junctions keep them well inside.
		double capLo = -5.0, capHi = 4.0;
		double yHi24 = 3.8;     // stages 2 and 4: their current sources saturate near pin 16
		double diodeIs = 2.52e-9, diodeN = 1.752; // D30, D31 (1S2473, 1N4148 class)
		double beta = 300;      // TR26, TR27 (2SC1815GR: hFE 200-400)
		// The buffers are P-MOS source followers (OML; AS3109 Fig. 2): OUT = CAP + |V_TO| +
		// sqrt(2 I_D / K), where I_D is what the load leaves of the pull-up's current (stages 1, 3:
		// 20k to pin 16) or of the current source (2, 4). So their gain is below 1 and moves with
		// the level, and stages 1 and 3 top out where the follower cuts off (about +2.5 V). There
		// are no maker's data for the P-MOS: K'W/L 1 mA/V^2 with |V_TO| 2.3 V puts |V_GS| at the
		// 2.9-3 V OML measured (tools/spice/filter/ir3109). The P-MOS's channel-length modulation
		// (the decode's lambda 0.01 per volt, drain on pin 8) takes about 15 mV off |V_GS| at rest
		// and adds lambda I_D of output conductance.
		double bufK = 1e-3, bufVto = 2.3, bufLambda = 0.01;
		double vccAboveBus = 4.0;
		// Stages 2 and 4's current sources: lateral PNPs mirrored off a 22k reference from 9 V, in
		// the decode's devices (ngspice at 25 C) 368.7 uA with OUT at the bus and 123k of output
		// resistance (their own Early effect; 0.38 mA ideal).
		double srcAmps = 0.3687e-3, srcOhms = 123e3;
	};

	void setParams(const Params &p);
	void prepare(double fs);
	void reset();

	// One sample. iAbc: the OTAs' bias current from the expo converter (A). res: VR18 travel 0-1.
	// mixVolts, mixOhms: the source mixer seen from C45, as a Thevenin source (volts at the
	// mixer's open-circuit node, its resistance). Returns the BA662's input (pin 3 against the
	// bus, through R130/R132, C53 and R139).
	double tick(double iAbc, double res, double mixVolts, double mixOhms);

	// The buffer gains (docs/model-filter.md 2), for tests.
	double bufferGain(int i) const { return m_buf[i]; }
	// The OTAs' output current per I_ABC (Params::otaGain1uA), and the stages' pole frequency at
	// rest for an I_ABC: g_m into the capacitor and its pin through the 68k/68k/560 network, times
	// the buffers' gain (their geometric mean around the loop). Probes.
	double otaGain(double iAbc) const;
	double poleHz(double iAbc) const;

	// What the solve did since prepare() (solver_stats.h).
	const SolverStats &stats() const { return m_stats; }

	// Probes.
	double out4() const { return m_y[3]; }   // OUT4 against the bus
	double diode() const { return m_x[4]; }  // the D30/D31 node
	double tr27() const { return m_x[5]; }   // TR27's emitter, AC
	double stage(int i) const { return m_y[i]; }
	int lastIterations() const { return m_iters; } // the last sample's, retries included

	// Component values (docs/circuit-filter.md).
	static constexpr double kR68 = 68e3, kR560 = 560, kR113 = 15e3, kR129 = 5.6e3;
	static constexpr double kR131 = 10e3, kR136 = 15e3, kR137 = 33e3, kR130 = 150e3, kR139 = 1e3;
	static constexpr double kC45 = 10e-6, kC52 = 10e-6, kC53 = 10e-6;

private:
	// One step of m_t. Starts Newton from a quadratic prediction off the last three samples, or,
	// with linear > 0, from the last point plus linear times the last step's change (the retries'
	// shorter steps). Returns whether Newton converged; either way the step is taken.
	bool solve(double iAbc, double a, double vm, double gmix, double linear = 0);
	// A sample: solve(), and if it doesn't converge, the sample again from where it started in 2,
	// 4, 8 and then 16 steps, until every step converges (kMaxParts).
	bool solveSample(double iAbc, double a, double vm, double gmix);
	static constexpr int kMaxParts = 16;
	SolverStats m_stats;

	Params m_p;
	double m_fs = 176400, m_t = 1 / 176400.0;
	// Unknowns: c1..c4 (the CAP pins against the bus), d (diode node), e (TR27 emitter AC).
	double m_x[6] = {};
	double m_xPrev[6] = {}, m_xPrev2[6] = {}; // the two samples before, for the predictor
	double m_y[4] = {};
	double m_f[4] = {}; // integrator slopes at the last sample (trapezoidal)
	double m_buf[4] = {1, 1, 1, 1}; // the buffers' small-signal gains at rest (prepare())
	// The buffers' loads: drain current a - b x OUT (a: the pull-up's or source's current with OUT
	// at the bus; b: its conductance plus the load's), and each capacitor's rest voltage.
	double m_bufA[4] = {}, m_bufB[4] = {}, m_cap0[4] = {};
	// The P-MOS's K with its channel-length modulation, as K (1 + lambda V_SD) = m_bufK0 + m_bufKw w
	// in w = CAP + |V_TO| (V_SD taken at OUT = w plus each stage's overdrive at rest).
	double m_bufK0[4] = {}, m_bufKw = 0;
	void buffer(int i, double c, double &y, double &dy) const;
	double otaGainLn(double lnI) const;
	// What the mirrors' saturation near the rails (Params::otaIs) changes of an OTA's output current
	// over I_ABC (-t inside them, t its input pair's tanh), at CAP pin x against the bus, and its
	// derivatives in t and x. Exactly 0 more than 20 V_T inside.
	void otaOut(double t, double x, double &g, double &gt, double &gx) const;
	double m_v45 = 0, m_v52 = 0, m_v53 = 0, m_v3 = 0;
	bool m_mixInit = false;
	// After a reset the coupling capacitors C45 and C53 charge up to kSettleSpeed times faster, easing back to
	// their real rate over kSettleSeconds, so they start at the mean of what they couple, as in a
	// unit that's been on a while: a cold C45 holding an arbitrary instant of the saw would leak a
	// DC step into every gate. The easing matters: a fast capacitor ripples at the audio rate, and
	// stopping it short would freeze one instant of that ripple instead.
	static constexpr double kSettleSeconds = 0.2, kSettleSpeed = 400;
	int m_settle = 0, m_settleLen = 1;
	double couplingRate() const
	{
		const double r = double(m_settle) / m_settleLen;
		return 1.0 + (kSettleSpeed - 1.0) * r * r * r;
	}
	int m_iters = 0;
	// TR26/TR27 operating point (docs/circuit-filter.md 3.2).
	double m_g26 = 1.58, m_follow26 = 0.994, m_kJ = 0.1, m_ie27 = 0.218e-3, m_cSat = -3.29;
	double m_eLo = -6.47, m_eHi = 7.83; // TR27's emitter at the rails, against its rest
	// The integrators' gains for the last I_ABC (solve() recomputes them only when it changes), and
	// the diode node's bound (prepare()).
	double m_kIabc = -1, m_kStage[4] = {}, m_kLeak[4] = {};
	// Where the OTA outputs' mirrors saturate for the last I_ABC, before the input pair's share:
	// pin 8 + V_BE(I_ABC / 2) and pin 16 - V_EB(I_ABC / 2), against the bus.
	double m_x0Lo = -5, m_x0Hi = 4;
	double m_lnHalfIs = 34.85; // ln(0.5 / otaIs) (prepare())
	double m_dMax = 1;
};

} // namespace sh101
