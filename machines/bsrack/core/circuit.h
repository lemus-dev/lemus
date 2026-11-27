#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// A circuit run sample by sample from its netlist, the way the schematic draws it: resistors,
// capacitors, pots and switches, diodes and bipolar transistors (SPICE's Gummel-Poon equations
// without charge storage), JFETs and MOSFETs (SPICE's level 1), ideal op-amps, and voltage and
// current sources that are constants or inputs. Each Bass Station Rack block is one such netlist
// (core/netlists/*.cir), transcribed from the schematic part by part (docs/circuit.md); the same
// netlist exports to ngspice (toSpice) for the SPICE comparisons (tools/spice; the MOSFET and the
// treatment of nodes that only leakage sets: machines/tr909/docs/circuit/solver-mosfet.md; this is the 909's solver, by way of the Minimoog and the MS-20 at c38e9f5).
//
// The solver is the nodal DK method (Yeh, Abel and Smith 2010; Holters and Zolzer 2015): modified
// nodal analysis with trapezoidal companions for the capacitors, reduced to a state-space system
// whose only implicit part is the nonlinear devices' port voltages, solved with Newton each sample.
// Pots and switches are part of the linear system, so moving one rebuilds the matrices (a few
// microseconds for a voice).
namespace bsrack::circuit {

// Netlist syntax, one element per line, SPICE-like ("*" comments, "+" continues a line, values
// with SPICE suffixes f p n u m k meg g, trailing unit letters ignored, node "0" or "gnd" is ground):
//   R<name> a b <ohms>
//   C<name> a b <farads> [da=<fraction>] [leak=<ohms>] [be]
//                                                      (da: dielectric absorption, Dow's model: four
//                                                       series R-C branches across it, time constants
//                                                       0.01, 0.1, 1 and 10 s, each fraction/4 of C,
//                                                       so a soaked capacitor gives back about
//                                                       `fraction` of its voltage; leak: a resistor
//                                                       across it. Both are plain R and C elements.
//                                                       be: integrated by backward Euler, for a
//                                                       capacitor a transistor empties in a
//                                                       microsecond, where the trapezoidal rule
//                                                       overshoots the reset.)
//   D<name> anode cathode <model>
//   Q<name> collector base emitter <model>            (NPN or PNP, from the model)
//   J<name> drain gate source <model>                 (N- or P-channel JFET, from the model)
//   M<name> drain gate source <model>                 (N- or P-channel enhancement MOSFET, from the
//                                                       model: SPICE's level 1 with the bulk on the
//                                                       source, no charge storage, no gate current)
//   B<name> abc vee in+ in- out ota [is= rs= gm=]      (an OTA such as the BA662: the current into
//                                                       abc, through a diode and rs to vee, sets the
//                                                       output current gm*I_abc*tanh(vd/2V_T) into out)
//   B<name> in+ in- out vee comparator [isink= vg= vsat= vcm= vos=] (an open-collector comparator
//                                                       such as the AN6912: out sinks to vee while
//                                                       in+ + vos < in-; inputs above vcm (volts to
//                                                       ground) count as vcm, as an LM339's do)
//   V<name> plus minus <volts> | input <name>          (a constant, or an input set each sample)
//   I<name> from to <amps> | input <name>              (current from `from` through the source to `to`)
//   E<name> out+ out- in+ in- <gain>                   (voltage-controlled voltage source)
//   X<name> in+ in- out opamp [gbw [gain [vsat [sr]]]] [trap]
//                                                      (ideal op-amp: in+ = in-, any output current;
//                                                       with gbw, a single-pole one; with vsat, its
//                                                       output clips softly at +-vsat; with sr (V/us),
//                                                       its input stage saturates, slewing at sr. Its
//                                                       pole is integrated by backward Euler, which
//                                                       keeps a MHz follower loop from ringing at
//                                                       Nyquist; trap: by the trapezoidal rule, for
//                                                       a stage whose gain puts its loop in the tens
//                                                       of kHz, where backward Euler's missing phase
//                                                       lag moves what it sits in)
//   P<name> a wiper b <ohms> <control> [taper]         (pot; the control's position 0-1 moves the
//                                                       wiper from b to a; taper lin (default),
//                                                       a<mid> audio: the share of the track at mid
//                                                       rotation as two straight segments, e.g. a0.15,
//                                                       c<mid> the reverse; l<mid> a log law, the
//                                                       share (B^t - 1) / (B - 1) with B for <mid> at
//                                                       mid rotation, r<mid> its reverse; j<x0>/<at>/
//                                                       <share>: none up to rotation x0, straight to
//                                                       <share> at <at>, straight to all: the Allen-
//                                                       Bradley type J "modified log", jr<...> its
//                                                       reverse; p<gamma>: the share rotation^gamma)
//   S<name> a b <control> [<on ohms> [<off ohms>]]     (closed while the control is >= 0.5; "!name"
//                                                       closes it below 0.5)
//   .model <name> D(IS= N= RS=)   .model <name> NPN(IS= BF= BR= NF= NR= VAF= ISE= NE= ISC= NC=)
//   .model <name> NJF(VTO= BETA= LAMBDA= IS= N=)    (and PNP, PJF)
//   .model <name> NMOS(VTO= KP= LAMBDA= W= L=)         (and PMOS, whose VTO is negative, as SPICE's;
//                                                       the channel's beta is KP/2 W/L)
//   .output <name> <node> [<node>]                     (a voltage to read, node minus node)
//   .control <name> <default>
//   .param <name>=<value> ...                          ({name} anywhere is replaced by its value)
struct Netlist {
	struct Element {
		char kind = 0;          // R C D Q J M B V I E X P S
		std::string name;
		std::vector<int> nodes; // as listed in the syntax above
		double value = 0;       // ohms, farads, volts, amps, gain
		std::string model;      // D/Q/J/M: model name
		int input = -1;         // V/I: input index, or -1 for a constant
		int control = -1;       // P/S: control index
		bool invert = false;    // S: closed below 0.5
		double taperMid = 0.5;  // P: share of the track at mid rotation
		bool taperExp = false;  // P: the share an exponential of the rotation (log/reverse-log), not two segments
		double taperPow = 0;    // P: the share the rotation to this power (p<gamma), when not 0
		double taperX0 = -1, taperAt = 0, taperShare = 0; // P, the j law (taperX0 >= 0): see above
		bool taperRev = false;                               // P, the j law: the reverse (jr)
		bool be = false;        // C: integrated by backward Euler
		double theta = 0;       // C (an op-amp's pole): the theta method's theta (0: as the kind decides)
		double ron = 0.1, roff = 1e9;
	};
	struct Model {
		enum Type { Diode, Npn, Pnp, Njf, Pjf, Nmos, Pmos, Ota, Comparator, Limiter, Slew } type = Diode;
		double is = 1e-14, n = 1, rs = 0;
		double bf = 100, br = 1, nf = 1, nr = 1, vaf = 0, ise = 0, ne = 1.5, isc = 0, nc = 2;
		double vto = -2, beta = 1e-4, lambda = 0; // JFET (Shichman-Hodges, SPICE's level 1)
		double kp = 2e-5, w = 100e-6, l = 100e-6; // MOSFET (level 1, as SPICE's defaults; beta = kp/2 w/l)
		double gm = 1, isink = 16e-3, vg = 0.5e-3, vsat = 0.15, vcm = 1e9, vos = 0; // OTA, comparator
		double vop = 1e9, von = -1e9, iscl = 0; // an OTA's output clamps (vop, von: diodes of iscl to them)
	};
	struct Output {
		std::string name;
		int plus = 0, minus = 0;
	};

