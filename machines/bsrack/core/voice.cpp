#include "voice.h"

#include "netlists.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace bsrack {

namespace {
constexpr double kVt = 0.025852; // kT/q at 27 C
constexpr double kIs = 1e-14;    // the DTA124E's internal PNP [F: a small-signal PNP's]
} // namespace

Voice::Voice() = default;
Voice::~Voice() = default;

double Voice::expoCurrent(double vp, double iref, const VoiceParts &p)
{
	// Transistor 1 (the reference): its pin on AGND, its collector at the op-amp's virtual ground
	// with iref drawn off it: the common emitter sits at Ve = Veb1 (1 + R1/R2) + R1 iref / beta.
	const double r = p.fmaR1 / p.fmaR2;
	const double veb1 = kVt * std::log(iref / kIs);
	const double ve = veb1 * (1 + r) + p.fmaR1 * iref / p.fmaBeta;
	// Transistor 2: Ve - vp = Veb2 (1 + R1/R2) + R1 Ic2 / beta, the pin driven stiffly (the divider's
	// loading is in the caller's Thevenin voltage). Newton on Veb2.
	double x = veb1 - (vp / (1 + r));
	for (int k = 0; k < 30; ++k) {
		const double ic = kIs * std::exp(std::min(x / kVt, 80.0));
		const double g = ve - vp - x * (1 + r) - p.fmaR1 * ic / p.fmaBeta;
		const double dg = -(1 + r) - p.fmaR1 * ic / (p.fmaBeta * kVt);
		const double step = g / dg;
		x -= std::clamp(step, -0.1, 0.1);
		if (std::abs(step) < 1e-9)
			break;
	}
	return kIs * std::exp(std::min(x / kVt, 80.0));
}

void Voice::prepare(double rate)
{
	m_rate = rate;
	m_runRate = 2 * rate;
	m_dt = 1.0 / m_runRate;
	m_cvCoef = 1.0 - std::exp(-m_dt / m_p.cvTau);
	m_volCoef = 1.0 - std::exp(-m_dt / m_p.volTau);
	m_vcaCoef = 1.0 - std::exp(-m_dt / m_p.vcaTau);
	m_hpCoef = std::exp(-2 * M_PI * m_p.couplingHz * m_dt);
	m_quietLimit = int(0.1 * rate);

	const std::string models = netlists::models;
	std::map<std::string, double> p1, p2;
	if (m_p.c33 > 0) {
		p1["c33"] = m_p.c33;
		p2["c37"] = m_p.c33;
	}
	m_s1 = std::make_unique<circuit::Circuit>(circuit::Netlist::parse(models + netlists::vcf1, p1));
	m_s2 = std::make_unique<circuit::Circuit>(circuit::Netlist::parse(models + netlists::vcf2, p2));
	m_s1->setName("vcf1");
	m_s2->setName("vcf2");
	for (auto *c : {m_s1.get(), m_s2.get()})
		c->setRate(m_runRate);
	const auto &n1 = m_s1->netlist(), &n2 = m_s2->netlist();
	m_s1Mix = n1.input("imix");
	m_s1A1 = n1.input("iabc1");
	m_s1A2 = n1.input("iabc2");
	m_s1Ar = n1.input("iabcr");
	m_s1Out = n1.output("out");
	m_s2In = n2.input("in");
	m_s2A1 = n2.input("iabc1");
	m_s2A2 = n2.input("iabc2");
	m_s2Ar = n2.input("iabcr");
	m_s2Out = n2.output("out");
	m_decim.setStages(1);
	m_decim.reset();
	m_started = false;
}

int Voice::takeUnconverged()
{
	int n = 0;
	for (auto *c : {m_s1.get(), m_s2.get()})
		if (c != nullptr) {
			n += int(c->stats().failures);
			c->resetStats();
		}
	return n;
}

