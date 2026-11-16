// The SH-101's filter section: the cutoff CV summer and expo, the IR3109 VCF, the ENV
// generator and the BA662 VCA with the output stage (docs/model-filter.md).
#include "filter.h"

#include "env_gen.h"
#include "osc_circuit.h"
#include "vca_ba662.h"
#include "vcf_ir3109.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

namespace {
constexpr double kPi = 3.14159265358979323846;
// The rails (core/osc_circuit.h, docs/circuit-filter.md 1): +15 V is IC1b's output (14.87 V), the
// half-filled arrow on SN p.7: FREQ's top and R187 only. +14 V is TR3's emitter (14.3 V), the
// filled arrow: the ENV GEN, IC16 and IC18's supply, R195 (KYBD), R196 (S6), TR26/TR27, the BA662.
constexpr double kVref = 5.0, kV14 = oscm::kV14;
// IC18b (control board): an inverting summer about the bus, R192 33k (docs/circuit-filter.md 4).
constexpr double kR192 = 33e3, kR186 = 200e3, kR187 = 680e3, kR185 = 100e3, kR184 = 220e3;
constexpr double kR189 = 100e3, kR191 = 100e3, kR193 = 150e3, kR195 = 33e3;
// The pin-9 divider.
constexpr double kR128 = 4.7e3, kR127 = 5.6e3, kR126 = 1.8e3, kR125 = 820;
constexpr double kExpoSlewHz = 5e3; // OML: the CV input slew-limits above about 4 kHz
// The IR3109's compression at the top of its range (expoCurrent), fitted to a Juno-106 voice: a
// knee of order 3 (cube roots).
constexpr double kExpoKneeAmps = 408e-6;

double taperB(double x) { return std::clamp(x, 0.0, 1.0); }

// A pot as a Thevenin source: wiper fraction k of the voltage across it, R(k) = Rpot k(1-k).
double potCurrent(double volts, double k, double rPot, double rSeries)
{
	return volts * k / (rSeries + rPot * k * (1 - k));
}

// The KYBD path to R193: the post-glide KCV through VR8 into VR21 100k B to ground, TR42 (2SA1015
// follower, R195 33k from +14 V). Returns TR42's emitter; vEb carries its V_EB from one call to
// the next (three fixed-point steps from there are plenty).
double kybdEmitter(double kcvVolts, double k21, double vr8, double &vEb)
{
	const double vth = kcvVolts * 100e3 * k21 / (100e3 + vr8);
	const double rth = 1 / (1 / (100e3 * k21 + 1e-3) + 1 / (vr8 + 100e3 * (1 - k21) + 1e-3));
	double ve = vth + vEb;
	for (int k = 0; k < 3; ++k) {
		const double ie = std::max((kV14 - ve) / kR195 - (ve - kVref) / kR193, 1e-6);
		// 2SA1015: V_BE 0.62 V at 1 mA; its base current flows out into the wiper.
		vEb = 0.025693 * std::log(ie / 3.3e-14);
		ve = vth + ie / 301 * rth + vEb;
	}
	return ve;
}
} // namespace

FilterCalibration FilterCalibration::serviceNotes() { return {}; }
// A real unit (docs/model.md, "Calibrations"): the IR3109's expo slope as measured on a chip
// (Open Music Labs, 20.8 mV/oct) with its reference set so FREQ 0 sits where a real unit's does
// (its ENV plateau at 4.8 kHz and rest near 31 Hz at FREQ 0, KYBD 10, KCV 3.0 V); the ENV's weight
// at the cutoff summer set to that unit's 7.27-octave sweep, which two more units confirm
// (0.80-0.82); its ENV pots 0.9 x nominal (release 4.2 s, attack plateau 3.3 s). VR8 is trimmed by
// the service procedure for these values.
FilterCalibration FilterCalibration::realUnit()
{
	FilterCalibration c = serviceNotes();
	c.vcfMvPerOct = 20.8;
	c.vcfI0uA = 5.0;
	c.envCvScale = 0.82;
	c.envPotScale = 0.9;
	return c;
}