	std::vector<std::string> nodes{"0"}; // node 0 is ground
	std::vector<Element> elements;
	std::map<std::string, Model> models;
	std::vector<std::string> inputs;
	std::vector<std::string> controls;
	std::vector<double> controlDefaults;
	std::vector<Output> outputs;

	// Parses `text`, with {name} parameters replaced first (the .param lines, then `params`, which
	// win). Throws std::runtime_error with the line on errors.
	static Netlist parse(const std::string &text, const std::map<std::string, double> &params = {});

	// The circuit's runtime partitions, from its .part lines, as netlists of their own, in order:
	//   .part <name> [in=<node>,<node>...] [sleep] <element> <element> ...
	// A partition has the listed elements, the supplies and inputs whose nodes they use, and each
	// in= node as an input "@<node>" (the voltage another partition works out, this sample's if it
	// runs earlier, else the last sample's); it outputs "@<node>" for each node another partition
	// takes. A resistor across a cut is listed in both, its far node an input on each side. "sleep"
	// lets the partition stop on its own while its outputs are at rest and its in= nodes don't move
	// (a VCA whose envelope is down), though the rest of its voice runs. "over=N" runs it at N times
	// the voice's rate, N steps a sample (a filter whose top octave the voice's rate would warp).
	// Empty if there are no .part lines.
	struct Partition {
		std::string name, text;
		bool sleeps = false;
		int over = 1;
	};
	static std::vector<Partition> partitions(const std::string &text);

	int node(const std::string &name) const; // -1 if there is none
	int input(const std::string &name) const;
	int control(const std::string &name) const;
	int output(const std::string &name) const;
};

double parseValue(const std::string &text); // "4.7k" -> 4700; throws on junk

// A pot's resistance from its wiper to `b` as a share of the whole, at rotation `position` (0-1).
double potShare(double position, double taperMid, bool exponential = false);
// The j law: no share up to rotation x0, straight to `share` at rotation `at`, straight to 1.
double potShareJ(double position, double x0, double at, double share);

// A matrix stored by columns, each padded to an even length: the step's products run down the
// columns two rows at a time (Circuit's colMatVec), each row's sum in the order of a plain loop.
struct ColMat {
	int rows = 0, cols = 0, ld = 0;
	std::vector<double> d;
	void assign(int r, int c)
	{
		rows = r;
		cols = c;
		ld = r + (r & 1);
		d.assign(size_t(ld) * size_t(c), 0.0);
	}
	double &operator()(int r, int c) { return d[size_t(c) * size_t(ld) + size_t(r)]; }
	double operator()(int r, int c) const { return d[size_t(c) * size_t(ld) + size_t(r)]; }
	double *col(int c) { return d.data() + size_t(c) * size_t(ld); }
	const double *col(int c) const { return d.data() + size_t(c) * size_t(ld); }
};

class Circuit {
public:
	Circuit() = default;
	explicit Circuit(const Netlist &netlist) { load(netlist); }

