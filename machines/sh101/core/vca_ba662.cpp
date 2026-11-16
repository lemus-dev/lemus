// The SH-101's VCA (BA662A, TR28) and output stage (docs/model-filter.md 5).
#include "vca_ba662.h"

#include "env_gen.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kR30 = 100e3, kR29 = 2.2e3, kR28 = 1e3, kC2 = 2.2e-6, kC12 = 1e-9;

// A limit that is linear inside and rounds off over the last 10 % (the op-amps' output stages).
double softLimit(double v, double lo, double hi)
{
	auto knee = [](double x, double top) {
		const double k = 0.9 * top;
		if (x <= k)
			return x;
		const double r = top - k;
		return k + r * std::tanh((x - k) / r);
	};
	return v >= 0 ? knee(v, hi) : -knee(-v, -lo);
}
} // namespace

void Ba662Vca::setParams(const Params &p)
{
	m_p = p;
	prepare(m_fs);
}

void Ba662Vca::prepare(double fs)
{
	m_fs = fs;
	m_t = 1 / fs;
	const double rl = m_p.loadOhms;
	// C2 into R30 in parallel with the rest of the jack board and the load.
	const double rHp = 1 / (1 / kR30 + 1 / (kR29 + kR28 + rl));
	const double wHp = std::tan(kPi / (2 * kPi * kC2 * rHp) / fs);
	m_hpA = (1 - wHp) / (1 + wHp);
	// C12 at R29's far end, driven through R29, loaded by R28 and the load.
	const double rLp = 1 / (1 / kR29 + 1 / (kR28 + rl));
	// The pole (about 76 kHz) sits near Nyquist: warp the bilinear to be exact at 10 kHz instead.
	const double fLp = 1 / (2 * kPi * kC12 * rLp);
	const double ref = kPi * 10e3 / fs;
	const double wLp = kPi * fLp / fs * std::tan(ref) / ref;
	m_lpA = (1 - wLp) / (1 + wLp);
	m_lpB = wLp / (1 + wLp);
	m_jackGain = (kR28 + rl) / (kR29 + kR28 + rl) * rl / (kR28 + rl);
	reset();
}

void Ba662Vca::reset()
{
	m_ie = 0;
	m_ccVolts = -1e300; // prepare() and setParams() come through here: no stale result
	m_v8 = 0;
	m_v9 = 9;
	m_hpX = m_hpY = m_lpX = m_lpY = 0;
}

void Ba662Vca::setVolume(double travel) { m_vol = EnvGen::taperA(travel); }

double Ba662Vca::controlCurrent(double volts, double ohms)
{
	// I = (V - Vref - V_EB(I)) / R with V_EB = Vt ln(1 + I/Is): Newton on I, from the last one.
	if (volts == m_ccVolts && ohms == m_ccOhms && m_ie == m_ccStart && m_ccStart == m_ccOut)
		return m_ie; // the same inputs from a fixed point
	m_ccVolts = volts;
	m_ccOhms = ohms;
	m_ccStart = m_ie;
	const Params &p = m_p;
	const double drive = volts - p.vRef + p.kneeShiftVolts;
	if (drive <= 0.3) {
		// Far below the knee: the junction's own current only.
		const double i = p.tr28Is * (std::exp(std::max(drive, -1.0) / p.vt) - 1);
		m_ie = std::max(i, 0.0);
		m_ccOut = m_ie;
		return m_ie;
	}
	double i = std::max(m_ie, 1e-12);
	for (int k = 0; k < 20; ++k) {
		const double veb = p.vt * std::log1p(i / p.tr28Is);
		const double f = i * ohms + veb - drive;
		const double df = ohms + p.vt / (p.tr28Is + i);
		double di = -f / df;
		// Keep i positive: the log near zero is steep.
		if (i + di <= 0)
			di = -0.5 * i;
		i += di;
		if (std::abs(di) < 1e-13)
			break;
	}
	m_ie = i;
	m_ccOut = i;
	return i;
}

double Ba662Vca::envControlCurrent(double envVolts, double v14)
{
	// S6's ENV contact as a Thevenin source: R197 from the ENV line, R196 from +14 V. It sits
	// 9.3 V x 3.3k / 123.3k = 0.25 V above the ENV, so TR28 turns on with the ENV that much lower.
	const double g = 1 / kR197 + 1 / kR196;
	return controlCurrent((envVolts / kR197 + v14 / kR196) / g, 1 / g + kR133);
}

double Ba662Vca::tick(double in, double iAbc)
{
	const Params &p = m_p;
	// BA662: I_ABC tanh(v/2Vt) into R140 47k, less the pair's losses (docs/circuit-filter.md 6.2).
	// `in` is pin 3's open-circuit voltage; the pair's input resistance (4 beta V_T / I_ABC, with
	// R138 on pin 2 in series) divides it against pin 3's source resistance.
	const double rIn = 4 * p.inputBeta * p.vt + kR138 * iAbc; // x I_ABC
	const double vEff = (in * rIn / (rIn + p.pin3Ohms * iAbc) + p.vosVolts) / (1 + iAbc / p.lossAmps);
	double v6 = p.km * iAbc * std::tanh(vEff / (2 * p.vt)) * kR140;
	// Pin 6/8 swing on 0/+14 V about the bus: down to ~0.8 V, up past the panel's levels.
	m_v8 = softLimit(v6, -4.2, 8.0);
	// VOLUME into IC1a, an M5218 on +9 V: about 1.2-7.8 V.
	double w = softLimit(m_vol * m_v8, -3.8, 2.8);
	if (m_v9 < 9) // switched off: under +9 V less 1.2 V, and not below ground
		w = std::max(std::min(m_v9 > kVoiceDiesBelow ? w : 0.0, m_v9 - 6.2), -5.0);
	// C2's high-pass, then the jack board's divider and C12.
	m_hpY = m_hpA * m_hpY + (1 + m_hpA) / 2 * (w - m_hpX);
	m_hpX = w;
	const double x = m_hpY * m_jackGain;
	m_lpY = m_lpA * m_lpY + m_lpB * (x + m_lpX);
	m_lpX = x;
	return m_lpY;
}

} // namespace sh101
