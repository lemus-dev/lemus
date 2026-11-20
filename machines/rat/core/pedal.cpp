#include "pedal.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdio>
#include <cstring>
#include <complex>
#include <vector>

namespace rat {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr int kRecompileInterval = 64; // samples between recompiles while a knob moves

// Nonlinear ports, in the order the netlist declares them.
enum Sense { kVd, kVy, kVj, kVgs, kVds, kNumSense };
enum Inject { kIy, kIo, kIdiode, kIds, kNumInject };

} // namespace

namespace {

// Alpha's published audio tapers 05A..30A as linear-cubic piecewise laws (Holmes & van
// Walstijn, DAFx-19, Table 2): linear between knots 0-1, 2-3, 4-5 and 6-7, and a cubic between
// the others that meets both neighbouring lines with their slopes. One set of x knots for all.
constexpr int kKnots = 8;
constexpr double kAlphaX[kKnots] = {0, 0.05, 0.30, 0.51, 0.70, 0.92, 0.97, 1};
constexpr double kAlphaMid[] = {0.05, 0.10, 0.15, 0.20, 0.25, 0.30};
constexpr double kAlphaY[][kKnots] = {
	{0, 0.003, 0.015, 0.057, 0.284, 0.954, 0.999, 1},
	{0, 0.003, 0.028, 0.111, 0.363, 0.959, 0.999, 1},
	{0, 0.003, 0.063, 0.162, 0.410, 0.958, 1.000, 1},
	{0, 0.004, 0.084, 0.210, 0.443, 0.952, 1.000, 1},
	{0, 0.002, 0.123, 0.259, 0.501, 0.954, 0.999, 1},
	{0, 0.004, 0.151, 0.311, 0.542, 0.965, 1.000, 1},
};
constexpr int kLaws = int(std::size(kAlphaMid));

} // namespace

double taper(double rotation, double mid, double end)
{
	// The law's knots for this mid value, interpolated between Alpha's laws (and extrapolated
	// past 05A and 30A, kept increasing).
	const double c = std::clamp((mid - kAlphaMid[0]) / (kAlphaMid[1] - kAlphaMid[0]), -0.8, kLaws - 1.0);
	const int k0 = std::clamp(int(std::floor(c)), 0, kLaws - 2);
	const double w = c - k0;
	double y[kKnots];
	for (int k = 0; k < kKnots; ++k) {
		y[k] = std::clamp((1 - w) * kAlphaY[k0][k] + w * kAlphaY[k0 + 1][k], 0.0, 1.0);
		if (k > 0)
			y[k] = std::max(y[k], y[k - 1] + 1e-4 * (kAlphaX[k] - kAlphaX[k - 1]));
	}
	y[kKnots - 1] = 1;
	// Real pots reach the end of their track earlier than the published curve (0.97): scale the
	// rotation so the track ends at `end`.
	const double x = std::clamp(rotation * kAlphaX[kKnots - 2] / end, 0.0, 1.0);
	auto slope = [&](int k) { return (y[k + 1] - y[k]) / (kAlphaX[k + 1] - kAlphaX[k]); };
	int k = 0;
	while (k < kKnots - 2 && x > kAlphaX[k + 1])
		++k;
	if (k % 2 == 0)
		return y[k] + slope(k) * (x - kAlphaX[k]);
	const double h = kAlphaX[k + 1] - kAlphaX[k], t = (x - kAlphaX[k]) / h;
	const double m0 = slope(k - 1), m1 = slope(k + 1);
	return (2 * t * t * t - 3 * t * t + 1) * y[k] + (t * t * t - 2 * t * t + t) * h * m0 +
		(-2 * t * t * t + 3 * t * t) * y[k + 1] + (t * t * t - t * t) * h * m1;
}

Parts rat2Parts()
{
	Parts p;
	p.rPulldown = 2.2e6;
	p.rInBias = 2.2e6;
	p.cOut = 10e-6;
	p.rOutPulldown = 10e3;
	return p;
}

Parts chinaParts()
{
	Parts p;
	p.rPulldown = 2.2e6; // R12 as on the US RAT2
	p.cFilter = 2.7e-9;
	return p;
}

Calibration datasheetCalibration()
{
	return Calibration{};
}