// Fitted to Roland's SH-101 plug-in (tools/compare/fit_roland.py, docs/model.md): its FREQ law
// spans 12.2 octaves where the circuit with the measured rails spans 13.1 at the real unit's
// 20.8 mV/oct, so the expo takes 22.1 mV/oct and the reference current that centres the law on
// Roland's (3.9 uA); the ENV's weight keeps ENV 10 at 7.4 octaves (Roland 7.65; the depth at
// lower ENV settings follows a different law) and the ENV pots follow its decay and release
// curves (best on average at 0.8; its longest times follow a steeper law).
FilterCalibration FilterCalibration::roland()
{
	FilterCalibration c = realUnit();
	c.vcfMvPerOct = 22.1;
	c.vcfI0uA = 3.9;
	c.envCvScale = 0.89;
	c.envPotScale = 0.8;
	return c;
}

bool FilterCalibration::set(const std::string &name, double v)
{
	if (name == "vcfMvPerOct") vcfMvPerOct = v;
	else if (name == "vcfI0uA") vcfI0uA = v;
	else if (name == "th1Ohms") th1Ohms = v;
	else if (name == "vr8Ohms") vr8Ohms = v;
	else if (name == "v15Rail") v15Rail = v;
	else if (name == "envPotScale") envPotScale = v;
	else if (name == "envCvScale") envCvScale = v;
	else if (name == "envPotMid") envPotMid = v;
	else if (name == "vcaOffsetMv") vcaOffsetMv = v;
	else if (name == "vcaKneeShiftVolts") vcaKneeShiftVolts = v;
	else if (name == "loadOhms") loadOhms = v;
	else return false;
	return true;
}

struct FilterSection::Impl {
	Controls c;
	FilterCalibration cal;
	Performance perf;
	CpuLines lines;
	double rate = 176400, t = 1 / 176400.0;

	Ir3109Vcf vcf;
	EnvGen env;
	Ba662Vca vca;

	// Derived from the calibration.
	double divRatio = 0.111, vExpo = 0.0274, i0 = 2.4e-6, iPeak = 0.313e-3, vr8 = 28.58e3;
	// State.
	double v9 = 0, v9Lp = 0, iAbcVcf = 0, iVca = 0, envV = kVref;
	double kSlew = 1; // pin 9's one-pole, per sample (prepare)
	double kVcfMod = 0; // VR20's A taper at the control's travel (applyControls)
	double vEb42 = 0.62;
	// TR42's last solution. Once its V_EB comes back unchanged, the same inputs give the same
	// emitter voltage again, so it isn't solved again until an input moves.
	double kybdKcv = 0, kybdK21 = 0, kybdVr8 = 0, kybdVe = 0;
	bool kybdFixed = false;
	// expoCurrent's last input and output, and ln(I0 / I_knee).
	double expoIn = 0, expoOut = -1, lnI0Knee = 0;
	// The circuit's own noise at the mixer node (resistors, the OTAs' inputs): far below anything
	// audible, it is what starts self-oscillation with every source at 0, as in the real unit. A
	// fixed-seed generator keeps renders repeatable.
	static constexpr double kNoiseVolts = 20e-6; // RMS
	uint32_t noiseState = 0x9e3779b9u;
	double circuitNoise()
	{
		double s = 0;
		for (int k = 0; k < 4; ++k) {
			noiseState ^= noiseState << 13;
			noiseState ^= noiseState >> 17;
			noiseState ^= noiseState << 5;
			s += double(noiseState) / 4294967296.0 - 0.5;
		}
		return s * kNoiseVolts * std::sqrt(3.0); // four uniforms: variance 4/12
	}

