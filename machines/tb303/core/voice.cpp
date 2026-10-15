// The TB-303 voice board, modelled from the service-notes schematic (docs/circuit.md).
//
// Signal path at the internal rate (4x the host at 44.1/48 kHz): saw core and the Q8 square
// shaper (at twice that again), the input network, the four-capacitor transistor ladder with
// its Q12 input pair and Q22 top pair, the Q21 output amplifier and the resonance feedback
// through the dual pot, then the two-branch VCA input and the BA662 VCA. The control
// circuits (gate conditioning, MEG, VEG, accent switch and sweep, expo converter) run once
// per host sample; the ladder current and VCA bias current are interpolated between.
#include "voice.h"

#include <algorithm>
#include <cmath>

namespace tb303 {

namespace {
constexpr double kVt = 0.025693; // thermal voltage at 25 C
constexpr double kV12 = 11.85;   // "+12 V" rail: 5.333 V x (1 + 2.2k/1.8k)
constexpr double kVref = 5.333;  // reference rail
constexpr double kPi = 3.14159265358979323846;
// Digital full scale at the OUTPUT jack: 2.72 V peak puts the unit at the same loudness as
// Roland's own TB-303 software for the same knob settings (mean level over the comparison suite).
constexpr double kFullScaleVolts = 2.72;

// Pot laws. Japanese A (audio) taper: `mid` of the track at mid rotation (15 % nominal), as an
// exponential curve (shape 0) or as the two straight resistive segments carbon pots are made of,
// meeting at mid rotation (shape 1).
double taperA(double x, double mid = 0.15, double shape = 0)
{
	const double s = 1 / mid - 1; // (sqrt(b)-1)/(b-1) = 1/(sqrt(b)+1) = mid
	const double b = s * s;
	x = std::clamp(x, 0.0, 1.0);
	const double curve = (std::pow(b, x) - 1) / (b - 1);
	const double segments = x < 0.5 ? 2 * mid * x : mid + (1 - mid) * (2 * x - 1);
	return curve + shape * (segments - curve);
}
double taperB(double x) { return std::clamp(x, 0.0, 1.0); }

// Diode current for voltage v (Shockley, with the exponent limited so Newton stays sane).
struct Diode {
	double is, nvt;
	double current(double v) const
	{
		const double a = v / nvt;
		if (a > 40)
			return is * (std::exp(40.0) * (1 + a - 40) - 1);
		return is * (std::exp(a) - 1);
	}
	double slope(double v) const
	{
		const double a = v / nvt;
		return is / nvt * std::exp(std::min(a, 40.0));
	}
	// Voltage across the diode in series with resistance r, driven by voltage v (v = vd + i*r).
	double solve(double v, double r, double guess) const
	{
		double vd = std::clamp(guess, -1.0, v > 0 ? v : 0.0);
		if (v <= 0)
			return v;
		for (int i = 0; i < 30; ++i) {
			const double f = vd + current(vd) * r - v;
			const double df = 1 + slope(vd) * r;
			const double step = f / df;
			vd -= step;
			if (vd > v)
				vd = v;
			if (std::abs(step) < 1e-9)
				break;
		}
		return vd;
	}
};
constexpr Diode kSignalDiode{4e-9, 1.9 * kVt}; // 1S1588/1N4148 class

// expm1(min(a, 40)): a junction's exp(V/V_T) - 1. Below -40 it is exactly -1 in double, so the
// call is skipped (most junctions here sit far below that while they're off).
double junctionExpm1(double a)
{
	return a < -40 ? -1.0 : std::expm1(std::min(a, 40.0));
}

// (1 + p)^(-1/n) for p >= 0: the binomial series to p^5 while p < 0.02 (relative error under
// 3e-12), otherwise pow.
template <int n>
double rootRecip(double p)
{
	if (p >= 0.02)
		return 1 / std::pow(1 + p, 1.0 / n);
	constexpr double a = -1.0 / n;
	constexpr double c1 = a, c2 = c1 * (a - 1) / 2, c3 = c2 * (a - 2) / 3, c4 = c3 * (a - 3) / 4, c5 = c4 * (a - 4) / 5;
	return 1 + p * (c1 + p * (c2 + p * (c3 + p * (c4 + p * c5))));
}

// A limiter that stays linear almost to +/-lim, with a short knee (transistor stages clip like
// this; tanh compresses too early): v / (1 + (v/lim)^6)^(1/6).
double hardLimit(double v, double lim)
{
	const double r = v / lim, r2 = r * r;
	return v * rootRecip<6>(r2 * r2 * r2);
}

// The same with a sharper knee, for an output that runs into a saturating transistor:
// v / (1 + (v/lim)^16)^(1/16).
double sharpLimit(double v, double lim)
{
	const double r = v / lim, r2 = r * r, r4 = r2 * r2, r8 = r4 * r4;
	return v * rootRecip<16>(r8 * r8);
}

// One-pole high-pass (trapezoidal). y = a*x + b(state) form for implicit loops.
struct HighPass {
	double g = 0, s = 0, ga = 1; // ga = 1/(1+g)
	void set(double fc, double fs)
	{
		g = std::tan(kPi * std::min(fc, 0.45 * fs) / fs);
		ga = 1 / (1 + g);
	}
	double a() const { return ga; }
	double b() const { return -s * ga; }
	double process(double x)
	{
		const double v = (x - s) * g / (1 + g);
		const double lp = v + s;
		s = lp + v;
		return x - lp;
	}
	void commit(double x) { process(x); }
};
} // namespace

struct Voice::Impl {
	// ------------------------------------------------------------------ constants
	double fs = 176400, h = 1 / 176400.0; // internal rate
	double fsv = 352800, hv = 1 / 352800.0; // VCO rate (2x internal)
	double fsc = 44100, hc = 1 / 44100.0;   // control rate (host)
	Calibration cal;

	// ------------------------------------------------------------------ CV, slide (§2.2-2.3)
	double vDac = 2.0;  // ladder output
	double vCv = 2.0;   // TP4 (C35 when sliding)

	// ------------------------------------------------------------------ VCO (§3)
	double phase = 0;   // 0..1 through the falling ramp
	double vcoHz = 65.4;
	// Saw levels at the Q28 source and the reset time, from a SPICE simulation of the saw core
	// (tools/spice/vco.cir, data-sheet device models): the reset can't charge C33 above the rail
	// less Q27's saturation, D25 and Q25's V_BE, so the top is ~10.85 V, not the 12 V the
	// service notes' scope sketch is labelled with. The bottom (5.3-6.1 V across the parts'
	// spread) and the ~1 us reset come from the same simulation.
	static constexpr double kSawTop = 10.85, kSawBottom = 5.6;
	static constexpr double kResetSec = 1e-6; // reset time (adds to the period)

	// Q8 square shaper (§3.5): states V_E (C11), q = C10 voltage; solved node voltages.
	double q8Ve = 8.6, q8Q = 0, q8Vb = 8.0, q8Vc = 5.4;
	double c17 = 0; // VCF input coupling state (C17), relative to the bias node

	// 2x -> 1x half-band for the VCO output
	Decimator vcoDown;
	float vcoBuf[2] = {};