Calibration capturedCalibration()
{
	// Fitted to real RATs within bounds from bench measurements (docs/model.md, "Calibrations").
	// Everything not set here is the datasheet set.
	Calibration c;
	c.name = "Captured";
	// Shared trims and the OP07: tools/compare/fit_nam.py --opamp op07 on 24 NAM captures of two
	// RAT2s with stated knobs (build/fit/v2/op07.log). The captured pedals clip like OP07 units.
	c.op07SlewVPerUs = 0.247998;
	c.op07SlewAsym = -0.113762;
	c.op07GbwHz = 634156;
	c.op07OutLo = 1.24186;  // a hard limit 3.25 V below the bias (bench: 3.17 V)
	c.op07OutHi = 8.469;    // 3.98 V above it (bench: beyond 3.95 V)
	c.op07OutOhms = 45.2055;
	c.diodeN = 1.56599;
	// The pots' laws: the only measured vintage RAT pot, a 1979 big box's DISTORTION (8.65 k of
	// 85.6 k at mid: 10.1 %; research.md section 11), for DISTORTION and VOLUME. FILTER is fitted to
	// the one capture of a 1984 White Face (6 % at mid): with the RAT2 fit's 14.6 % that pedal was
	// 3-5 dB brighter above 4 kHz, with 10.1 % still 1-2.5 dB (improvements.md). The track ends stay
	// as the RAT2 fit had them.
	c.distMid = 0.101;
	c.distEnd = 0.99;       // at the fit's bound
	c.filterMid = 0.06;
	c.filterEnd = 0.935801;
	c.volumeMid = 0.101;
	c.volumeEnd = 0.933913;
	c.jfet.vto = -3.41871;  // source at 2.68 V (real units: 2.6-2.9 V); IDSS 5.7 mA
	// The LM308: tools/compare/fit_lm308.py on a 1990 RAT2's published scope and FFT shots and
	// the captures of two LM308 RATs, the shared trims held (build/fit/v2/lm308b.log).
	c.slewVPerUs = 0.3;     // at the fit's bound (bench: 0.11-0.24 V/us at 9 V, 0.2-0.28 at 15 V)
	c.slewAsym = -0.128993; // falling edges faster, as on every bench LM308
	c.gbwHz = 366809;
	c.outLo = 0.888253;     // 3.6 V below the bias
	c.outHi = 7.88825;      // 3.4 V above it: the positive side clips first, 0.2 V sooner
	c.outKnee = 0.180115;   // a soft knee (bench)
	c.outOhms = 87.4102;
	return c;
}

struct Pedal::Devices {
	const OpAmpModel &op;
	double outOhms;
	double is, nVt;
	const Jfet &jfet;

	void eval(const double *v, double *i, double (*G)[kMaxV]) const
	{
		for (int r = 0; r < kNumInject; ++r)
			for (int c = 0; c < kNumSense; ++c)
				G[r][c] = 0;
		double dvd, dvy;
		i[kIy] = op.inputCurrent(v[kVd], v[kVy], dvd, dvy);
		G[kIy][kVd] = dvd;
		G[kIy][kVy] = dvy;
		double dout;
		i[kIo] = op.output(v[kVy], dout) / outOhms;
		G[kIo][kVy] = dout / outOhms;
		double dd;
		i[kIdiode] = diodePair(v[kVj], is, nVt, dd);
		G[kIdiode][kVj] = dd;
		double dg, dds;
		i[kIds] = jfet.current(v[kVgs], v[kVds], dg, dds);
		G[kIds][kVgs] = dg;
		G[kIds][kVds] = dds;
	}

	void limit(const double *v, double *dv) const
	{
		// Junction: SPICE-style logarithmic damping of large forward steps.
		const double vlim = 2 * nVt;
		const double vn = v[kVj] + dv[kVj];
		if (std::fabs(dv[kVj]) > vlim && std::fabs(vn) > 0.3) {
			const double a = std::fabs(dv[kVj]);
			dv[kVj] = std::copysign(vlim * (1 + std::log(a / vlim)), dv[kVj]);
		}
		dv[kVd] = std::clamp(dv[kVd], -1.0, 1.0);
		dv[kVy] = std::clamp(dv[kVy], -2.0, 2.0);
		dv[kVgs] = std::clamp(dv[kVgs], -1.0, 1.0);
		dv[kVds] = std::clamp(dv[kVds], -1.0, 1.0);
	}
};

Pedal::Pedal()
{
	m_cal = datasheetCalibration();
	build();
}

void Pedal::setParts(const Parts &p)
{
	m_parts = p;
	build();
	reset();
}

void Pedal::setCalibration(const Calibration &c)
{
	m_cal = c;
	build();
	reset();
}

void Pedal::setOpAmp(OpAmp o)
{
	m_opamp = o;
	build();
	reset();
}

void Pedal::prepare(double rate)
{
	m_rate = rate;
	m_glide = 1.0 - std::exp(-1.0 / (0.008 * m_rate));
	compileAll();
	reset();
}

