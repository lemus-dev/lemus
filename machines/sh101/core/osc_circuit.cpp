// The oscillator section's circuit blocks. Values and topology: docs/circuit-osc.md; the model as
// built and its validation: docs/model-osc.md.
#include "osc_circuit.h"

#include <algorithm>
#include <cmath>

namespace sh101::oscm {

double taperB(double t) { return std::clamp(t, 0.0, 1.0); }

double taperA(double t)
{
	// (b^t - 1) / (b - 1) passes 15 % at half travel when sqrt(b) = 1 / 0.15 - 1.
	constexpr double s = 1 / 0.15 - 1, b = s * s;
	t = std::clamp(t, 0.0, 1.0);
	return (std::pow(b, t) - 1) / (b - 1);
}

Thevenin potWiper(double vTop, double rSrc, double vBot, double rPot, double k)
{
	const double rt = (1 - k) * rPot + rSrc, rb = k * rPot;
	if (rb <= 0)
		return {vBot, 0};
	if (rt <= 0)
		return {vTop, 0};
	return {vBot + (vTop - vBot) * rb / (rt + rb), rt * rb / (rt + rb)};
}

double opampClamp(double v, double vNeg, double vPos) { return std::clamp(v, vNeg + 1.5, vPos - 1.5); }

// ---------------------------------------------------------------------------------------------
// Portamento

void Portamento::prepare(double rate)
{
	m_dt = 1 / rate;
	coefficients();
}

void Portamento::reset(double volts)
{
	m_v = volts;
	m_vc11 = 0;
	m_vx = 0;
	m_busy = int(0.01 / m_dt);
}

void Portamento::set(double knob, PortaMode mode, double tauMax)
{
	// The wiper is tied to the PORTA IN end, so the glide resistor is the track from the wiper to
	// the C10 end: the A taper, plus the track's end resistance.
	m_rPot = kRpot * (kRend + (1 - kRend) * taperA(knob));
	m_tauScale = tauMax / (kRpot * kC10);
	if (mode != m_mode)
		m_busy = int(0.01 / m_dt);
	m_mode = mode;
	coefficients();
}

void Portamento::coefficients()
{
	const double c = kC10 * m_tauScale;
	m_kOff = 1 - std::exp(-m_dt / (m_rPot * c));
	// TR1's channel near V_DS = 0 is 1 / (2 beta |VP|): keep the explicit steps well inside it.
	const double tauOn = c / (2 * kBeta * kVp);
	m_subSteps = std::max(1, int(std::ceil(m_dt / (0.1 * tauOn))));
}

double Portamento::tick(double kcv, bool gate, bool portaOff)
{
	// Node X: C11 from GATE IN through R15, R16 from PORTA OFF; in AUTO it is TR5's base, with
	// R23 to ground and D2 below it. C11 is solved by backward Euler: a source (gate - vc11)
	// behind R15 + dt/C11.
	const double vg = gate ? kGateHigh : 0, vpo = portaOff ? kPortaOffHigh : 0;
	const double re = kR15 + m_dt / kC11, src = vg - m_vc11;
	const bool auto_ = m_mode == PortaMode::Auto;
	// After an edge on GATE IN or PORTA OFF the node settles within a few C11 time constants; past
	// that it is left where it settled.
	if (gate != m_lastGate || portaOff != m_lastPortaOff) {
		m_busy = int(0.01 / m_dt);
		m_lastGate = gate;
		m_lastPortaOff = portaOff;
	}
	double vx;
	bool tr5 = false;
	if (auto_ && m_busy <= 0) {
		vx = m_vx;
		tr5 = m_tr5;
	} else if (auto_) {
		// TR5's base-emitter and D2 as diodes; TR4 (and so TR1) turns on once TR5's collector
		// current covers R18's 0.6 V / 22k, about 30 µA (V_BE 0.53 V).
		// Saturated (its collector can sink no more than R19 lets through, about 134 µA), TR5's
		// base takes the rest through the base-collector junction.
		constexpr double is = 3.3e-14, beta = 300, isd = 2.52e-9, nd = 1.752, icMax = (kV14 - 0.6) / 100e3;
		vx = m_vx;
		for (int k = 0; k < 8; ++k) {
			const double eb = std::exp(std::min(vx, 0.9) / kVT), ed = std::exp(std::min(-vx, 0.9) / (nd * kVT));
			const double sat = is * eb > icMax;
			const double ib = is / beta * (eb - 1) + (sat ? is * eb - icMax : 0);
			const double dib = is / beta * eb / kVT + (sat ? is * eb / kVT : 0);
			const double f = (src - vx) / re + (vpo - vx) / kR16 - vx / kR23 - ib + isd * (ed - 1);
			const double df = -1 / re - 1 / kR16 - 1 / kR23 - dib - isd * ed / (nd * kVT);
			vx = std::clamp(vx - f / df, -1.0, 0.8);
		}
		tr5 = is * std::exp(vx / kVT) > 30e-6;
		--m_busy;
	} else {
		vx = (src / re + vpo / kR16) / (1 / re + 1 / kR16);
	}
	m_vc11 += m_dt / kC11 * (src - vx) / re;
	m_vx = vx;
	m_tr5 = tr5;
	// ON: TR5's base is left to R23 (glide always). OFF: R24 holds it on (no glide).
	m_on = m_mode == PortaMode::Off || (auto_ && tr5);
	if (!m_on) {
		m_v += (kcv - m_v) * m_kOff;
		return m_v;
	}
	// TR1 on: its gate follows PORTA IN through R17, so the channel is a current-limited switch.
	// Discharging C10, PORTA IN is the source and V_GS = 0: at most I_DSS. Charging, C10's side is
	// the source and the gate junction lifts V_GS to about 0.6 V.
	const int sub = m_subSteps;
	const double h = m_dt / sub, c = kC10 * m_tauScale;
	for (int k = 0; k < sub; ++k) {
		const double vds = std::abs(kcv - m_v);
		const double vgs = kcv > m_v ? std::min(vds, 0.6) : 0.0;
		const double vov = vgs + kVp;
		const double i = vds < vov ? kBeta * (2 * vov * vds - vds * vds) : kBeta * vov * vov * (1 + kLambda * (vds - vov));
		const double dv = (kcv > m_v ? i : -i) + (kcv - m_v) / m_rPot;
		m_v += h * dv / c;
	}
	return m_v;
}

// ---------------------------------------------------------------------------------------------
// Bender board

void Bender::prepare(double rate)
{
	m_dt = 1 / rate;
	m_kRelease = std::exp(-m_dt / (kR27 * kC9));
	// VR3 LFO MOD OFFSET: IC2a's output at the bus with the MOD released (no tail current), which
	// needs VR3's wiper at the voltage pin 3 settles to (circuit-osc.md 6.3).
	m_vr3 = (kV15 / kR10 + kVref / kR8) / (1 / kR10 + 1 / kR8);
	reset();
}

void Bender::reset() { m_v9 = 0; }

void Bender::set(const Controls &c, const Performance &p, double leverVolts)
{
	m_lever = std::clamp(p.bend, -1.0, 1.0) * leverVolts;
	m_modOn = p.modPush >= 0.5;
	m_kBendVco = taperB(c.bendVco);
	m_kBendVcf = taperB(c.bendVcf);
	m_kLfo = taperA(c.lfoModDepth);
}

Bender::Out Bender::tick(double triVolts, const Thevenin &vcoModIn)
{
	Out o;
	// TR6 holds C9 empty through R27 until the MOD switch opens its base; then C9 charges through
	// R22 from +14 V, less TR3's base current (TR3 follows C9 into R25). TR3's collector is TR2's
	// emitters, a V_BE below the bus, so once C9 reaches about 5 V TR3 saturates and C9 stops there:
	// the fade-in is the first 0.2 s of R22 C9's rise, and the tail current tops out near 57 µA.
	constexpr double kVbe3 = 0.58, kVceSat = 0.03;
	const double veMax = kVref - 0.51 - kVceSat;
	if (m_modOn) {
		const double ve = m_v9 - kVbe3;
		if (ve >= veMax) {
			m_v9 = veMax + kVbe3;
		} else {
			const double ib = std::max(ve, 0.0) / (kR25 * (kBeta + 1));
			m_v9 += m_dt / kC9 * ((kV14 - m_v9) / kR22 - ib);
		}
	} else {
		m_v9 *= m_kRelease;
	}
	const double ve = std::min(m_v9 - kVbe3, veMax);
	if (ve >= veMax && m_modOn) // saturated: R22's current leaves through TR3's emitter too
		o.tail = std::max(veMax / kR25 - (kV14 - m_v9) / kR22, 0.0);
	else
		o.tail = kBeta / (kBeta + 1) * std::max(ve, 0.0) / kR25;
	// TR2's left base sees the triangle through R13 / R7 against the bus, its right base the bus.
	// With no tail current (the MOD released and C9 below a V_BE) the pair carries nothing.
	const double vd = (triVolts - kVref) * kR7 / (kR13 + kR7);
	const double th = o.tail != 0 ? std::tanh(vd / (2 * kVT)) : 0.0;
	const double i1 = o.tail * 0.5 * (1 + th), i2 = o.tail - i1;
	// IC2a's pin 3: R10 from +15 V (SN p.5's half-filled arrow on R10 and R11, though the rest of
	// the bender board is on +14 V), R8 to the bus, R9 to the trim, and TR2's left collector.
	const double v3 = (kV15 / kR10 + kVref / kR8 + m_vr3 / kR9 - i1) / (1 / kR10 + 1 / kR8 + 1 / kR9);
	// The difference amplifier: R12 converts the collectors' difference (TL062 on +14 / -5 V).
	o.ic2a = std::clamp(kVref - kR12 * (i1 - i2) + kR12 / kR9 * (m_vr3 - v3), -5.0 + 1.5, kV14 - 1.5);
	const Thevenin lfo = potWiper(o.ic2a, 0, kVref, 100e3, m_kLfo);
	// IC3b buffers the lever; VR4 and VR5 take the depths (IR9022 on +14 V / ground).
	const double ic3b = opampClamp(kVref + m_lever, 0, kV14);
	const Thevenin vco = potWiper(ic3b, 0, kVref, 100e3, m_kBendVco);
	o.vcfBend = potWiper(ic3b, 0, kVref, 100e3, m_kBendVcf);
	o.vcfLfo = lfo;
	const double i = into(vcoModIn, kVref, kR1) + into(lfo, kVref, kR2) + into(vco, kVref, kR4);
	o.ic3a = opampClamp(kVref - kR3 * i, 0, kV14);
	return o;
}

// ---------------------------------------------------------------------------------------------
// IC12b

void PitchMixer::prepare(double rate) { m_k = 1 - std::exp(-1 / (rate * kR89 * kC35)); }
void PitchMixer::reset() { m_y = 0; }

double PitchMixer::tick(double ic3a, const Thevenin &tune)
{
	const double target = -kR89 * ((ic3a - kVref) / kR91 + (tune.v - kVref) / (kR90 + tune.r));
	m_y += (target - m_y) * m_k;
	return opampClamp(kVref + m_y, 0, kV14); // IC12b on +14 V / ground (SN p.7's filled arrow)
}

// ---------------------------------------------------------------------------------------------
// CEM3340

double Cem3340::idealHz(double ic) const
{
	// f = 3 I_EG / (2 VCC CF) with I_EG = I_REF exp(-I_OM RS / VT), I_OM = (22 VT / RT)(1 - I_C RZ / 3):
	// VT cancels, and so does VCC (I_REF = VCC / R101).
	constexpr double fRef = 3 / (2 * kRref * kCF);
	if (ic == m_icLast && rS == m_rsLast)
		return m_hzLast; // a held CV: the same frequency
	m_icLast = ic;
	m_rsLast = rS;
	m_hzLast = fRef * std::exp(-22 * rS / kRT * (1 - std::max(ic, 0.0) * kRZ / 3.0));
	return m_hzLast;
}

double Cem3340::hz(double ic) const
{
	const double f = idealHz(ic);
	return f / (1 + tDelay * f);
}

double Cem3340::hfTrack(double idealHz)
{
	// Pin 7 sources I_EG / 4 into R199; R100 feeds that voltage back into pin 15 (a virtual ground).
	const double iEg = idealHz * 2 * kVCC * kCF / 3;
	return iEg / 4 * kR199 / kR100;
}

double Cem3340::current(double vCv, double vRange, double vIc12b, double lastIdealHz) const
{
	return vCv / kR104 + vRange / rRange + vIc12b / kR103 + vVr7 / kR108 + hfTrack(lastIdealHz);
}

// ---------------------------------------------------------------------------------------------
// PWM

double PwmNetwork::vbe(double ie) { return kVT * std::log(std::max(ie, 1e-12) / 3.3e-14 + 1); } // 2SC1815: IS 33 fA (0.62 V at 1 mA)

double PwmNetwork::pin5(PwmSource src, double slider, double triVolts, double envVolts)
{
	// The same inputs from a V_BE the last call left unchanged give the same result.
	const double in = src == PwmSource::Lfo ? triVolts : src == PwmSource::Env ? envVolts : 0.0;
	if (src == m_src && slider == m_slider && in == m_in && vr4 == m_vr4 && m_vbe == m_vbeIn && m_vbe == m_vbeOut)
		return m_out;
	m_src = src;
	m_slider = slider;
	m_in = in;
	m_vr4 = vr4;
	m_vbeIn = m_vbe;
	Thevenin s;
	switch (src) {
	case PwmSource::Manual: s = {kV14, kR178}; break;
	case PwmSource::Lfo: {
		const double g = 1 / kR176 + 1 / kR177;
		s = {(triVolts / kR176 + kV14 / kR177) / g, 1 / g};
		break;
	}
	case PwmSource::Env: s = {envVolts, kR190}; break;
	}
	const double vw = potWiper(s.v, s.r, kVref, 100e3, taperB(slider)).v;
	const double x = kR94 + vr4;
	// TR25's V_BE at its emitter current, from the last call's (it moves slowly).
	const double ve = std::max(vw - vbe(std::max(vw - m_vbe, 0.0) / (kR96 + x)), 0.0);
	m_vbe = vw - ve > 0.3 ? vw - ve : 0.55;
	m_vbeOut = m_vbe;
	m_out = ve * x / (kR96 + x);
	return m_out;
}

double PwmNetwork::trimVr4(double vHalf)
{
	// PULSE WIDTH at 0 puts the wiper on the bus whatever S4 says.
	double x = 128e3;
	for (int k = 0; k < 5; ++k) {
		const double ve = kVref - vbe((kVref - 0.55) / (kR96 + x));
		x = kR96 * vHalf / (ve - vHalf);
	}
	return std::clamp(x - kR94, 0.0, 50e3);
}

double PwmNetwork::tau() const
{
	const double x = kR94 + vr4;
	return kC37 * kR96 * x / (kR96 + x);
}

// ---------------------------------------------------------------------------------------------
// Waveform switch

double ModSwitch::thresholdHigh(LfoWave w)
{
	// The TR5/TR6 collectors with both off: R35 from +15 V, R32 into TR8's base, and R31 to pin 11,
	// which S2 loads with R169 (TRIANGLE, RANDOM) or R188 through IC18a's input (SQUARE) to the
	// bus, or leaves open (NOISE).
	double g = 1 / kR35 + 1 / kR32, gv = kV15 / kR35 + 0.62 / kR32;
	if (w != LfoWave::Noise) {
		g += 1 / (kR31 + kR169);
		gv += kVref / (kR31 + kR169);
	}
	return gv / g;
}

double ModSwitch::square(bool clk)
{
	// CLK high = TR5 on = the node at saturation.
	const double node = clk ? kVsat : thresholdHigh(LfoWave::Square);
	return kVref + (node - kVref) * kR188 / (kR31 + kR188);
}

double ModSwitch::random(int code)
{
	const double v = std::min(2.0 * code / 12.0, kV14 - 1.5); // IC11a (TL062 on +14 / -5 V), gain 2
	return kVref + (v - kVref) * kR188 / (kR175 + kR188);
}

// ---------------------------------------------------------------------------------------------
// Noise

void Noise::prepare(double rate)
{
	m_rate = rate;
	set(m_rms, m_cornerHz);
	reset();
}

void Noise::reset()
{
	m_lp = m_sh = m_x1 = 0;
	m_haveSpare = false;
}

void Noise::set(double rmsVolts, double cornerHz)
{
	m_rms = rmsVolts;
	m_cornerHz = cornerHz;
	m_a1 = std::exp(-2 * kPi * cornerHz / m_rate);
	// IC12a: (1 + (R86 ∥ C32) / R88) = (23 + s/w2) / (1 + s/w2), bilinear with w2 prewarped.
	const double w2 = 1 / (kR86 * kC32), g = 1 + kR86 / kR88;
	const double kk = 1 / std::tan(w2 / (2 * m_rate));
	m_b0 = (g + kk) / (1 + kk);
	m_b1 = (g - kk) / (1 + kk);
	m_a = (1 - kk) / (1 + kk);
	// The RMS of the shaped noise for unit-variance white noise, from the filters' response.
	double sum = 0;
	constexpr int n = 4096;
	for (int i = 0; i < n; ++i) {
		const double w = kPi * (i + 0.5) / n;
		const double c = std::cos(w), s = std::sin(w);
		// one-pole: (1 - a1) / (1 - a1 z^-1)
		const double lpRe = 1 - m_a1 * c, lpIm = m_a1 * s;
		const double lp2 = (1 - m_a1) * (1 - m_a1) / (lpRe * lpRe + lpIm * lpIm);
		const double nRe = m_b0 + m_b1 * c, nIm = -m_b1 * s, dRe = 1 + m_a * c, dIm = -m_a * s;
		const double sh2 = (nRe * nRe + nIm * nIm) / (dRe * dRe + dIm * dIm);
		sum += lp2 * sh2;
	}
	const double unit = std::sqrt(sum / n);
	m_sigma = unit > 0 ? rmsVolts / unit : 0;
}

double Noise::uniform()
{
	// xorshift128+
	uint64_t s1 = m_s[0];
	const uint64_t s0 = m_s[1];
	m_s[0] = s0;
	s1 ^= s1 << 23;
	m_s[1] = s1 ^ s0 ^ (s1 >> 17) ^ (s0 >> 26);
	return double((m_s[1] + s0) >> 11) * (1.0 / 9007199254740992.0);
}

double Noise::gauss()
{
	if (m_haveSpare) {
		m_haveSpare = false;
		return m_spare;
	}
	const double u1 = std::max(uniform(), 1e-300), u2 = uniform();
	const double r = std::sqrt(-2 * std::log(u1));
	m_spare = r * std::sin(2 * kPi * u2);
	m_haveSpare = true;
	return r * std::cos(2 * kPi * u2);
}

double Noise::tick()
{
	const double x = gauss() * m_sigma;
	m_lp = (1 - m_a1) * x + m_a1 * m_lp;
	const double y = m_b0 * m_lp + m_b1 * m_x1 - m_a * m_sh;
	m_x1 = m_lp;
	m_sh = y;
	return kVref + y; // R87 / R88 / C31 return to the bus, so IC12a idles there
}

// ---------------------------------------------------------------------------------------------
// BLEP

double Blep::residual(double x)
{
	// B is the integral of the cubic B-spline kernel (support -2..2); the residual is B minus the
	// unit step.
	double b;
	if (x <= -2)
		b = 0;
	else if (x <= -1) {
		const double u = 2 + x;
		b = u * u * u * u / 24;
	} else if (x <= 0)
		b = 1.0 / 24 + (4 * x - 2 * x * x * x - 0.75 * x * x * x * x + 2.75) / 6;
	else if (x <= 1) {
		const double y = -x;
		b = 1 - (1.0 / 24 + (4 * y - 2 * y * y * y - 0.75 * y * y * y * y + 2.75) / 6);
	} else if (x < 2) {
		const double u = 2 - x;
		b = 1 - u * u * u * u / 24;
	} else
		b = 1;
	return b - (x >= 0 ? 1 : 0);
}

void Blep::reset(double level)
{
	m_pos = 0;
	m_buf[0] = m_buf[1] = level;
	m_buf[2] = m_buf[3] = 0;
}

void Blep::step(double h, double frac)
{
	m_buf[m_pos] += h * residual(-1 - frac);
	m_buf[(m_pos + 1) & 3] += h * residual(-frac);
	m_buf[(m_pos + 2) & 3] += h * residual(1 - frac);
	m_buf[(m_pos + 3) & 3] += h * residual(2 - frac);
}

double Blep::push(double naive)
{
	m_buf[(m_pos + 2) & 3] += naive;
	const double out = m_buf[m_pos];
	m_buf[m_pos] = 0;
	m_pos = (m_pos + 1) & 3;
	return out;
}

// ---------------------------------------------------------------------------------------------
// VCO

void Vco::prepare(double rate)
{
	m_rate = rate;
	reset();
}

void Vco::reset()
{
	m_phase = 0;
	m_lastPw = 0.5;
	m_pulse = false;
	m_q1 = m_q2 = false;
	m_nEdges = 0;
}

void Vco::tick(double hz, double pwPhase)
{
	m_nEdges = 0;
	m_q1Changed = m_q2Changed = false;
	const double dp = std::clamp(hz / m_rate, 0.0, 0.45);
	const double p0 = m_phase, pw0 = m_lastPw, pw1 = pwPhase;
	double p1 = p0 + dp;
	// The pulse is high while phase >= pw: track sign changes of g = phase - pw, which moves
	// linearly within the sample; the wrap resets the phase to 0.
	auto cross = [&](double ga, double gb, double fa, double fb) {
		// One segment [fa, fb] of the interval with g going from ga to gb.
		const bool hi = gb >= 0;
		if (hi != m_pulse) {
			const double t = ga != gb ? std::clamp(ga / (ga - gb), 0.0, 1.0) : 1.0;
			addEdge(fa + (fb - fa) * t, hi ? Edge::Rise : Edge::Fall);
			m_pulse = hi;
		}
	};
	if (p1 >= 1) {
		const double fw = std::clamp((1 - p0) / dp, 1e-9, 1.0);
		const double pww = pw0 + (pw1 - pw0) * fw;
		cross(p0 - pw0, 1 - pww, 0, fw);
		addEdge(fw, Edge::Wrap);
		// At the wrap the phase drops to 0: the pulse falls unless pw <= 0.
		if (m_pulse && pww > 0) {
			addEdge(fw, Edge::Fall);
			m_pulse = false;
		}
		p1 -= 1;
		cross(0 - pww, p1 - pw1, fw, 1);
		// The 4013: Q1 toggles at the wrap (TR41 lets go of the clock as the saw drops through
		// 4.6 V); Q2 toggles on Q1's rising edge.
		m_q1 = !m_q1;
		m_q1Changed = true;
		if (m_q1) {
			m_q2 = !m_q2;
			m_q2Changed = true;
		}
		m_wrapFrac = fw;
	} else {
		cross(p0 - pw0, p1 - pw1, 0, 1);
		m_wrapFrac = 0;
	}
	m_phase = p1;
	m_lastPw = pw1;
}

void Vco::addEdge(double frac, Edge::Kind k)
{
	if (m_nEdges < kMaxEdges)
		m_edges[m_nEdges++] = {std::max(frac, 1e-9), k};
}

// ---------------------------------------------------------------------------------------------
// Sub-oscillator and pulse levels

namespace {
// 1S2473 (1N4148 class): IS 2.52 nA, N 1.752.
double diodeDrop(double i) { return 1.752 * kVT * std::log(std::max(i, 1e-15) / 2.52e-9 + 1); }

Thevenin orStage(int nHigh)
{
	// nHigh flip-flop outputs at +14 V (the 4013's supply) through 47k each and a diode, into VR15's 100k.
	if (nHigh == 0)
		return {0, 1e12};
	const double r = 47e3 / nHigh;
	double v = 9;
	for (int k = 0; k < 8; ++k)
		v = (kV14 - diodeDrop(v / 100e3 / nHigh)) * 100e3 / (100e3 + r);
	const double vd = diodeDrop(v / 100e3 / nHigh);
	return {kV14 - vd, r};
}
} // namespace

Thevenin SubLevels::top(SubMode mode, bool q1, bool q2)
{
	switch (mode) {
	case SubMode::Oct1Square: return {q1 ? kV14 : 0.0, 47e3};
	case SubMode::Oct2Square: return {q2 ? kV14 : 0.0, 47e3};
	case SubMode::Oct2Pulse: return orStage(int(q1) + int(q2));
	}
	return {0, 1e12};
}

Thevenin PulseStage::top(bool high) { return high ? Thevenin{kV14, kR92} : Thevenin{kVsat, 20}; }

} // namespace sh101::oscm
