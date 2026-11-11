#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

// A circuit run sample by sample from its netlist, the way the schematic draws it: resistors,
// capacitors, pots and switches, diodes and bipolar transistors (SPICE's Gummel-Poon equations
// without charge storage), ideal op-amps, and voltage and current sources that are constants or
// inputs. Each TR-808 voice is one such netlist (core/netlists.h), transcribed from the service
// notes part by part (docs/circuit.md); the same netlist exports to ngspice (toSpice) for the SPICE
// comparisons (tools/spice).
//
// The solver is the nodal DK method (Yeh, Abel and Smith 2010; Holters and Zolzer 2015): modified
// nodal analysis with trapezoidal companions for the capacitors, reduced to a state-space system
// whose only implicit part is the nonlinear devices' port voltages, solved with Newton each sample.
// Pots and switches are part of the linear system, so moving one rebuilds the matrices (a few
// microseconds for a voice).
namespace tr808::circuit {

// Netlist syntax, one element per line, SPICE-like ("*" comments, "+" continues a line, values
// with SPICE suffixes f p n u m k meg g, trailing unit letters ignored, node "0" or "gnd" is ground):
//   R<name> a b <ohms>
//   C<name> a b <farads> [da=<fraction>] [leak=<ohms>]
//                                                      (da: dielectric absorption, Dow's model: four
//                                                       series R-C branches across it, time constants
//                                                       0.01, 0.1, 1 and 10 s, each fraction/4 of C,
//                                                       so a soaked capacitor gives back about
//                                                       `fraction` of its voltage; leak: a resistor
//                                                       across it. Both are plain R and C elements.)
//   D<name> anode cathode <model>
//   Q<name> collector base emitter <model>            (NPN or PNP, from the model)
//   J<name> drain gate source <model>                 (N- or P-channel JFET, from the model)
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
//   X<name> in+ in- out opamp [gbw [gain [vsat [sr]]]] (ideal op-amp: in+ = in-, any output current;
//                                                       with gbw, a single-pole one; with vsat, its
//                                                       output clips softly at +-vsat; with sr (V/us),
//                                                       its input stage saturates, slewing at sr)
//   P<name> a wiper b <ohms> <control> [taper]         (pot; the control's position 0-1 moves the
//                                                       wiper from b to a; taper lin (default),
//                                                       a<mid> audio: the share of the track at mid
//                                                       rotation as two straight segments, e.g. a0.15,
//                                                       c<mid> the reverse)
//   S<name> a b <control> [<on ohms> [<off ohms>]]     (closed while the control is >= 0.5; "!name"
//                                                       closes it below 0.5)
//   .model <name> D(IS= N= RS=)   .model <name> NPN(IS= BF= BR= NF= NR= VAF= ISE= NE= ISC= NC=)
//   .model <name> NJF(VTO= BETA= LAMBDA= IS= N=)    (and PNP, PJF)
//   .output <name> <node> [<node>]                     (a voltage to read, node minus node)
//   .control <name> <default>
//   .param <name>=<value> ...                          ({name} anywhere is replaced by its value)
struct Netlist {
	struct Element {
		char kind = 0;          // R C D Q J B V I E X P S
		std::string name;
		std::vector<int> nodes; // as listed in the syntax above
		double value = 0;       // ohms, farads, volts, amps, gain
		std::string model;      // D/Q: model name
		int input = -1;         // V/I: input index, or -1 for a constant
		int control = -1;       // P/S: control index
		bool invert = false;    // S: closed below 0.5
		double taperMid = 0.5;  // P: share of the track at mid rotation
		double ron = 0.1, roff = 1e9;
	};
	struct Model {
		enum Type { Diode, Npn, Pnp, Njf, Pjf, Ota, Comparator, Limiter, Slew } type = Diode;
		double is = 1e-14, n = 1, rs = 0;
		double bf = 100, br = 1, nf = 1, nr = 1, vaf = 0, ise = 0, ne = 1.5, isc = 0, nc = 2;
		double vto = -2, beta = 1e-4, lambda = 0; // JFET (Shichman-Hodges, SPICE's level 1)
		double gm = 1, isink = 16e-3, vg = 0.5e-3, vsat = 0.15, vcm = 1e9, vos = 0; // OTA, comparator
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
	// (a VCA whose envelope is down), though the rest of its voice runs. Empty if there are no .part
	// lines.
	struct Partition {
		std::string name, text;
		bool sleeps = false;
	};
	static std::vector<Partition> partitions(const std::string &text);