void Pedal::build()
{
	const Parts &p = m_parts;
	m_net = Netlist();
	Netlist &n = m_net;

	// Sources: u0 the signal at the input jack (through the source impedance), u1 the battery.
	n.source("src", "0");
	n.source("vbat", "0");
	m_rSource = n.resistor("src", "in", m_cal.sourceOhms);

	// Supply filter and the V/2 bias for the op amp.
	n.resistor("vbat", "v9", p.rSupply);
	n.capacitor("v9", "0", p.cSupply);
	n.resistor("v9", "vref", p.rBias1);
	n.resistor("vref", "0", p.rBias2);
	n.capacitor("vref", "0", p.cBias);

	// Input network: R12 pull-down, C3, R4 to Vref at the C3 / R5 junction, R5, C4.
	n.resistor("in", "0", p.rPulldown);
	n.capacitor("in", "a", p.cIn);
	n.resistor("a", "vref", p.rInBias);
	n.resistor("a", "p", p.rIn);
	n.capacitor("p", "0", p.cRf);

	// Op amp: input stage into Y, compensation capacitor and DC-gain resistor at Y, output
	// stage as a Norton source (Go f(Vy) in parallel with Go) at the output pin.
	double cc = p.cComp, slew = m_cal.slewVPerUs * 1e6, gbw = m_cal.gbwHz;
	if (m_opamp == OpAmp::OP07) {
		cc = 30e-12; // internal; only the ratios matter
		slew = m_cal.op07SlewVPerUs * 1e6;
		gbw = m_cal.op07GbwHz;
	}
	const double gm = 2 * kPi * gbw * cc;
	m_op.it = slew * cc;
	m_op.vte = m_op.it / (2 * gm);
	m_op.asym = m_opamp == OpAmp::OP07 ? m_cal.op07SlewAsym : m_cal.slewAsym;
	m_op.outLo = m_opamp == OpAmp::OP07 ? m_cal.op07OutLo : m_cal.outLo;
	m_op.outHi = m_opamp == OpAmp::OP07 ? m_cal.op07OutHi : m_cal.outHi;
	m_op.knee = m_opamp == OpAmp::OP07 ? m_cal.op07OutKnee : m_cal.outKnee;
	m_outOhms = m_opamp == OpAmp::OP07 ? m_cal.op07OutOhms : m_cal.outOhms;
	m_cComp = n.capacitor("y", "0", cc);
	m_rY = n.resistor("y", "0", m_cal.dcGain / gm);
	m_rOut = n.resistor("o", "0", m_outOhms);

	// Feedback: DISTORTION pot (as a rheostat) with 100 pF across it, two RC legs to ground.
	m_rDistA = n.resistor("o", "n", 1e3);
	n.capacitor("o", "n", p.cFb);
	n.resistor("n", "l1", p.rLeg1);
	n.capacitor("l1", "0", p.cLeg1);
	n.resistor("n", "l2", p.rLeg2);
	n.capacitor("l2", "0", p.cLeg2);

	// Clipper.
	n.capacitor("o", "c", p.cClip);
	n.resistor("c", "d", p.rClip);
	m_rDiode = n.resistor("d", "dj", m_cal.diodeRs);

	// FILTER.
	n.resistor("d", "f1", p.rFilter);
	m_rFilterPot = n.resistor("f1", "f", 1e3);
	n.capacitor("f", "0", p.cFilter);

	// JFET buffer: gate returned to ground, drain on the filtered rail.
	n.capacitor("f", "g", p.cGate);
	n.resistor("g", "0", p.rGate);
	n.resistor("s", "0", p.rSource);

	// C13, R15 pull-down, VOLUME into the load.
	n.capacitor("s", "v1", p.cOut);
	n.resistor("v1", "0", p.rOutPulldown);
	m_rVolTop = n.resistor("v1", "w", 1e3);
	m_rVolBottom = n.resistor("w", "0", 1e3);
	m_rLoad = n.resistor("w", "0", m_cal.loadOhms);

	// Nonlinear ports (order: Sense and Inject enums).
	n.sense("p", "n");
	n.sense("y", "0");
	n.sense("dj", "0");
	n.sense("g", "s");
	n.sense("v9", "s");
	n.inject("y", "0");
	n.inject("o", "0");
	n.inject("0", "dj");
	n.inject("s", "v9");

	// Outputs: the jack, then probes.
	n.output("w", "0");
	n.output("o", "0");
	n.output("d", "0");
	n.output("f", "0");
	n.output("s", "0");

	m_diodeIs = m_cal.diodeIs;
	m_diodeNVt = m_cal.diodeN * kVt;
	m_dirty = true;
	applyControls();
	compileAll();
}

void Pedal::applyControls()
{
	const Parts &p = m_parts;
	const double rmin = m_cal.potMinOhms;
	m_net.setResistance(m_rDistA, std::max(rmin, p.rDist * taper(m_now.distortion, m_cal.distMid, m_cal.distEnd)));
	m_net.setResistance(m_rFilterPot,
		std::max(rmin, p.rFilterPot * taper(m_now.filter, m_cal.filterMid, m_cal.filterEnd)));
	const double v = taper(m_now.volume, m_cal.volumeMid, m_cal.volumeEnd);
	m_net.setResistance(m_rVolTop, std::max(rmin, p.rVolume * (1 - v)));
	m_net.setResistance(m_rVolBottom, std::max(rmin, p.rVolume * v));
}

void Pedal::compileStep()
{
	m_compiler.compile(m_net, 1.0 / m_rate, m_m);
	const DKMatrices &m = m_m;
	for (int k = 0; k < kStateChunks; ++k)
		for (int lane = 0; lane < 2; ++lane) {
			const int r = 2 * k + lane;
			const bool in = r < m.nz;
			for (int c = 0; c < kMaxStates; ++c)
				m_colA[c][k][lane] = in ? m.A[r][c] : 0.0;
			for (int c = 0; c < kMaxSources; ++c)
				m_colB[c][k][lane] = in ? m.B[r][c] : 0.0;
			for (int c = 0; c < kMaxI; ++c)
				m_colC[c][k][lane] = in ? m.C[r][c] : 0.0;
		}
	for (int k = 0; k < kSenseChunks; ++k)
		for (int lane = 0; lane < 2; ++lane) {
			const int r = 2 * k + lane;
			const bool in = r < m.nv;
			for (int c = 0; c < kMaxStates; ++c)
				m_colD[c][k][lane] = in ? m.D[r][c] : 0.0;
			for (int c = 0; c < kMaxSources; ++c)
				m_colE[c][k][lane] = in ? m.E[r][c] : 0.0;
		}
}

void Pedal::compileAll()
{
	compileStep();
	m_compiler.compile(m_net, 0.0, m_dc);
	m_dirty = false;
	m_sinceCompile = 0;
}

void Pedal::setControls(const Controls &c, bool immediate)
{
	m_target = c;
	if (immediate) {
		m_now = c;
		applyControls();
		compileAll();
	}
}