	// ------------------------------------------------------------------ VCF (§4)
	double u[4] = {0, 0, 0, 0}; // ladder cap voltages C18, C19, C24, C26
	double T[5] = {0, 0, 0, 0, 0};
	double xPrev = 0;
	double fPrev[4] = {0, 0, 0, 0};
	double fPrev2[4] = {0, 0, 0, 0}; // the step before's, for the Newton start
	double ifPrev = 10e-6, ifNow = 10e-6; // ladder current, interpolated per sample
	double lnIfPrev = std::log(10e-6), lnIfNow = std::log(10e-6); // and its log (render interpolates that)
	HighPass hpC25;  // ladder top -> Q21 bases (in loop)
	HighPass hpC14;  // Q19 -> VCF OUT
	HighPass hpC15;  // pot wiper -> Q18
	HighPass hpC23;  // Q18 -> R97/R98 (in loop)
	double resWiper = 0; // loaded VR4-A wiper fraction
	double ampGain = 5.94; // Q21 into R110, through Q19 (V per unit tanh)
	double loopFollower = 0.966; // R67 into Q18's input, and Q18's gain (feedback only)
	double rLoad = 38e3;   // differential input resistance of Q21: 2 x (100k || r_pi)
	double vcfOut = 0, vcfWiper = 0;

	// ------------------------------------------------------------------ control circuits
	// GATE_INT (Q36/C72, §2.4): v = C72 voltage (V_EB of Q36)
	double c72 = 0, gateInt = 0; // C72 voltage = Q36's V_EB
	// Q36 (2SA733): I_S and beta from the data sheet, saturation with beta_R 5, base resistance
	// as in the SPICE models (tools/spice/models-datasheet.lib); q36Veb is the intrinsic V_EB.
	double q36Is = 3.3e-14, q36Beta = 300, q36Sat = 1 / 6.0, q36Rb = 50, q36Veb = 0;
	// C54 (the Q38 one-shot's coupling cap, from Q37's collector): its voltage. After the gate,
	// Q37's collector rises and C54 charges through R143 from Q36's base, which keeps Q36 on
	// for a little longer.
	double c54 = 0.03 - 11.85;
	bool gateIn = false, gateWas = false;
	double pulse = 0; // Q38 one-shot, seconds remaining
	double pulseSec = 1.3e-3;
	double vb32 = 0, iB32 = 0; // Q32's base (C41) and base current
	bool oneShot = false;       // the Q38 pulse is on this step
	// MEG (§5.1-5.2)
	double c62 = 0, meg = 0;
	// accent latch and the accent paths (§5.3, §5.5, §6.4)
	bool accent = false;
	double vAcc = 0, c36 = 0, iAcc = 0, c13 = 3.19, iD24 = 0, vdD24 = 0, vdD27 = 0.5;
	double sweepWiper = 3.27;
	// VEG (§6.2)
	double c42 = 11.5, iVeg = 0;
	double iE31 = 0, iB31 = 0, q31Ve = 0.6, vdD35 = 0.5; // Q31: emitter and base currents, V_E
	double iAbcPrev = 0, iAbcNow = 0;
	// cutoff / expo (§4.5)
	// Node E: Q9's emitter (2SA733 follower, base at 2.667 V from R64/R65). It sinks the converter's
	// currents, so it moves with them: ~3.22 V at rest, ~40 mV higher at the top of an env-mod
	// sweep, while C13 (accent sweep) holds its voltage (SPICE: tools/spice/env.cir).
	double nodeE = 3.225;
	static constexpr double kQ9Is = 3.3e-14, kQ9Beta = 300;
	static double nodeEFor(double iE9)
	{
		iE9 = std::max(iE9, 1e-9);
		return kVref / 2 - iE9 / (kQ9Beta + 1) * 5e3 + kVt * std::log(iE9 / kQ9Is + 1);
	}
	double tm3 = 106e3; // calibrated in calibrate()
	// A pot's taper, kept for its last knob position (controlStep needs three, several times).
	struct Taper {
		double x = -1, mid = 0, shape = 0, value = 0;
		double operator()(double knob, double m, double s)
		{
			if (knob != x || m != mid || s != shape) {
				x = knob;
				mid = m;
				shape = s;
				value = taperA(knob, m, s);
			}
			return value;
		}
	};
	Taper cutoffTaper, envModTaper, decayTaper;
	double vSum = 3.27, vR71 = 0, iE10 = 0;
	double ladderK = 1; // I_S11/I_S10 (absorbed into TM3)

	// ------------------------------------------------------------------ VCA / output (§6-7)
	double c21 = 0, c22 = 0; // VCA input coupling caps (relative)
	HighPass hpOut;          // C38/C59/C56 DC blocking
	double vcaOut = 0;

	// ------------------------------------------------------------------ setup
	void setRates(double internal, double host)
	{
		fs = internal;
		h = 1 / fs;
		fsv = 2 * fs;
		hv = 1 / fsv;
		fsc = host;
		hc = 1 / fsc;
		vcoDown.setStages(1);
		hpC14.set(1 / (2 * kPi * 1e-6 * 50e3), fs);           // 3.2 Hz
		hpC15.set(1 / (2 * kPi * 1e-6 * 98e3), fs);           // 1.6 Hz
		hpC23.set(1 / (2 * kPi * 1e-6 * 12.2e3), fs);         // 13 Hz
		hpOut.set(3.2, fs);
		applyCalibration();
	}

	void applyCalibration()
	{
		// C25/C27 0.1 uF into R113/R114 100k in parallel with Q21's r_pi = beta/g_m.
		const double iSide = cal.q21TailmA * 1e-3 / 2;
		const double rpi = cal.q21Beta * kVt / iSide;
		const double req = 1 / (1 / 100e3 + 1 / rpi);
		hpC25.set(1 / (2 * kPi * 0.1e-6 * req), fs);
		rLoad = 2 * req;
		// The real stages lose a little of the ideal gain (SPICE of the schematic, tools/spice/
		// vcf.cir): Q21's output resistance (Early voltage ~100 V) and Q19's input in parallel with
		// R110, then the Q19 and Q18 emitter followers (0.994 and 0.989 at their ~0.5 mA), and R67
		// 2.2k into Q18's ~94k input (0.977). Together ~10 %: without it the resonance loop runs
		// hot and self-oscillates where the circuit doesn't. SPICE then gives the Q21 stage another
		// 2.5 % less gain than this, flat across cutoff: the passband level (0.2 dB) and the ring's
		// decay at full resonance (every cutoff) both match with it; its source isn't pinned down.
		const double rCollector = 1 / (1 / 22e3 + iSide / 100.0 + 1 / 2.4e6);
		ampGain = rCollector * iSide * 0.994 * 0.975;
		loopFollower = 0.989 * 0.977;
		calibrateTm3();
	}

	// The ring at full CUT OFF at rest (env mod, MEG and accent at 0, C13 at the summing node:
	// the state calibrateTm3 sets TM3 in), with node E solved for the currents it sinks there.
	double fullCutOffRingHz()
	{
		double i = 0;
		for (int it = 0; it < 30; ++it) {
			double vth, rth;
			envModThevenin(0, 0, vth, rth);
			const double g5 = 1 / (rth + 220e3), g71 = 1 / 2.2e3;
			const double vs = (vth * g5 + nodeE * g71) / (g5 + g71);
			const double v71 = vs - nodeE;
			i = ladderK * ladderCurrent(1.0, v71);
			double vth3, rth3, ra3;
			cutoffPot(1.0, vth3, rth3, &ra3);
			const double iE10 = i / std::exp(std::min(v71 / kVt, 30.0));
			const double i63 = (vth - vs) / (rth + 220e3);
			const double raE = 10e3 + 50e3 * envModTaper(0, cal.taperAMid, cal.taperAShape) + 1;
			nodeE = nodeEFor((vth3 - (iE10 + i / 300) * rth3 - nodeE) / ra3 + iE10 + (vth - i63 * rth - nodeE) / raE +
			                 (vs - nodeE) / 2.2e3 + i);
		}
		constexpr double iMax = 0.65e-3;
		return ringHzForCurrent(i / std::sqrt(1 + (i / iMax) * (i / iMax)));
	}

