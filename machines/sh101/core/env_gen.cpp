// The SH-101's ENV GEN (docs/model-filter.md 4).
#include "env_gen.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

namespace {
constexpr double kR141 = 270, kR164 = 150, kR148 = 220; // in series with A, D, R
constexpr double kR158 = 82e3, kVr24 = 100e3;           // the threshold divider
constexpr double kR163 = 47, kC58 = 10e-6;              // TR31's emitter node
constexpr double kR142 = 12e3, kR41 = 33e3, kC57 = 0.047e-6; // the GATE level (pin 83)
// TRIG detector (p.9 TRIG, IC4b/IC5): C13 4.7 nF into R21 68k, IC5b x(1 + 470k/15k) with
// C14 150 pF across R23, then a window comparator at about a diode drop.
constexpr double kTrigTau = 4.7e-9 * 68e3, kTrigGain = 1 + 470.0 / 15.0, kTrigPole = 470e3 * 150e-12;
constexpr double kTrigLevel = 0.6;
// Transistors (2SC1815/2SA1015, tools/spice/filter/models.lib): saturation current, the switches'
// saturation resistance, and TR37's emitter resistor.
constexpr double kVt = 0.025693, kIs = 3.3e-14, kRsat = 8, kR166 = 33e3, kBeta = 300, kBetaR = 5;
constexpr double kR157 = 47e3, kR159 = 33e3, kR165 = 10e3;
constexpr double kVo16Drop = 2.0; // IC16a high: the IR9022 swings to 2 V below its +14 V supply
// At rest C59 sits above the bus by TR31's base current in R163 and its saturation (SPICE).
constexpr double kRestLift = 0.015;
} // namespace

double EnvGen::taperA(double x) { return taperA(x, 0.15); }

double EnvGen::taperA(double x, double mid)
{
	const double s = 1 / mid - 1;
	const double b = s * s;
	x = std::clamp(x, 0.0, 1.0);
	return (std::pow(b, x) - 1) / (b - 1);
}

void EnvGen::setParams(const Params &p)
{
	m_p = p;
	setControls(m_a, m_d, m_s, m_r, m_mode);
}

void EnvGen::prepare(double fs)
{
	m_fs = fs;
	m_t = 1 / fs;
	setParams(m_p);
	reset();
}

void EnvGen::reset()
{
	m_v58 = m_v57 = m_p.vRef;
	m_v = m_p.vRef + kRestLift;
	m_latch = m_attack = false;
	m_reset = true;
	m_i = 0;
	m_trigHp = m_trigAmp = 0;
	m_kcvInit = false;
}

void EnvGen::setControls(double attack, double decay, double sustain, double release, Mode mode)
{
	m_a = attack;
	m_d = decay;
	m_r = release;
	m_ra = kR141 + m_p.potEndOhms + m_p.potOhms * taperA(attack, m_p.potMid);
	m_rd = kR164 + m_p.potEndOhms + m_p.potOhms * taperA(decay, m_p.potMid);
	m_rr = kR148 + m_p.potEndOhms + m_p.potOhms * taperA(release, m_p.potMid);
	m_s = std::clamp(sustain, 0.0, 1.0);
	m_mode = mode;
	// The threshold (pin 90) and SUSTAIN (pin 91) nodes: R158 from +14 V, VR24 to the bus, and
	// TR37's base current (its emitter into R166 33k) out of the wiper.
	const double g1 = 1 / kR158, gt = 1 / std::max(kVr24 * (1 - m_s), 1.0), gb = 1 / std::max(kVr24 * m_s, 1.0);
	const double gx = 1 / (kR166 * (kBeta + 1)), vbe = 0.6;
	// g1 (V14 - Vth) = gt (Vth - Vs);  gt (Vth - Vs) = gb (Vs - Vref) + gx (Vs - vbe)
	const double a11 = g1 + gt, a12 = -gt, b1 = g1 * m_p.v14;
	const double a21 = -gt, a22 = gt + gb + gx, b2 = gb * m_p.vRef + gx * vbe;
	const double det = a11 * a22 - a12 * a21;
	m_vth = (b1 * a22 - a12 * b2) / det;
	m_vs = (a11 * b2 - a21 * b1) / det;
	// The per-sample factors (tick).
	const double t = m_t;
	m_kTrigHp = std::exp(-t / kTrigTau);
	m_kTrigAmp = 1 - std::exp(-t / kTrigPole);
	m_kAttack = std::exp(-t / (m_ra * m_p.c59));
	m_kDecay = std::exp(-t / (m_rd * m_p.c59));
	m_k58 = 1 - std::exp(-t / (kR163 * kC58));
	m_vbe37 = kVt * std::log((m_vs - 0.6) / kR166 / kIs + 1);
	// TR35 in the decay, saturated by IC16a through R159 with R165 across its base: with next to no
	// collector current its base takes I_S e^(V_BE/V_T) (1/beta_F + 1/(1 + beta_R)), 0.17 I_S e^(...)
	// for the 2SC1815 (beta_R 5), which sets V_BE (about 0.56 V). All of R159's current, through
	// R165 or the junction, leaves by TR35's emitter into TR36 (the T node).
	{
		const double tNode0 = m_vs - m_vbe37, vo16 = m_p.v14 - kVo16Drop;
		double vbe = 0.56, ib = 0;
		for (int k = 0; k < 4; ++k) {
			ib = (vo16 - vbe - tNode0) / kR159 - vbe / kR165;
			vbe = kVt * std::log(std::max(ib, 1e-9) / (kIs * (1.0 / kBeta + 1.0 / (1 + kBetaR))));
		}
		m_ib35 = std::max(ib, 0.0);
		m_i159 = std::max((vo16 - vbe - tNode0) / kR159, 0.0);
	}
	m_r57On = kR142 * kR41 / (kR142 + kR41) + kRsat;
	m_v57On = ((m_p.v14 - m_p.vceSat) / kR142 + m_p.vRef / kR41) * m_r57On;
	m_k57On = std::exp(-t / (m_r57On * kC57));
	m_k57Off = std::exp(-t / (kR41 * kC57));
}

