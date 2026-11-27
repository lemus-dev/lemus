// The MS-20's two VCO cores and wave shapers in C++, driven by the charge currents core/netlists/
// vcoctl.cir works out (docs/circuit/vco.md §9: the solver can't step through the PUT latch, and the
// reset needs MHz rates). What each part does and where its numbers come from:
//
// - The core: the timing capacitor (C2 6200 pF, C11 3000 pF) referred to +15 V, its lower plate
//   (the ramp) pulled down by the charge current; at the trip (7.51 V) the PUT latch fires and,
//   after its dead time, the ramp is back at the top (13.52 V). Trip, top and dead time from the
//   ngspice runs of vco1.cir / vco2.cir with the parasitics (vco.md §6). The reset is a band-limited
//   step (polyBLEP).
// - The buffer: the JFET follower (gain 0.906, vco.md §9) and C1/C12 with R13/R61 10K (1.6 Hz):
//   node 10 / node 11, the falling sawtooth at the check points.
// - VCO 1's shapers: IC2A comparing node 10 with PW's wiper (pulse), IC2B rectifying it
//   (triangle), node 10 through R4 (saw), white noise; VCO 2's: node 11 direct (saw), IC6A against
//   ground (square) or R70/R69's 2.70 V (narrow), the 4011's XOR of IC2A and IC6A (ring). The
//   4558 comparators slew at 1 V/µs between ±14 V; each source as its Thevenin equivalent into the
//   switch common, which VCO LEVEL loads (VR13 100KA, VR14 100KB, into R75/R77 and IC8A's virtual
//   ground): the common's voltage is what mixer.cir takes.
#pragma once

#include <array>

#include "controls.h"

namespace ms20 {

class Vco {
public:
	struct Core {
		double c;    // farads
		double trip; // volts
		double top;  // volts
		double dead; // seconds from the trip to the top
	};
	// VCO 1 and VCO 2's cores (vco.md §6). VCO 2's swing (top - trip) is 6.80 V, not the netlist's
	// 6.02 V: a real unit's narrow pulse (duty 6.25 % against R70/R69's 2.705 V threshold) puts
	// node 11 at 6.1-6.2 Vpp (docs/compare.md §1), and the same swing centres VCO 2's §7-2 trims,
	// which the netlist's leaves out of range. The trip is the divider's; the top is the uncertain
	// end (the reset's charge storage, which the netlist lacks, lifts it more on the half-size C11).
	// VCO 1's measured swing (x1.017 ± 0.07) leaves its netlist value.
	static Core core1() { return {6200e-12, 7.507, 13.523, 0.45e-6}; }
	static Core core2() { return {3000e-12, 7.507, 7.507 + 6.80, 0.27e-6}; }

	explicit Vco(Core core) : m_core(core) {}
	void setRate(double rate);
	// From rest: the ramp at the top, the coupling capacitor settled, the comparators low.
	void reset();

	// One sample with the charge current `amps` (vcoctl.cir's ia1/ia2): the ramp and node 10 / 11
	// (the buffered, AC-coupled ramp) at the sample's end, as continuous time has them. The reset's
	// step is reported for the shapers to band-limit (resetAt, step()).
	void step(double amps);
	// `seconds` on with the charge current `amps` held, at once: where the ramp is after that long
	// running free (the machine asleep, Machine::render). C1's high-pass keeps its state.
	void advance(double amps, double seconds);
	double ramp() const { return m_v; }
	double nodeStart() const { return m_nodeStart; } // node 10/11 at the sample's start
	double nodeEnd() const { return m_nodeEnd; }     // and at its end
	bool resetLast() const { return m_resetAt >= 0; }
	double resetAt() const { return m_resetAt; }     // where in the sample the reset's step fell (0-1)
	double resetStep() const;                        // its height at the node, volts
	double nodeBeforeReset() const { return m_nodeLow; } // the node just before the step

private:
	Core m_core;
	double m_dt = 1.0 / 48000;
	double m_v = 0;       // the ramp, volts
	double m_dead = -1;   // seconds of dead time left (>= 0: the latch is resetting)
	double m_hp = 0;      // C1's charge: the high-pass's state
	double m_hpA = 0;     // its coefficient
	double m_nodeStart = 0, m_nodeEnd = 0, m_nodeLow = 0;
	double m_resetAt = -1;
};

// A 4558 comparator's output: ±14 V, slewing at 1 V/µs toward the side its inputs choose.
class SlewComparator {
public:
	void setRate(double rate) { m_step = kSlew / rate; }
	void reset(bool high) { m_y = high ? kSat : -kSat; m_high = high; }
	// One sample in which the decision is `before` until fraction `at` (0-1), then `after` (at < 0:
	// no change). Returns the output at the sample's end.
	double step(bool high, double at = -1);
	double value() const { return m_y; }
	bool high() const { return m_high; }
	// When, within the last sample, the output crossed `level` going up (+1), down (-1), or not (0).
	int crossed(double level, double &at) const;
	// The last sample's corners, where the output's slope changed (it started slewing, reached a
	// rail, or turned): where in the sample (0-1) and by how much (volts a sample), for polyBLAMP.
	struct Corner {
		double at, slope;
	};
	int corners() const { return m_cornerCount; }
	const Corner &corner(int k) const { return m_corners[size_t(k)]; }

	static constexpr double kSat = 14.0, kSlew = 1e6;

private:
	double m_y = -kSat, m_yStart = -kSat;
	bool m_high = false;
	double m_step = kSlew / 48000;
	double m_turnAt = -1; // where in the last sample the target changed
	std::array<Corner, 4> m_corners{};
	int m_cornerCount = 0;
	void addCorner(double at, double slope)
	{
		if (slope != 0 && m_cornerCount < int(m_corners.size()))
			m_corners[size_t(m_cornerCount++)] = {at, slope};
	}
};

// The two VCOs, their shapers and VCO LEVEL's load: the switch commons mixer.cir takes.
class Vcos {
public:
	Vcos() : m_vco1(Vco::core1()), m_vco2(Vco::core2()) {}
	void setRate(double rate);
	void reset();
	// One sample: the charge currents, the white noise (KLM-128's output, volts) and the controls.
	void step(double ia1, double ia2, double white, const Controls &c);
	// Both cores `seconds` on, running free on the charge currents (Vco::advance).
	void advance(double ia1, double ia2, double seconds)
	{
		m_vco1.advance(ia1, seconds);
		m_vco2.advance(ia2, seconds);
	}
	double common1() const { return m_common1; }
	double common2() const { return m_common2; }
	double ramp1() const { return m_vco1.ramp(); }
	double ramp2() const { return m_vco2.ramp(); }
	double node10() const { return m_vco1.nodeEnd(); }
	double node11() const { return m_vco2.nodeEnd(); }

private:
	Vco m_vco1, m_vco2;
	SlewComparator m_ic2a, m_ic6a;
	bool m_ringHigh = false;
	// The commons one sample late, so a step's polyBLEP correction reaches the sample before it.
	double m_pend1 = 0, m_pend2 = 0;
	double m_common1 = 0, m_common2 = 0;
};

} // namespace ms20
