#include "preamp.h"

#include "qvgt/netlists.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <string>

namespace qvgt {

namespace {

// The SSM2120's references. The VCAs': R104 120K from +12 V into I_REF, which sits a couple of
// diodes above -12 V (the sheet: I_REF = (V+ - V_IREF) / R); the detectors': 1M each from +12 V
// (R65, R66) into the rectifiers' I_REF, at 0 V.
constexpr double kVcaIref = 183e-6;
constexpr double kDetIref = 12e-6;
// LOG AV's capacitors: C21 4.7u (the compressor's detector), C29 10u (the gate's).
constexpr double kDet1Cap = 4.7e-6, kDet2Cap = 10e-6;
// The control amplifiers as the THRESH networks set them (circuit.md, "U4"): CONOUT = gain LOG AV
// + offset, down to the output's swing (-10.5 V) and sinking only.
constexpr double kComGain = 31.064, kComOffset = -0.766;
constexpr double kGateGain = 31.769, kGateThreshGain = -0.7692;
constexpr double kSwing = -10.5;
// The CD4051/4053 on resistance at 17 V.
constexpr double kRon = 125;

// The compressor's amount: U14's channel to its resistor (0: the channel is grounded, the
// network isn't driven; 7: no resistor).
constexpr double kU14[8] = {0, 8.2e3, 3.3e3, 2e3, 1.2e3, 620, 300, 0};
constexpr double kR86 = 1.3e3, kR83 = 200, kR81 = 100;

// Q1's gate network as the compressor's control output sees it through R75: +12 V by R41 390K,
// R11 220K to ground (its Thevenin source), then R75 220K.
constexpr double kGateTh = 12.0 * 220e3 / (390e3 + 220e3);
constexpr double kGateRth = 390e3 * 220e3 / (390e3 + 220e3) + 220e3;

// D11 (1N4148, as models.cir)
constexpr double kDiodeIs = 2.52e-9, kDiodeNVt = 1.752 * Ssm2120::kVt;

int need(int index, const char *what)
{
	if (index < 0)
		throw std::runtime_error(std::string("qvgt preamp: no ") + what);
	return index;
}

} // namespace

void Chain::load(const std::string &name, const std::string &text)
{
	m_name = name;
	const std::string models = std::string(netlists::models) + "\n";
	// tools: QVGT_WHOLE runs every netlist as one circuit, its .part lines ignored
	static const bool whole = std::getenv("QVGT_WHOLE") != nullptr;
	auto parts = circuit::Netlist::partitions(text);
	if (parts.empty() || whole)
		parts.assign(1, {name, text, false, 1});
	m_stages.clear();
	m_links.clear();
	m_inNames.clear();
	m_outNames.clear();
	m_ctlNames.clear();
	m_ins.clear();
	m_ctls.clear();
	m_outs.clear();
	auto slot = [](std::vector<std::string> &names, const std::string &n) {
		for (size_t k = 0; k < names.size(); ++k)
			if (names[k] == n)
				return int(k);
		names.push_back(n);
		return int(names.size() - 1);
	};
	for (const auto &p : parts) {
		auto c = std::make_unique<circuit::Circuit>(circuit::Netlist::parse(models + p.text));
		c->setName(name + "/" + p.name);
		const auto &net = c->netlist();
		const int si = int(m_stages.size());
		std::vector<Link> links;
		for (int i = 0; i < int(net.inputs.size()); ++i) {
			const auto &n = net.inputs[size_t(i)];
			if (!n.empty() && n[0] == '@') {
				// from the earlier stage that has it as an output
				bool found = false;
				for (int s = 0; s < si && !found; ++s) {
					const int o = m_stages[size_t(s)]->netlist().output(n);
					if (o >= 0) {
						links.push_back({i, s, o});
						found = true;
					}
				}
				if (!found)
					throw std::runtime_error("qvgt preamp: " + name + "/" + p.name + " takes " + n + " from no earlier part");
				continue;
			}
			const int k = slot(m_inNames, n);
			m_ins.resize(m_inNames.size());
			m_ins[size_t(k)].push_back({si, i});
		}
		for (int o = 0; o < int(net.outputs.size()); ++o) {
			const auto &n = net.outputs[size_t(o)].name;
			if (!n.empty() && n[0] == '@')
				continue;
			const int k = slot(m_outNames, n);
			m_outs.resize(m_outNames.size());
			m_outs[size_t(k)] = {si, o};
		}
		for (int i = 0; i < int(net.controls.size()); ++i) {
			const int k = slot(m_ctlNames, net.controls[size_t(i)]);
			m_ctls.resize(m_ctlNames.size());
			m_ctls[size_t(k)].push_back({si, i});
		}
		if (p.over != 1)
			throw std::runtime_error("qvgt preamp: " + name + "/" + p.name + ": over= is the chain's (Chain::setOver)");
		m_stages.push_back(std::move(c));
		m_links.push_back(std::move(links));
	}
	m_held.clear();
	for (const auto &n : m_inNames)
		m_held.push_back(n.rfind("noise_", 0) == 0);
	m_last.assign(m_inNames.size(), 0.0);
	m_want.assign(m_inNames.size(), 0.0);
}

void Chain::setRate(double rate)
{
	for (size_t s = 0; s < m_stages.size(); ++s)
		m_stages[s]->setRate(s < overStages() ? rate * m_over : rate);
}

void Chain::solveDc()
{
	for (size_t s = 0; s < m_stages.size(); ++s) {
		for (const auto &l : m_links[s])
			m_stages[s]->setInput(l.input, m_stages[size_t(l.from)]->output(l.output));
		m_stages[s]->solveDc();
	}
	for (size_t i = 0; i < m_last.size(); ++i) {
		const auto &[s, k] = m_ins[i].front();
		m_last[i] = m_stages[size_t(s)]->inputValue(k);
	}
}

void Chain::stepStages(size_t from, size_t to)
{
	for (size_t s = from; s < to; ++s) {
		for (const auto &l : m_links[s])
			m_stages[s]->setInput(l.input, m_stages[size_t(l.from)]->output(l.output));
		m_stages[s]->step();
	}
}

void Chain::step()
{
	if (m_over == 1) {
		stepStages(0, m_stages.size());
		return;
	}
	const size_t n = m_last.size();
	for (size_t i = 0; i < n; ++i) {
		const auto &[s, k] = m_ins[i].front();
		m_want[i] = m_stages[size_t(s)]->inputValue(k);
	}
	for (int j = 1; j <= m_over; ++j) {
		const double a = double(j) / m_over;
		for (size_t i = 0; i < n; ++i)
			if (!m_held[i])
				setInput(int(i), m_last[i] + (m_want[i] - m_last[i]) * a);
		stepStages(0, overStages());
	}
	std::swap(m_last, m_want);
	stepStages(overStages(), m_stages.size());
}

int Chain::input(const std::string &name) const
{
	for (size_t k = 0; k < m_inNames.size(); ++k)
		if (m_inNames[k] == name)
			return int(k);
	return -1;
}

int Chain::output(const std::string &name) const
{
	for (size_t k = 0; k < m_outNames.size(); ++k)
		if (m_outNames[k] == name)
			return int(k);
	return -1;
}

int Chain::control(const std::string &name) const
{
	for (size_t k = 0; k < m_ctlNames.size(); ++k)
		if (m_ctlNames[k] == name)
			return int(k);
	return -1;
}

void Chain::setInput(int i, double v)
{
	for (const auto &[s, k] : m_ins[size_t(i)])
		m_stages[size_t(s)]->setInput(k, v);
}

std::vector<double *> Chain::inputSlots(int i)
{
	std::vector<double *> out;
	for (const auto &[s, k] : m_ins[size_t(i)])
		out.push_back(m_stages[size_t(s)]->inputSlot(k));
	return out;
}

void Chain::setControl(int c, double v)
{
	for (const auto &[s, k] : m_ctls[size_t(c)])
		if (m_stages[size_t(s)]->controlValue(k) != v)
			m_stages[size_t(s)]->setControl(k, v);
}

Preamp::Preamp()
{
	m_input.load("input", netlists::input);
	m_od.load("overdrive", netlists::overdrive);
	m_shaper.load("shaper", netlists::shaper);
	m_gate.load("gateout", netlists::gateout);
	// The shaper's op-amps' poles by the trapezoidal rule: against ngspice on the bench's plucks,
	// its output's top octave 0.3-0.4 dB low with backward Euler, within 0.1 dB with it (the clipper
	// and the tone stack swing their op-amps across the diodes and into the rails); the other
	// circuits come out closer with backward Euler (docs/circuit.md, "Integration").
	m_shaper.setPoleTrap(true);
	if (const char *t = std::getenv("QVGT_POLETRAP")) // tools: the circuits named ("all", "none")
		for (auto *c : {&m_input, &m_od, &m_shaper, &m_gate})
			c->setPoleTrap(std::string(t) == "all" || (std::string(t) != "none" && std::string(t).find(c->name()) != std::string::npos));
	if (const char *o = std::getenv("QVGT_OVER")) { // tools: "shaper:2,overdrive:2"
		const std::string spec = o;
		for (auto *c : {&m_input, &m_od, &m_shaper, &m_gate}) {
			const auto at = spec.find(c->name() + ":");
			if (at != std::string::npos) { // "shaper:2/2": the first two partitions
				char *end = nullptr;
				const long n = std::strtol(spec.c_str() + at + c->name().size() + 1, &end, 10);
				c->setOver(int(n), *end == '/' ? std::atoi(end + 1) : 0);
			}
		}
	}

	const auto &in = m_input;
	m_inLeft = need(in.input("left"), "left");
	m_inRight = need(in.input("right"), "right");
	m_outP8 = need(in.output("p8"), "p8");
	m_outDryL = need(in.output("dryl"), "dryl");
	m_outDryR = need(in.output("dryr"), "dryr");
	m_ctlInput = need(in.control("input"), "input");

	const auto &od = m_od;
	m_odIn = need(od.input("vca"), "vca");
	m_odOut = need(od.output("od8"), "od8");
	m_ctlOd = need(od.control("od"), "od");

	const auto &sh = m_shaper;
	m_shIn = need(sh.input("od"), "od");
	m_shCon = need(sh.input("conout"), "conout");
	m_shOut = need(sh.output("pre"), "pre");
	m_ctlDist = need(sh.control("dist"), "dist");
	for (int c = 0; c < 8; ++c)
		m_ctlCh[c] = need(sh.control("ch" + std::to_string(c)), "ch");
	m_ctlPresence = need(sh.control("presence"), "presence");
	m_ctlFlat = need(sh.control("flat"), "flat");
	m_ctlCab = need(sh.control("cab"), "cab");
	m_ctlCab2 = need(sh.control("cab2"), "cab2");
	m_ctlBass = need(sh.control("bass"), "bass");

	const auto &gt = m_gate;
	m_gtIn = need(gt.input("vca"), "vca");
	m_gtRet = need(gt.input("ret"), "ret");
	m_gtOut = need(gt.output("out"), "out");
	m_gtSend = need(gt.output("send"), "send");
	m_ctlLoop = need(gt.control("loop"), "loop");

	{
		Chain *chains[4] = {&m_input, &m_od, &m_shaper, &m_gate};
		for (int c = 0; c < 4; ++c)
			for (int i = 0; i < chains[c]->inputs(); ++i)
				if (chains[c]->inputName(i).rfind("noise_", 0) == 0)
					m_noiseSlots.push_back(chains[c]->inputSlots(i));
	}
	m_det1.c = kDet1Cap;
	m_det2.c = kDet2Cap;
	m_det1.iref = m_det2.iref = kDetIref;
	m_vcaIref = kVcaIref;
}

void Preamp::prepare(double rate)
{
	m_rate = rate;
	m_dt = 1 / rate;
	for (auto *c : {&m_input, &m_od, &m_shaper, &m_gate})
		c->setRate(rate);
	m_vca1In.setup(20e3, 1e-6, rate);    // R34, C13
	m_det1In.setup(20e3, 10e-6, rate);   // R35, C32
	m_det2In.setup(20e3, 0.47e-6, rate); // R63, C28
	m_vca2In.setup(30e3, 1e-6, rate);    // R103, C27
	m_envDecay = std::exp(-1 / (rate * 330e3 * 0.1e-6));
	// white noise to the step rate's Nyquist frequency: sigma = density sqrt(rate / 2)
	m_opampSigma = 18e-9 * std::sqrt(rate / 2);
	m_vcaSigma = 19.6e-12 * std::sqrt(rate / 2);
	m_det1.setup(m_dt);
	m_det2.setup(m_dt);
	reset();
}

void Preamp::reset()
{
	m_dirty = true;
	applyControls();
	for (auto *c : {&m_input, &m_od, &m_shaper, &m_gate}) {
		for (int i = 0; i < c->inputs(); ++i)
			c->setInput(i, 0);
		c->solveDc();
	}
	m_vca1In.reset();
	m_det1In.reset();
	m_det2In.reset();
	m_vca2In.reset();
	m_det1.y = m_det2.y = 0;
	m_d11 = 0;
	m_env = 0;
	m_noise.reseed(0x9e3779b9u);
	m_conout1 = m_conout2 = m_vcMinus1 = m_vcPlus2 = 0;
}

void Preamp::setControls(uint8_t u10, uint8_t u11, uint8_t l0400, double inputPot)
{
	if (u10 == m_u10 && u11 == m_u11 && l0400 == m_l0400 && inputPot == m_inputPot && !m_dirty)
		return;
	m_u10 = u10;
	m_u11 = u11;
	m_l0400 = l0400;
	m_inputPot = inputPot;
	m_dirty = true;
	applyControls();
}

void Preamp::applyControls()
{
	if (!m_dirty)
		return;
	m_dirty = false;
	auto bit = [](uint8_t v, int b) { return (v >> b) & 1; };

	m_input.setControl(m_ctlInput, m_inputPot);

	// U11: b0 presence/bright, b1-b3 the compression (bit-reversed: U14's C, B, A), b4-b6 the
	// overdrive DAC, b7 the effects loop.
	m_compChannel = bit(m_u11, 3) + 2 * bit(m_u11, 2) + 4 * bit(m_u11, 1);
	const int od = (m_u11 >> 4) & 7;
	m_od.setControl(m_ctlOd, od ? 1 : 0);
	// VCA 1's +Vc: R82 100 to ground, the DAC's 27K/13K/6.8K from the latch's 5 V outputs, and
	// R40A 13K from -12 V through U3C while the overdrive is on (D12-D14 OR its bits into U3's
	// select).
	{
		const double r[3] = {27e3, 13e3, 6.8e3};
		double g = 1 / 100.0, i = 0;
		for (int b = 0; b < 3; ++b) {
			g += 1 / r[b];
			i += bit(m_u11, 4 + b) * 5.0 / r[b];
		}
		if (od) {
			g += 1 / (13e3 + kRon);
			i += -12.0 / (13e3 + kRon);
		}
		m_vcPlus1 = i / g;
	}
	m_shaper.setControl(m_ctlPresence, bit(m_u11, 0));
	// U3's select (pin 11) takes U11 Q12 through R122 10K, but J8's switch grounds it while no plug
	// is in the return: the loop is out whatever the program says (the owner's manual: "the effects
	// loop will be bypassed if there are no jacks inserted").
	m_gate.setControl(m_ctlLoop, m_returnPlugged ? bit(m_u11, 7) : 0);

	// U10: b0 distortion on, b1 flat, b2-b4 U8's select (C, B, A), b5 bass boost, b6 cabinet 2,
	// b7 cabinet on.
	m_shaper.setControl(m_ctlDist, bit(m_u10, 0));
	m_shaper.setControl(m_ctlFlat, bit(m_u10, 1));
	const int ch = bit(m_u10, 4) + 2 * bit(m_u10, 3) + 4 * bit(m_u10, 2);
	for (int c = 0; c < 8; ++c)
		m_shaper.setControl(m_ctlCh[c], c == ch ? 1 : 0);
	m_shaper.setControl(m_ctlBass, bit(m_u10, 5));
	m_shaper.setControl(m_ctlCab2, bit(m_u10, 6));
	m_shaper.setControl(m_ctlCab, bit(m_u10, 7));

	// 0400h b0-b4: the gate's threshold DAC (U37's outputs through 82K, 39K, 20K, 10K, 8.2K into
	// U39's virtual ground, 6.8K feedback).
	{
		const double r[5] = {82e3, 39e3, 20e3, 10e3, 8.2e3};
		double i = 0;
		for (int b = 0; b < 5; ++b)
			i += bit(m_l0400, b) * 5.0 / r[b];
		m_threshold = std::max(-6.8e3 * i, kSwing);
	}
}

// VCA 1's -Vc from the compressor's control output: U14's resistor, its on resistance and R86
// into the node D11 clamps (anode to ground), then R83 200 and R81 100 to ground (-Vc at their
// junction). Newton on the node.
double Preamp::compressorVcMinus(double conout) const
{
	if (m_compChannel == 0)
		return 0; // U14's common grounded: the node at 0 V
	const double rs = kU14[m_compChannel] + kRon + kR86, gs = 1 / rs, gl = 1 / (kR83 + kR81);
	double x = m_d11;
	for (int it = 0; it < 30; ++it) {
		// current into the node: (conout - x)/rs + Is (exp(-x/nVt) - 1) - x gl = 0
		const double e = std::exp(std::min(-x / kDiodeNVt, 80.0));
		const double f = (conout - x) * gs + kDiodeIs * (e - 1) - x * gl;
		const double df = -gs - kDiodeIs * e / kDiodeNVt - gl;
		double dx = -f / df;
		dx = std::clamp(dx, -0.1, 0.1);
		x += dx;
		if (std::abs(dx) < 1e-9)
			break;
	}
	const_cast<Preamp *>(this)->m_d11 = x;
	return x * kR81 / (kR83 + kR81);
}

void Preamp::stepNoise()
{
	for (const auto &slots : m_noiseSlots) {
		const double v = m_hiss ? m_opampSigma * m_noise.gauss() : 0.0;
		for (double *u : slots)
			*u = v;
	}
}

Preamp::Out Preamp::step(double inL, double inR, double fxReturn)
{
	Out o;
	stepNoise();
	m_input.setInput(m_inLeft, inL);
	m_input.setInput(m_inRight, inR);
	m_input.step();
	const double p8 = m_p8 = m_input.output(m_outP8);
	o.dryL = m_input.output(m_outDryL);
	o.dryR = m_input.output(m_outDryR);
	// U16 drives D15 while U2 pin 8 is above C52; R145 discharges it
	m_env = std::max(p8, m_env * m_envDecay);
	o.envelope = m_env;

	// U4's detectors and control amplifiers
	m_det1.step(m_det1In(p8));
	m_det2.step(m_det2In(p8));
	// the compressor's: sinks only, so it can't go above what its load leaves it at (Q1's gate
	// network through R75, against U14's path to ground; open with U14 on channel 0)
	{
		const double want = std::max(kComGain * m_det1.logAv() + kComOffset, kSwing);
		const double rLoad = m_compChannel ? kU14[m_compChannel] + kRon + kR86 + kR83 + kR81 : 0;
		const double rest = m_compChannel ? kGateTh * rLoad / (kGateRth + rLoad) : kGateTh;
		m_conout1 = std::min(want, rest);
	}
	m_vcMinus1 = compressorVcMinus(m_conout1);
	// the gate's: into R? 750 and R? 200 to ground (+Vc of VCA 2 at their junction)
	m_conout2 = std::clamp(kGateGain * m_det2.logAv() + kGateThreshGain * m_threshold, kSwing, 0.0);
	m_vcPlus2 = m_conout2 * 200.0 / 950.0;

	// VCA 1, the overdrive
	const double g1 = Ssm2120::gain(m_vcPlus1, m_vcMinus1);
	double i1 = Ssm2120::vcaAt(m_vca1In(p8), g1, m_vcaIref);
	if (m_hiss)
		i1 += m_vcaSigma * std::pow(g1, 0.475) * m_noise.gauss();
	m_od.setInput(m_odIn, i1);
	m_od.step();
	const double od8 = m_od.output(m_odOut);

	// the distortion, tone, cabinet and bass boost
	m_shaper.setInput(m_shIn, od8);
	m_shaper.setInput(m_shCon, m_conout1);
	m_shaper.step();
	const double pre = m_shaper.output(m_shOut);

	// VCA 2 (the gate), the effects loop, U13C
	const double g2 = Ssm2120::gain(m_vcPlus2, 0);
	double i2 = Ssm2120::vcaAt(m_vca2In(pre), g2, m_vcaIref);
	if (m_hiss)
		i2 += m_vcaSigma * std::pow(g2, 0.475) * m_noise.gauss();
	m_gate.setInput(m_gtIn, i2);
	m_gate.setInput(m_gtRet, fxReturn);
	m_gate.step();
	o.toAdc = m_gate.output(m_gtOut);
	o.send = m_gate.output(m_gtSend);
	if (m_tap) {
		m_probe = {inL, inR, p8, o.dryL, o.dryR, i1, od8, m_conout1, pre, i2, o.toAdc, o.send};
		m_tap(*this);
	}
	return o;
}

double Preamp::vca1Db() const
{
	return (m_vcPlus1 - m_vcMinus1) / Ssm2120::kVoltsPerDb;
}

double Preamp::vca2Db() const
{
	return m_vcPlus2 / Ssm2120::kVoltsPerDb;
}

} // namespace qvgt