	void load(const Netlist &netlist);
	const Netlist &netlist() const { return m_net; }
	void setName(const std::string &name) { m_name = name; } // for diagnostics
	const std::string &name() const { return m_name; }

	// The sample rate the companions are built for. Rebuilds.
	void setRate(double rate);
	double rate() const { return m_rate; }

	// Controls (pots and switches), 0-1. A change rebuilds the matrices before the next step.
	void setControl(int index, double position);
	double controlValue(int index) const { return m_controls[size_t(index)]; }

	// Settles every capacitor at the circuit's DC operating point with the inputs as they are now
	// (caps open; Newton, then gmin stepping, gain stepping, source stepping if needed). Returns
	// false, the state unchanged, if it didn't converge (a free-running VCO has no operating point
	// Newton can reach).
	bool solveDc();

	// Inputs, held until changed. Within a step they move linearly from their values at the last
	// step to these.
	void setInput(int index, double value) { m_u[size_t(m_constCount + index)] = value; }
	double inputValue(int index) const { return m_u[size_t(m_constCount + index)]; }

	// One sample: solves the nonlinear devices, updates the capacitors and the outputs. A sample
	// whose Newton solve fails (a stiff switching event) is redone in 4, 16 or 64 steps, and the
	// next 32 samples then run in 4 steps each; substeps(n) asks for that ahead of a known event.
	void step();
	void substeps(int samples) { m_subLeft = std::max(m_subLeft, samples); }
	// Holds: after a second failed sample within 256, the samples stay in quarters until a full
	// step, tried every 32 to 512 samples, agrees with them to 1 mV. For a resonance faster than
	// full steps follow (a KORG35 self-oscillating near 40 kHz at 4x): full steps that converge
	// there lock onto a quarter of the rate, and alternating them with quarters scrambled the
	// oscillation into noise in the audio band (docs/improvements.md, 2026-11-16). Off by default.
	void setHold(bool on) { m_hold = on && !holdDisabled(); }
	// The next `quarters` quarter steps by backward Euler (the samples they fall in are substepped):
	// an input's edge (a trigger) that a capacitor couples into a junction rings with the
	// trapezoidal rule, whose companion current flips sign each step while the junction clamps the
	// node (the claves' mute fell to -28 V where SPICE gives -15). SPICE drops to backward Euler
	// after a breakpoint the same way.
	void edge(int quarters) { m_edgeLeft = std::max(m_edgeLeft, quarters); }
	// The next sample's input changes are a step at its start rather than a ramp across it (a
	// trigger's edge takes well under a microsecond, a sample 10), and its first quarter runs in
	// sixteenths: C57 couples the snare's edge into IC14a as a 5 us spike, which the op-amp slews on.
	void jump() { m_jump = true; }
	void setTolerance(double volts) { m_tolerance = volts; }
	// Linear mode's tolerance: the voltage error a junction's linearisation may make before Newton
	// takes over (default 0.1 mV, or $MS20_LINTOL). A circuit whose behaviour hangs exponentially on
	// a junction voltage (the KORG35's cutoff, 0.4 % a 0.1 mV) wants less (docs/circuit/filters.md).
	void setLinearTolerance(double volts);
	double output(int index) const { return m_decim ? m_yOut[size_t(index)] : m_y[size_t(index)]; }
	// Outputs decimated from the quarters (off by default): where the solver takes a sample in
	// quarters (a hold, substeps), the outputs are filtered from them (circuit.cpp, decimTaps)
	// rather than read at the sample's end, and every output comes kDecimDelay samples late, filtered
	// or not, so the two agree. A KORG35 self-oscillating near 37 kHz at 4x is held in quarters;
	// read at the samples alone, its harmonics folded into the band (docs/improvements.md).
	void setDecimation(bool on);
	// After a rate change by `ratio` (new steps per old step), the decimation's history resampled
	// to the new step, so the outputs keep their delay in time across the change.
	void retime(double ratio);
	// The output the decimation will give at the next step if no quarter of the solver's comes in
	// (its delay line's next sample): what a block at the old rate would publish next.
	double pendingOutput(int index) const;
	static constexpr int kDecimDelay = 3;

	// Any node's voltage after the last step (solves the full system; for tests and probes).
	double nodeVoltage(int node) const;
	double capVoltage(int cap) const
	{
		syncCaps();
		return cap >= 0 && cap < m_nx ? m_vc[size_t(cap)] : 0;
	}
	int capIndex(const std::string &name) const; // by element name, -1 if none
	double capValue(int cap) const { return m_capF[size_t(cap)]; } // farads

	// The state (capacitor voltages and currents, port voltages, inputs), to save and restore.
	struct State {
		std::vector<double> vc, ic, v, u;
	};
	State state() const
	{
		syncCaps();
		return {m_vc, m_ic, m_v, m_u};
	}
	void setState(const State &s);
	// state() into `into`, reusing its storage (no allocation once it has the sizes): for a sample loop.
	void copyState(State &into) const
	{
		syncCaps();
		into.vc = m_vc;
		into.ic = m_ic;
		into.v = m_v;
		into.u = m_u;
	}