	// ------------------------------------------------------------------ helpers
	// Ladder ring frequency for tail current I (Hz): self-oscillation sits at w_z/sqrt(2) with
	// w_z = I/(4 V_T C) for the 33 nF stages; the 18 nF bottom cap moves it by ~2 %.
	static double ringHzForCurrent(double i) { return 0.692 * i / (2 * kVt * 33e-9) / (2 * kPi); }

	// Ladder current from the expo converter (§4.5). The cutoff pot (Thevenin vth, rth) drives
	// I_E10 through R73 + TM3 into the reference node, one V_BE11 above node E; Q11 multiplies
	// it by exp(V_R71/V_T). V_BE11 follows the ladder current (about 90 mV between 1 and 40 uA),
	// which takes headroom from the pot at high currents, as a SPICE simulation of the converter
	// shows (tools/spice/env.cir). Solved by Newton on the ladder current.
	static constexpr double kQ11Is = 3.3e-14; // 2SC945: V_BE 0.62 V at 1 mA (data sheet)
	void cutoffPot(double knob, double &vth, double &rth, double *raOut = nullptr)
	{
		const double t = cutoffTaper(knob, cal.cutoffTaperMid > 0 ? cal.cutoffTaperMid : cal.taperAMid, cal.taperAShape);
		const double ra = 10e3 + 50e3 * t + 1; // R47 + pin1..wiper
		const double rb = 50e3 * (1 - t) + 1;
		vth = nodeE + (kV12 - nodeE) * ra / (ra + rb);
		rth = ra * rb / (ra + rb);
		if (raOut != nullptr)
			*raOut = ra;
	}
	double ladderCurrent(double knob, double vR71)
	{
		double vth, rth;
		cutoffPot(knob, vth, rth);
		const double r = rth + 100e3 + tm3;
		const double a = std::exp(std::min(vR71 / kVt, 30.0));
		double x = std::max(1e-12, a * (vth - nodeE - 0.52) / r);
		// The pot's current feeds Q10's emitter and Q11's base (I/beta): x = a (I_pot - x/beta).
		constexpr double beta11 = 300;
		for (int it = 0; it < 40; ++it) {
			const double g = x - a * ((vth - nodeE - kVt * std::log(x / kQ11Is)) / r - x / beta11);
			const double dg = 1 + a * (kVt / (x * r) + 1 / beta11);
			const double nx = std::max(1e-12, x - g / dg);
			const bool done = std::abs(nx - x) < 1e-6 * x;
			x = nx;
			if (done)
				break;
		}
		return x;
	}

	// Env-mod pot wiper Thevenin (§4.5): pin 3 = MEG, pin 1 via R61 10k to node E.
	void envModThevenin(double knob, double vMeg, double &vth, double &rth)
	{
		const double t = envModTaper(knob, cal.taperAMid, cal.taperAShape);
		const double ra = 10e3 + 50e3 * t + 1;
		const double rb = 50e3 * (1 - t) + 1;
		vth = nodeE + (vMeg - nodeE) * ra / (ra + rb);
		rth = ra * rb / (ra + rb);
	}

	void calibrateTm3()
	{
		// TM3 is set so that the ring at cutoff centre, resonance max, env mod/decay/accent
		// minimum, sits at cal.vcfRingHz. At rest MEG = 0 and C13 sits at the summing node.
		// Node E depends on the currents TM3 sets: iterate to the rest state.
		const double target = cal.vcfRingHz * 2 * kPi * 2 * kVt * 33e-9 / 0.692;
		for (int it = 0; it < 20; ++it) {
			double vth, rth;
			envModThevenin(0, 0, vth, rth);
			const double vs = (vth / (rth + 220e3) + nodeE / 2.2e3) / (1 / (rth + 220e3) + 1 / 2.2e3);
			const double factor = std::exp((vs - nodeE) / kVt);
			// Solve for tm3: target = factor * (vth3 - E - V_BE11(target))/(rth3 + 100k + tm3)
			double vth3, rth3, ra3;
			cutoffPot(0.5, vth3, rth3, &ra3);
			const double vbe11 = kVt * std::log(target / kQ11Is);
			tm3 = std::max(0.0, factor * (vth3 - nodeE - vbe11) / target - rth3 - 100e3);
			const double iE10 = target / factor;
			const double i63 = (vth - vs) / (rth + 220e3);
			const double iE9 = (vth3 - iE10 * rth3 - nodeE) / ra3 + iE10 + (vth - i63 * rth - nodeE) / (10e3 + 1) +
			                   (vs - nodeE) / 2.2e3 + target;
			nodeE = nodeEFor(iE9);
		}
	}

