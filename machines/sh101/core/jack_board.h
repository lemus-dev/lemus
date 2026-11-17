#pragma once

#include "solver_stats.h"

namespace sh101 {

// Jack board A's mute (docs/circuit-filter.md 7, docs/model-filter.md 5). C2's output reaches
// the OUTPUT jack through R29 2.2k and R28 1k, and TR7 (2SC1815-GR, R35 47k) can shunt the node
// between them to ground. At switch-on C13 (10 uF from +9 V) lifts node A to +9 V, and D4 and R35
// drive TR7 until C13 has charged through R36 220k and TR7's base: about 1 s with the output
// shunted, the negative half-wave back first (TR7 passes it once its base current can't hold it,
// and its reverse beta is low), the output fully open after about 1.5 s. Switched on, the same
// path (D3 or R36, D4, R35, TR7's base-collector junction) conducts whenever the node swings below
// about -1 V, so loud negative peaks are softly limited.
//
// Switched off, +9 V falls (railVolts()), and TR8 (2SA1015-GR, R37 10k) turns on once it is about
// 0.6 V below C14 (47 uF, charged through D5): C14 drives TR7 through R35 and shunts the output
// within a millisecond or two, and holds it for seconds. The rest of the voice falls with the
// rails (Voice): its sound stops once the +15 V regulator drops out, and IC1a's output falls from
// the bus to ground, which C2 passes on as a low thump that TR7 mostly takes. The rail's fall is an
// estimate (kRailTau in the .cpp); after kDownSeconds what is left at the jack is TR7's saturation
// offset (about 5 mV, decaying over seconds), faded out so the plugin ends on exact zeros.
//
// TR7's current also flows back through R29 into C2 and R30, which the rest of the output stage
// (Ba662Vca) leaves out: it is added here by superposition, the network with TR7 off giving the
// jack's voltage and C2's side (Yi) carrying TR7's share. That share includes switching on: IC1a's
// output steps to the +5 V bus and charges C2 through R29, which TR7, already on, takes to ground.
//
// Solved per sample (backward Euler for C13 and C2, Newton for the five nodes) while the mute acts
// or the node goes negative enough to conduct; otherwise C2's share decays on its own.
// tools/spice/jack checks it against ngspice.
class JackBoard {
public:
	void prepare(double fs);
	void setLoad(double ohms); // what the OUTPUT jack drives
	void powerOn();            // switched on now: C13 discharged
	void settle();             // switched on long ago
	void powerOff();           // switched off now: the rails start to fall
	// Switched off and still sounding: tick() goes on until the thump is over and faded out.
	bool poweringDown() const { return m_down; }
	// +9 V for the next tick(): 9 V, or falling after powerOff().
	double railVolts() const { return m_v9; }
	// The power-on mute still drives TR7. False once C13 has charged far enough that TR7's base
	// gets nothing (node A under 50 mV).
	bool powering() const { return m_powering; }
	// The mute is still audible: node A above 0.8 V, where TR7 can still sink the louder half of a
	// note (about the first 1.4 s).
	bool muting() const { return m_powering && m_va > 0.8; }

	// One sample: `jack` is the OUTPUT jack's voltage as the rest of the network gives it with TR7
	// off (Ba662Vca::tick); returns it with TR7.
	double tick(double jack);

	double nodeA() const { return m_va; } // C13's lower plate (volts)
	// What its Newton did since prepare() (solver_stats.h; no retries).
	const SolverStats &stats() const { return m_stats; }

private:
	double solve(double vth);
	double solveDown(double x0, double v9Prev);

	double m_fs = 176400, m_dt = 1 / 176400.0;
	double m_load = 47e3;
	double m_kth = 1;                // jack -> node voltage (R28 into the load)
	double m_kyx = 1;                // C2's side -> the node, through R29 (TR7 off)
	double m_relaxA = 1, m_relaxY = 1; // node A and C2's share decaying per sample (not conducting)
	double m_va = 0, m_vb = 0, m_vbase = 0, m_x = 0; // node A, D4's cathode, TR7's base, the node
	double m_yi = 0;                 // C2's side: TR7's share of its voltage
	bool m_powering = false;
	// Switched off: +9 V, C14's top (TR8's emitter), TR8's base, samples since.
	double m_v9 = 9, m_v9Prev = 9, m_e8 = 9, m_b8 = 9;
	long m_downN = 0;
	bool m_down = false;
	SolverStats m_stats;
};

} // namespace sh101
