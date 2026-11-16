// The oscillator section: the synth and bender boards from the S/H outputs to the source mixer
// (docs/circuit-osc.md, docs/model-osc.md). The blocks are in osc_circuit.*; this joins them.
#include "osc.h"

#include "osc_circuit.h"

#include <algorithm>
#include <cmath>

namespace sh101 {

using namespace oscm;

OscCalibration OscCalibration::serviceNotes() { return {}; }
// A real unit (docs/model.md, "Calibrations"): tuned to A440, the portamento a real unit
// measures (0.78 s at full, inside the 2 MΩ pot's and C10's tolerance), and the noise of the two
// units that agree (Perfect Circuit 1.00 V, gstormelectro 0.96 V at IC12a with the 1150 Hz corner,
// each recording's chain taken out with its own saw over 200 Hz-2.5 kHz; docs/model-osc.md).
OscCalibration OscCalibration::realUnit()
{
	OscCalibration c = serviceNotes();
	c.vcoTuneHz = 440;
	c.portaTauSec = 0.78;
	c.noiseRms = 0.98;
	return c;
}

// Fitted to Roland's SH-101 plug-in (tools/compare/fit_roland.py, docs/model.md): its MASTER
// TUNE of 440 Hz instead of the service notes' 442, and its noise at +2.9 dB re the saw, louder
// than any real unit measured. Everything else is the service notes'.
OscCalibration OscCalibration::roland()
{
	OscCalibration c = serviceNotes();
	c.vcoTuneHz = 440;
	c.noiseRms = 1.30;
	return c;
}

bool OscCalibration::set(const std::string &name, double v)
{
	if (name == "vcoTuneHz") vcoTuneHz = v;
	else if (name == "vcoScale") vcoScale = v;
	else if (name == "rangeScale") rangeScale = v;
	else if (name == "vcoDelayUs") vcoDelayUs = v;
	else if (name == "pwFullVolts") pwFullVolts = v;
	else if (name == "portaTauSec") portaTauSec = v;
	else if (name == "leverVolts") leverVolts = v;
	else if (name == "noiseRms") noiseRms = v;
	else if (name == "noiseCornerHz") noiseCornerHz = v;
	else return false;
	return true;
}

namespace {
constexpr double kR109 = 150e3, kR110 = 680e3, kR111 = 470e3, kR112 = 820e3; // the mixer
constexpr double kRpot = 100e3;
constexpr double kFmax = 57e3; // CEM3340 typical maximum with 1 nF

// A source with state-dependent Thevenin resistance, reported behind one fixed resistor: the
// voltage that pushes the same current into the mixer node (which sits near the bus).
double norton(const Thevenin &t, double rMix, double rReport)
{
	return kVref + (t.v - kVref) * rReport / (rMix + t.r);
}
} // namespace

struct OscSection::Impl {
	double rate = 176400, dt = 1 / 176400.0;
	Controls c;
	OscCalibration cal;
	Performance perf;
	CpuLines lines;

	Portamento porta;
	Bender bender;
	PitchMixer ic12b;
	Cem3340 cem;
	PwmNetwork pwm;
	Noise noise;
	Vco vco;
	Blep sawBlep, pulseBlep, subBlep;

	// Derived from the controls.
	double gSaw = 0, rSaw = kR111;          // VR14: saw volts -> reported volts (linear)
	double pulseV[2] = {kVref, kVref}, rPulse = kR110;
	double subV[4] = {kVref, kVref, kVref, kVref}, rSub = kR112; // by q1 + 2 q2
	double gNoise = 0, rNoise = kR109;
	Thevenin tune;
	double kVcoMod = 0;

	// State.
	double lastIdeal = 0, hz = 0, cv = 0, pin5 = 2.5;
	double pin5K = 1;
	double modNoiseHp = 0, modNoiseHpK = 1, modNoiseOut = kVref, modNoiseLpK = 1, modNoiseX1 = kVref;
	double sawLevel = 0;
	int subState = 0;