	// The constant sources (the supplies, and what the netlist hangs on them as constants: a trim's
	// offset span, a shared rail's other draw) at `scale` times their netlist values, from the next
	// step: the rails rising after a switch-on and falling after a switch-off (Machine::switchOn).
	// The op-amps' output swing (vsat) follows the rails less its headroom. Each built step size's
	// constant share is worked out again (the matrices don't hold the constants); linear mode, whose
	// matrices do, is left, and kept off while `moving` (the scale changes again within a few
	// steps). At 1, as loaded, nothing differs from a circuit that was never scaled.
	void setConstantScale(double scale, bool moving = false);
	double constantScale() const { return m_constScale; }
	// Switched off long ago: every capacitor, port and output at 0 V (the decimation's history
	// too), the solver's history gone. The constant scale stays as it is.
	void discharge();

	// Statistics since the last reset of them: Newton iterations and steps, the worst residual,
	// steps that needed smaller steps, and steps that failed even then.
	struct Stats {
		long steps = 0, iterations = 0, substepped = 0, failures = 0, factorizations = 0;
		long held = 0; // substepped samples of a hold (Circuit::step: full steps kept failing)
		long linearSteps = 0, linearizations = 0; // steps taken in linear mode, and its rebuilds
		long softened = 0; // smallest steps that needed the comparators softened to converge
		long nyquist = 0;  // MS20_NYQ: full steps where a port reversed direction by over 0.1 mV twice running
		double worstResidual = 0;
	};
	const Stats &stats() const { return m_stats; }
	void resetStats() { m_stats = {}; }

	// The netlist as an ngspice deck: the controls at their current positions, the inputs as PWL
	// sources from `inputWaves` (per input: time, value pairs; missing ones hold their value now),
	// a transient run of `seconds` at `step`, and the outputs written by wrdata.
	std::string toSpice(const std::vector<std::vector<std::pair<double, double>>> &inputWaves, double seconds, double step,
	                    const std::string &dataFile) const;

	int stateCount() const { return m_nx; }
	int portCount() const { return m_np; }

	// Newton's linear algebra on the sparse nodal system instead of the dense port matrices (see
	// "The sparse Newton" in circuit.cpp): the same iterates, the same results to rounding, and for a
	// circuit with many ports (a ladder filter's 32) several times cheaper. Off by default (a small
	// circuit gains nothing); a circuit whose network doesn't fit it (sparseInfo().ok false) stays on
	// the dense one. Rebuilds.
	void setSparse(bool on);
	bool sparse() const { return m_sparseWanted; }
	struct SparseInfo {
		bool ok = false;      // in use (at the sample's step size)
		int nodes = 0;        // the Jacobian's size: the devices' nodes that aren't fixed by a source
		int entries = 0;      // its entries after fill
		long ops = 0;         // multiply-adds a factorisation
		long fallbacks = 0;   // factorisations that met a small pivot and went dense
	};
	SparseInfo sparseInfo() const;

private:
	// A diode (one port), bipolar transistor (two ports: b-e, b-c), JFET or MOSFET (g-s, g-d), OTA
	// (abc-vee, in+ - in-, out-ground), comparator (in+ to ground, in- to ground, out-vee), an op-amp's
	// output limiter (pole-ground, out-ground) or its slewing input stage (one port, across the pole's
	// R).
	struct Device {
		const Netlist::Model *model = nullptr;
		int port = 0;    // its first port
		int ports = 1;
		double sign = 1; // -1 for PNP and P-channel
		// a diode's or BJT's 1 / (n V_T) for its terms (forward, reverse, B-E and B-C leakage),
		// 1 / VAF, 1 / BF, 1 / BR: products rather than divisions in each Newton iteration
		double kf = 0, kr = 0, ke = 0, kc = 0, kaf = 0, kbf = 0, kbr = 0;
	};
	// The state-space system for one step size (row-major matrices). Level 0 is the sample, level
	// n a quarter of level n-1.
	struct Level {
		double T = 0;
		bool built = false;
		std::vector<char> be;   // per capacitor: integrated by backward Euler (theta 1)
		std::vector<double> theta, hist; // per capacitor: the theta method's theta, and (1 - theta)/theta
		std::vector<double> gc; // 2C/T per capacitor (C/T by backward Euler)
		std::vector<double> vcScale; // x to the capacitor's voltage: 1 / (2 gc), or 1 / gc by backward Euler
		std::vector<double> A, B, C, D, E, F, Dy, Ey, Fy;
		// The same packed by rows over w = [x | the inputs that vary | the ports' currents]: P is
		// [D E] (the ports before Newton), S is [Dy Ey Fy; A B C] (the outputs, then the next
		// states), so each row is one loop; sConst is [yConst; xConst].
		std::vector<double> P, S, sConst;
		ColMat Pc, Sc, Fc; // P, S and F by columns, for the step's products
		std::vector<double> FT; // F transposed (its columns contiguous, for factor)
		std::vector<double> pConst, xConst, yConst; // E, B, Ey times the constant sources
		std::vector<double> luM;     // the active block of M, factored
		std::vector<int> luPiv;
		bool luValid = false;
		std::vector<double> cols;    // M's device columns (column c at c * np)
		std::vector<int> active, pos; // the active ports, and each port's place among them (-1)
		int nActive = 0;
		std::vector<double> fColMax; // each column of F's largest entry
		std::vector<double> fColMaxW; // the same with each row times its port's weight (the residual's cutoff)
		// The sparse Newton's numbers for this step size (Circuit::Sparse has the structure): Y, the
		// network's Schur complement onto the devices' nodes (Circuit::Sparse's packing); K, the
		// Jacobian Y + the devices' stamps, factored in place, dinv its pivots' reciprocals, Lr and U1
		// its L and U (each U row over its pivot) in the solve's passes' order; gp, gs the device
		// blocks K was built from (G', the inactive columns zero): a pair's four, the others' by port
		// and block column (np x 3). For factor()'s test of a column: fArg, per port, the row of F's
		// largest entry in its column; per pair, low2 a bound (sparseBuild) and pairF its ports' fColMax
		// and F's entries at their fArg rows.
		struct Sparse {
			bool on = false;    // built: Newton uses it
			bool dense = false; // K factored densely this time (a small pivot): Kd, piv
			long build = 0;     // which sparseBuild made these (Circuit::Sparse::fi)
			std::vector<double> Y, K, dinv, gp, gs, Kd, Lr, U1, low2, pairF;
			std::vector<int> piv;
			std::vector<int> fArg;
		} sp;
	};
	static constexpr int kLevels = 4;