	// ------------------------------------------------------------------ control step
	// Runs once per host sample: gate conditioning, MEG, accent, VEG, expo converter.
	void controlStep(const Controls &k, const CpuLines &lines, bool retrigger)
	{
		const double dt = hc;
		// --- CV and slide: C35 tracks the ladder through 100k while SLIDE is high.
		vDac = (lines.note - 12) / 12.0;
		// (the slide filter itself runs at the internal rate, see vcoSample)

		// --- Gate input (Q37) and the Q38 one-shot on each rising edge.
		const bool g = lines.gate && !retrigger;
		if (g && !gateWas) {
			pulse = pulseSec;
		}
		gateWas = g;
		gateIn = g;
		accent = lines.accent;

		// --- GATE_INT (§2.4): Q37 pulls Q36's base down through R143 10k against R142 10k,
		// with C72 10 uF from base to the rail. C72's voltage is Q36's intrinsic V_EB plus the
		// drops across R147 22 R (emitter) and the base resistance: it takes ~5 ms to reach
		// turn-on (the note-start delay). After the gate Q36 stays saturated for a few ms while
		// C72 bleeds through R142, then the collector (R145 10k) falls away, slowed by R147's
		// degeneration: ~15 ms to 10 %, as a SPICE simulation of the circuit gives
		// (tools/spice/env.cir) and Whittle measured.
		{
			const int sub = 4;
			const double h = dt / sub;
			const double c54Gate = 1 - std::exp(-h / (20e3 * 0.047e-6));
			// Saturated once its collector reaches the rail: R145's current plus what the VEG
			// supply draws through D35.
			const double iSat = (kV12 - 0.1) / 10e3 + iE31;
			for (int k = 0; k < sub; ++k) {
				// Intrinsic V_EB v from c72 = v + R147 i_E + r_b i_B (Newton).
				double v = q36Veb, iC = 0, iB = 0, diB = 0, dr = 1;
				for (int it = 0; it < 30; ++it) {
					const double iF = q36Is * std::exp(std::min(v, 0.95) / kVt);
					const double dF = iF / kVt;
					const bool sat = iF > iSat;
					iC = sat ? iSat : iF;
					iB = iF / q36Beta + (sat ? (iF - iSat) * q36Sat : 0.0);
					diB = dF * (1 / q36Beta + (sat ? q36Sat : 0.0));
					const double diC = sat ? 0.0 : dF;
					const double r = v + 22.0 * (iC + iB) + q36Rb * iB - c72;
					dr = 1 + 22.0 * (diC + diB) + q36Rb * diB;
					const double step = std::clamp(r / dr, -0.05, 0.05);
					v -= step;
					if (std::abs(step) < 1e-10)
						break;
				}
				q36Veb = v;
				// C72: R143 charges it while the gate is on; R142 and the base current discharge
				// it (backward Euler, linearised through the junction). Q37 saturates at ~30 mV.
				// With the gate off, R143 carries C54's charging current instead: from Q36's base
				// through R143, C54 and R148 + R149 (20k) to the rail, until C54 has charged.
				double i143;
				if (gateIn) {
					i143 = (kV12 - c72 - 0.03) / 10e3;
					c54 += ((0.03 - kV12) - c54) * c54Gate;
				} else {
					i143 = std::max(0.0, (kV12 - c72 - kV12 - c54) / 30e3);
					c54 += h * i143 / 0.047e-6;
				}
				const double g = diB / dr + 1 / 10e3 + (gateIn ? 1 / 10e3 : 0.0);
				const double f = i143 - c72 / 10e3 - iB;
				c72 += h * f / (10e-6 + h * g);
				c72 = std::max(0.0, c72);
				// R145 10k carries the collector current less what the VEG supply draws (D35).
				gateInt = std::clamp((iC - iE31) * 10e3, 0.0, kV12 - 0.1);
			}
		}

		// --- MEG: C62 charged through D37 + R152 100R during the pulse, discharged through
		// R138 68k + DECAY (1M A, shorted on accent) into the D28/D27/D26 string.
		const double rDecay = 68e3 + (accent ? 0.0 : cal.decayPotOhms * decayTaper(k.decay, cal.decayTaperMid, cal.decayTaperShape));
		const double c62F = cal.megCapUF * 1e-6;
		oneShot = pulse > 0;
		if (pulse > 0) {
			const double vTop = kV12 - 0.05 - 0.6; // Q38 saturated, D37
			const double tau = 100 * c62F;
			c62 = vTop - (vTop - c62) * std::exp(-dt / tau);
			pulse -= dt;
		}
		{
			// Implicit step of C62 discharging into R and the diode string. With i = 0 the string
			// stands at vStr0; while C62 is below that, every iteration gives i = 0 again.
			static const double vStr0 = kSignalDiode.nvt * std::log1p(0.19e-3 / kSignalDiode.is);
			double i = 0;
			for (int it = 0; it < 4 && c62 > vStr0; ++it) {
				const double vStr = it == 0 ? vStr0
				                            : 2 * kSignalDiode.nvt * std::log1p(i / kSignalDiode.is) +
				                                  kSignalDiode.nvt * std::log1p((i + 0.19e-3) / kSignalDiode.is);
				const double inew = std::max(0.0, (c62 - vStr) / rDecay);
				i = 0.5 * (i + inew);
			}
			c62 -= i * dt / c62F;
			c62 = std::max(c62, 0.0);
		}
		// Darlington Q41/Q40 into the Q39 current sink: two V_BE below, never below 0 V.
		meg = std::max(0.0, c62 - 1.25) * (cal.megPeakV / (kV12 - 0.05 - 0.6 - 1.25));

		// --- Accent switch IC12-A into VR7 (50k B, pin 1 to ground). With the switch open the
		// pot's top floats and the wiper sits on up to 50k to ground.
		const double x7 = taperB(k.accent);
		const double vth7 = accent ? meg * x7 : 0.0;
		const double rth7 = accent ? 50e3 * x7 * (1 - x7) + 200 : 50e3 * x7 + 1;

		// --- Accent sweep network as seen through D24: R46 47k -> VR4-B (50k B, same shaft
		// as RESONANCE) -> C13 1 uF; wiper -> R72 100k -> summing node. C13 enters as its
		// backward-Euler companion, so the network is a Thevenin source v0 behind rtot.
		const double w = taperB(k.resonance);
		const double r1w = cal.vr4bOhms * w + 10, rwc = cal.vr4bOhms * (1 - w) + 10;
		const double gwc = 1 / rwc, g72 = 1 / 100e3;
		const double gcap = cal.c13UF * 1e-6 / dt;
		const double gc = gwc * gcap / (gwc + gcap);
		const double v0 = (gc * c13 + g72 * vSum) / (gc + g72);
		const double rtot = 47e3 + r1w + 1 / (gc + g72);

		// --- Solve the VR7 wiper node: source (vth7, rth7) feeding R119 47k to C36 and the
		// D24 path, by Newton on its voltage (an explicit update is unstable at full ACCENT).
		{
			double v = vAcc;
			for (int it = 0; it < 8; ++it) {
				const double drive = v - v0;
				double id = 0, did = 0;
				if (drive > 0) {
					vdD24 = kSignalDiode.solve(drive, rtot, vdD24);
					id = std::max(0.0, (drive - vdD24) / rtot);
					const double rd = kSignalDiode.nvt / (id + kSignalDiode.is);
					did = 1 / (rtot + rd);
				}
				const double f = (vth7 - v) / rth7 - (v - c36) / 47e3 - id;
				const double df = -1 / rth7 - 1 / 47e3 - did;
				const double step = f / df;
				v -= step;
				iD24 = id;
				if (std::abs(step) < 1e-7)
					break;
			}
			vAcc = v;
			if (vAcc - v0 <= 0)
				iD24 = 0;
		}

		// --- C36: R119 47k from the ACCENT wiper; R120 22k and D27 (solved with pin 1, below)
		// into the VCA's I_ABC input.
		c36 += dt / 33e-9 * ((vAcc - c36) / 47e3 - iAcc);

		// --- Accent sweep wiper and C13.
		sweepWiper = (iD24 + gc * c13 + g72 * vSum) / (gc + g72);
		c13 += (sweepWiper - c13) * gc * dt / (cal.c13UF * 1e-6);

		// --- VEG: Q32 discharges C42, R123 1.5M recharges it, then Q31 and pin 1.
		stepVeg(dt);
		solvePin1();
		iAbcPrev = iAbcNow;
		iAbcNow = iVeg + iAcc;

		// --- Summing node (Q10 base): env mod via R63 220k, accent sweep via R72 100k,
		// R71 2.2k to node E.
		double vth5, rth5;
		envModThevenin(k.envMod, meg, vth5, rth5);
		const double g5 = 1 / (rth5 + 220e3), g71 = 1 / 2.2e3;
		vSum = (vth5 * g5 + sweepWiper * g72 + nodeE * g71) / (g5 + g72 + g71);
		vR71 = vSum - nodeE;
		ifPrev = ifNow;
		// Q11 saturates once its collector (R69 2.2k below Q12's emitters, ~4.7 V) nears node
		// E: about 0.65 mA, ~40 kHz of cutoff. Soft-limit there.
		const double iRaw = ladderK * ladderCurrent(k.cutoff, vR71);
		// Node E for the next step, from the currents Q9 sinks: the cutoff pot (R47) and I_E10,
		// the env-mod pot (R61), R71 and the ladder current.
		{
			double vth3, rth3, ra3;
			cutoffPot(k.cutoff, vth3, rth3, &ra3);
			const double iE10 = iRaw / std::exp(std::min(vR71 / kVt, 30.0));
			const double iPot = iE10 + iRaw / 300;
			const double i63 = (vth5 - vSum) / (rth5 + 220e3);
			const double raE = 10e3 + 50e3 * envModTaper(k.envMod, cal.taperAMid, cal.taperAShape) + 1;
			const double iE9 = (vth3 - iPot * rth3 - nodeE) / ra3 + iE10 + (vth5 - i63 * rth5 - nodeE) / raE +
			                   (vSum - nodeE) / 2.2e3 + iRaw;
			nodeE = nodeEFor(iE9);
		}
		constexpr double iMax = 0.65e-3;
		ifNow = std::max(1e-9, iRaw / std::sqrt(1 + (iRaw / iMax) * (iRaw / iMax)));
		lnIfPrev = lnIfNow;
		lnIfNow = std::log(ifNow);
	}

