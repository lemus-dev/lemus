// ProCo RAT (1984 "White Face" small box, LM308), modelled part by part from the schematic: see
// docs/circuit.md for the transcription and docs/model.md for the model and its validation.
//
// The whole circuit (input network, LM308 gain stage with its 30 pF compensation, 1N914
// clippers, FILTER, 2N5458 buffer, VOLUME) is one nodal DK system solved with Newton at the
// oversampled rate. Pot moves recompile the system at a control rate.
#pragma once

#include "devices.h"
#include "dk.h"

#include <string>

namespace rat {

// Component values: the original RAT's (1981 big box to the 1984-88 small boxes; docs/circuit.md
// section 6): the RAT2 column of docs/circuit.md, which follows G. Tranter's Multi-RAT Rev G traces
// of a 1992 RAT2 and the 1986 factory drawing, with the Multi-RAT's RAT column where they differ:
// no R12 input pull-down, R4 1M, C13 1 uF and no R15 at the output. Designators are the
// Multi-RAT's (= the factory's).
struct Parts {
	double vcc = 9.0;
	// Supply: R1 into C1 (D1, C10 have no audio effect with a stiff battery).
	double rSupply = 47, cSupply = 100e-6;
	// R2 / R3 bias divider, C2.
	double rBias1 = 100e3, rBias2 = 100e3, cBias = 1e-6;
	// Input: R12 pull-down (not fitted: 1 GOhm), C3, R4 bias (to the C3 / R5 junction), R5 series,
	// C4 shunt.
	double rPulldown = 1e9, cIn = 22e-9, rInBias = 1e6, rIn = 1e3, cRf = 1e-9;
	// Gain stage: DISTORTION (100k A rheostat) with C5 100 pF, legs R7/C6 and R6/C7, C8 30 pF.
	double rDist = 100e3, cFb = 100e-12;
	double rLeg1 = 560, cLeg1 = 4.7e-6, rLeg2 = 47, cLeg2 = 2.2e-6;
	double cComp = 30e-12;
	// Clipper: R8, C9, D2/D3 to ground.
	double rClip = 1e3, cClip = 4.7e-6;
	// FILTER: 100k A rheostat, R9, C11.
	double rFilterPot = 100e3, rFilter = 1.6e3, cFilter = 3.3e-9;
	// Buffer: C12, R10 gate to ground, Q1, R11 source resistor.
	double cGate = 22e-9, rGate = 1e6, rSource = 10e3;
	// Output: C13, R15 pull-down (not fitted: 1 GOhm), VOLUME (100k A divider).
	double cOut = 1e-6, rOutPulldown = 1e9, rVolume = 100e3;
};

// For the tools: the US RAT2's values (1988-, with the LED bypass): R12 2M2, R4 2M2, C13 10 uF,
// R15 10k (docs/circuit.md sections 6 and 8), the circuit the reference RAT2s were fitted on.
Parts rat2Parts();
// For the tools: the China-made RAT2's (2008-, OP07): the original RAT's values with the RAT2's R12
// and C11 2.7 nF (kanengomibako's trace of "RAT 3A SEC-5-0 REV B"; freestompboxes t=12468; research.md
// section 11).
Parts chinaParts();

enum class OpAmp { LM308, OP07 };

// Unit trims and device parameters. Two are shipped: Datasheet (typical values from the
// datasheets and the schematic) and Captured (fitted to NAM captures of a real RAT; see
// tools/compare/fit_nam.py).
struct Calibration {
	std::string name = "Datasheet";
	// LM308 with 30 pF: slew rate and gain-bandwidth set the macromodel's tail current and
	// effective Vt. Output swing on a single 9 V supply.
	double slewVPerUs = 0.25; // NSC datasheet typ (LTC macromodel: 0.20)
	double slewAsym = 0;      // rising slew x (1 + a), falling x (1 - a)
	double gbwHz = 0.5e6; // NSC curves with 30 pF (research.md section 2; LTC macromodel: 0.6 MHz)
	double dcGain = 3e5;
	double outLo = 1.0, outHi = 8.0, outKnee = 0.05; // about 1 V from each rail (NSC typ, LTC model)
	double outOhms = 50;  // after the limiter, so it adds IR drop under load (LTC model: 1.0 V from the rails at the pin)
	// OP07 (later RAT2s, for the tools): internally compensated, a narrower swing (about 2.3 V from each rail
	// at +-15 V) and a hard limit (bench measurements at 9 V).
	double op07SlewVPerUs = 0.3, op07GbwHz = 0.6e6, op07SlewAsym = 0;
	double op07OutLo = 2.3, op07OutHi = 6.7, op07OutKnee = 0.02, op07OutOhms = 60;
	// 1N914
	double diodeIs = 2.52e-9, diodeN = 1.752, diodeRs = 0.568;
	// 2N5458: LTspice standard.jft (IDSS 4.06 mA, Vp -2.88 V)
	Jfet jfet{-2.882, 488.9e-6, 3.167e-3};
	// Pot tapers, all three 100k audio, as Alpha's published laws: the resistance fraction at
	// half rotation picks the law (0.15 = "15A"), and the rotation where the track reaches its
	// end stretches it (0.97 as published; measured Alpha pots end nearer 0.94).
	double distMid = 0.15, filterMid = 0.15, volumeMid = 0.15;
	double distEnd = 0.97, filterEnd = 0.97, volumeEnd = 0.97;
	double potMinOhms = 20;  // residual resistance at the end stops
	// What the pedal sees: source impedance and the load on its output.
	double sourceOhms = 1e3, loadOhms = 1e6;
};

Calibration datasheetCalibration();
Calibration capturedCalibration();
// Sets a field by its short name (slew, gbw, outlo, diodeis, distmid, ...), for the fitting
// tools. False for an unknown name.
bool setCalibrationField(Calibration &c, const std::string &name, double value);

// Knob rotation 0..1 to resistance fraction for Alpha's audio law with the given mid value,
// its track ending at rotation `end`.
double taper(double rotation, double mid, double end = 0.97);

struct Controls {
	double distortion = 0.5; // rotation, 0 = fully anticlockwise
	double filter = 0.5;
	double volume = 0.5;
};

class Pedal {
public:
	Pedal();