	int node(const std::string &name) const; // -1 if there is none
	int input(const std::string &name) const;
	int control(const std::string &name) const;
	int output(const std::string &name) const;
};

double parseValue(const std::string &text); // "4.7k" -> 4700; throws on junk

// A pot's resistance from its wiper to `b` as a share of the whole, at rotation `position` (0-1).
double potShare(double position, double taperMid);

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
	// The op-amps' internal poles (c<X>_pole) by the trapezoidal rule rather than backward Euler.
	// Backward Euler is first order and takes the top octave down; it is the default because the
	// trapezoidal rule maps a MHz closed loop near z = -1, where it rings, so a netlist gets it only
	// where that was checked (VoiceBoard). Kept across load(); set it before the circuit runs (a
	// state from the other rule doesn't carry over).
	void setPoleTrap(bool on);
	bool poleTrap() const { return m_poleTrap; }

	// Controls (pots and switches), 0-1. A change rebuilds the matrices before the next step.
	void setControl(int index, double position);
	double controlValue(int index) const { return m_controls[size_t(index)]; }

	// Settles every capacitor at the circuit's DC operating point with the inputs as they are now
	// (caps open, Newton with source stepping if needed). Returns false if it didn't converge.
	bool solveDc();

	// Inputs, held until changed. Within a step they move linearly from their values at the last
	// step to these.
	void setInput(int index, double value) { m_u[size_t(m_constCount + index)] = value; }
	double inputValue(int index) const { return m_u[size_t(m_constCount + index)]; }

	// One sample: solves the nonlinear devices, updates the capacitors and the outputs. A sample
	// whose Newton solve fails (a stiff switching event) is redone in 4, 16 or 64 steps, and the
	// next 32 samples then run in 4 steps each; substeps(n) asks for that ahead of a known event.
	void step()
	{
		// the common case inline: a linear step with nothing asked of the sample (stepRest has the rest)
		if (m_lin.on && !m_dirty && !m_jump && m_subLeft == 0 && m_edgeLeft == 0 && linearSample()) {
			++m_stats.steps;
			return;
		}
		stepRest();
	}
	void substeps(int samples) { m_subLeft = std::max(m_subLeft, samples); }
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
	double output(int index) const { return m_y[size_t(index)]; }
	// The inputs (index order) and outputs where they are kept, for a caller that moves them all
	// each step.
	double *inputs() { return m_u.data() + m_constCount; }
	const double *outputs() const { return m_y.data(); }

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

	// Statistics since the last reset of them: Newton iterations and steps, the worst residual,
	// steps that needed smaller steps, and steps that failed even then.
	struct Stats {
		long steps = 0, iterations = 0, substepped = 0, failures = 0, factorizations = 0;
		long linearSteps = 0, linearizations = 0; // steps taken in linear mode, and its rebuilds
		long softened = 0; // smallest steps that needed the comparators softened to converge
		long nyquist = 0;  // TR808_NYQ: full steps where a port reversed direction by over 0.1 mV twice running
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

private:
	// A diode (one port), bipolar transistor (two ports: b-e, b-c), JFET (g-s, g-d), OTA (abc-vee,
	// in+ - in-, out-ground), comparator (in+ to ground, in- to ground, out-vee), an op-amp's output
	// limiter (pole-ground, out-ground) or its slewing input stage (one port, across the pole's R).
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
		std::vector<char> be;   // per capacitor: integrated by backward Euler
		std::vector<double> gc; // 2C/T per capacitor (C/T by backward Euler)
		std::vector<double> vcScale; // x to the capacitor's voltage: 1 / (2 gc), or 1 / gc by backward Euler
		std::vector<double> A, B, C, D, E, F, Dy, Ey, Fy;
		// The same packed by rows over w = [x | the inputs that vary | the ports' currents]: P is
		// [D E] (the ports before Newton), S is [A B C; Dy Ey Fy] (the next states, then the
		// outputs: a state buffer's [x | y]), so each row is one loop; sConst is [xConst; yConst].
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
	};
	static constexpr int kLevels = 4;