double EnvGen::tick(bool gate, double kcvVolts, bool lfoSquare)
{
	const Params &p = m_p;
	const double t = m_t;

	// TRIG: a pulse while the differentiated KCV, amplified, is beyond a diode drop.
	if (!m_kcvInit) {
		m_kcvLast = kcvVolts;
		m_kcvInit = true;
	}
	m_trigHp = m_kTrigHp * (m_trigHp + kcvVolts - m_kcvLast);
	m_kcvLast = kcvVolts;
	m_trigAmp += (kTrigGain * m_trigHp - m_trigAmp) * m_kTrigAmp;
	const bool trig = std::abs(m_trigAmp) > kTrigLevel;

	// TR32: gate off (TR29 off), or S7 deck 2's line: TRIG in GATE+TRIG, the LFO square in LFO
	// (the attack runs in the square's high half, which starts at CLK RST).
	m_reset = !gate || (m_mode == Mode::GateTrig && trig) || (m_mode == Mode::Lfo && !lfoSquare);

	// The switches' saturation, with the last sample's current (it changes slowly).
	const double vce = p.vceSat + kRsat * m_i;
	if (m_reset) {
		// TR33 on: TR31 releases C59 into its emitter node (R163 to the bus, C58), TR34 is held
		// off through D32, and D33 pulls IC16a's input low, clearing its latch.
		m_latch = false;
		m_attack = false;
		// TR31's base current (R157 from TR33's collector) leaves through R163 as well.
		const double e31 = m_v58 + kR163 * (p.v14 - 0.65 - p.vRef) / kR157;
		const double i = m_v > e31 + vce ? (m_v - vce - e31) / m_rr : 0.0;
		// Semi-implicit: C59 and C58 exchange charge through the release path.
		const double k = 1 + t / (p.c59 * m_rr) + t / (kC58 * m_rr);
		const double di = i / k;
		m_v -= t / p.c59 * di;
		m_v58 += t / kC58 * di;
		m_i = di;
	} else if (!m_latch) {
		// Attack: TR34 from +14 V until IC16a trips at the threshold.
		m_attack = true;
		const double target = p.v14 - vce;
		const double v0 = m_v;
		m_v = target + (m_v - target) * m_kAttack;
		m_i = (m_v - v0) * p.c59 / t;
		if (m_v >= m_vth) {
			m_latch = true; // IC16a high, held by D34
			m_attack = false;
		}
	} else {
		// Decay through TR35 into the SUSTAIN follower: TR37's V_BE down, TR36's V_EB up at the
		// decay current, so as the current dies the level sags below SUSTAIN. TR36 only sinks:
		// C59 doesn't rise to a raised SUSTAIN.
		// TR35 is saturated by IC16a through R159 (R165 across its base): it sinks up to beta x
		// its base current and, run backwards, sources up to the base current itself. Its emitter
		// sits on the follower, which carries R159's current too (setControls).
		m_attack = false;
		const double tNode0 = m_vs - m_vbe37;
		const double ib = m_ib35;
		const double veb36 = kVt * std::log((m_i159 + std::max(m_i, 0.0)) / kIs + 1);
		const double target = tNode0 + veb36 + vce;
		const double v0 = m_v;
		double v = target + (m_v - target) * m_kDecay;
		const double i = (v0 - v) * p.c59 / t;
		if (i > kBeta * ib)
			v = v0 - kBeta * ib * t / p.c59;
		else if (i < -ib)
			v = v0 + ib * t / p.c59;
		m_v = v;
		m_i = (v0 - m_v) * p.c59 / t;
	}
	m_v58 += (p.vRef - m_v58) * m_k58;

	// The GATE level at pin 83: TR30 (on with the gate) through R142 12k, R41 33k to the bus,
	// C57 47 nF. In GATE mode R133 and TR28's emitter load the node (the last sample's current).
	const double vSrc = gate ? m_v57On : p.vRef;
	m_v57 = vSrc + (m_v57 - vSrc) * (gate ? m_k57On : m_k57Off) - t / kC57 * m_gateLoad;
	return m_v;
}

} // namespace sh101