	void build();
	void buildLevel(Level &L, bool allBackwardEuler);
	void constShares(Level &L); // a built level's pConst, xConst, yConst and sConst from m_u (setConstantScale)
	// A step size's system, built the first time it is needed (the substep levels rarely are).
	Level &level(int lv)
	{
		Level &L = m_inEdge ? m_edgeLevels[lv] : m_levels[lv];
		if (!L.built)
			buildLevel(L, m_inEdge);
		return L;
	}
	void buildIfDirty() { if (m_dirty) build(); }
	void stepSample();  // step()'s sample
	void decimate();    // the outputs from this sample's quarters (setDecimation)
	void decimReset();  // the filter's history at the outputs as they stand
	bool solveDcCore(); // solveDc
	void deviceCurrents(const double *v, double *i, double *jac) const; // jac: np x np block-diagonal
	void deviceEval(const Device &d, const double *v, double *i, double *jac) const; // one device's ports
	void deviceEvalRaw(const Device &d, const double *v, double *i, double *jac) const; // without anchor and gmin
	// a bipolar transistor's parameters, as deviceEvalRaw takes them (bjtEval), and its first port
	struct Bjt {
		double sign, is, ise, isc, kf, kr, ke, kc, kaf, kbf, kbr;
		int port = 0;
	};
	static void bjtEval(const Bjt &b, const double *v, double *i, double *jac, int np);
	void deviceCurrentsSparse(const double *v, double *i, double *jac) const; // deviceCurrents by m_sp's lists
	// Newton and its linear algebra, compiled for the port counts up to 8 (NP; 0 = any): the same
	// operations, with the loops over the ports unrolled.
	bool newton(Level &L, const double *p, double *v, double *i, int maxIterations);
	template <int NP> bool newtonT(Level &L, const double *p, double *v, double *i, int maxIterations);
	template <int NP> bool factor(Level &L);
	template <int NP> void solveActive(Level &L, double *r) const;
	// The sparse Newton (setSparse; "The sparse Newton" in circuit.cpp): its structure, worked out
	// once from the network's pattern (sparseAnalyse); a step size's numbers from S, the level's MNA
	// matrix before factoring (sparseBuild); K factored (sparseFactor, factorSparse: with r, the step
	// solved too) and Newton's step solved with it (solveSparse: b = V^T G' r, the passes, dv = r - V z).
	struct Sparse {
		bool analysed = false, ok = false;
		int nd = 0, nnz = 0, nL = 0;   // D: the devices' nodes (and any unknown that couldn't go first)
		std::vector<int> lOrder;       // the network's other unknowns, in elimination order
		std::vector<char> fixed;       // per MNA unknown: a node a grounded source sets, or its current
		std::vector<int> dUnknown;     // D's MNA unknowns, in pivot order
		std::vector<int> portP, portM; // each port's nodes' places in D (nd, b's spare slot: ground or fixed)
		// K's filled pattern in pivot order, its entries kept as [diagonal | L by columns | U by rows]:
		// pivot k's rows below it lRow[lBeg[k]..lBeg[k + 1]] (L's entries nd + e), its columns right of
		// it uCol[uBeg[k]..uBeg[k + 1]] (U's entries nd + nL + u); L entry e's row update writes upd[updBeg[e]..]
		std::vector<int> lBeg, lRow, uBeg, uCol, updBeg, upd;
		std::vector<int> at;             // nd x nd: entry (i, j)'s place, -1 outside the pattern
		std::vector<int> entRow, entCol; // each entry's (i, j)
		// Devices with two ports on one plus node (a transistor's b-e and b-c, a FET's g-s and g-d):
		// their first port, their nodes x y z (b e c) in D, and the nine places xx xy xz yx yy yz zx zy zz
		struct Pair {
			int port = 0;
			int node[3] = {};
			int t[9] = {};
		};
		std::vector<Pair> pairs;
		std::vector<int> pairNodes;  // each pair's first port and x, y, z, four apiece (the solve's)
		std::vector<Bjt> bjts;       // deviceCurrentsSparse's transistors, and the other devices
		std::vector<int> nonBjts;
		std::vector<int> others;     // the other devices (m_devices' indices), port by port:
		std::vector<int> stamp;      // per port q and block column j, G'[q][c]'s four places (+ - - +)
		// the solve's two passes as terms (spRowPass): L's rows in order, U's from the last; each
		// term's row, column, entry (among L's or U's) and whether it starts its row
		std::vector<int> fRow, fCol, fIdx, bRow, bCol, bIdx;
		std::vector<char> fStart, bStart;
		std::vector<int> lToF, uToB; // each L entry's forward term, each U entry's back term
		long ops = 0, fallbacks = 0;
		std::vector<double> b;       // scratch (nd + 1)
		// newtonT's F i, for the currents fiI with fiLevel's F (its sp.build); zeros (scratch)
		std::vector<double> fi, fiI, zeros;
		const Level *fiLevel = nullptr;
		long fiBuild = 0, builds = 0;
	} m_sp;
	bool m_sparseWanted = false;
	void sparseAnalyse(const std::vector<double> &S);
	void sparseBuild(Level &L, const std::vector<double> &S);
	bool sparseFactor(double *val, double *dinv, double *lr, double *u1, double *b) const; // in place; false on a small pivot
	bool factorSparse(Level &L, double *r);
	void solveSparse(Level &L, double *r);
	void sparseScatter(const Level &L, const double *r, double *b) const;
	void sparseFinish(const Level &L, double *r, double *b) const;
	void sparseGather(double *r, double *b) const;
	// Advances by one step of level `lv` to inputs `u`; false (state unchanged) if Newton fails.
	bool advance(int lv, const double *u);
	// Advances by one step of level `lv`, splitting it into four of the next level if it fails.
	bool advanceSplit(int lv, const double *u0, const double *u1);
	// Four steps of level `lv`, the inputs moving from u0 to u1.
	bool advanceFour(int lv, const double *u0, const double *u1);
	bool advanceEdge(const double *u1); // a sample after jump()
	// Comparators: the op-amps (with a slew rate and an output limit) that no negative feedback
	// holds at the sample rate's scale (a Schmitt trigger's, an open loop's; Circuit::build tells
	// them). Their pole is integrated by the trapezoidal rule (backward Euler, as the others', made
	// a flip start up to a step early: the snare's VCOs ran 0.5 % sharp at 96 kHz), and a sample in
	// which one flips (its input stage's current reverses, or its output moves over kFlipVolts) is
	// redone in kFlipSteps steps, which place the flip within a few tenths of a microsecond.
	struct Comp {
		int slew = -1, lim = -1, cap = -1; // the slew device's port, the limiter's first port, the pole's capacitor
		double imax = 1, gm = 1, vsat = 1; // the input stage's current and 1/r, the output's limit
		bool comparator = false;
	};
	std::vector<Comp> m_comps;
	// The op-amps' output limiters (setConstantScale): the model, its port, its swing as loaded, and
	// its comparator's index in m_comps (-1: none).
	struct Lim {
		Netlist::Model *model = nullptr;
		int port = 0;
		double vsat = 0;
		int comp = -1;
	};
	std::vector<Lim> m_limiters;
	bool m_compsClassified = false;
	int m_comparators = 0;
	bool advanceFine(const double *u0, const double *u1); // a sample in kFlipSteps steps
	struct Saved {
		std::vector<double> x, xpLast, vc, ic, v, i, vOld, y;
		bool xValid = false, capsStale = false, prevValid = false;
	} m_saved;
	bool flipped(const Saved &s) const; // a comparator flipped since s
	bool flipped() const { return flipped(m_saved); }
	bool flipAhead() const; // a comparator's input crosses in the next sample
	// A comparator between its rails at ports v (its input stage short of saturation, or its output
	// short of the rail): the sample after a flip's redone sample goes straight to the sixteenths.
	bool flipping(const double *v) const;
	bool m_lastRedone = false; // the last full-sample step was a flip's, redone
	void saveStep(Saved &s);
	void restoreStep(const Saved &s, bool invalidate = true);
	void saveStep() { saveStep(m_saved); }
	void restoreStep() { restoreStep(m_saved); }
	void solveFull(const double *x, const double *u, const double *i, std::vector<double> &z) const;
	// Linear mode: while every device stays in a region where it is linear to within the window
	// (a junction off, an op-amp output inside its knee, a conducting junction within kLinWindow of
	// where it was linearised), a step is the linearised system's state update: no Newton.
	bool linearize();
	bool linearStep(const double *u);
	void leaveLinear();
	std::string portName(int port) const; // diagnostics: a port's device type, index and port