	void build();
	void buildLevel(Level &L, bool allBackwardEuler);
	// A step size's system, built the first time it is needed (the substep levels rarely are).
	Level &level(int lv)
	{
		Level &L = m_inEdge ? m_edgeLevels[lv] : m_levels[lv];
		if (!L.built)
			buildLevel(L, m_inEdge);
		return L;
	}
	void buildIfDirty() { if (m_dirty) build(); }
	void deviceCurrents(const double *v, double *i, double *jac) const; // jac: np x np block-diagonal
	// Newton and its linear algebra, compiled for the port counts up to 8 (NP; 0 = any): the same
	// operations, with the loops over the ports unrolled.
	bool newton(Level &L, const double *p, double *v, double *i, int maxIterations);
	template <int NP> bool newtonT(Level &L, const double *p, double *v, double *i, int maxIterations);
	template <int NP> bool factor(Level &L);
	template <int NP> void solveActive(Level &L, double *r) const;
	// Advances by one step of level `lv` to inputs `u`; false (state unchanged) if Newton fails.
	bool advance(int lv, const double *u);
	template <int NP> bool advanceT(int lv, const double *u); // compiled for the port count (as newtonT)
	// Advances by one step of level `lv`, splitting it into four of the next level if it fails.
	bool advanceSplit(int lv, const double *u0, const double *u1);
	// Four steps of level `lv`, the inputs moving from u0 to u1.
	bool advanceFour(int lv, const double *u0, const double *u1);
	bool advanceEdge(const double *u1); // a sample after jump()
	void solveFull(const double *x, const double *u, const double *i, std::vector<double> &z) const;
	// Linear mode: while every device stays in a region where it is linear to within the window
	// (a junction off, an op-amp output inside its knee, a conducting junction within kLinWindow of
	// where it was linearised), a step is the linearised system's state update: no Newton.
	bool linearize();
	bool linearStep(const double *u);
	bool linearSample(); // linearStep on this sample's inputs; out of linear mode (leaveLinear) if it fails
	void stepRest();     // step() but its inline case
	void leaveLinear();

	Netlist m_net;
	std::string m_name;
	double m_rate = 96000;
	bool m_poleTrap = false; // setPoleTrap
	std::vector<double> m_controls;
	bool m_dirty = true;

