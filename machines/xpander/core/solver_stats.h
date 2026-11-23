#pragma once

#include <algorithm>

namespace xpander {

// What an iterative solve did, counted since the voice's prepare(). A solve that doesn't converge
// within its iterations is retried in shorter steps where the solver can (retried); one that still
// doesn't converge is taken as it stands and counted (failures). tools/corners checks that failures
// stay at zero over the controls' extremes: an unconverged step is silent in every other check
// (docs/improvements.md, 2026-11-16).
struct SolverStats
{
    long solves = 0, retried = 0, failures = 0;
    long iterations = 0; // over all solves, the retries' included
    int worst = 0;       // the most iterations one solve (or one short step of a retry) took
    SolverStats& operator+=(const SolverStats& o)
    {
        solves += o.solves;
        retried += o.retried;
        failures += o.failures;
        iterations += o.iterations;
        worst = std::max(worst, o.worst);
        return *this;
    }
};

// A voice's solvers: the CEM3372's Newton on its cells and resonance loop, one solve a sample
// (vcf); the CEM3374's, two oscillators (vco): the times of the comparator's events inside each
// sample, the event loop of each sample, the bulk-resistance solve at each new control current.
struct Solvers
{
    SolverStats vcf, vco;
    long failures() const { return vcf.failures + vco.failures; }
    Solvers& operator+=(const Solvers& o)
    {
        vcf += o.vcf;
        vco += o.vco;
        return *this;
    }
};

} // namespace xpander