	// rate: the rate process() runs at (the oversampled rate).
	void prepare(double rate);
	void setCalibration(const Calibration &c);
	void setOpAmp(OpAmp o);
	void setParts(const Parts &p);
	const Parts &parts() const { return m_parts; }
	const Calibration &calibration() const { return m_cal; }
	OpAmp opAmp() const { return m_opamp; }

	// Targets; the pedal moves to them over a few milliseconds (a knob is turned, not set).
	void setControls(const Controls &c, bool immediate = false);
	const Controls &controls() const { return m_now; }

	// Recomputes the DC operating point and starts from it.
	void reset();

	// Takes on another pedal's whole running state (circuit values, matrices, knobs, node
	// voltages and history), without allocating: the two then process identically. Both must have
	// been built with the same parts, calibration and op amp.
	void copyStateFrom(const Pedal &o);

	// One sample: input volts (into the input jack) to output volts (at the output jack).
	double process(double vin);
	// One sample each of two pedals (the two channels), exactly as process() on each, with
	// their solves interleaved so the processor overlaps them.
	static void processPair(Pedal &a, Pedal &b, double va, double vb, double &ya, double &yb);

	// Node voltages from the last sample, for displays and tests.
	struct Probe {
		double opOut = 0, clip = 0, filter = 0, source = 0;
	};
	const Probe &probe() const { return m_probe; }
	// Compute the probe node voltages each sample (off by default: costs time).
	void enableProbes(bool on) { m_probes = on; }