bool Pedal::solveDC()
{
	Devices dev{m_op, m_outOhms, m_diodeIs, m_diodeNVt, m_cal.jfet};
	double v[kMaxV]{}, i[kMaxI]{}, p[kMaxV];
	auto solveAt = [&](double supply, int maxIter) {
		m_u[0] = 0;
		m_u[1] = supply;
		for (int r = 0; r < m_dc.nv; ++r) {
			p[r] = 0;
			for (int c = 0; c < m_dc.nu; ++c)
				p[r] += m_dc.E[r][c] * m_u[c];
		}
		return solveNewton(m_dc, p, v, i, dev, maxIter, 1e-11);
	};
	// Start where the circuit sits: the op amp at half supply, the diodes off, the JFET's
	// source a couple of volts up with the gate at 0 V.
	auto guess = [&] {
		v[kVd] = 0;
		v[kVy] = 0.5 * m_parts.vcc;
		v[kVj] = 0;
		v[kVgs] = -2.0;
		v[kVds] = m_parts.vcc - 2.0;
	};
	guess();
	bool ok = solveAt(m_parts.vcc, 300) >= 0;
	if (!ok) {
		// Source stepping from half supply as a fallback.
		guess();
		ok = true;
		for (int s = 15; s <= 30 && ok; ++s)
			ok = solveAt(m_parts.vcc * s / 30, 300) >= 0;
	}
	if (!ok && std::getenv("RAT_DEBUG"))
		std::fprintf(stderr, "rat: DC operating point did not converge\n");
	// Capacitor voltages at the operating point give history currents with no cap current.
	for (int k = 0; k < m_m.nz; ++k) {
		double vc = 0;
		for (int c = 0; c < m_dc.nu; ++c)
			vc += m_dc.Pu[k][c] * m_u[c];
		for (int c = 0; c < m_dc.ni; ++c)
			vc += m_dc.Pi[k][c] * i[c];
		m_z[k] = m_m.Gc[k] * vc;
	}
	for (int k = 0; k < m_m.nv; ++k)
		m_v[k] = m_vPrev[k] = v[k];
	for (int k = 0; k < m_m.ni; ++k)
		m_i[k] = i[k];
	return ok;
}

void Pedal::reset()
{
	m_now = m_target;
	applyControls();
	compileAll();
	m_failures = m_iterations = m_samples = 0;
	m_solved = m_retried = m_unconverged = 0;
	// If the DC solve fails, start from zero and let the bias charge up instead (the slowest
	// time constant, C13 into R15 and VOLUME, is about 0.1 s).
	const bool dcOk = solveDC();
	m_dcFailures = dcOk ? 0 : 1;
	if (!dcOk) {
		for (auto &z : m_z)
			z = 0;
		for (auto &v : m_v)
			v = 0;
	}
	// Let the operating point settle in the discrete-time system.
	for (int k = 0; k < int(m_rate * (dcOk ? 0.02 : 1.5)); ++k)
		process(0.0);
	m_failures = m_iterations = m_samples = 0;
}

void Pedal::controlStep()
{
	// Knob smoothing (about 8 ms) and recompiles while the knobs move.
	if (!m_dirty && m_now.distortion == m_target.distortion && m_now.filter == m_target.filter &&
		m_now.volume == m_target.volume)
		return;
	const double a = m_glide;
	bool moving = false;
	auto glide = [&](double &now, double target) {
		const double d = target - now;
		if (std::fabs(d) > 1e-5) {
			now += a * d;
			moving = true;
		} else if (d != 0) {
			now = target;
			moving = true;
		}
	};
	glide(m_now.distortion, m_target.distortion);
	glide(m_now.filter, m_target.filter);
	glide(m_now.volume, m_target.volume);
	if (moving)
		m_dirty = true;
	if (m_dirty && (++m_sinceCompile >= kRecompileInterval || !moving)) {
		applyControls();
		compileStep();
		m_dirty = false;
		m_sinceCompile = 0;
	}
}

template <int L>
void Pedal::stepAny(Pedal *const *ped, const double *vin, double *out)
{
	switch (ped[0]->m_m.nz) {
	case 9: return step<9, L>(ped, vin, out);
	case 10: return step<10, L>(ped, vin, out);
	case 11: return step<11, L>(ped, vin, out);
	case 12: return step<12, L>(ped, vin, out);
	case 13: return step<13, L>(ped, vin, out);
	case 14: return step<14, L>(ped, vin, out);
	case 15: return step<15, L>(ped, vin, out);
	case 16: return step<16, L>(ped, vin, out);
	default: return step<kMaxStates, L>(ped, vin, out);
	}
}

