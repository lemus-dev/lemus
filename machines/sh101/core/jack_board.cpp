// Jack board A's mute: TR7 and its drive from C13, and from C14 when switched off
// (docs/model-filter.md 5).
#include "jack_board.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace sh101 {

namespace {
// Parts as drawn (jack board A, OPH178-2; C2 on the bender board), models as in
// tools/spice/filter/models.lib.
constexpr double kR28 = 1e3, kR29 = 2.2e3, kR30 = 100e3, kC2 = 2.2e-6;
constexpr double kR35 = 47e3, kR36 = 220e3, kC13 = 10e-6;
constexpr double kR37 = 10e3, kC14 = 47e-6; // TR8's base resistor, the power-off mute's store
constexpr double kSupply = 9.0;                        // C13's upper plate, +9 V
constexpr double kBus = 5.0;                           // IC1a's output at rest: the +5 V bus
constexpr double kVt = 0.025693;                       // 25 C, as the rest of the voice
constexpr double kQIs = 3.3e-14, kQBf = 300, kQBr = 5; // TR7, 2SC1815-GR; TR8, 2SA1015-GR, the same
constexpr double kDIs = 2.52e-9, kDVt = 1.752 * kVt;   // D3, D4, 1S2473
// Switched on, the path from ground to the node (D3 or R36, D4, R35, TR7's base-collector
// junction) carries under 1 pA while the node stays above this, which moves it by nanovolts.
constexpr double kConductsBelow = -0.4;
constexpr double kPoweredBelow = 0.05; // node A: TR7's base gets nothing from it any more
// Switched off, +9 V falls as C4 (100 uF) and the rest of the rail's capacitors feed the unit's
// load: about 110 mA at 9 V (the 1 W rating) from about 150 uF. An estimate: the thump's level
// depends on it (SPICE: -41 dBFS at 12 ms, -30 at 5 ms, under -45 at 25 ms).
constexpr double kRailTau = 12e-3;
constexpr double kDownSeconds = 0.3, kFadeSeconds = 0.2; // the thump, then TR7's offset faded out

// exp() with the argument held where the Newton steps can't overflow it.
double junction(double v, double vt) { return std::exp(std::min(v, 1.2) / vt); }
} // namespace

void JackBoard::prepare(double fs)
{
	m_fs = fs;
	m_dt = 1 / fs;
	setLoad(m_load);
}

void JackBoard::setLoad(double ohms)
{
	m_load = ohms;
	m_kth = (kR28 + ohms) / ohms;
	m_kyx = (kR28 + ohms) / (kR29 + kR28 + ohms);
	m_relaxA = std::exp(-m_dt / (kR36 * kC13));
	m_relaxY = std::exp(-m_dt / (kC2 / (1 / kR30 + 1 / (kR29 + kR28 + ohms))));
}

void JackBoard::powerOn()
{
	// C13 discharged (D3 clamped node A while the rails fell): node A follows +9 V up. C2 too:
	// its side follows IC1a's output up to the bus.
	m_va = kSupply;
	m_vb = kSupply - 0.6;
	m_vbase = 0.6;
	m_x = 0;
	m_yi = kBus;
	m_powering = true;
	m_down = false;
	m_v9 = m_v9Prev = m_e8 = m_b8 = kSupply; // C14 charges through D5 at once
}

void JackBoard::settle()
{
	m_va = m_vb = m_vbase = m_x = m_yi = 0;
	m_powering = false;
	m_down = false;
	m_v9 = m_v9Prev = m_e8 = m_b8 = kSupply;
}

void JackBoard::powerOff()
{
	if (m_down)
		return;
	m_down = true;
	m_downN = 0;
}

double JackBoard::tick(double jack)
{
	const double x0 = jack * m_kth; // the node with TR7 off
	if (m_down) {
		double out = solveDown(x0, m_v9Prev) / m_kth;
		m_v9Prev = m_v9;
		const double t = double(++m_downN) * m_dt;
		m_v9 = kSupply * std::exp(-t / kRailTau);
		if (t > kDownSeconds) {
			const double fade = 1 - (t - kDownSeconds) / kFadeSeconds;
			if (fade <= 0) {
				settle(); // over: the plugin is silent from here
				return 0;
			}
			out *= fade;
		}
		return out;
	}
	if (!m_powering) {
		const double yi = m_yi * m_relaxY;
		const double x = x0 + m_kyx * yi;
		if (x > kConductsBelow) {
			m_va *= m_relaxA; // C13 settles through R36; nothing else conducts
			m_yi = yi;
			m_x = x;
			return x / m_kth;
		}
	}
	return solve(x0) / m_kth;
}