	// Newton solves that hit the iteration limit since reset (should stay 0).
	long failures() const { return m_failures; }
	long iterations() const { return m_iterations; }
	long samples() const { return m_samples; }
	// What the solves did since reset, the operating point's settling included (tools/corners).
	struct SolverStats {
		long solves = 0;     // per-sample Newton solves: two a sample (op amp and diodes; JFET)
		long retried = 0;    // started again from the last sample's solution
		long failures = 0;   // still unconverged: the last iterate was used (should stay 0)
		long dcFailures = 0; // DC operating points that didn't converge (reset, recovery)
	};
	SolverStats solverStats() const { return {2 * m_solved, m_retried, m_unconverged, m_dcFailures}; }

	// Small-signal response from the input jack to the output jack at f Hz, linearised at
	// the DC operating point (diodes off), for the response display. Magnitude in dB.
	double smallSignalDb(double hz);

	// The model as an ngspice netlist (the same nodes and values, the devices as behavioural
	// sources with the same equations), for checking the solver against SPICE. The input
	// source is "Vsrc src 0" with the given SPICE source specification (e.g. a PWL).
	std::string spiceNetlist(const std::string &inputSpec) const;

	struct Devices; // the nonlinear evaluator (defined in pedal.cpp)

private:
	void build();
	void applyControls();
	void compileAll();
	void compileStep();
	bool solveDC();
	void controlStep();
	template <int L>
	static void stepAny(Pedal *const *ped, const double *vin, double *out);
	template <int NZ, int L>
	static void step(Pedal *const *ped, const double *vin, double *out);

	Parts m_parts;
	Calibration m_cal;
	OpAmp m_opamp = OpAmp::LM308;
	double m_rate = 384000;

	Netlist m_net;
	DKCompiler m_compiler;
	DKMatrices m_m, m_dc;
	OpAmpModel m_op;
	double m_diodeIs = 0, m_diodeNVt = 0;
	double m_outOhms = 50; // the fitted op amp's output resistance

	// Resistor indices for the pots and trims.
	int m_rDistA = -1, m_rFilterPot = -1, m_rVolTop = -1, m_rVolBottom = -1, m_rOut = -1;
	int m_rY = -1, m_rSource = -1, m_rLoad = -1, m_rDiode = -1;
	int m_cComp = -1;

	Controls m_target, m_now;
	int m_sinceCompile = 0;
	double m_glide = 0.0003;
	bool m_probes = false;
	bool m_dirty = true;

	// The per-sample matrices again, column-major and padded to whole vectors, so step() forms
	// each product for all rows at once. Every row's sum keeps its order, so the result is the
	// same as row by row. A pair asks for 8-byte alignment only (unaligned loads and stores, as
	// fast as aligned ones on current x64 and ARM): on Windows a Pedal can sit at 8 mod 16, where
	// a 16-byte-aligned store faults (the RAT's first Windows build crashed constructing one).
	typedef double V2 __attribute__((vector_size(16), aligned(8)));
	static constexpr int kStateChunks = kMaxStates / 2, kSenseChunks = (kMaxV + 1) / 2;
	V2 m_colA[kMaxStates][kStateChunks]{}, m_colB[kMaxSources][kStateChunks]{}, m_colC[kMaxI][kStateChunks]{};
	V2 m_colD[kMaxStates][kSenseChunks]{}, m_colE[kMaxSources][kSenseChunks]{};

	double m_z[kMaxStates]{};
	double m_v[kMaxV]{}, m_vPrev[kMaxV]{};
	double m_i[kMaxI]{};
	double m_u[kMaxSources]{};
	Probe m_probe;
	long m_failures = 0, m_iterations = 0, m_samples = 0;
	// solverStats(): counted from before the operating point's settling (m_failures from after it).
	long m_solved = 0, m_retried = 0, m_unconverged = 0, m_dcFailures = 0;
};

} // namespace rat