// One sub-sample of a DCO, its output (volts above VSS) one sub-sample late so the polyBLEP
// residual can reach back. slope: volts per sub-sample; resets: fractions 0..1 within the
// sub-sample, in order.
double Voice::runDco(Dco &d, double slope, const float *resets, int nResets, double, double, bool pulse, double threshold)
{
	const double g = 1 + m_p.r32 / m_p.r31, hi = m_p.oscHigh, lo = m_p.oscLow;
	double corrPrev = 0, corrCur = 0;
	auto step = [&](double at, double h) {
		const double u = 1 - at;
		corrPrev += h * u * u * 0.5;
		corrCur -= h * at * at * 0.5;
	};
	auto saw = [&](double v) { return std::min(g * v, hi); };
	double t = 0;
	for (int i = 0; i <= nResets; ++i) {
		const double until = i < nResets ? double(resets[i]) : 1.0;
		// the comparator crossing on the way up, if it falls in this stretch
		if (pulse && !d.high && slope > 0) {
			const double tc = t + (threshold - d.v) / slope;
			if (tc >= t && tc < until) {
				d.high = true;
				step(tc, hi - lo);
			}
		}
		d.v += slope * (until - t);
		t = until;
		if (i < nResets) {
			// the reset: the capacitor emptied (the 4070's pulse is microseconds: taken as an instant)
			if (pulse) {
				if (d.high)
					step(t, lo - hi);
				d.high = false;
			} else
				step(t, -saw(d.v));
			d.v = 0;
			if (pulse && threshold <= 0) {
				d.high = true;
				step(t, hi - lo);
			}
		}
	}
	d.v = std::min(d.v, m_p.vdd - m_p.vss); // no resets: the capacitor sits at its top
	const double naive = pulse ? (d.high ? hi : lo) : saw(d.v);
	const double out = d.pendingBlep + corrPrev;
	d.pendingBlep = naive + corrCur;
	return out;
}