	// Sizes: unknowns of the MNA system, states (capacitors), ports, inputs (constants first).
	int m_n = 0, m_nx = 0, m_np = 0, m_nu = 0, m_ny = 0, m_constCount = 0;
	std::vector<int> m_caps; // element index of each capacitor
	std::vector<double> m_capF; // their capacitance
	// Integrated by backward Euler rather than the trapezoidal rule: the op-amps' internal poles,
	// whose closed loops (MHz) the trapezoidal rule would map near z = -1, ringing at Nyquist, and
	// the 10 pF on nodes between junctions (nanoseconds through a conducting one: the cowbell's VCA
	// rang at Nyquist on them, costing Newton iterations and SPICE agreement)
	std::vector<char> m_capBE;
	std::vector<Device> m_devices;
	// The bipolar transistors and diodes, the most of every circuit's devices, with their
	// parameters together (deviceCurrents runs them first, in loops of their own), and the others.
	struct Bjt {
		int p = 0;
		double s = 1, is = 0, kf = 0, kr = 0, ke = 0, kc = 0, ise = 0, isc = 0, kaf = 0, kbf = 0, kbr = 0;
	};
	struct Diode {
		int p = 0;
		double is = 0, kf = 0;
	};
	std::vector<Bjt> m_bjts;
	std::vector<Diode> m_diodes;
	std::vector<int> m_otherDevices; // indices into m_devices
	std::vector<int> m_portPlus, m_portMinus; // nodes of each port
	std::vector<double> m_portVt, m_portVcrit, m_portSign;
	std::vector<double> m_portWeight; // Newton judges each port's residual times this (output volts per port volt)
	std::vector<int> m_portClass[4]; // the ports Newton steps unlimited, as open collectors, limiters' inputs, junctions
	std::vector<char> m_portKind;    // each port's class

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
	// sample: x() holds it while m_xValid, and m_vc/m_ic are worked out from it (and xpLast()) only
	// when something needs them (another step size, a rebuild, the sleep checks, a saved state).
	// Three buffers in turn, each [x | y | (pad) | v | (pad)]: the state now (x()), the next step's
	// (xn(), which a step's products write), and the last step's (xpLast()); a full step moves them
	// round. A linear step's ports (v, at m_vOff) stay with its state.
	std::vector<double> m_sb;
	int m_sbStride = 0, m_vOff = 0;
	int m_bx = 0, m_bxn = 1, m_bxp = 2;
	double *x() { return m_sb.data() + size_t(m_bx) * size_t(m_sbStride); }
	double *xn() { return m_sb.data() + size_t(m_bxn) * size_t(m_sbStride); }
	double *xpLast() { return m_sb.data() + size_t(m_bxp) * size_t(m_sbStride); }
	const double *x() const { return m_sb.data() + size_t(m_bx) * size_t(m_sbStride); }
	const double *xpLast() const { return m_sb.data() + size_t(m_bxp) * size_t(m_sbStride); }
	void rotate() // the step's next state becomes the state, the state the last one
	{
		const int b = m_bxp;
		m_bxp = m_bx;
		m_bx = m_bxn;
		m_bxn = b;
	}
	bool m_xValid = false;
	mutable bool m_capsStale = false;
	void syncCaps() const; // m_vc/m_ic from x() and xpLast(), if stale
	void dropX();          // m_vc/m_ic current, x() no longer used
	int m_subLeft = 0;
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
	static double defaultTolerance();
	double m_soften = 1;       // the comparators' transition widths are multiplied by this (DC solve)
	// scratch
	std::vector<double> m_w; // advance's states when worked out from the capacitors' voltages and currents
	std::vector<double> m_vp, m_p, m_r, m_vOld, m_jac, m_vSave, m_vcSave, m_icSave, m_iSave, m_uMid, m_uTo;
	std::vector<double> m_splitTo[kLevels], m_splitFrom[kLevels]; // advanceSplit's inputs, per level
	mutable std::vector<double> m_ra;
	std::vector<int> m_sigPorts;
	Stats m_stats;

	struct Linear {
		bool on = false;
		int wait = 0, backoff = 8; // samples until the next try, and the wait after a short run
		long since = 0;            // samples in linear mode since it was entered
		std::vector<double> Kx, Ku, kv, Phi, Gam, kap, Yx, Yu, ky; // ports, states, outputs from (x, u)
		// by columns over [x | u]: the next states' and outputs' rows [Phi Gam; Yx Yu], then from row
		// m_vOff the ports' [Kx Ku]; kvm their constants [kap; ky; kv]: a step's [next states; outputs;
		// v] in one pass down the columns, into the next state buffer (linearStep)
		ColMat KM;
		std::vector<double> kvm;
		std::vector<double> lo, hi, v0, i0, G;                     // the ports' windows; the linearisation point
		std::vector<double> v;                                     // the ports at the linearisation (then the steps' own)
		std::vector<double> M, c, col, Ix, Iu, ki;                 // linearize's scratch
		std::vector<int> piv;
	} m_lin;
};

} // namespace tr808::circuit
