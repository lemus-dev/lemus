#pragma once

namespace ms20 {

// What the board's circuit solvers did (core/circuit.h, every block), counted since the machine was
// built. A sample whose Newton solve doesn't converge is redone in shorter steps (substepped, which
// also counts the steps a fast input, an edge or a comparator's flip asks for); one that still
// doesn't converge in the shortest is taken as it stands (failures). An operating point that
// doesn't converge (prepare, POWER on) leaves its block where it was (dcFailures). tools/corners
// checks that failures stay at zero over the controls' extremes: an unconverged sample is silent in
// every other check (docs/improvements.md, 2026-11-16).
struct SolverStats {
	long solves = 0;     // steps of every block (an oversampled block's every step)
	long substepped = 0; // samples taken in shorter steps
	long held = 0;       // of those, held in quarters because full steps kept failing (Circuit::step)
	long softened = 0;   // shortest steps that converged only with the comparators softened
	long failures = 0;   // samples that didn't converge even in the shortest steps
	long dcFailures = 0; // operating points that didn't converge
	SolverStats &operator+=(const SolverStats &o)
	{
		solves += o.solves;
		substepped += o.substepped;
		held += o.held;
		softened += o.softened;
		failures += o.failures;
		dcFailures += o.dcFailures;
		return *this;
	}
};

} // namespace ms20