double JackBoard::solve(double x0)
{
	// Unknowns: node A (C13, R36, D3, D4), B (D4, R35), TR7's base, the node X (R29, R28 into the
	// load, TR7's collector) and C2's share Yi (C2 and R30 to AC ground, R29). The node is x0 plus
	// TR7's share. The Jacobian is tridiagonal.
	const double vaPrev = m_va, yiPrev = m_yi, gc13 = kC13 / m_dt, gc2 = kC2 / m_dt;
	const double gl = 1 / (kR28 + m_load);
	double v[5] = {m_va, m_vb, m_vbase, m_x, m_yi};
	for (int it = 0; it < 40; ++it) {
		const double va = v[0], vb = v[1], vbase = v[2], x = v[3], yi = v[4];
		const double e4 = kDIs * junction(va - vb, kDVt), e3 = kDIs * junction(-va, kDVt);
		const double id4 = e4 - kDIs, gd4 = e4 / kDVt, id3 = e3 - kDIs, gd3 = e3 / kDVt;
		const double ef = kQIs * junction(vbase, kVt), er = kQIs * junction(vbase - x, kVt);
		const double iF = ef - kQIs, iR = er - kQIs, gf = ef / kVt, gr = er / kVt;
		const double ic = iF - iR * (1 + 1 / kQBr), ib = iF / kQBf + iR / kQBr;
		const double xi = x - x0;
		const double f[5] = {gc13 * (va - vaPrev) + va / kR36 + id4 - id3, -id4 + (vb - vbase) / kR35,
		                     -(vb - vbase) / kR35 + ib, (xi - yi) / kR29 + xi * gl + ic,
		                     gc2 * (yi - yiPrev) + yi / kR30 + (yi - xi) / kR29};
		// Rows: sub (a), diagonal (b), super (c).
		double a[5] = {0, -gd4, -1 / kR35, gf - gr * (1 + 1 / kQBr), -1 / kR29};
		double b[5] = {gc13 + 1 / kR36 + gd4 + gd3, gd4 + 1 / kR35, 1 / kR35 + gf / kQBf + gr / kQBr,
		               1 / kR29 + gl + gr * (1 + 1 / kQBr), gc2 + 1 / kR30 + 1 / kR29};
		double c[5] = {-gd4, -1 / kR35, -gr / kQBr, -1 / kR29, 0};
		double d[5];
		for (int i = 0; i < 5; ++i)
			d[i] = -f[i];
		// Thomas algorithm.
		for (int i = 1; i < 5; ++i) {
			const double m = a[i] / b[i - 1];
			b[i] -= m * c[i - 1];
			d[i] -= m * d[i - 1];
		}
		double dx[5];
		dx[4] = d[4] / b[4];
		for (int i = 3; i >= 0; --i)
			dx[i] = (d[i] - c[i] * dx[i + 1]) / b[i];
		// Junction voltages move at most 0.1 V a step.
		const double step = std::max({std::abs(dx[0] - dx[1]), std::abs(dx[2]), std::abs(dx[2] - dx[3])});
		const double k = step > 0.1 ? 0.1 / step : 1.0;
		double most = 0;
		for (int i = 0; i < 5; ++i) {
			v[i] += k * dx[i];
			most = std::max(most, std::abs(k * dx[i]));
		}
		// Converged once a step falls under 0.1 nV, or under 10 uV if the junctions' limit didn't
		// cut it: Newton converges quadratically here, the step after one under 10 uV is about
		// 1 nV, so the iterations that would only confirm it are skipped.
		if (most < 1e-10 || (most < 1e-5 && k == 1.0))
			break;
	}
	m_va = v[0];
	m_vb = v[1];
	m_vbase = v[2];
	m_x = v[3];
	m_yi = v[4];
	if (m_powering && m_va < kPoweredBelow)
		m_powering = false;
	return m_x;
}