	void applyControls()
	{
		const double k14 = taperB(c.sawLevel);
		gSaw = k14;
		rSaw = kR111 + k14 * (1 - k14) * kRpot;
		// VR13 from TR24's collector (R92 when off, saturated when on) to the bus.
		const double k13 = taperB(c.pulseLevel);
		const Thevenin ph = potWiper(PulseStage::top(true).v, PulseStage::top(true).r, kVref, kRpot, k13);
		const Thevenin pl = potWiper(PulseStage::top(false).v, PulseStage::top(false).r, kVref, kRpot, k13);
		rPulse = kR110 + 0.5 * (ph.r + pl.r);
		pulseV[1] = norton(ph, kR110, rPulse);
		pulseV[0] = norton(pl, kR110, rPulse);
		// VR15 from the S5 contact to ground (OML's correction: not the bus).
		const double k15 = taperB(c.subLevel);
		Thevenin sub[4];
		double rs = 0;
		for (int s = 0; s < 4; ++s) {
			const Thevenin top = SubLevels::top(c.subMode, s & 1, s & 2);
			sub[s] = potWiper(top.v, top.r, 0, kRpot, k15);
			rs += sub[s].r / 4;
		}
		rSub = kR112 + rs;
		for (int s = 0; s < 4; ++s)
			subV[s] = norton(sub[s], kR112, rSub);
		const double k16 = taperB(c.noiseLevel);
		gNoise = k16;
		rNoise = kR109 + k16 * (1 - k16) * kRpot;
		// TUNE: VR9 100k (B) from the bus to ground; clockwise moves the wiper to ground (sharp).
		tune = potWiper(kVref, 0, 0, kRpot, 1 - std::clamp(c.tune, 0.0, 1.0));
		kVcoMod = taperA(c.vcoMod);
		porta.set(c.portamento, c.portaMode, cal.portaTauSec);
		bender.set(c, perf, cal.leverVolts);
	}

	void applyCalibration()
	{
		// VR6: RS for the trimmed scale (1 V/oct at 100k: I_oct = 10 µA).
		cem.rS = 3 * std::log(2.0) * Cem3340::kRT * cal.vcoScale / (22 * Cem3340::kRZ * 10e-6);
		cem.rRange = 100e3 / cal.rangeScale;
		cem.tDelay = cal.vcoDelayUs * 1e-6;
		// VR7: the service notes' test mode U&D (KCV 4.75 V, RANGE 0), TUNE centred, everything else
		// at rest, gives vcoTuneHz.
		const Thevenin tuneMid = potWiper(kVref, 0, 0, kRpot, 0.5);
		const double v12b = kVref - PitchMixer::kR89 * (tuneMid.v - kVref) / (PitchMixer::kR90 + tuneMid.r);
		const double f = cal.vcoTuneHz / (1 - cem.tDelay * cal.vcoTuneHz);
		const double ic = (std::log(f / 1000.0) + 22 * cem.rS / Cem3340::kRT) * 3 * Cem3340::kRT /
		                  (22 * cem.rS * Cem3340::kRZ);
		cem.vVr7 = 0;
		cem.vVr7 = std::clamp((ic - cem.current(4.75, 0, v12b, f)) * Cem3340::kR108, -5.0, 0.0);
		// VR4: PULSE WIDTH at 0 gives a square (pin 5 at half the 100 % voltage).
		pwm.vr4 = PwmNetwork::trimVr4(0.5 * cal.pwFullVolts);
		pin5K = 1 - std::exp(-dt / pwm.tau());
		noise.set(cal.noiseRms, cal.noiseCornerHz);
		applyControls();
	}