	Netlist m_net;
	std::string m_name;
	double m_rate = 96000;
	std::vector<double> m_controls;
	bool m_dirty = true;
	double m_constScale = 1; // setConstantScale
	bool m_afterControl = false; // a control moved since the last step (limitJunction, the limiters' damping)
	std::vector<double> m_dxPrev; // newtonT: each limiter port's last step, in knees

	// Sizes: unknowns of the MNA system, states (capacitors), ports, inputs (constants first).
	int m_n = 0, m_nx = 0, m_np = 0, m_nu = 0, m_ny = 0, m_constCount = 0;
	std::vector<int> m_caps; // element index of each capacitor
	std::vector<double> m_capF; // their capacitance
	// Integrated by backward Euler rather than the trapezoidal rule: the op-amps' internal poles,
	// whose closed loops (MHz) the trapezoidal rule would map near z = -1, ringing at Nyquist, and
	// the 10 pF on nodes between junctions (nanoseconds through a conducting one: the cowbell's VCA
	// rang at Nyquist on them, costing Newton iterations and SPICE agreement)
	std::vector<char> m_capBE;
	std::vector<double> m_capTheta; // each capacitor's theta: 1 backward Euler, 0.5 trapezoidal, or an op-amp pole's theta=
	// a capacitor's voltage and current from the companion states after and before a step:
	// v = theta (x[n] + (1 - theta)/theta x[n-1]) / gc, i = theta (x[n] - x[n-1]) (trapezoidal (x[n] +
	// x[n-1]) / 2gc and (x[n] - x[n-1]) / 2; backward Euler x[n] / gc and x[n] - x[n-1])
	static void capFromX(const Level &L, int k, double xn, double xp, double &v, double &i)
	{
		if (L.be[size_t(k)]) {
			v = xn * L.vcScale[size_t(k)];
			i = xn - xp;
		} else if (L.theta[size_t(k)] == 0.5) {
			v = (xn + xp) * L.vcScale[size_t(k)];
			i = 0.5 * (xn - xp);
		} else {
			v = (xn + L.hist[size_t(k)] * xp) * L.vcScale[size_t(k)];
			i = L.theta[size_t(k)] * (xn - xp);
		}
	}
	// the companion state's current term, (1 - theta)/theta i (trapezoidal i, backward Euler 0)
	static double capHist(const Level &L, int k, double i) { return L.be[size_t(k)] ? 0 : L.theta[size_t(k)] == 0.5 ? i : L.hist[size_t(k)] * i; }
	std::vector<Device> m_devices;
	std::vector<int> m_portPlus, m_portMinus; // nodes of each port
	std::vector<double> m_portVt, m_portVcrit, m_portSign;
	std::vector<double> m_portWeight; // Newton judges each port's residual times this (output volts per port volt)
	std::vector<int> m_portClass[5]; // the ports Newton steps unlimited, as open collectors, limiters' inputs, junctions, clamped outputs
	std::vector<double> m_portClampHi, m_portClampLo; // class 4: an OTA's output clamps (volts)
	// The MOSFETs, whose two ports Newton limits together (SPICE's fetlim and limvds): the first port,
	// the sign, the threshold in the sign's frame; and their ports' voltages before a Newton step.
	struct Mos {
		int port = 0;
		double sign = 1, vth = 0;
	};
	std::vector<Mos> m_mos;
	std::vector<double> m_mosOld;
	// Anchors: a port whose nodes the linear network alone barely ties down (joined to the rest only
	// through devices, as a transistor's base on an island of diodes, or an op-amp's output driving
	// nothing but devices) has a conductance across it in the linear network that its device's
	// current gives back (deviceCurrents), so the equations are the same but F stays well
	// conditioned. Per port (0: none), and the anchored ports; chosen at the first build.
	std::vector<double> m_anchor;
	std::vector<int> m_anchored;
	bool m_anchorsChosen = false;
	std::vector<int> m_shuntNodes; // nodes with no DC path to ground at all, even through devices