void Pedal::copyStateFrom(const Pedal &o)
{
	// Everything process() reads or changes. The calibration (it holds a string) and the
	// compiler's scratch matrices are left alone: the first is the same by contract, the second
	// holds nothing between compiles.
	m_parts = o.m_parts;
	m_opamp = o.m_opamp;
	m_rate = o.m_rate;
	for (size_t k = 0; k < o.m_net.resistors().size(); ++k)
		m_net.setResistance(int(k), o.m_net.resistance(int(k)));
	for (size_t k = 0; k < o.m_net.capacitors().size(); ++k)
		m_net.setCapacitance(int(k), o.m_net.capacitance(int(k)));
	m_m = o.m_m;
	m_dc = o.m_dc;
	m_op = o.m_op;
	m_diodeIs = o.m_diodeIs;
	m_diodeNVt = o.m_diodeNVt;
	m_outOhms = o.m_outOhms;
	m_target = o.m_target;
	m_now = o.m_now;
	m_sinceCompile = o.m_sinceCompile;
	m_glide = o.m_glide;
	m_probes = o.m_probes;
	m_dirty = o.m_dirty;
	std::memcpy(m_colA, o.m_colA, sizeof m_colA);
	std::memcpy(m_colB, o.m_colB, sizeof m_colB);
	std::memcpy(m_colC, o.m_colC, sizeof m_colC);
	std::memcpy(m_colD, o.m_colD, sizeof m_colD);
	std::memcpy(m_colE, o.m_colE, sizeof m_colE);
	std::memcpy(m_z, o.m_z, sizeof m_z);
	std::memcpy(m_v, o.m_v, sizeof m_v);
	std::memcpy(m_vPrev, o.m_vPrev, sizeof m_vPrev);
	std::memcpy(m_i, o.m_i, sizeof m_i);
	std::memcpy(m_u, o.m_u, sizeof m_u);
	m_probe = o.m_probe;
	m_failures = o.m_failures;
	m_iterations = o.m_iterations;
	m_samples = o.m_samples;
	m_solved = o.m_solved;
	m_retried = o.m_retried;
	m_unconverged = o.m_unconverged;
	m_dcFailures = o.m_dcFailures;
}

double Pedal::process(double vin)
{
	controlStep();
	Pedal *const ped[1] = {this};
	double y;
	stepAny<1>(ped, &vin, &y);
	return y;
}

void Pedal::processPair(Pedal &a, Pedal &b, double va, double vb, double &ya, double &yb)
{
	a.controlStep();
	b.controlStep();
	if (a.m_m.nz != b.m_m.nz) {
		// Different circuits (never in the plugin): one at a time.
		Pedal *const pa[1] = {&a}, *const pb[1] = {&b};
		stepAny<1>(pa, &va, &ya);
		stepAny<1>(pb, &vb, &yb);
		return;
	}
	Pedal *const ped[2] = {&a, &b};
	const double vin[2] = {va, vb};
	double y[2];
	stepAny<2>(ped, vin, y);
	ya = y[0];
	yb = y[1];
}

namespace {
// Newton stops when a step is under these (V). It converges quadratically, so the step before
// that one has already put the solution within about 1e-9 V of the circuit's (the op amp and
// diodes) and nearer still for the JFET: the output is within -129 dB of solving to 1e-7 V
// (ratgolden --check, 2026-11-19), and the samples take 1.9 iterations instead of 2.4.
constexpr double kTolA = 3e-5, kTolJ = 1e-4;

// The op amp and the diodes (sensed vd, vy, vj; currents into Y, out of the output stage,
// through the diodes).
struct EvalA {
	const OpAmpModel *op;
	double go, is, nVt;
	void operator()(const double *v, double *i, double (*G)[3]) const
	{
		double dvd, dvy, dout, dd;
		i[0] = op->inputCurrent(v[0], v[1], dvd, dvy);
		i[1] = op->output(v[1], dout) * go;
		i[2] = diodePair(v[2], is, nVt, dd);
		G[0][0] = dvd;
		G[0][1] = dvy;
		G[0][2] = 0;
		G[1][0] = 0;
		G[1][1] = dout * go;
		G[1][2] = 0;
		G[2][0] = G[2][1] = 0;
		G[2][2] = dd;
	}
};

struct LimitA {
	double nVt;
	void operator()(const double *v, double *dv) const
	{
		const double vlim = 2 * nVt;
		if (std::fabs(dv[2]) > vlim && std::fabs(v[2] + dv[2]) > 0.3)
			dv[2] = std::copysign(vlim * (1 + std::log(std::fabs(dv[2]) / vlim)), dv[2]);
		dv[0] = std::clamp(dv[0], -1.0, 1.0);
		dv[1] = std::clamp(dv[1], -2.0, 2.0);
	}
};

// The JFET (sensed vgs, vds; its drain current).
struct EvalJ {
	const Jfet *jf;
	void operator()(const double *v, double *i, double (*G)[2]) const
	{
		double dg, dd;
		i[0] = jf->current(v[0], v[1], dg, dd);
		G[0][0] = dg;
		G[0][1] = dd;
	}
};

struct LimitJ {
	void operator()(const double *, double *dv) const
	{
		dv[0] = std::clamp(dv[0], -1.0, 1.0);
		dv[1] = std::clamp(dv[1], -1.0, 1.0);
	}
};

} // namespace