float Voice::process(const VoiceFrame &f, float filterIn, double volumePot)
{
	const auto &p = m_p;
	if (!m_started) {
		// a load or a wake: the buffers at their codes at once, the filter from rest
		for (int c = 0; c < kCvChannels; ++c)
			m_cv[size_t(c)] = dacVolts(f.dac[size_t(c)]);
		m_volA = p.vss + 5.0 * f.volA / 255.0;
		m_volB = p.vss + 5.0 * f.volB / 255.0;
		m_s1->solveDc();
		m_s2->solveDc();
		m_started = true;
	}
	const double iref = -p.vss; // each converter's reference: AGND (virtual ground) to VSS
	float subs[2];
	for (int s = 0; s < 2; ++s) {
		// the S&H buffers settle on their codes
		for (int c = 0; c < kCvChannels; ++c)
			m_cv[size_t(c)] += m_cvCoef * (dacVolts(f.dac[size_t(c)]) - m_cv[size_t(c)]);
		m_volA += m_volCoef * (p.vss + 5.0 * f.volA / 255.0 - m_volA);
		m_volB += m_volCoef * (p.vss + 5.0 * f.volB / 255.0 - m_volB);

		// the converters, each from its divider's Thevenin voltage
		auto divider = [](double v, double rin, double rg) { return v * rg / (rin + rg); };
		const double ia = expoCurrent(divider(m_cv[kOscACv], p.dcoRin, p.dcoRgnd), iref / p.dcoRref, p);
		const double ib = expoCurrent(divider(m_cv[kOscBCv], p.dcoRin, p.dcoRgnd), iref / p.dcoRref, p);
		const double ivcf = expoCurrent(divider(m_cv[kVcfCv], p.vcfRin, p.vcfRgnd), iref / p.vcfRref, p) + p.vcfFloor;
		const double ivca = expoCurrent(divider(m_cv[kVcaCv], p.vcaRin, p.vcaRgnd), iref / p.vcaRref, p);
		const double ires = std::max(p.resFloor, (m_cv[kResCv] - p.vss - p.abcDrop) / p.r58);
		const double iabcA = std::max(0.0, (m_volA - p.vss - p.abcDrop) / p.r35);
		const double iabcB = std::max(0.0, (m_volB - p.vss - p.abcDrop) / p.r35);
		const double abcTarget = std::max(0.0, ivca - p.abcDrop / p.r79);
		m_vcaAbc += m_vcaCoef * (abcTarget - m_vcaAbc);
		if (abcTarget == 0.0 && m_vcaAbc < 1e-12)
			m_vcaAbc = 0; // closed: C60 empties through R79

		// the DCOs: this sub-sample's resets, the ramps' slopes
		float ra[VoiceFrame::kMaxEdges], rb[VoiceFrame::kMaxEdges];
		int na = 0, nb = 0;
		for (int i = 0; i < f.nEdgesA; ++i)
			if (const double t = 2.0 * f.edgesA[size_t(i)] - s; t >= 0 && t < 1)
				ra[na++] = float(t);
		for (int i = 0; i < f.nEdgesB; ++i)
			if (const double t = 2.0 * f.edgesB[size_t(i)] - s; t >= 0 && t < 1)
				rb[nb++] = float(t);
		const double thrA = (m_cv[kOscAPwm] - p.vss) * p.r31 / (p.r31 + p.r32);
		const double thrB = (m_cv[kOscBPwm] - p.vss) * p.r31 / (p.r31 + p.r32);
		const double oa = runDco(m_a, ia * m_dt / p.dcoCap, ra, na, 0, 0, f.osc1Pulse, thrA) + p.vss;
		const double ob = runDco(m_b, ib * m_dt / p.dcoCap, rb, nb, 0, 0, f.osc2Pulse, thrB) + p.vss;

		// the mixer OTAs into OSC_MIX, and FILTER INPUT
		const double k = p.r34 / (p.r33 + p.r34);
		double imix = iabcA * std::tanh(oa * k / (2 * kVt)) + iabcB * std::tanh(ob * k / (2 * kVt));
		imix += filterIn * p.extVolts * (1.0 - f.mixPot) / p.rExt;

		// the filter
		m_s1->setInput(m_s1Mix, imix);
		m_s1->setInput(m_s1A1, ivcf / 4);
		m_s1->setInput(m_s1A2, ivcf / 4);
		m_s1->setInput(m_s1Ar, ires);
		m_s1->step();
		const double out1 = m_s1->output(m_s1Out);
		m_s2->setInput(m_s2In, out1);
		m_s2->setInput(m_s2A1, ivcf / 4);
		m_s2->setInput(m_s2A2, ivcf / 4);
		m_s2->setInput(m_s2Ar, ires);
		m_s2->step();
		const double out2 = m_s2->output(m_s2Out);
		const double vcaIn = f.slope12 ? out1 : out2;

		// the VCA into U22C (inverting), C38
		const double vk = p.r74 / (p.r73 + p.r74);
		const double v = -m_vcaAbc * std::tanh(vcaIn * vk / (2 * kVt)) * p.r80;
		m_hp = m_hpCoef * (m_hp + v - m_hpIn);
		m_hpIn = v;
		subs[s] = float(m_hp);
	}
	// VOLUME: an audio-taper pot [F: 10% of the track at mid rotation]
	const double vol = volumePot <= 0.5 ? volumePot * 0.2 : 0.1 + (volumePot - 0.5) * 1.8;
	const float out = float(m_decim.process(subs) * vol / p.outVolts);

	// settled: the VCA closed with its CV at rest, the output under -120 dBFS
	const bool cvAtRest = std::abs(m_cv[kVcaCv] - dacVolts(f.dac[kVcaCv])) < 1e-4;
	m_quiet = (m_vcaAbc <= 0.0 && cvAtRest && std::abs(out) < 1e-6f) ? std::min(m_quiet + 1, m_quietLimit) : 0;
	return out;
}

} // namespace bsrack