	// MNA without the capacitors, and the incidence matrices (row-major).
	std::vector<double> m_G, m_Nx, m_Nu, m_Nn;
	Level m_levels[kLevels];
	Level m_edgeLevels[kLevels]; // the same with every capacitor by backward Euler, for edges
	bool m_inEdge = false;
	int m_edgeLeft = 0;
	bool m_jump = false;

	// State: capacitor voltages and currents, port voltages and currents, inputs now and at the
	// last step, outputs.
	mutable std::vector<double> m_vc, m_ic;
	std::vector<double> m_v, m_i, m_u, m_uLast, m_y;
	// Between full-sample steps the state is kept as the companion states (the next step's x is the
	// last step's x[n]), not converted to the capacitors' voltages and currents and back each
	// sample: m_x holds it while m_xValid, and m_vc/m_ic are worked out from it (and m_xpLast) only
	// when something needs them (another step size, a rebuild, the sleep checks, a saved state).
	std::vector<double> m_x;
	bool m_xValid = false;
	mutable bool m_capsStale = false;
	void syncCaps() const; // m_vc/m_ic from m_x and m_xpLast, if stale
	void dropX();          // m_vc/m_ic current, m_x no longer used
	std::vector<double> m_xpLast; // the companion states the last step started from
	int m_subLeft = 0;
	// A hold (setHold, step): full steps failed twice within kHoldWindow samples, so the samples
	// run in quarters until a trial full step agrees with them (holdTrial). m_holdLen is the hold's
	// current length in samples, m_failStep the step the last full step failed at; m_holdWrong: a
	// trial in this hold converged somewhere else than the quarters.
	bool m_hold = false, m_held = false, m_holdWrong = false;
	int m_holdLen = 0;
	long m_failStep = 0;
	Saved m_trialFrom;
	std::vector<double> m_trialVc, m_trialY;
	bool holdTrial(const double *u1); // a full step from here, kept in m_trial*, then undone
	static bool holdDisabled();       // MS20_NOHOLD
	int m_maxIt = 40; // Newton's iterations for the next advance
	// setDecimation: this sample's quarters (ny x 4) if the solver took them (m_quarters); the
	// quarters' ring (ny x 32) and which were the solver's; the samples' ring (ny x kDecimDelay + 1)
	bool m_decim = false, m_quarters = false;
	std::vector<double> m_q4, m_qRing, m_yRing, m_yPrev, m_yOut;
	std::vector<char> m_qReal;
	int m_qPos = 0, m_yPos = 0, m_realInWindow = 0;
	bool m_current = false; // m_i and m_jac are the devices' at m_v (Newton skips evaluating them first)
	bool m_prevValid = false; // m_vOld is the step before's ports (a full-sample Newton step)
	// Whether Newton starts from the ports' straight-line prediction: it saves a resonator a third
	// of its iterations and costs a switching VCA some, so each circuit keeps whichever needs fewer,
	// trying the other for a short window now and then.
	struct Predictor {
		bool on = false, trial = false;
		long steps = 0, iterations = 0; // in the current window
		double rate = 1e9;              // iterations a step in the last window of the current choice
		static constexpr long kWindow = 16384, kTrial = 1024;
		bool use() const { return on != trial; }
		void count(long its)
		{
			++steps;
			iterations += its;
			const long len = trial ? kTrial : kWindow;
			if (steps < len)
				return;
			const double r = double(iterations) / double(steps);
			if (trial) {
				if (r < 0.95 * rate) // the other way is better: keep it
					on = !on;
				trial = false;
			} else {
				rate = r;
				trial = true;
			}
			steps = iterations = 0;
		}
	} m_predict;
	double m_tolerance = defaultTolerance(); // Newton: largest port-voltage residual accepted, volts
	double m_linTol = defaultLinearTolerance(); // linear mode's tolerance, volts
	static double defaultTolerance();
	static double defaultLinearTolerance();
	double m_soften = 1;       // the comparators' transition widths are multiplied by this (DC solve)
	double m_dcGmin = 0;       // a conductance across every port (the DC solve's gmin stepping)
	double m_dcGain = 1;       // the op-amps' open-loop gain is multiplied by this (DC gain stepping)
	bool m_dcFailed = false;   // the last solveDc didn't converge (toSpice then starts ngspice from this state)
	// the op-amps' output stages as VCVSs (no vsat): their row in the MNA, the input node, the gain
	struct OpOut {
		int row = 0, in = 0;
		double gain = 0;
	};
	std::vector<OpOut> m_opOut;
	std::vector<int> m_limiterPorts; // the op-amps' limiters' control ports (their gain, gain stepping)
	// scratch
	std::vector<double> m_w; // advance's [x | varying inputs | currents] (Level::P, Level::S)
	std::vector<double> m_yx; // advance's outputs and next states
	std::vector<double> m_vp, m_p, m_xn, m_r, m_vOld, m_jac, m_vSave, m_vcSave, m_icSave, m_iSave, m_uMid, m_uTo;
	std::vector<double> m_splitTo[kLevels], m_splitFrom[kLevels]; // advanceSplit's inputs, per level
	mutable std::vector<double> m_ra;
	std::vector<int> m_sigPorts;
	Stats m_stats;