// One sample for L pedals (L = 1, or 2 for the two channels): each phase runs for every lane
// before the next, and the Newton solves alternate iteration by iteration, so the processor
// can overlap the lanes' dependency chains. Each lane's arithmetic is exactly that of L = 1.
template <int NZ, int L>
void Pedal::step(Pedal *const *ped, const double *vin, double *out)
{
	constexpr int kChunksV = (kNumSense + 1) / 2, kChunksZ = (NZ + 1) / 2;
	double p[L][2 * kChunksV];
	for (int l = 0; l < L; ++l) {
		Pedal &P = *ped[l];
		P.m_u[0] = vin[l];
		P.m_u[1] = P.m_parts.vcc;
		V2 pc[kChunksV];
		for (int k = 0; k < kChunksV; ++k)
			pc[k] = V2{0, 0};
		for (int c = 0; c < NZ; ++c)
			for (int k = 0; k < kChunksV; ++k)
				pc[k] += P.m_colD[c][k] * P.m_z[c];
		for (int c = 0; c < 2; ++c)
			for (int k = 0; k < kChunksV; ++k)
				pc[k] += P.m_colE[c][k] * P.m_u[c];
		for (int k = 0; k < kChunksV; ++k) {
			p[l][2 * k] = pc[k][0];
			p[l][2 * k + 1] = pc[k][1];
		}
	}

	// The JFET's drain current does not reach the nodes upstream of its gate, so the system
	// is block triangular: solve the op amp and diodes (3 x 3) first, then the JFET (2 x 2).
	double FA[L][3][3], pA[L][3], vA[L][3], iA[L][3];
	EvalA evalA[L];
	LimitA limitA[L];
	for (int l = 0; l < L; ++l) {
		const Pedal &P = *ped[l];
		const DKMatrices &m = P.m_m;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c)
				FA[l][r][c] = m.F[r][c];
			pA[l][r] = p[l][r] + m.F[r][kIds] * P.m_i[kIds];
			// Start from a linear prediction of where the solution is heading.
			vA[l][r] = 2 * P.m_v[r] - P.m_vPrev[r];
			iA[l][r] = P.m_i[r];
		}
		evalA[l] = EvalA{&P.m_op, 1.0 / P.m_outOhms, P.m_diodeIs, P.m_diodeNVt};
		limitA[l] = LimitA{P.m_diodeNVt};
	}
	{
		NewtonRun<3, 3> run[L];
		bool done[L] = {};
		for (int left = L; left > 0;)
			for (int l = 0; l < L; ++l)
				if (!done[l] && run[l].iterate(FA[l], pA[l], vA[l], iA[l], evalA[l], limitA[l], 60, kTolA)) {
					done[l] = true;
					--left;
				}
		for (int l = 0; l < L; ++l) {
			Pedal &P = *ped[l];
			int it = run[l].result;
			if (it < 0) {
				// Retry from the previous solution instead of the prediction.
				++P.m_retried;
				for (int r = 0; r < 3; ++r)
					vA[l][r] = P.m_v[r];
				it = newtonBlock<3, 3>(FA[l], pA[l], vA[l], iA[l], evalA[l], limitA[l], 200, 1e-7);
			}
			if (it < 0) {
				++P.m_failures;
				++P.m_unconverged;
			} else
				P.m_iterations += it;
		}
	}

	double FJ[L][2][1], pJ[L][2], vJ[L][2], iJ[L][1];
	EvalJ evalJ[L];
	for (int l = 0; l < L; ++l) {
		const Pedal &P = *ped[l];
		const DKMatrices &m = P.m_m;
		for (int r = 0; r < 2; ++r) {
			FJ[l][r][0] = m.F[3 + r][kIds];
			double s = p[l][3 + r];
			for (int k = 0; k < 3; ++k)
				s += m.F[3 + r][k] * iA[l][k];
			pJ[l][r] = s;
			vJ[l][r] = P.m_v[3 + r];
		}
		iJ[l][0] = P.m_i[kIds];
		evalJ[l] = EvalJ{&P.m_cal.jfet};
	}
	{
		NewtonRun<2, 1> run[L];
		bool done[L] = {};
		for (int left = L; left > 0;)
			for (int l = 0; l < L; ++l)
				if (!done[l] && run[l].iterate(FJ[l], pJ[l], vJ[l], iJ[l], evalJ[l], LimitJ{}, 60, kTolJ)) {
					done[l] = true;
					--left;
				}
		for (int l = 0; l < L; ++l) {
			if (run[l].result < 0) {
				++ped[l]->m_failures;
				++ped[l]->m_unconverged;
			}
			++ped[l]->m_samples;
			++ped[l]->m_solved;
		}
	}

	for (int l = 0; l < L; ++l) {
		Pedal &P = *ped[l];
		const DKMatrices &m = P.m_m;
		for (int r = 0; r < 3; ++r) {
			P.m_vPrev[r] = P.m_v[r];
			P.m_v[r] = vA[l][r];
			P.m_i[r] = iA[l][r];
		}
		P.m_v[3] = vJ[l][0];
		P.m_v[4] = vJ[l][1];
		P.m_i[kIds] = iJ[l][0];

		double y[kMaxOut];
		const int ny = P.m_probes ? m.ny : 1;
		for (int r = 0; r < ny; ++r) {
			double s = 0;
			for (int c = 0; c < NZ; ++c)
				s += m.Do[r][c] * P.m_z[c];
			for (int c = 0; c < 2; ++c)
				s += m.Eo[r][c] * P.m_u[c];
			for (int c = 0; c < kNumInject; ++c)
				s += m.Fo[r][c] * P.m_i[c];
			y[r] = s;
		}
		V2 zn[kChunksZ];
		for (int k = 0; k < kChunksZ; ++k)
			zn[k] = V2{0, 0};
		for (int c = 0; c < NZ; ++c)
			for (int k = 0; k < kChunksZ; ++k)
				zn[k] += P.m_colA[c][k] * P.m_z[c];
		for (int c = 0; c < 2; ++c)
			for (int k = 0; k < kChunksZ; ++k)
				zn[k] += P.m_colB[c][k] * P.m_u[c];
		for (int c = 0; c < kNumInject; ++c)
			for (int k = 0; k < kChunksZ; ++k)
				zn[k] += P.m_colC[c][k] * P.m_i[c];
		for (int r = 0; r < NZ; ++r)
			P.m_z[r] = zn[r / 2][r % 2];

		if (!std::isfinite(y[0])) {
			// Should never happen; recover rather than emit NaNs forever. Counted as unconverged.
			++P.m_unconverged;
			P.m_target = P.m_now;
			if (!P.solveDC())
				++P.m_dcFailures;
			out[l] = 0.0;
			continue;
		}
		if (P.m_probes) {
			P.m_probe.opOut = y[1];
			P.m_probe.clip = y[2];
			P.m_probe.filter = y[3];
			P.m_probe.source = y[4];
		}
		out[l] = y[0];
	}
}

