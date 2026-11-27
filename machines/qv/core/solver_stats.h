#pragma once

namespace qv {

// What the iterative solves did, counted since prepare(): the dry VCA's control voltage
// (Unit::updateDryCv), the one nonlinear solve in the model. One that doesn't converge in its
// iterations is taken as it stands and counted (failures). tools/corners checks that failures stay
// at zero over the extremes: an unconverged sample is silent in every other check.
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

} // namespace qv