	// ------------------------------------------------------------------ VCO (runs at fsv)
	double tuneOct = 0;
	double sawHeld = 12.0; // one-sample hold so the polyBLEP can correct the sample before a reset
	// The ramp for the last CV (and tuning): the pow is only redone when they change.
	double rampCv = -1e9, rampTune = 0, rampScale = 0, rampCents = 0, ramp = 1, dphi = 0;

	// One saw sample at the VCO rate (volts at the Q28 source). The expo current sets the
	// ramp; the reset adds a fixed flyback time to each period. The upward jump is
	// band-limited with a 2-point polyBLEP; output is delayed by one VCO sample.
	double sawStep()
	{
		if (vCv != rampCv || tuneOct != rampTune || cal.vcoScale != rampScale || cal.tuneCents != rampCents) {
			rampCv = vCv;
			rampTune = tuneOct;
			rampScale = cal.vcoScale;
			rampCents = cal.tuneCents;
			const double f = 110.0 * std::pow(2.0, (vCv - 2.75) * cal.vcoScale + tuneOct + cal.tuneCents / 1200);
			ramp = std::max(1 / f - kResetSec, 8 * hv);
			vcoHz = 1 / (ramp + kResetSec);
			dphi = hv / ramp;
		}
		phase += dphi;
		constexpr double jump = kSawTop - kSawBottom;
		double held = sawHeld;
		if (phase >= 1) {
			const double since = (phase - 1) * ramp;        // seconds since the ramp hit bottom
			const double ta = std::min(since / hv, 1.0);    // fraction of a sample since the jump
			phase = std::max(0.0, since - kResetSec) / ramp; // next ramp starts after the flyback
			const double naive = kSawTop - jump * phase;
			held += 0.5 * jump * ta * ta;
			sawHeld = naive - 0.5 * jump * (1 - ta) * (1 - ta);
		} else {
			sawHeld = kSawTop - jump * phase;
		}
		return held;
	}

	// Q8 PNP shaper (§3.5), backward Euler + Newton on V_E, q (C10), V_B, V_C.
	double squareStep(double vSaw, bool loaded)
	{
		constexpr double R45 = 22e3, C11 = 1e-6, R36 = 10e3, R35 = 100e3, R34 = 10e3, C10 = 0.01e-6;
		constexpr double Is = 2e-14, bF = 250, bR = 5;
		constexpr double g45 = 1 / R45, g36 = 1 / R36, g35 = 1 / R35, g34 = 1 / R34, iVt = 1 / kVt;
		constexpr double iaF = (1 + bF) / bF, iaR = (1 + bR) / bR, ibF = 1 / bF, ibR = 1 / bR; // 1/alpha, 1/beta
		const double cC11 = C11 * fsv, cC10 = C10 * fsv; // the capacitors' backward-Euler conductances
		const double gLoad = loaded ? 1 / 222.2e3 : 0.0; // R62 into the VCF input (via C17)
		const double vLoad = kVref + c17;               // C17 holds the average
		double ve = q8Ve, q = q8Q, vb = q8Vb, vc = q8Vc;
		const double ve0 = q8Ve, q0 = q8Q;
		// The C10 row couples only q and V_B: it is eliminated first (k), leaving 3 x 3.
		const double j11 = cC10 + g34, k = -g34 / j11, i11 = 1 / j11;
		for (int it = 0; it < 8; ++it) {
			// Junction exponentials, linear above 40 V_T so Newton stays sane. Below -50 V_T a
			// junction's current is -Is to double precision and its slope negligible: no exp.
			const double aF = (ve - vb) * iVt, aR = (vc - vb) * iVt;
			const double eF = aF < -50 ? 0.0 : std::exp(std::min(aF, 40.0)), eR = aR < -50 ? 0.0 : std::exp(std::min(aR, 40.0));
			const double iF = Is * ((aF > 40 ? eF * (1 + aF - 40) : eF) - 1), iR = Is * ((aR > 40 ? eR * (1 + aR - 40) : eR) - 1);
			const double dF = Is * iVt * eF, dR = Is * iVt * eR;
			const double iE = iF * iaF - iR, iC = iF - iR * iaR, iB = iF * ibF + iR * ibR;
			const double vx = vSaw - q;
			// Residuals: C11, C10, base KCL, collector KCL.
			const double f0 = (ve - ve0) * cC11 - ((kV12 - ve) * g45 - iE);
			const double f1 = (q - q0) * cC10 - (vx - vb) * g34;
			const double f2 = iB + (vSaw - vb) * g35 + (vx - vb) * g34;
			const double f3 = iC - (vc - kVref) * g36 - gLoad * (vc - vLoad);
			// Jacobian wrt (ve, q, vb, vc); row 1 is (0, j11, g34, 0), row 0 has no q.
			const double j00 = cC11 + g45 + dF * iaF, j02 = dR - dF * iaF, j03 = -dR;
			const double j20 = dF * ibF, j22 = -dF * ibF - dR * ibR - g35 - g34 - k * g34, j23 = dR * ibR;
			const double j30 = dF, j32 = dR * iaR - dF, j33 = -dR * iaR - g36 - gLoad;
			// Solve J d = -F. Column 0's pivot is row 0 (j00 > dF (1 + 1/beta) > j20, j30).
			const double i00 = 1 / j00;
			const double m2 = j20 * i00, m3 = j30 * i00;
			double a = j22 - m2 * j02, b = j23 - m2 * j03, r2 = -f2 + k * f1 + m2 * f0;
			double c = j32 - m3 * j02, e = j33 - m3 * j03, r3 = -f3 + m3 * f0;
			if (std::abs(c) > std::abs(a)) {
				std::swap(a, c);
				std::swap(b, e);
				std::swap(r2, r3);
			}
			const double ia = 1 / a, m = c * ia;
			const double d3 = (r3 - m * r2) / (e - m * b);
			const double d2 = (r2 - b * d3) * ia;
			const double d0 = (-f0 - j02 * d2 - j03 * d3) * i00;
			const double d1 = (-f1 - g34 * d2) * i11;
			// Limit junction steps (SPICE-style) to keep the exponentials tame.
			const double lim = 0.1;
			ve += std::clamp(d0, -1.0, 1.0);
			q += std::clamp(d1, -2.0, 2.0);
			vb += std::clamp(d2, -lim * 5, lim * 5);
			vc += std::clamp(d3, -2.0, 2.0);
			if (std::abs(d0) + std::abs(d2) + std::abs(d3) < 1e-7)
				break;
		}
		q8Ve = ve;
		q8Q = q;
		q8Vb = vb;
		q8Vc = vc;
		return vc;
	}