	void applyCalibration()
	{
		const double rPar = 1 / (1 / kR127 + 1 / (kR126 + cal.th1Ohms));
		divRatio = kR125 / (kR128 + rPar + kR125);
		vExpo = cal.vcfMvPerOct * 1e-3 / std::log(2.0);
		// VR8 VCF WIDTH, trimmed as the service notes do unless the calibration fixes it: at KYBD
		// 10, F5's volt of KCV over F4's, through TR42 and R193 into R192 and the pin-9 divider,
		// must move pin 9 by one octave of the expo (F5 an octave above F4; the filter's own law is
		// straight there). The trim makes up TR42's own gain, about 0.995, as a technician trimming
		// by ear would. Out of VR8's 0-47k, it stops at the end.
		const double r8 = cal.vcfMvPerOct * 1e-3 / (kR192 / kR193 * divRatio);
		if (cal.vr8Ohms > 0) {
			vr8 = cal.vr8Ohms;
		} else {
			// F4 and F5 at TRANSPOSE M: KCV 29 and 41 twelfths of a volt. Secant on VR8.
			auto settled = [](double kcv, double r) {
				double vEb = 0.62, ve = 0;
				for (int k = 0; k < 4; ++k)
					ve = kybdEmitter(kcv, 1, r, vEb);
				return ve;
			};
			auto gain = [&settled](double r) { return settled(41 / 12.0, r) - settled(29 / 12.0, r); };
			double a = std::clamp(100e3 * (1 / r8 - 1), 1.0, 47e3), b = a * 0.98;
			double ga = gain(a) - r8, gb = gain(b) - r8;
			for (int k = 0; k < 20 && std::abs(gb) > 1e-9 && gb != ga; ++k) {
				const double next = b - gb * (b - a) / (gb - ga);
				a = b;
				ga = gb;
				b = std::clamp(next, 0.0, 47e3);
				gb = gain(b) - r8;
			}
			vr8 = std::clamp(b, 0.0, 47e3);
		}
		i0 = cal.vcfI0uA * 1e-6;
		lnI0Knee = std::log(i0) - std::log(kExpoKneeAmps);
		expoOut = -1;
		EnvGen::Params ep;
		ep.potOhms = 1e6 * cal.envPotScale;
		ep.potMid = cal.envPotMid;
		ep.v14 = kV14;
		env.setParams(ep);
		Ba662Vca::Params vp;
		vp.vosVolts = cal.vcaOffsetMv * 1e-3;
		vp.loadOhms = cal.loadOhms;
		vp.kneeShiftVolts = cal.vcaKneeShiftVolts;
		vca.setParams(vp);
		applyControls();
		// TR28's current at the ENV's peak, the 100 % of the vca() probe.
		Ba662Vca probe;
		probe.setParams(vp);
		iPeak = probe.envControlCurrent(env.threshold(), kV14);
	}

	void applyControls()
	{
		env.setControls(c.attack, c.decay, c.sustain, c.release, EnvGen::Mode(int(c.trigger)));
		vca.setVolume(c.volume);
		kVcfMod = EnvGen::taperA(c.vcfMod);
	}

	// IC18b's output against the bus, from every input of the cutoff summer.
	double summer(double envVolts, const OscFrame &osc)
	{
		double i = (cal.v15Rail - kVref) / kR187;
		// FREQ: VR17 100k B from +15 V to ground, R186 200k.
		const double w = taperB(c.vcfFreq);
		i += (cal.v15Rail * w - kVref) / (kR186 + 100e3 * w * (1 - w));
		// ENV: VR19 100k B from the ENV line to the bus, R189 100k.
		i += cal.envCvScale * potCurrent(envVolts - kVref, taperB(c.vcfEnv), 100e3, kR189);
		// MOD: VR20 100k A from IC18a (the MODULATOR wave) to the bus, R191 100k.
		i += potCurrent(osc.modVolts, kVcfMod, 100e3, kR191);
		// KYBD: TR42's emitter into R193 150k.
		i += (kybdVolts(osc.kybdVolts, taperB(c.vcfKybd)) - kVref) / kR193;
		// Bender board (the oscillator section models it): VCF BEND into R185, the grip's LFO MOD
		// through VR6 into R184, each a Thevenin source against the bus.
		i += osc.vcfBend.volts / (kR185 + osc.vcfBend.ohms);
		i += osc.vcfLfo.volts / (kR184 + osc.vcfLfo.ohms);
		// IR9022 on 0/+14 V: its output stays 2 V from either rail.
		return std::clamp(-kR192 * i, 2.0 - kVref, kV14 - 2.0 - kVref);
	}

	double kybdVolts(double kcv, double k21)
	{
		if (kybdFixed && kcv == kybdKcv && k21 == kybdK21 && vr8 == kybdVr8)
			return kybdVe;
		const double before = vEb42;
		kybdVe = kybdEmitter(kcv, k21, vr8, vEb42);
		kybdFixed = vEb42 == before;
		kybdKcv = kcv;
		kybdK21 = k21;
		kybdVr8 = vr8;
		return kybdVe;
	}