	struct Linear {
		bool on = false;
		int wait = 0, backoff = 8; // samples until the next try, and the wait after a short run
		int exitPort = -1;         // the port whose window the last run left
		long since = 0;            // samples in linear mode since it was entered
		long updates = 0;          // devices linearised again (relinearize) since linearize
		double relinRate = 0;      // updates a step, a decaying average
		// M, mConst left as they were at the updates (rowsStale): the states and outputs from S and
		// the currents instead, cheaper over rowsRate updates a step (linearRows)
		bool rowsStale = false;
		double rowsRate = 0;
		// over [x | u]: the ports K, kv; the next states then the outputs M, mConst; Cs those rows'
		// currents' columns (from Level::S); Ic, ki the currents. K and M share their columns, KM
		// (M from row mRow, K's rows padded to even), and kv and mConst likewise, kvm; fused is a
		// step's [v; next states; outputs] from them (linearStep)
		ColMat KM, Cs;
		int mRow = 0;
		std::vector<double> kvm, fused, Ic, ki;
		std::vector<double> lu;     // I - F G factored
		ColMat Zf;                  // (I - F G)^-1 F
		std::vector<int> piv;
		std::vector<double> lo, hi, v0, i0, G; // the ports' windows; the linearisation point
		std::vector<double> zeff;              // each port's largest effect at the ports, weighted (deviceWindow)
		std::vector<double> v;                 // the ports at the last linear step
		// scratch
		std::vector<double> c, col, evI, evJac, Zc, T, GZ, CZc, loSave, hiSave, il, rows, gzc, zeros;
		std::vector<int> relin;
	} m_lin;
	std::vector<int> m_portDevice; // each port's device (an index into m_devices)
	int deviceWindow(const Device &d, const double *v0, const char *&why) const;
	bool relinearize(const Device &d, const double *v, const char *&why); // false: d not linear there (why)
	template <int K> bool relinearizeT(const Device &d, const double *v, const char *&why); // relinearize for K ports
	bool linearMatrices(); // lin's matrices from its devices' points (false: I - F G singular)
	void stepRows();       // lin's M, mConst (and Ic, ki) from K, kv and the devices' points
	void linearRows(const double *x, const double *u, const double *v, double *xn, double *y); // the next states, the outputs
	double zeffOf(int port) const;
};

} // namespace bsrack::circuit