	// ------------------------------------------------------------------ VCF (runs at fs)
	// x_in: Q12 left-base signal (V). Returns VCF OUT (V, AC) and leaves the wiper voltage.
	double vcfStep(double xIn, double iF, double resKnob)
	{
		// Loaded resonance pot: wiper into ~50k (Q18 bias 100k || R122 path 100k).
		const double kr = taperB(resKnob);
		const double rl = 50e3;
		resWiper = kr * rl / (rl + kr * (1 - kr) * 50e3);

		const double C[4] = {0.018e-6, 0.033e-6, 0.033e-6, 0.033e-6};
		constexpr double kInv2Vt = 1 / (2 * kVt);
		const double gTop = 1 / (rLoad * C[3]); // C26's discharge through Q21's input, per volt
		double kc[4];
		for (int i = 0; i < 4; ++i)
			kc[i] = iF * (0.5 / C[i]);

		// Linear parts of the feedback chain at this step:
		// w = aW*u4 + bW (C25/C27), amp = G tanh(w/2Vt), out = a14*amp + b14 (C14),
		// wiper = resWiper*out, q18 = a15*wiper + b15 (C15), fb = 0.18*(a23*q18 + b23) (C23).
		const double aW = hpC25.a(), bW = hpC25.b();
		const double a14 = hpC14.a(), b14 = hpC14.b();
		const double a15 = hpC15.a(), b15 = hpC15.b();
		const double a23 = hpC23.a(), b23 = hpC23.b();
		constexpr double div = 2.2 / 12.2;
		const double A = div * a23 * loopFollower * a15 * resWiper * a14 * ampGain;
		const double B = div * (a23 * loopFollower * (a15 * resWiper * b14 + b15) + b23);
		auto feedback = [&](double u4, double &dfb) {
			const double w = aW * u4 + bW;
			const double t = std::tanh(w * kInv2Vt);
			dfb = A * (1 - t * t) * kInv2Vt * aW;
			return A * t + B;
		};

		// Trapezoidal step, Newton on u1..u4, starting from an Adams-Bashforth step (about one
		// iteration fewer than starting from u; the tolerance is the same).
		double un[4];
		for (int i = 0; i < 4; ++i)
			un[i] = u[i] + h * (1.5 * fPrev[i] - 0.5 * fPrev2[i]);
		const double hh = 0.5 * h;
		double x = 0, tc[5];
		bool converged = false;
		for (int it = 0; it < 6; ++it) {
			double dfb;
			const double fb = feedback(un[3], dfb);
			x = xIn - fb;
			const double t0 = std::tanh(x * kInv2Vt);
			const double t1 = std::tanh((un[0] - un[1]) * kInv2Vt);
			const double t2 = std::tanh((un[1] - un[2]) * kInv2Vt);
			const double t3 = std::tanh((un[2] - un[3]) * kInv2Vt);
			const double t4 = std::tanh(un[3] * kInv2Vt);
			// The top node also feeds Q21 through C25/C27: w/rLoad leaves C26.
			const double iLC = (aW * un[3] + bW) * gTop; // i_L / C26
			const double f[4] = {kc[0] * (t0 - t1), kc[1] * (t1 - t2), kc[2] * (t2 - t3), kc[3] * (t3 - t4) - iLC};
			double F[4];
			for (int i = 0; i < 4; ++i)
				F[i] = un[i] - u[i] - hh * (f[i] + fPrev[i]);
			const double d0 = (1 - t0 * t0) * kInv2Vt, d1 = (1 - t1 * t1) * kInv2Vt, d2 = (1 - t2 * t2) * kInv2Vt,
			             d3 = (1 - t3 * t3) * kInv2Vt, d4 = (1 - t4 * t4) * kInv2Vt;
			// df/du: tridiagonal, plus the feedback in row 0, column 3.
			const double j00 = -kc[0] * d1, j01 = kc[0] * d1, j03 = kc[0] * d0 * (-dfb);
			const double j10 = kc[1] * d1, j11 = -kc[1] * (d1 + d2), j12 = kc[1] * d2;
			const double j21 = kc[2] * d2, j22 = -kc[2] * (d2 + d3), j23 = kc[2] * d3;
			const double j32 = kc[3] * d3, j33 = -kc[3] * (d3 + d4) - aW * gTop;
			// Solve (I - hh J) d = -F. Partial pivoting would never swap here: each diagonal is 1
			// plus the magnitudes the row's sub-diagonal entry is made of, and C18 < C19 = C24 = C26.
			// So this is the same elimination, on the nonzero entries only (row 1 fills in column 3).
			const double m00 = 1.0 - hh * j00, m01 = 0.0 - hh * j01, m03 = 0.0 - hh * j03;
			const double m10 = 0.0 - hh * j10, m12 = 0.0 - hh * j12;
			const double m21 = 0.0 - hh * j21, m23 = 0.0 - hh * j23;
			const double m32 = 0.0 - hh * j32;
			double m11 = 1.0 - hh * j11, m22 = 1.0 - hh * j22, m33 = 1.0 - hh * j33;
			double d[4] = {-F[0], -F[1], -F[2], -F[3]};
			const double e1 = m10 / m00;
			m11 -= e1 * m01;
			const double m13 = 0.0 - e1 * m03;
			d[1] -= e1 * d[0];
			const double e2 = m21 / m11;
			m22 -= e2 * m12;
			const double m23f = m23 - e2 * m13;
			d[2] -= e2 * d[1];
			const double e3 = m32 / m22;
			m33 -= e3 * m23f;
			d[3] -= e3 * d[2];
			d[3] = d[3] / m33;
			d[2] = (d[2] - m23f * d[3]) / m22;
			d[1] = (d[1] - m12 * d[2] - m13 * d[3]) / m11;
			d[0] = (d[0] - m01 * d[1] - m03 * d[3]) / m00;
			double mx = 0;
			for (int i = 0; i < 4; ++i) {
				un[i] += std::clamp(d[i], -0.1, 0.1);
				mx = std::max(mx, std::abs(d[i]));
			}
			// tanh at the updated state: after a converged step (|d| < 1e-10) the first-order
			// correction is exact to well below double precision.
			if (mx < 1e-10) {
				tc[0] = t0;
				tc[1] = t1 + (1 - t1 * t1) * ((d[0] - d[1]) * kInv2Vt);
				tc[2] = t2 + (1 - t2 * t2) * ((d[1] - d[2]) * kInv2Vt);
				tc[3] = t3 + (1 - t3 * t3) * ((d[2] - d[3]) * kInv2Vt);
				tc[4] = t4 + (1 - t4 * t4) * (d[3] * kInv2Vt);
				converged = true;
				break;
			}
		}
		// Commit the step: ladder state, derivatives, and the feedback-chain filters.
		if (!converged) {
			tc[0] = std::tanh(x * kInv2Vt);
			tc[1] = std::tanh((un[0] - un[1]) * kInv2Vt);
			tc[2] = std::tanh((un[1] - un[2]) * kInv2Vt);
			tc[3] = std::tanh((un[2] - un[3]) * kInv2Vt);
			tc[4] = std::tanh(un[3] * kInv2Vt);
		}
		const double t0 = tc[0], t1 = tc[1], t2 = tc[2], t3 = tc[3], t4 = tc[4];
		for (int i = 0; i < 4; ++i)
			fPrev2[i] = fPrev[i];
		fPrev[0] = kc[0] * (t0 - t1);
		fPrev[1] = kc[1] * (t1 - t2);
		fPrev[2] = kc[2] * (t2 - t3);
		fPrev[3] = kc[3] * (t3 - t4) - (hpC25.a() * un[3] + hpC25.b()) * gTop;
		for (int i = 0; i < 4; ++i)
			u[i] = un[i];
		const double w = hpC25.process(u[3]);
		const double amp = ampGain * std::tanh(w * kInv2Vt);
		vcfOut = hpC14.process(amp);
		vcfWiper = resWiper * vcfOut;
		const double q18 = hpC15.process(vcfWiper);
		hpC23.process(loopFollower * q18);
		return vcfOut;
	}