	// The noise as the waveform switch's NOISE contact passes it on: C61 into R173 / R174 (and R172
	// + R188), then R172 / C60 with R188 to the bus (circuit-osc.md 5.3).
	double modNoise(double n)
	{
		constexpr double ra = 1 / (1 / ModSwitch::kR173 + 1 / ModSwitch::kR174 + 1 / (ModSwitch::kR172 + ModSwitch::kR188));
		constexpr double aDc = (kVref / ModSwitch::kR173 + kVref / (ModSwitch::kR172 + ModSwitch::kR188)) * ra;
		// C61's high-pass on the noise's AC part.
		modNoiseHp = modNoiseHpK * (modNoiseHp + n - modNoiseX1);
		modNoiseX1 = n;
		const double a = aDc + modNoiseHp;
		const double target = (a * ModSwitch::kR188 + kVref * ModSwitch::kR172) / (ModSwitch::kR172 + ModSwitch::kR188);
		modNoiseOut += (target - modNoiseOut) * modNoiseLpK;
		return modNoiseOut;
	}
};

OscSection::OscSection() : m(new Impl) {}
OscSection::~OscSection() { delete m; }

void OscSection::prepare(double r)
{
	m->rate = r;
	m->dt = 1 / r;
	m->porta.prepare(r);
	m->bender.prepare(r);
	m->ic12b.prepare(r);
	m->noise.prepare(r);
	m->vco.prepare(r);
	constexpr double ra = 1 / (1 / ModSwitch::kR173 + 1 / ModSwitch::kR174 + 1 / (ModSwitch::kR172 + ModSwitch::kR188));
	m->modNoiseHpK = std::exp(-m->dt / (ModSwitch::kC61 * ra));
	constexpr double rb = ModSwitch::kR172 * ModSwitch::kR188 / (ModSwitch::kR172 + ModSwitch::kR188);
	m->modNoiseLpK = 1 - std::exp(-m->dt / (rb * ModSwitch::kC60));
	m->applyCalibration();
	reset();
}

void OscSection::reset()
{
	m->porta.reset(m->lines.kcv / 12.0);
	m->bender.reset();
	m->ic12b.reset();
	m->noise.reset();
	m->vco.reset();
	m->lastIdeal = 0;
	m->subState = 0;
	m->sawBlep.reset(kVref + m->gSaw * (0 - kVref));
	m->pulseBlep.reset(m->pulseV[0]);
	m->subBlep.reset(m->subV[0]);
	m->modNoiseHp = 0;
	m->modNoiseX1 = kVref;
	m->modNoiseOut = kVref;
}

void OscSection::setControls(const Controls &c)
{
	m->c = c;
	m->applyControls();
}

void OscSection::setCalibration(const OscCalibration &c)
{
	m->cal = c;
	m->applyCalibration();
}

void OscSection::setPerformance(const Performance &p)
{
	m->perf = p;
	m->bender.set(m->c, p, m->cal.leverVolts);
}

void OscSection::setLines(const CpuLines &l) { m->lines = l; }

OscFrame OscSection::tick(const ModFrame &mod, double envVolts)
{
	auto &s = *m;
	const double kcv = s.lines.kcv / 12.0, range = s.lines.range / 12.0;
	const double tri = mod.tri + kVref; // IC3a's output, absolute

	// The noise generator: the mixer's NOISE slider and the waveform switch both take it.
	const double noise = s.noise.tick();
	const double modNoise = s.modNoise(noise);

	// Portamento on the KCV (the CV IN jack): the CV line.
	s.cv = s.porta.tick(kcv, s.lines.gate, s.lines.portaOff);

	// The waveform switch into IC18a.
	double modAbs = kVref;
	switch (s.c.lfoWave) {
	case LfoWave::Triangle: modAbs = ModSwitch::triangle(tri); break;
	case LfoWave::Square: modAbs = ModSwitch::square(mod.square); break;
	case LfoWave::Random: modAbs = ModSwitch::random(s.lines.random); break;
	case LfoWave::Noise: modAbs = modNoise; break;
	}
	modAbs = opampClamp(modAbs, 0, kV14); // IC18a on +14 V / ground

	// VCO MOD (VR11) into the bender board's IC3a with the bend and the grip's LFO MOD, then IC12b.
	const Thevenin vcoMod = potWiper(modAbs, 0, kVref, kRpot, s.kVcoMod);
	const Bender::Out b = s.bender.tick(tri, vcoMod);
	const double v12b = s.ic12b.tick(b.ic3a, s.tune);

	// The CEM3340.
	const double ic = s.cem.current(s.cv, range, v12b, s.lastIdeal);
	const double ideal = s.cem.idealHz(ic);
	s.lastIdeal = ideal;
	s.hz = std::min(ideal / (1 + s.cem.tDelay * ideal), std::min(kFmax, 0.45 * s.rate));

	// PWM: pin 5 through C37; the SH-101 pulse is high above phase pin5 / (100 % voltage).
	s.pin5 += (s.pwm.pin5(s.c.pwmSource, s.c.pulseWidth, tri, envVolts) - s.pin5) * s.pin5K;
	s.vco.tick(s.hz, s.pin5 / s.cal.pwFullVolts);

	// Band-limited edges, then the naive values of this sample.
	const double sawStep = -10 * s.gSaw;
	for (int i = 0; i < s.vco.edges(); ++i) {
		const auto &e = s.vco.edge(i);
		switch (e.kind) {
		case Vco::Edge::Wrap: s.sawBlep.step(sawStep, e.frac); break;
		case Vco::Edge::Rise: s.pulseBlep.step(s.pulseV[1] - s.pulseV[0], e.frac); break;
		case Vco::Edge::Fall: s.pulseBlep.step(s.pulseV[0] - s.pulseV[1], e.frac); break;
		}
	}
	if (s.vco.wrapFrac() > 0) {
		const int ns = int(s.vco.q1()) + 2 * int(s.vco.q2());
		if (ns != s.subState) {
			s.subBlep.step(s.subV[ns] - s.subV[s.subState], s.vco.wrapFrac());
			s.subState = ns;
		}
	}
	const double saw = 10 * s.vco.phase();

	OscFrame f;
	f.mix[0] = {s.sawBlep.push(kVref + s.gSaw * (saw - kVref)), s.rSaw};
	f.mix[1] = {s.pulseBlep.push(s.pulseV[s.vco.pulseHigh() ? 1 : 0]), s.rPulse};
	f.mix[2] = {s.subBlep.push(s.subV[s.subState]), s.rSub};
	f.mix[3] = {kVref + s.gNoise * (noise - kVref), s.rNoise};
	f.modVolts = modAbs - kVref;
	f.kybdVolts = s.cv;
	f.lfoSquare = mod.square;
	f.vcfBend = {b.vcfBend.v - kVref, b.vcfBend.r};
	f.vcfLfo = {b.vcfLfo.v - kVref, b.vcfLfo.r};
	return f;
}

double OscSection::vcoHz() const { return m->hz; }
double OscSection::pitchVolts() const { return m->cv + m->lines.range / 12.0; }

} // namespace sh101