double Pedal::smallSignalDb(double hz)
{
	// Linearise the devices at the DC operating point and solve the complex MNA system.
	Devices dev{m_op, m_outOhms, m_diodeIs, m_diodeNVt, m_cal.jfet};
	double v[kMaxV]{}, i[kMaxI]{}, G[kMaxI][kMaxV];
	{
		// DC solution (cheap: a few Newton steps from the stored guess).
		double p[kMaxV];
		double u[kMaxSources] = {0, m_parts.vcc};
		for (int k = 0; k < m_dc.nv; ++k)
			v[k] = m_v[k];
		for (int r = 0; r < m_dc.nv; ++r) {
			p[r] = 0;
			for (int c = 0; c < m_dc.nu; ++c)
				p[r] += m_dc.E[r][c] * u[c];
		}
		solveNewton(m_dc, p, v, i, dev, 200, 1e-12);
		dev.eval(v, i, G);
	}
	using cd = std::complex<double>;
	const Netlist &n = m_net;
	const int nn = n.nodeCount(), ns = int(n.sources().size()), dim = nn + ns;
	std::vector<cd> s(size_t(dim * (dim + 1)), cd(0));
	auto S = [&](int r, int c) -> cd & { return s[size_t(r * (dim + 1) + c)]; };
	auto stamp = [&](int a, int b, cd y) {
		if (a >= 0)
			S(a, a) += y;
		if (b >= 0)
			S(b, b) += y;
		if (a >= 0 && b >= 0) {
			S(a, b) -= y;
			S(b, a) -= y;
		}
	};
	const cd jw(0, 2 * kPi * hz);
	for (int k = 0; k < nn; ++k)
		S(k, k) += n.gmin;
	for (const auto &r : n.resistors())
		stamp(r.a, r.b, 1.0 / r.value);
	for (const auto &c : n.capacitors())
		stamp(c.a, c.b, jw * c.value);
	for (int k = 0; k < ns; ++k) {
		const auto &src = n.sources()[size_t(k)];
		if (src.a >= 0) {
			S(src.a, nn + k) += 1.0;
			S(nn + k, src.a) += 1.0;
		}
		if (src.b >= 0) {
			S(src.b, nn + k) -= 1.0;
			S(nn + k, src.b) -= 1.0;
		}
	}
	// Linearised devices: injection k = sum_j G[k][j] (V(a_j) - V(b_j)) as VCCS stamps.
	for (int k = 0; k < kNumInject; ++k) {
		const auto &inj = n.injections()[size_t(k)];
		for (int j = 0; j < kNumSense; ++j) {
			const double g = G[k][j];
			if (g == 0)
				continue;
			const auto &se = n.senses()[size_t(j)];
			// Current g * (Va - Vb) into inj.a, out of inj.b: KCL rows get -g (leaving).
			auto add = [&](int row, int col, double val) {
				if (row >= 0 && col >= 0)
					S(row, col) += val;
			};
			add(inj.a, se.a, -g);
			add(inj.a, se.b, g);
			add(inj.b, se.a, g);
			add(inj.b, se.b, -g);
		}
	}
	// Input source = 1, supply = 0 (AC ground).
	S(nn + 0, dim) = 1.0;
	// Gaussian elimination.
	for (int c = 0; c < dim; ++c) {
		int piv = c;
		for (int r = c + 1; r < dim; ++r)
			if (std::abs(S(r, c)) > std::abs(S(piv, c)))
				piv = r;
		if (std::abs(S(piv, c)) == 0)
			return -200;
		if (piv != c)
			for (int k = c; k <= dim; ++k)
				std::swap(S(c, k), S(piv, k));
		for (int r = c + 1; r < dim; ++r) {
			const cd f = S(r, c) / S(c, c);
			if (f != cd(0))
				for (int k = c; k <= dim; ++k)
					S(r, k) -= f * S(c, k);
		}
	}
	std::vector<cd> x(static_cast<size_t>(dim));
	for (int r = dim - 1; r >= 0; --r) {
		cd acc = S(r, dim);
		for (int k = r + 1; k < dim; ++k)
			acc -= S(r, k) * x[size_t(k)];
		x[size_t(r)] = acc / S(r, r);
	}
	const int w = n.nodeIndex("w");
	return 20 * std::log10(std::abs(x[size_t(w)]) + 1e-30);
}

} // namespace rat