	// The VEG capacitor C42 (node N, Q31's base). The pulse charges C41 through D36 and R134 22k
	// and Q32 empties C42 through R132 100R; after the pulse C41 can only discharge through
	// Q32's own base, so Q32 keeps conducting for some 100 ms, sinking a few uA that hold C42
	// low against R123 1.5M from the rail: the VEG starts to fall about half as fast as R123
	// alone would let it (SPICE: tools/spice/env.cir). Q32 (2SC945, Ebers-Moll, tools/spice/
	// models-datasheet.lib) with R130 and R132 as a 200R limit on its collector current; both
	// capacitors step implicitly (C41 empties in microseconds through a fully on base).
	void stepVeg(double dt)
	{
		constexpr double is = 3.3e-14, bf = 300, br = 5;
		const double vE = 100 * iB32; // R130 carries the base current too
		// C41: f(v) rises with v.
		{
			double vD = 0;
			double v = vb32;
			for (int it = 0; it < 30; ++it) {
				const auto net = [&](double x) {
					double i134 = 0;
					if (oneShot) {
						const double drive = 11.7 - x; // Q38 saturated
						if (drive > 0) {
							vD = kSignalDiode.solve(drive, 22e3, vD);
							i134 = std::max(0.0, (drive - vD) / 22e3);
						}
					}
					const double iB = is / bf * junctionExpm1((x - vE) / kVt) +
					                  is / br * junctionExpm1((x - c42) / kVt);
					return x - vb32 - dt / 0.1e-6 * (i134 - iB);
				};
				const double fv = net(v);
				const double dv = 1e-6;
				const double slope = (net(v + dv) - fv) / dv;
				const double step = std::clamp(fv / slope, -0.05, 0.05);
				v -= step;
				if (std::abs(step) < 1e-9)
					break;
			}
			vb32 = v;
			iB32 = is / bf * junctionExpm1((vb32 - vE) / kVt) +
			       is / br * junctionExpm1((vb32 - c42) / kVt);
		}
		// C42 (Q31's base current flows out into it): g(V) rises with V.
		{
			const auto iC32 = [&](double vN) {
				const double iCC = is * junctionExpm1((vb32 - vE) / kVt);
				const double iEC = is * junctionExpm1((vb32 - vN) / kVt);
				const double i = iCC - iEC * (1 + 1 / br);
				const double iR = std::max(vN - vE, 0.0) / 200 + 1e-12;
				return i > 0 ? i * iR / (i + iR) : i;
			};
			const auto g = [&](double vN) { return vN - c42 - dt / 1e-6 * ((kV12 - vN) / 1.5e6 + iB31 - iC32(vN)); };
			double v = c42;
			for (int it = 0; it < 30; ++it) {
				const double gv = g(v);
				const double dv = 1e-6;
				const double slope = (g(v + dv) - gv) / dv;
				const double step = std::clamp(gv / slope, -0.5, 0.5);
				v -= step;
				if (std::abs(step) < 1e-9)
					break;
			}
			c42 = v;
		}
	}

	// The VCA's I_ABC input (IC15 pin 1) and what feeds it: Q31, the VEG's current source, from
	// GATE_INT through D35, R133 2.2k and R131 220k, and the accent current from C36 through R120
	// 22k and D27. Pin 1 is the BA662's diode-connected input with ~450 ohm in its emitter (~0.7 V,
	// tools/spice/ba662.lib), and Q31's base (C42) starts each note near 0 V, so Q31 starts
	// saturated: its base-collector junction conducts, and part of the accent current goes
	// through it into C42 instead of the VCA until C42 has charged (SPICE: tools/spice/env.cir).
	// Sets iVeg (Q31's collector current), iAcc (D27's), iE31, iB31 and q31Ve.
	void solvePin1()
	{
		// The 222.2k from GATE_INT sets the emitter current; V_E is last step's (a 0.1 V error
		// in it moves the current by 1 %).
		const double head = gateInt - q31Ve;
		double iE = 0;
		if (head > 0) {
			vdD35 = kSignalDiode.solve(head, 222.2e3, vdD35);
			iE = std::max(0.0, (head - vdD35) / 222.2e3);
		}
		// Q31 (2SA733, Ebers-Moll, tools/spice/models-datasheet.lib) with its emitter current
		// given; iEC is the base-collector junction's transport current.
		constexpr double is31 = 3.3e-14, bf31 = 300, br31 = 5;
		double iC = 0, iD = 0, iEC = 0;
		// Pin 1's current i balances Q31's collector and D27: f(i) falls with i.
		const auto f = [&](double i) {
			const double vP = kVt * std::log1p(i / 5e-16) + 450 * i + cal.pin1ShiftV;
			iEC = is31 * junctionExpm1((vP - c42) / kVt);
			iC = (iE + iEC) / (1 + 1 / bf31) - iEC * (1 + 1 / br31);
			const double drive = c36 - vP;
			iD = 0;
			if (drive > 0) {
				vdD27 = kSignalDiode.solve(drive, 22e3, vdD27);
				iD = std::max(0.0, (drive - vdD27) / 22e3);
			}
			return iC + iD - i;
		};
		double lo = 0, hi = iE + iAcc + 1e-6;
		for (int k = 0; k < 60 && f(hi) > 0; ++k)
			hi *= 2;
		double i = std::clamp(iAbcNow, lo, hi);
		for (int it = 0; it < 40; ++it) {
			const double fi = f(i);
			if (fi > 0)
				lo = i;
			else
				hi = i;
			const double di = 1e-3 * i + 1e-10;
			const double slope = (f(i + di) - fi) / di;
			double next = slope < 0 ? i - fi / slope : 0.5 * (lo + hi);
			if (!(next > lo && next < hi))
				next = 0.5 * (lo + hi);
			const bool done = std::abs(next - i) < 1e-11;
			i = next;
			if (done || hi - lo < 1e-11)
				break;
		}
		f(i);
		iVeg = iC;
		iAcc = iD;
		iE31 = iE;
		iB31 = iE - iC;
		q31Ve = c42 + kVt * std::log1p(std::max(0.0, (iE + iEC) / (1 + 1 / bf31)) / is31);
	}

	// ------------------------------------------------------------------ VCA (runs at fs)
	double vcaStep(double iAbc)
	{
		// Two series R-C branches into R124 2.2k: VCF OUT via R121 220k + C21 10 nF, and the
		// resonance wiper via R122 100k + C22 10 nF.
		const double r1 = 220e3 + 2.2e3, r2 = 100e3 + 2.2e3, c = 10e-9;
		// Trapezoidal series RC: i = (v - vc)/r, vc' = i/c
		const double g1 = h / (2 * r1 * c), g2 = h / (2 * r2 * c);
		const double i1 = (vcfOut - c21) / (r1 * (1 + g1));
		const double i2 = (vcfWiper - c22) / (r2 * (1 + g2));
		c21 += h / c * i1;
		c22 += h / c * i2;
		const double vin = 2.2e3 * (i1 + i2);
		// BA662 (tools/spice/ba662.lib: the chip as reverse engineered from an original, with its
		// clones' data sheets): a bipolar pair into Wilson mirrors, output current
		// I_ABC tanh(vin/2Vt) into ~52.5k, less the pair's losses. Its base currents through
		// R124/R125 and its emitter resistance attenuate the input more as I_ABC rises (-0.15 dB
		// at 48 uA, -0.6 dB at 170 uA), and the output saturates at 98.5 % of I_ABC (SPICE).
		const double vEff = vin / (1 + iAbc / 2.5e-3);
		const double v = 0.985 * iAbc * std::tanh(vEff / (2 * kVt)) * 52.5e3;
		// Swing about the ~5.3 V bias at pin 8: down to ~0.8 V, where the OTA's NPN mirror and
		// the buffer's current sink saturate; up as far as the buffer's 10k pull-up from the rail
		// can lift pin 8 against its load through C38 (VOLUME, R155 and the mixer): 4.7 V at full
		// VOLUME, 5.4 V lower down (setVolume).
		// The units Lemus plays saturate sooner (cal.vcaSwing, docs/model.md).
		return sharpLimit(v, (v >= 0 ? vcaTop : 4.5) * cal.vcaSwing);
	}