double JackBoard::solveDown(double x0, double p9Prev)
{
	// Switched off: the five nodes of solve(), with C13's upper plate on the falling +9 V, and
	// TR8's emitter (C14, D5 from +9 V) and base (R37 to +9 V), its collector on node B. Seven
	// unknowns, no longer a chain: Gaussian elimination. Only for the half second after switching
	// off.
	const double p9 = m_v9;
	const double vaPrev = m_va, yiPrev = m_yi, e8Prev = m_e8;
	const double gc13 = kC13 / m_dt, gc2 = kC2 / m_dt, gc14 = kC14 / m_dt;
	const double gl = 1 / (kR28 + m_load);
	double v[7] = {m_va, m_vb, m_vbase, m_x, m_yi, m_e8, m_b8};
	for (int it = 0; it < 60; ++it) {
		const double va = v[0], vb = v[1], vbase = v[2], x = v[3], yi = v[4], e8 = v[5], b8 = v[6];
		const double e4 = kDIs * junction(va - vb, kDVt), e3 = kDIs * junction(-va, kDVt);
		const double e5 = kDIs * junction(p9 - e8, kDVt);
		const double id4 = e4 - kDIs, gd4 = e4 / kDVt, id3 = e3 - kDIs, gd3 = e3 / kDVt;
		const double id5 = e5 - kDIs, gd5 = e5 / kDVt;
		// TR7 (NPN), TR8 (PNP: its junctions point the other way).
		const double ef = kQIs * junction(vbase, kVt), er = kQIs * junction(vbase - x, kVt);
		const double iF = ef - kQIs, iR = er - kQIs, gf = ef / kVt, gr = er / kVt;
		const double ic = iF - iR * (1 + 1 / kQBr), ib = iF / kQBf + iR / kQBr;
		const double pf = kQIs * junction(e8 - b8, kVt), pr = kQIs * junction(vb - b8, kVt);
		const double pF = pf - kQIs, pR = pr - kQIs, gpf = pf / kVt, gpr = pr / kVt;
		const double ic8 = pF - pR * (1 + 1 / kQBr); // out of TR8's collector into node B
		const double ib8 = pF / kQBf + pR / kQBr;    // out of its base
		const double ie8 = pF * (1 + 1 / kQBf) - pR; // into its emitter
		const double xi = x - x0;
		double f[7] = {gc13 * ((va - p9) - (vaPrev - p9Prev)) + va / kR36 + id4 - id3,
		               -id4 + (vb - vbase) / kR35 - ic8,
		               -(vb - vbase) / kR35 + ib,
		               (xi - yi) / kR29 + xi * gl + ic,
		               gc2 * (yi - yiPrev) + yi / kR30 + (yi - xi) / kR29,
		               gc14 * (e8 - e8Prev) + ie8 - id5,
		               (b8 - p9) / kR37 - ib8};
		double J[7][7] = {};
		J[0][0] = gc13 + 1 / kR36 + gd4 + gd3;
		J[0][1] = -gd4;
		J[1][0] = -gd4;
		J[1][1] = gd4 + 1 / kR35 + gpr * (1 + 1 / kQBr);
		J[1][2] = -1 / kR35;
		J[1][5] = -gpf;
		J[1][6] = gpf - gpr * (1 + 1 / kQBr);
		J[2][1] = -1 / kR35;
		J[2][2] = 1 / kR35 + gf / kQBf + gr / kQBr;
		J[2][3] = -gr / kQBr;
		J[3][2] = gf - gr * (1 + 1 / kQBr);
		J[3][3] = 1 / kR29 + gl + gr * (1 + 1 / kQBr);
		J[3][4] = -1 / kR29;
		J[4][3] = -1 / kR29;
		J[4][4] = gc2 + 1 / kR30 + 1 / kR29;
		J[5][1] = -gpr;
		J[5][5] = gc14 + gpf * (1 + 1 / kQBf) + gd5;
		J[5][6] = -gpf * (1 + 1 / kQBf) + gpr;
		J[6][1] = -gpr / kQBr;
		J[6][5] = -gpf / kQBf;
		J[6][6] = 1 / kR37 + gpf / kQBf + gpr / kQBr;
		double dx[7];
		for (int i = 0; i < 7; ++i)
			dx[i] = -f[i];
		for (int c = 0; c < 7; ++c) {
			int piv = c;
			for (int r = c + 1; r < 7; ++r)
				if (std::abs(J[r][c]) > std::abs(J[piv][c]))
					piv = r;
			if (piv != c) {
				std::swap(J[piv], J[c]);
				std::swap(dx[piv], dx[c]);
			}
			for (int r = c + 1; r < 7; ++r) {
				const double m = J[r][c] / J[c][c];
				if (m == 0)
					continue;
				for (int k = c; k < 7; ++k)
					J[r][k] -= m * J[c][k];
				dx[r] -= m * dx[c];
			}
		}
		for (int r = 6; r >= 0; --r) {
			for (int k = r + 1; k < 7; ++k)
				dx[r] -= J[r][k] * dx[k];
			dx[r] /= J[r][r];
		}
		// Junction voltages move at most 0.1 V a step.
		const double step = std::max({std::abs(dx[0] - dx[1]), std::abs(dx[0]), std::abs(dx[2]), std::abs(dx[2] - dx[3]),
		                              std::abs(dx[5] - dx[6]), std::abs(dx[1] - dx[6]), std::abs(dx[5])});
		const double k = step > 0.1 ? 0.1 / step : 1.0;
		double most = 0;
		for (int i = 0; i < 7; ++i) {
			v[i] += k * dx[i];
			most = std::max(most, std::abs(k * dx[i]));
		}
		// Converged once a step falls under 0.1 nV, or under 10 uV if the junctions' limit didn't
		// cut it: Newton converges quadratically here, the step after one under 10 uV is about
		// 1 nV, so the iterations that would only confirm it are skipped.
		if (most < 1e-10 || (most < 1e-5 && k == 1.0))
			break;
	}
	m_va = v[0];
	m_vb = v[1];
	m_vbase = v[2];
	m_x = v[3];
	m_yi = v[4];
	m_e8 = v[5];
	m_b8 = v[6];
	return m_x;
}

} // namespace sh101
