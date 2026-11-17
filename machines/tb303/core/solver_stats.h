#pragma once

namespace tb303 {

// What the voice's iterative solves did, counted since prepare(): each Newton (or safeguarded
// Newton) solve of the voice, by solver. A solve that doesn't converge in its iterations is
// retried in shorter steps where the solver can (retried); one that still doesn't converge is
// accepted as it stands and counted (failures). tools/corners checks that failures stay at zero
// over the controls' extremes: an unconverged sample is silent in every other check (on the
// SH-101 one such solve turned a self-oscillation into white noise; docs/improvements.md,
// 2026-11-16).
struct SolverStats {
	long solves = 0, retried = 0, failures = 0;
	int maxIterations = 0; // the most any one solve took (a retry's steps added up)
	SolverStats &operator+=(const SolverStats &o)
	{
		solves += o.solves;
		retried += o.retried;
		failures += o.failures;
		maxIterations = maxIterations > o.maxIterations ? maxIterations : o.maxIterations;
		return *this;
	}
};

// The solves, by what they solve (core/voice.cpp).
enum Solver {
	kSolverLadder,  // the VCF ladder and its feedback loop, per internal sample
	kSolverShaper,  // the Q8 square shaper, per VCO sample
	kSolverAccent,  // the ACCENT pot's wiper node (C36, D24), per host sample
	kSolverPin1,    // the BA662's I_ABC input with Q31 and D27, per host sample
	kSolverGate,    // Q36's V_EB (GATE_INT), four per host sample
	kSolverC41,     // the VEG: Q32's base (C41), per host sample
	kSolverC42,     // the VEG: C42, per host sample
	kSolverExpo,    // the expo converter's ladder current, per host sample
	// A signal diode with a series resistor, within the above: D24 (accent), D27 and D35 (pin1),
	// D36 (C41).
	kSolverD24,
	kSolverD27,
	kSolverD35,
	kSolverD36,
	kSolverCount
};
inline constexpr const char *kSolverNames[kSolverCount] = {"ladder", "shaper", "accent", "pin1", "gate", "c41",
                                                           "c42",    "expo",   "D24",    "D27",  "D35",  "D36"};

} // namespace tb303