namespace rat {

std::string Pedal::spiceNetlist(const std::string &inputSpec) const
{
	const Netlist &n = m_net;
	auto name = [&](int i) { return i < 0 ? std::string("0") : n.nodeName(i); };
	std::string s = "* RAT model mirror (generated by ratrender --netlist)\n";
	char buf[512];
	s += "Vsrc src 0 " + inputSpec + "\n";
	std::snprintf(buf, sizeof buf, "Vbat vbat 0 DC %.10g\n", m_parts.vcc);
	s += buf;
	int k = 0;
	for (const auto &r : n.resistors()) {
		std::snprintf(buf, sizeof buf, "R%d %s %s %.10g\n", k++, name(r.a).c_str(), name(r.b).c_str(), r.value);
		s += buf;
	}
	k = 0;
	for (const auto &c : n.capacitors()) {
		std::snprintf(buf, sizeof buf, "C%d %s %s %.10g\n", k++, name(c.a).c_str(), name(c.b).c_str(), c.value);
		s += buf;
	}
	// Quadratic knee: sp(x, s) = (min(max(x,-s),s) + s)^2 / 4s + max(x - s, 0).
	auto sp = [](const std::string &x, double w) {
		char b[512];
		std::snprintf(b, sizeof b, "((min(max(%s,-%.10g),%.10g)+%.10g)*(min(max(%s,-%.10g),%.10g)+%.10g)/%.10g+max(%s-%.10g,0))",
			x.c_str(), w, w, w, x.c_str(), w, w, w, 4 * w, x.c_str(), w);
		return std::string(b);
	};
	const OpAmpModel &op = m_op;
	const std::string vy = "V(y)";
	std::snprintf(buf, sizeof buf, "B_in 0 y I = %.10g*tanh(V(p,n)/%.10g)*(1+%.10g*tanh(V(p,n)/%.10g)) - %.10g*(", op.it,
		2 * op.vte, op.asym, 2 * op.vte, op.clampG);
	s += buf;
	char hi[64], lo[64];
	std::snprintf(hi, sizeof hi, "(V(y)-%.10g)", op.outHi + op.clampMargin);
	std::snprintf(lo, sizeof lo, "(%.10g-V(y))", op.outLo - op.clampMargin);
	s += sp(hi, 0.02) + " - " + sp(lo, 0.02) + ")\n";
	std::snprintf(hi, sizeof hi, "(V(y)-%.10g)", op.outLo);
	std::snprintf(lo, sizeof lo, "(V(y)-%.10g)", op.outHi);
	std::snprintf(buf, sizeof buf, "B_out 0 o I = %.10g*(%.10g + ", 1.0 / m_outOhms, op.outLo);
	s += buf + sp(hi, op.knee) + " - " + sp(lo, op.knee) + ")\n";
	std::snprintf(buf, sizeof buf, "B_d dj 0 I = %.10g*(exp(V(dj)/%.10g)-exp(-V(dj)/%.10g))\n", m_diodeIs, m_diodeNVt,
		m_diodeNVt);
	s += buf;
	const Jfet &j = m_cal.jfet;
	std::snprintf(buf, sizeof buf,
		"B_j v9 s I = %.10g*min(max(V(v9,s),0),max(V(g,s)-(%.10g),0))*(2*max(V(g,s)-(%.10g),0)-min(max(V(v9,s),0),max(V(g,s)-(%.10g),0)))*(1+%.10g*V(v9,s))\n",
		j.beta, j.vto, j.vto, j.vto, j.lambda);
	s += buf;
	return s;
}

} // namespace rat

namespace rat {

bool setCalibrationField(Calibration &c, const std::string &name, double v)
{
	struct F {
		const char *n;
		double Calibration::*p;
	};
	static const F fields[] = {
		{"slew", &Calibration::slewVPerUs}, {"slewasym", &Calibration::slewAsym}, {"gbw", &Calibration::gbwHz},
		{"dcgain", &Calibration::dcGain}, {"op07outlo", &Calibration::op07OutLo}, {"op07outhi", &Calibration::op07OutHi},
		{"outlo", &Calibration::outLo}, {"outhi", &Calibration::outHi}, {"outknee", &Calibration::outKnee},
		{"outohms", &Calibration::outOhms}, {"op07slew", &Calibration::op07SlewVPerUs},
		{"op07slewasym", &Calibration::op07SlewAsym},
		{"op07outknee", &Calibration::op07OutKnee}, {"op07outohms", &Calibration::op07OutOhms},
		{"op07gbw", &Calibration::op07GbwHz}, {"diodeis", &Calibration::diodeIs}, {"dioden", &Calibration::diodeN},
		{"dioders", &Calibration::diodeRs}, {"distmid", &Calibration::distMid},
		{"filtermid", &Calibration::filterMid}, {"volumemid", &Calibration::volumeMid},
		{"distend", &Calibration::distEnd}, {"filterend", &Calibration::filterEnd},
		{"volumeend", &Calibration::volumeEnd},
		{"potmin", &Calibration::potMinOhms}, {"sourceohms", &Calibration::sourceOhms},
		{"loadohms", &Calibration::loadOhms},
	};
	for (const auto &f : fields)
		if (name == f.n) {
			c.*(f.p) = v;
			return true;
		}
	if (name == "jfetvto")
		c.jfet.vto = v;
	else if (name == "jfetbeta")
		c.jfet.beta = v;
	else if (name == "jfetlambda")
		c.jfet.lambda = v;
	else
		return false;
	return true;
}

} // namespace rat
