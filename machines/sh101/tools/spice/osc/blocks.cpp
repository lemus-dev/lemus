// sh101oscblocks: one block of the oscillator section on its own, for comparison with a SPICE
// simulation of the same block (tools/spice/osc/compare.py) and for the service-note checks.
//
//   sh101oscblocks lfo KNOB WAVE SECONDS RATE OUT.txt [RST_ON RST_OFF]
//       the MODULATOR (core/lfo.*): rows "t tri clk" (tri absolute volts); WAVE tri|square|random|noise;
//       CLK RST held from RST_ON to RST_OFF seconds
//   sh101oscblocks lfohz KNOB WAVE
//       the free-running frequency the model's law gives
//   sh101oscblocks porta KNOB MODE TAU RATE SECONDS EVENTS OUT.txt
//       portamento: MODE on|auto|off; EVENTS "t,kcv,gate,portaoff;..." (gate/portaoff 0/1, held
//       until the next event); rows "t cv x on" (x: TR5's base node)
//   sh101oscblocks grip TRIHZ DEPTH PUSH_ON SECONDS RATE OUT.txt
//       the grip's LFO MOD: an ideal triangle (0.1..10.3 V) at TRIHZ, MOD switch closed from
//       PUSH_ON; rows "t tri ic2a tail vr6wiper"
//   sh101oscblocks pwm SOURCE V      PULSE WIDTH swept 0..1: rows "slider pin5 width"
//       SOURCE man|lfo|env, V the triangle (lfo) or ENV (env) volts, absolute
//   sh101oscblocks levels SUBLEVEL PULSELEVEL
//       the pulse and sub levels at the sliders, and the mixer currents they give
//   sh101oscblocks pitch KCV RANGE MODV VCOMOD TUNE BEND BENDVCO
//       DC: IC3a, IC12b, the pin-15 current and the frequency (MODV: IC18a's output, absolute)
//   sh101oscblocks vco HZ PIN5 RATE SECONDS OUT.txt
//       the VCO's band-limited waveforms at RATE: rows "t saw pulse sub1 sub2 subpulse" (at the
//       slider tops, all sliders at 10, absolute volts)
//   sh101oscblocks noise RATE SECONDS OUT.txt
//       IC12a's output: rows "t v"
//   sh101oscblocks section KNOBS... see main()
#include "sh101/core/lfo.h"
#include "sh101/core/osc.h"
#include "sh101/core/osc_circuit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace sh101;
using namespace sh101::oscm;