	double expoCurrent(double v9rel)
	{
		// A real IR3109's law, measured end to end on a Juno-106 voice (the same 68k, 560 R and
		// 240 pF per stage; docs/model-filter.md 2, "The law, from a Juno-106"): exponential within
		// a few cents from 6 Hz to about 10 kHz, then compressing, -32 / -72 / -131 cents at 15 /
		// 20 / 25 kHz. A knee of order 3 at 408 uA (39.6 kHz at this filter's 97 Hz/uA) fits it
		// within 2.5 cents RMS up to 28 kHz:
		//   I = I_knee q / (1 + q^3)^(1/3), with q = I0 e^(-V9/vExpo) / I_knee.
		if (v9rel == expoIn && expoOut >= 0)
			return expoOut;
		const double q = std::exp(lnI0Knee - v9rel / vExpo);
		expoIn = v9rel;
		expoOut = kExpoKneeAmps * q / std::cbrt(1 + q * q * q);
		return expoOut;
	}
};

FilterSection::FilterSection() : m(new Impl) {}
FilterSection::~FilterSection() { delete m; }

void FilterSection::prepare(double r)
{
	m->rate = r;
	m->t = 1 / r;
	m->kSlew = 1 - std::exp(-2 * kPi * kExpoSlewHz * m->t);
	m->vcf.prepare(r);
	m->env.prepare(r);
	m->vca.prepare(r);
	m->applyCalibration();
	reset();
}

void FilterSection::reset()
{
	m->vcf.reset();
	m->env.reset();
	m->vca.reset();
	m->v9Lp = 0;
	m->iAbcVcf = m->iVca = 0;
	m->envV = kVref;
	m->vEb42 = 0.62;
	m->kybdFixed = false;
	m->noiseState = 0x9e3779b9u;
}

void FilterSection::setControls(const Controls &c)
{
	m->c = c;
	m->applyControls();
}

void FilterSection::setCalibration(const FilterCalibration &c)
{
	m->cal = c;
	m->applyCalibration();
}

void FilterSection::setPerformance(const Performance &p) { m->perf = p; }
void FilterSection::setLines(const CpuLines &l) { m->lines = l; }
void FilterSection::setSupply(double v9) { m->vca.setSupply(v9); }

double FilterSection::tickEnv(const ModFrame &mod)
{
	m->envV = m->env.tick(m->lines.gate, m->lines.kcv / 12.0, mod.square);
	return m->envV;
}

double FilterSection::tick(const OscFrame &osc)
{
	Impl &s = *m;
	// Cutoff: IC18b, the pin-9 divider, the expo.
	const double v18 = s.summer(s.envV, osc);
	s.v9 = v18 * s.divRatio;
	s.v9Lp += (s.v9 - s.v9Lp) * s.kSlew;
	s.iAbcVcf = s.expoCurrent(s.v9Lp);

	// The source mixer as one Thevenin source.
	double g = 0, gv = 0;
	for (const auto &src : osc.mix) {
		if (src.ohms <= 0)
			continue;
		g += 1 / src.ohms;
		gv += src.volts / src.ohms;
	}
	const double mixV = (g > 0 ? gv / g : 0) + s.circuitNoise(), mixR = g > 0 ? 1 / g : 1e12;
	const double vin = s.vcf.tick(s.iAbcVcf, s.c.vcfRes, mixV, mixR);

	// VCA control: the ENV through R197 (R196 from +14 V on S6's side) and R133, or the GATE level
	// through R133.
	if (s.c.vcaMode == VcaMode::Env) {
		s.iVca = s.vca.envControlCurrent(s.envV, kV14);
		s.env.setGateLoad(0);
	} else {
		s.iVca = s.vca.controlCurrent(s.env.gateLevel(), Ba662Vca::kR133);
		s.env.setGateLoad(s.iVca);
	}
	const double iAbc = s.iVca * 300.0 / 301.0; // TR28's alpha
	const double out = s.vca.tick(vin, iAbc);
	return std::isfinite(out) ? out : 0.0;
}

// The stages' pole frequency at rest for the present I_ABC (Ir3109Vcf::poleHz).
double FilterSection::vcfHz() const { return m->vcf.poleHz(m->iAbcVcf); }
double FilterSection::env() const { return (m->envV - kVref) / (m->env.threshold() - kVref); }
double FilterSection::vca() const { return m->iVca / m->iPeak; }
double FilterSection::out4() const { return m->vcf.out4(); }
double FilterSection::pin9() const { return m->v9Lp; }
double FilterSection::iAbc() const { return m->iAbcVcf; }

} // namespace sh101