	// VOLUME (VR8 50k A, pin 1 to ground) with R155 47k and Q34's emitter (~3.4k) on its wiper:
	// the load on the BA662's output, which sets its top (vcaTop), and the wiper's share.
	static constexpr double kMixerIn = 50.4e3;
	double volShare = 1, vcaTop = 4.7;
	void setVolume(double volumeKnob)
	{
		const double x = taperA(volumeKnob);
		const double ra = 50e3 * x + 1, rb = 50e3 * (1 - x) + 1; // ra: wiper to ground
		const double rw = ra * kMixerIn / (ra + kMixerIn);
		volShare = rw / (rb + rw);
		vcaTop = (kV12 - 5.29) * (rb + rw) / (rb + rw + 10e3);
	}

	double outputStage(double vca)
	{
		// C38 -> VR8 50k A -> R155 47k into Q34's emitter, gain -R153/(R155 + Z_in): Q34's
		// emitter is not quite a virtual ground (~3.4k at its few uA, SPICE: tools/spice/out.cir),
		// so the stage gains 1.98, not 2.13.
		const double vw = vca * volShare;
		double v = -vw * 100e3 / kMixerIn;
		v = hpOut.process(v);
		// Q33/Q34 class-A mixer: the output idles at ~4.1 V on the 11.8 V rail, so it clips
		// asymmetrically: Q33 saturates ~6.9 V above idle, and the output can't fall far below
		// 0 V (Q33 off, R160 to ground), ~4.25 V below (SPICE).
		return hardLimit(v, v >= 0 ? 6.9 : 4.25);
	}
};

// ====================================================================== Voice

double fullCutOffHz(const Calibration &c)
{
	auto v = std::make_unique<Voice::Impl>();
	v->cal = c;
	v->applyCalibration();
	return v->fullCutOffRingHz();
}

Voice::Voice() : m_impl(std::make_unique<Impl>()) {}
Voice::~Voice() = default;

void Voice::setCalibration(const Calibration &c)
{
	m_cal = c;
	m_impl->cal = c;
	m_impl->applyCalibration();
}

void Voice::prepare(double hostRate)
{
	m_hostRate = hostRate;
	int stages = 0;
	while (stages < 3 && hostRate * (1 << stages) < 176000)
		++stages;
	m_factor = 1 << stages;
	m_rate = hostRate * m_factor;
	m_decimator.setStages(stages);
	m_work.assign(size_t(m_factor), 0.0f);
	m_impl->cal = m_cal;
	m_impl->setRates(m_rate, hostRate);
	reset();
}

double Voice::latencySamples() const
{
	return m_decimator.latency();
}

void Voice::reset()
{
	auto &v = *m_impl;
	const Calibration cal = v.cal;
	const double rate = m_rate, host = m_hostRate;
	*m_impl = Impl();
	m_impl->cal = cal;
	m_impl->setRates(rate, host);
	m_decimator.reset();
	m_lines = CpuLines();
	m_retrigger = false;
	// Settle the bias networks (shaper emitter, coupling caps) with the gate off.
	std::vector<float> scratch(size_t(m_hostRate * 0.05) + 1);
	m_inWarmup = true;
	for (int i = 0; i < 12; ++i)
		render(scratch.data(), int(scratch.size()));
	m_inWarmup = false;
	m_needsReset = false;
}

void Voice::setLines(const CpuLines &lines, unsigned triggers)
{
	if (triggers != m_triggers && lines.gate && m_lines.gate && !lines.slide)
		m_retrigger = true;
	m_triggers = triggers;
	m_lines = lines;
}

Voice::Probe Voice::probe() const
{
	const auto &v = *m_impl;
	Probe p;
	p.cvVolts = v.vCv;
	p.vcoHz = v.vcoHz;
	p.vcfHz = Impl::ringHzForCurrent(v.ifNow);
	p.meg = v.meg;
	p.veg = std::max(0.0, v.iVeg);
	p.gate = v.gateInt;
	p.accentSweep = v.sweepWiper;
	return p;
}

void Voice::render(float *out, int n, float *s1, float *tp6)
{
	if (m_needsReset && !m_inWarmup) {
		m_needsReset = false;
		reset();
	}
	auto &v = *m_impl;
	const Controls &k = m_controls;
	// TUNING: 50k B pot; the expo sees a fraction of its wiper (§3.2).
	v.tuneOct = (taperB(k.tuning) - 0.5) * 2 * v.cal.tuningRangeCents / 1200;
	v.setVolume(k.volume);
	const bool square = k.square;
	const double slideStep = 1 - std::exp(-v.hv / 22e-3);
	for (int s = 0; s < n; ++s) {
		v.controlStep(k, m_lines, m_retrigger);
		m_retrigger = false;
		const double lnPrev = v.lnIfPrev, lnNow = v.lnIfNow;
		for (int j = 0; j < m_factor; ++j) {
			const double frac = double(j + 1) / m_factor;
			const double iF = std::exp(lnPrev + (lnNow - lnPrev) * frac);
			const double iAbc = v.iAbcPrev + (v.iAbcNow - v.iAbcPrev) * frac;
			// VCO at twice the internal rate, halved by a half-band filter.
			for (int o = 0; o < 2; ++o) {
				// Slide RC on the CV, then the saw and (always running) the square shaper.
				if (m_lines.slide)
					v.vCv += (v.vDac - v.vCv) * slideStep;
				else
					v.vCv = v.vDac;
				const double saw = v.sawStep();
				const double sq = v.squareStep(saw, square);
				v.vcoBuf[o] = float(square ? sq : saw);
			}
			const double osc = v.vcoDown.process(v.vcoBuf);
			// Input network: R62 220k, C17 1 uF, R70 2.2k to the bias node.
			const double i17 = (osc - kVref - v.c17) / 222.2e3;
			v.c17 += v.h / 1e-6 * i17;
			const double xIn = 2.2e3 * i17;
			const double vcf = v.vcfStep(xIn, iF, k.resonance);
			const double vca = v.vcaStep(iAbc);
			const double o = v.outputStage(vca);
			m_work[size_t(j)] = float(o / kFullScaleVolts);
			if (j == m_factor - 1 && s1 != nullptr) {
				s1[s] = float(osc);
				tp6[s] = float(vcf);
			}
		}
		out[s] = m_decimator.process(m_work.data());
		if (!std::isfinite(out[s])) {
			// Never pass a numerical failure to the host: silence, and restart the circuit
			// on the next call.
			for (int r = s; r < n; ++r)
				out[r] = 0;
			m_needsReset = true;
			return;
		}
	}
}

} // namespace tb303