namespace {

LfoWave waveOf(const char *s)
{
	if (!std::strcmp(s, "square")) return LfoWave::Square;
	if (!std::strcmp(s, "random")) return LfoWave::Random;
	if (!std::strcmp(s, "noise")) return LfoWave::Noise;
	return LfoWave::Triangle;
}

int lfo(int argc, char **argv)
{
	if (argc < 7)
		return 2;
	const double knob = std::atof(argv[2]), seconds = std::atof(argv[4]), rate = std::atof(argv[5]);
	const double rstOn = argc > 8 ? std::atof(argv[7]) : -1, rstOff = argc > 8 ? std::atof(argv[8]) : -1;
	FILE *out = std::fopen(argv[6], "w");
	Lfo l;
	l.prepare(rate);
	l.setRate(knob);
	l.setWave(waveOf(argv[3]));
	const long n = long(seconds * rate);
	for (long i = 0; i < n; ++i) {
		const double t = i / rate;
		l.setClkRst(t >= rstOn && t < rstOff);
		const auto o = l.tick();
		std::fprintf(out, "%.7f %.5f %d\n", (i + 1) / rate, o.tri + kVref, int(o.square));
	}
	std::fclose(out);
	return 0;
}

int lfohz(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	Lfo l;
	l.prepare(48000);
	l.setRate(std::atof(argv[2]));
	l.setWave(waveOf(argv[3]));
	std::printf("%.6f %.4f %.4f\n", l.hz(), ModSwitch::thresholdLow(), ModSwitch::thresholdHigh(waveOf(argv[3])));
	return 0;
}

PortaMode modeOf(const char *s)
{
	if (!std::strcmp(s, "on")) return PortaMode::On;
	if (!std::strcmp(s, "auto")) return PortaMode::Auto;
	return PortaMode::Off;
}

int porta(int argc, char **argv)
{
	if (argc < 9)
		return 2;
	const double knob = std::atof(argv[2]), tau = std::atof(argv[4]), rate = std::atof(argv[5]),
	             seconds = std::atof(argv[6]);
	struct Ev {
		double t, kcv;
		bool gate, po;
	};
	std::vector<Ev> evs;
	std::stringstream ss(argv[7]);
	std::string item;
	while (std::getline(ss, item, ';')) {
		Ev e{};
		int g = 0, p = 0;
		if (std::sscanf(item.c_str(), "%lf,%lf,%d,%d", &e.t, &e.kcv, &g, &p) == 4) {
			e.gate = g;
			e.po = p;
			evs.push_back(e);
		}
	}
	FILE *out = std::fopen(argv[8], "w");
	Portamento pm;
	pm.prepare(rate);
	pm.set(knob, modeOf(argv[3]), tau);
	pm.reset(evs.empty() ? 0 : evs[0].kcv);
	size_t k = 0;
	double kcv = 0;
	bool gate = false, po = false;
	const long n = long(seconds * rate);
	for (long i = 0; i < n; ++i) {
		const double t = i / rate;
		while (k < evs.size() && evs[k].t <= t) {
			kcv = evs[k].kcv;
			gate = evs[k].gate;
			po = evs[k].po;
			++k;
		}
		const double cv = pm.tick(kcv, gate, po);
		std::fprintf(out, "%.7f %.6f %.5f %d\n", (i + 1) / rate, cv, pm.nodeX(), int(pm.shorted()));
	}
	std::fclose(out);
	return 0;
}

int grip(int argc, char **argv)
{
	if (argc < 8)
		return 2;
	const double triHz = std::atof(argv[2]), depth = std::atof(argv[3]), pushOn = std::atof(argv[4]),
	             seconds = std::atof(argv[5]), rate = std::atof(argv[6]);
	FILE *out = std::fopen(argv[7], "w");
	Bender b;
	b.prepare(rate);
	Controls c;
	c.lfoModDepth = depth;
	c.bendVco = 0;
	c.bendVcf = 0;
	const double lo = ModSwitch::thresholdLow(), hi = ModSwitch::thresholdHigh(LfoWave::Triangle);
	const long n = long(seconds * rate);
	for (long i = 0; i < n; ++i) {
		const double t = i / rate;
		Performance p;
		p.modPush = t >= pushOn ? 1 : 0;
		b.set(c, p, 1.48);
		double ph = std::fmod(t * triHz, 1.0);
		const double tri = lo + (hi - lo) * (ph < 0.5 ? 2 * ph : 2 - 2 * ph);
		const auto o = b.tick(tri, {kVref, 0});
		std::fprintf(out, "%.7f %.5f %.5f %.4e %.5f\n", (i + 1) / rate, tri, o.ic2a, o.tail, o.vcfLfo.v);
	}
	std::fclose(out);
	return 0;
}

int pwm(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	PwmSource src = PwmSource::Manual;
	if (!std::strcmp(argv[2], "lfo")) src = PwmSource::Lfo;
	if (!std::strcmp(argv[2], "env")) src = PwmSource::Env;
	const double v = std::atof(argv[3]);
	PwmNetwork net;
	net.vr4 = PwmNetwork::trimVr4(2.5);
	std::printf("# vr4 %.1f ohm, tau %.3f ms\n", net.vr4, net.tau() * 1e3);
	for (int i = 0; i <= 20; ++i) {
		const double s = i / 20.0;
		double p5 = 0;
		for (int k = 0; k < 4; ++k)
			p5 = net.pin5(src, s, v, v);
		std::printf("%.3f %.5f %.4f\n", s, p5, std::clamp(1 - p5 / 5.0, 0.0, 1.0));
	}
	return 0;
}

int levels(int argc, char **argv)
{
	if (argc < 4)
		return 2;
	const double ks = std::atof(argv[2]), kp = std::atof(argv[3]);
	for (bool h : {false, true}) {
		const Thevenin t = PulseStage::top(h);
		const Thevenin w = potWiper(t.v, t.r, kVref, 100e3, kp);
		std::printf("pulse %s top %.4f V / %.0f ohm  wiper %.4f V / %.0f ohm\n", h ? "high" : "low ", t.v, t.r, w.v, w.r);
	}
	for (SubMode m : {SubMode::Oct1Square, SubMode::Oct2Square, SubMode::Oct2Pulse})
		for (int s = 0; s < 4; ++s) {
			const Thevenin t = SubLevels::top(m, s & 1, s & 2);
			const Thevenin w = potWiper(t.v, t.r, 0, 100e3, ks);
			std::printf("sub %d q1 %d q2 %d top %.4f V / %.0f ohm  wiper %.4f V / %.0f ohm\n", int(m), s & 1, (s >> 1) & 1,
			            t.v, t.r, w.v, w.r);
		}
	return 0;
}

int pitch(int argc, char **argv)
{
	if (argc < 9)
		return 2;
	const double kcv = std::atof(argv[2]), range = std::atof(argv[3]), modv = std::atof(argv[4]),
	             vcoMod = std::atof(argv[5]), tune = std::atof(argv[6]), bend = std::atof(argv[7]),
	             bendVco = std::atof(argv[8]);
	// The OscSection's calibration, steady state.
	OscSection sec;
	sec.prepare(176400);
	Controls c;
	c.vcoMod = vcoMod;
	c.tune = tune;
	c.bendVco = bendVco;
	c.lfoModDepth = 0;
	sec.setControls(c);
	Performance p;
	p.bend = bend;
	sec.setPerformance(p);
	// Rebuild the DC path here with the same blocks.
	Bender b;
	b.prepare(176400);
	b.set(c, p, OscCalibration{}.leverVolts);
	const Thevenin w11 = potWiper(modv, 0, kVref, 100e3, taperA(vcoMod));
	Bender::Out o{};
	for (int i = 0; i < 10; ++i)
		o = b.tick(kVref, w11);
	PitchMixer pm;
	pm.prepare(176400);
	const Thevenin w9 = potWiper(kVref, 0, 0, 100e3, 1 - tune);
	double v12 = 0;
	for (int i = 0; i < 20000; ++i)
		v12 = pm.tick(o.ic3a, w9);
	// The calibrated CEM3340 (copied from OscSection's applyCalibration via a probe run).
	CpuLines l;
	l.kcv = int(std::lround(kcv * 12));
	l.range = int(std::lround(range * 12));
	sec.setLines(l);
	// The triangle that puts IC18a at MODV (TRIANGLE: R170 / R188 scale it by 0.68 about the bus).
	ModFrame mf;
	mf.tri = float((modv - kVref) * (ModSwitch::kR170 + ModSwitch::kR188) / ModSwitch::kR188);
	for (int i = 0; i < 40000; ++i)
		sec.tick(mf, kVref);
	std::printf("ic3a %.6f ic12b %.6f hz %.4f\n", o.ic3a, v12, sec.vcoHz());
	return 0;
}

int vco(int argc, char **argv)
{
	if (argc < 7)
		return 2;
	const double hz = std::atof(argv[2]), p5 = std::atof(argv[3]), rate = std::atof(argv[4]),
	             seconds = std::atof(argv[5]);
	FILE *out = std::fopen(argv[6], "w");
	Vco v;
	v.prepare(rate);
	Blep saw, pul, s1, s2, sp;
	const double sawG = 1;
	saw.reset(0);
	pul.reset(PulseStage::top(false).v);
	auto subTop = [](SubMode m, bool q1, bool q2) { return SubLevels::top(m, q1, q2).v * 100e3 / (100e3 + SubLevels::top(m, q1, q2).r); };
	s1.reset(subTop(SubMode::Oct1Square, false, false));
	s2.reset(subTop(SubMode::Oct2Square, false, false));
	sp.reset(subTop(SubMode::Oct2Pulse, false, false));
	double sv1 = subTop(SubMode::Oct1Square, false, false), sv2 = subTop(SubMode::Oct2Square, false, false),
	       svp = subTop(SubMode::Oct2Pulse, false, false);
	const double ph = PulseStage::top(true).v * 100e3 / (100e3 + PulseStage::top(true).r) + kVref * PulseStage::top(true).r / (100e3 + PulseStage::top(true).r);
	const double pl = PulseStage::top(false).v;
	const long n = long(seconds * rate);
	for (long i = 0; i < n; ++i) {
		v.tick(hz, p5 / 5.0);
		for (int e = 0; e < v.edges(); ++e) {
			const auto &ed = v.edge(e);
			if (ed.kind == Vco::Edge::Wrap)
				saw.step(-10 * sawG, ed.frac);
			else
				pul.step(ed.kind == Vco::Edge::Rise ? ph - pl : pl - ph, ed.frac);
		}
		if (v.wrapFrac() > 0) {
			const double n1 = subTop(SubMode::Oct1Square, v.q1(), v.q2()), n2 = subTop(SubMode::Oct2Square, v.q1(), v.q2()),
			             np = subTop(SubMode::Oct2Pulse, v.q1(), v.q2());
			s1.step(n1 - sv1, v.wrapFrac());
			s2.step(n2 - sv2, v.wrapFrac());
			sp.step(np - svp, v.wrapFrac());
			sv1 = n1;
			sv2 = n2;
			svp = np;
		}
		const double a = saw.push(10 * v.phase()), b = pul.push(v.pulseHigh() ? ph : pl);
		const double c1 = s1.push(sv1), c2 = s2.push(sv2), c3 = sp.push(svp);
		std::fprintf(out, "%.8f %.6f %.6f %.6f %.6f %.6f\n", (i + 1) / rate, a, b, c1, c2, c3);
	}
	std::fclose(out);
	return 0;
}

int noise(int argc, char **argv)
{
	if (argc < 5)
		return 2;
	const double rate = std::atof(argv[2]), seconds = std::atof(argv[3]);
	FILE *out = std::fopen(argv[4], "w");
	Noise nz;
	nz.prepare(rate);
	const OscCalibration cal;
	nz.set(cal.noiseRms, cal.noiseCornerHz);
	const long n = long(seconds * rate);
	for (long i = 0; i < n; ++i)
		std::fprintf(out, "%.8f %.6f\n", i / rate, nz.tick());
	std::fclose(out);
	return 0;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: sh101oscblocks lfo|lfohz|porta|grip|pwm|levels|pitch|vco|noise ...\n");
		return 2;
	}
	const std::string cmd = argv[1];
	int r = 2;
	if (cmd == "lfo") r = lfo(argc, argv);
	else if (cmd == "lfohz") r = lfohz(argc, argv);
	else if (cmd == "porta") r = porta(argc, argv);
	else if (cmd == "grip") r = grip(argc, argv);
	else if (cmd == "pwm") r = pwm(argc, argv);
	else if (cmd == "levels") r = levels(argc, argv);
	else if (cmd == "pitch") r = pitch(argc, argv);
	else if (cmd == "vco") r = vco(argc, argv);
	else if (cmd == "noise") r = noise(argc, argv);
	if (r == 2)
		std::fprintf(stderr, "bad arguments for %s\n", cmd.c_str());
	return r;
}
