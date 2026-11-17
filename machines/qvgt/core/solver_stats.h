#pragma once

namespace qvgt {

// What the iterative solves did, counted since prepare(): the preamp's circuits (circuit.h, each
// step and each DC solve), the compressor's control node (Preamp::compressorVcMinus) and the dry
// VCA's control voltage (Unit::updateDryCv). A solve that doesn't converge in its iterations is
// redone in shorter steps where the solver can (retried); one that still doesn't converge is taken
// as it stands and counted (failures). tools/corners checks that failures stay at zero over the
// extremes: an unconverged sample is silent in every other check (docs/improvements.md,
// 2026-11-16).
struct SolverStats {
	long solves = 0, retried = 0, failures = 0;
	SolverStats &operator+=(const SolverStats &o)
	{
		solves += o.solves;
		retried += o.retried;
		failures += o.failures;
		return *this;
	}
};

} // namespace qvgt
