#include "voiceboard.h"

#include "parallel.h"
#include "tr808/netlists.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <string>

namespace tr808 {

// Tools: per-block timing and sleep diagnostics (read once).
const bool kProfile = std::getenv("TR808_PROFILE") != nullptr;
const bool kSleepDebug = std::getenv("TR808_SLEEPDEBUG") != nullptr;

// A trigger edge: the samples run in quarter steps, and the quarter steps of those by backward Euler.
constexpr int kEdgeSubsteps = 8;
const int kEdgeBackwardEuler = std::getenv("TR808_EDGEBE") ? std::atoi(std::getenv("TR808_EDGEBE")) : 8;

namespace {
// Tools: TR808_NETLISTS=DIR reads DIR/<name>.cir in place of a built-in netlist, where there is
// one (experiments without a rebuild).
const char *netlistText(const char *name, const char *builtIn)
{
	static const char *dir = std::getenv("TR808_NETLISTS");
	if (dir == nullptr)
		return builtIn;
	static std::map<std::string, std::string> loaded;
	auto it = loaded.find(name);
	if (it == loaded.end()) {
		std::ifstream f(std::string(dir) + "/" + name + ".cir");
		std::stringstream text;
		if (f)
			text << f.rdbuf();
		it = loaded.emplace(name, f ? text.str() : std::string(builtIn)).first;
	}
	return it->second.c_str();
}
} // namespace

namespace {
// The white source's volts per sample for noiseRms (TM4's level at TP4, docs/model.md): IC24a's
// gain falls from 92 above its 7.2 kHz zero, so the rms is the density x sqrt(92^2 x 7.23 kHz x
// pi / 2). (TR808_NONOISE, tools: none.)
double noiseScaleFor(double noiseRms, double rate, bool always = false)
{
	const double density = noiseRms / std::sqrt(91.9 * 91.9 * 7230.0 * 3.14159265358979323846 / 2);
	return std::getenv("TR808_NONOISE") && !always ? 0.0 : density * std::sqrt(rate / 2);
}
} // namespace

double Calibration::knob(const char *name, double tick) const
{
	const auto it = knobLaws.find(name);
	const auto &law = it == knobLaws.end() ? knobLaw : it->second;
	const double t = std::clamp(tick, 0.0, 1.0);
	const double xs[5] = {0, 0.25, 0.5, 0.75, 1}, ys[5] = {0, law[0], law[1], law[2], 1};
	int k = 0;
	while (k < 3 && t > xs[k + 1])
		++k;
	return ys[k] + (ys[k + 1] - ys[k]) * (t - xs[k]) / (xs[k + 1] - xs[k]);
}

Calibration unitCalibration()
{
	// Michael Fischer's TR-808 (serial 103852), fitted to his recordings at every knob mark
	// (tools/compare/fit.py; docs/model.md, "Calibration"): the parts' tolerances, as that unit has
	// them.
	Calibration c;
	// The six metal oscillators, from the line spectra of its cymbal and open hat (the four fixed
	// ones 3-7 % off the typical thresholds' frequencies) and its cowbell (TM1 and TM2 a little short).
	c.cbHz1 = 558.3;
	c.cbHz2 = 823.6;
	c.metalHz = {486.39, 287.82, 378.58, 198.83};
	// Their squares' high time: the cowbell's even lines (2f1, 2f2, 4f1, 4f2, 6f1, 2f2 - f1) all
	// match within 0.5 dB at 0.54-0.55, 4-6 dB low at the typical thresholds' 0.47 (the HD14584's
	// thresholds spread 1.6-3.4 V at 5 V; IC1 runs on ~4.7 V). The cymbal and hats agree.
	c.metalDuty = 0.545;
	// TM3, a service trim: Raum 45's, the archive.org and Reverb's units all sit here (their claps'
	// tails within 0.6-1.3 dB from 0.9 to 7 kHz, their accents within 1.3 dB); Fischer's unit is
	// 0.26 (darker, longer tail, accenting 3.5 dB harder)
	c.cpOffset = 0.37;
	// TM4, the noise trim: the toms' noise tails sit 3.3 dB over the notes' 130 mV rms, flat from
	// 125 Hz to 2 kHz and 30 to 450 ms (an average-responding meter alone reads Gaussian noise 1 dB
	// low); its snare and clap agree.
	c.noiseRms = 0.19;
	// Its knobs' ticks against their pots, where his recordings show it: the rotation at which the
	// model meets the unit at each tick (tools/compare/marks.py). The three TUNING pots by pitch
	// (each pot's tom and conga agree within 0.02) and the cymbal's TONE by centroid share a shape,
	// a track that starts about 1.3 ticks up and ends about 0.9 short of 10; the BD's DECAY fits
	// that shape better than a straight scale on the ticks it wasn't taken from (BD LSD 0.89 ->
	// 0.62). The snare's SNAPPY has its own, fitted with the snare at the group's rate (2026-11-04:
	// its median LSD over the 25 recordings 1.73 -> 1.48, every 2.5 tick nearer). The open hat's
	// and cymbal's DECAY and the snare's TONE fit straight better; LEVEL, ACCENT and the BD's TONE
	// have no evidence and stay straight.
	const std::array<double, 3> shape{0.153, 0.4705, 0.80};
	c.knobLaws = {
		{"ltTuning", {0.163, 0.458, 0.735}},
		{"mtTuning", {0.142, 0.442, 0.740}},
		{"htTuning", {0.167, 0.483, 0.797}},
		{"cyTone", {0.154, 0.471, 0.833}},
		{"bdDecay", shape},
		{"sdSnappy", {0.18, 0.55, 0.90}},
	};
	c.parts = {
		{"bd.p6", 585e3}, // DECAY's pot (500k B): the range to 1.5 s at full
		// IC2a's output limit, the narrowed trig's accented level: the archive.org unit's cowbell,
		// cymbal and hats all accent as 11.4 V (a uPC4558 within its spec on the 14.1 V +B1; the
		// typical part's 12.8 is the netlist's). Fischer's hits aren't accented.
		{"mtrig.ntsat", 11.4},
		// the toms: each resonator's parts and its TUNING pot (500 ohm, the range's ends)
		{"lt.c76", 53.07e-9}, {"lt.c78", 11.59e-9}, {"lt.c77", 47.7e-9}, {"lt.r230", 838e3}, {"lt.p11", 489.6},
		{"mt.c90", 27.17e-9}, {"mt.c92", 11.19e-9}, {"mt.c91", 26.23e-9}, {"mt.r259", 862.3e3}, {"mt.p13", 527.2},
		{"ht.c103", 25.04e-9}, {"ht.c105", 5.705e-9}, {"ht.c104", 21.93e-9}, {"ht.r286", 850.6e3}, {"ht.p15", 540.3},
		// the claves: R313 (390k printed, which can't decay) and the mute's C113
		{"rs.r313", 500e3}, {"rs.c117", 2.318e-9}, {"rs.c119", 2.213e-9}, {"rs.c113", 36e-9},
		// the rim shot: R318 sets its 455 Hz into the VCA (the balance with the 1.7 kHz that Fischer's
		// unit and the archive.org one agree on, -6 dB, measured from the onset), C118 (in circuit only
		// for RS) puts that line on the unit's 1712 Hz, and R324 (unprinted) its level against the
		// other voices
		{"rs.r318", 165e3}, {"rs.c118", 2.35e-9}, {"rs.r324", 120e3},
		{"cb.c34", 1.13e-6}, // the cowbell's tail (C9 and R82 as printed, with the squares' duty above)
		{"cy.p2", 1.566e6}, {"cy.c41", 1.048e-6}, {"cy.r125", 3551}, // the cymbal's long envelope, TONE
		{"oh.p3", 1.948e6}, {"oh.r132", 119.5e3}, {"oh.c60", 7.355e-6}, // the open hat's DECAY timer
		// the clap: IC21b's band-pass a little lower (its reverb tail was 2-4 dB short at 0.4-0.8 kHz
		// and 2-3 dB over at 1.3-8 kHz) and its "reverb" envelope a little longer, each within the
		// parts' 20 %
		{"cp.c128", 5.64e-9}, {"cp.c129", 5.05e-9}, {"cp.c138", 56.4e-9},
		// and C144 (0.47/50, an electrolytic) 15 % low: the sawtooth's second and third teeth
		// 9.1-9.8 and 20.8-22.8 ms from the onset in all four real units (10.9 and 23.7 as drawn)
		{"cp.c144", 0.40e-6},
		// the step ticks: the common trig's edges into the mix, between the archive.org unit's and
		// Synthmania's levels against their voices (mix.cir; its 534 us high-pass is rtk3 x ctk3)
		{"mix.rtk3", 107e6}, {"mix.ctk3", 534e-6 / 107e6},
	};
	return c;
}

namespace {

const char *const kVoiceNames[kVoices] = {"bd", "sd", "lt", "mt", "ht", "rs", "cp", "cb", "cy", "oh", "ch"};

// The board's signals, by name: the shared ones (trig, ntrig, mono, acc, nz, wn, pn, o1-o6, hi, lo),
// each voice's data line (data.<voice>) and jack (jack.<voice>), and the signals between a voice's
// partitions (<voice>.<name>).
class Signals {
public:
	int index(const std::string &name)
	{
		const auto it = m_index.find(name);
		if (it != m_index.end())
			return it->second;
		m_index[name] = int(m_values.size());
		m_values.push_back(0);
		return int(m_values.size() - 1);
	}
	double *data() { return m_values.data(); }
	const double *data() const { return m_values.data(); }
	int size() const { return int(m_values.size()); }
	int find(const std::string &name) const
	{
		const auto it = m_index.find(name);
		return it == m_index.end() ? -1 : it->second;
	}
	double &operator[](int i) { return m_values[size_t(i)]; }
	double operator[](int i) const { return m_values[size_t(i)]; }
	void clear() { std::fill(m_values.begin(), m_values.end(), 0.0); }

private:
	std::map<std::string, int> m_index;
	std::vector<double> m_values;
};

bool sharedSignal(const std::string &n)
{
	static const char *const shared[] = {"trig", "ntrig", "mono", "acc", "nz", "wn", "pn", "o1", "o2",
	                                     "o3", "o4", "o5", "o6", "hi", "lo"};
	for (const char *s : shared)
		if (n == s)
			return true;
	return false;
}

// A netlist's input or output name to the board's signal name, for a block of voice `owner`.
std::string signalName(const std::string &n, const char *owner)
{
	if (!n.empty() && n[0] == '@') // a node between a voice's partitions
		return std::string(owner ? owner : "board") + "." + n.substr(1);
	if (sharedSignal(n) || owner == nullptr)
		return n;
	if (n == "data")
		return std::string("data.") + owner;
	if (n == "dataoh")
		return "data.oh";
	if (n == "datach")
		return "data.ch";
	if (n == "out")
		return std::string("jack.") + owner;
	if (n == "oh" || n == "ch")
		return "jack." + n;
	return std::string(owner) + "." + n;
}

// One netlist running as a circuit, wired to the signals. A block sleeps (isn't stepped, its
// outputs held) once it has settled; wake() starts it again.
struct Block {
	circuit::Circuit c;
	std::vector<std::pair<int, int>> ins;  // (input index, signal)
	std::vector<std::pair<int, int>> outs; // (output index, signal)
	std::map<std::string, int> controls;
	bool awake = true;
	bool sleeps = true;  // false: always runs (the noise generator, the metal source), or its Unit decides
	bool ownSleep = false; // a partition that may sleep within its awake voice (.part ... sleep)
	std::vector<int> partIns;              // its inputs from the other partitions (signals)
	std::vector<bool> cutOut;              // which outputs go to the other partitions (not jacks)
	std::vector<bool> backOut;             // and of those, which only to partitions run before it
	std::vector<double> partRef;           // their values when it fell asleep
	double floor = 1e-5; // outputs under this (volts) count as quiet
	int quiet = 0;       // samples the outputs have stayed quiet
	std::vector<double> ref; // the timing capacitors' voltages at the start of the quiet window
	std::vector<double> yRef; // and the outputs'
	long steps = 0;
	double seconds = 0; // stepping time (TR808_PROFILE)
	static inline int serial = 0;

	void load(const char *text, const char *owner, Signals &sig, const std::map<std::string, double> &params = {})
	{
		const std::string all = std::string(netlistText("models", netlists::models)) + "\n" + text;
		c.load(circuit::Netlist::parse(all, params));
		c.setName(std::string(owner ? owner : "board") + ":" + std::to_string(++serial));
		probeNodes.clear();
		const auto &net = c.netlist();
		ins.clear();
		outs.clear();
		partIns.clear();
		for (size_t i = 0; i < net.inputs.size(); ++i) {
			ins.push_back({int(i), sig.index(signalName(net.inputs[i], owner))});
			if (!net.inputs[i].empty() && net.inputs[i][0] == '@')
				partIns.push_back(ins.back().second);
		}
		cutOut.clear();
		for (size_t o = 0; o < net.outputs.size(); ++o) {
			outs.push_back({int(o), sig.index(signalName(net.outputs[o].name, owner))});
			cutOut.push_back(!net.outputs[o].name.empty() && net.outputs[o].name[0] == '@');
		}
		backOut = cutOut; // until Unit::finish knows the partitions' order
		controls.clear();
		for (size_t k = 0; k < net.controls.size(); ++k)
			controls[net.controls[k]] = int(k);
		floor = 1e-5; // see Unit::quietFloor
		awake = true;
		quiet = 0;
		steps = 0;
	}
	bool set(const char *name, double v)
	{
		const auto it = controls.find(name);
		if (it == controls.end() || c.controlValue(it->second) == v)
			return false;
		c.setControl(it->second, v);
		wake();
		return true;
	}
	void wake()
	{
		awake = true;
		quiet = 0;
	}
	// The scope (VoiceBoard::setProbe): outputs on these nodes after the netlist's own, read with
	// c.output(outs.size() + k). The circuit is loaded again with them, its state, rate and controls
	// kept, so each is worked out at every step as any output is.
	std::vector<int> probeNodes;
	void retap(const std::vector<int> &nodes)
	{
		if (nodes == probeNodes)
			return;
		auto net = c.netlist();
		net.outputs.resize(net.outputs.size() - probeNodes.size());
		for (size_t k = 0; k < nodes.size(); ++k)
			net.outputs.push_back({"#probe" + std::to_string(k), nodes[k], 0});
		circuit::Circuit fresh;
		fresh.setPoleTrap(c.poleTrap());
		fresh.load(net);
		fresh.setName(c.name());
		fresh.setRate(c.rate());
		for (int k = 0; k < int(net.controls.size()); ++k)
			fresh.setControl(k, c.controlValue(k));
		fresh.setState(c.state());
		c = std::move(fresh);
		probeNodes = nodes;
	}
	void takeInputs(const double *sig)
	{
		for (const auto &[i, s] : ins)
			c.setInput(i, sig[s]);
	}
	void putOutputs(double *sig) const
	{
		for (const auto &[o, s] : outs)
			sig[s] = c.output(o);
	}
	void settle(double *sig)
	{
		takeInputs(sig);
		c.solveDc();
		putOutputs(sig);
		wake();
	}
	void step(double *sig, int window)
	{
		if (!awake) {
			// a partition asleep on its own wakes when what the others give it moves (20 mV: an envelope's
	// trigger, not its creep)
			bool moved = false;
			for (size_t k = 0; k < partIns.size() && k < partRef.size(); ++k)
				moved = moved || std::abs(sig[partIns[k]] - partRef[k]) > 20e-3;
			if (!moved)
				return;
			wake();
		}
		{
			double *in = c.inputs();
			const auto *p = ins.data();
			const size_t n = ins.size();
			for (size_t k = 0; k < n; ++k)
				in[p[k].first] = sig[p[k].second];
		}
		if (kProfile) {
			const auto t0 = std::chrono::steady_clock::now();
			c.step();
			seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		} else {
			c.step();
		}
		++steps;
		const double *yo = c.outputs();
		const auto *po = outs.data();
		const size_t no = outs.size();
		if (!sleeps) { // its Unit judges it
			for (size_t k = 0; k < no; ++k)
				sig[po[k].second] = yo[po[k].first];
			return;
		}
		// Quiet: every output within `floor` of where it was when the quiet window began (audio
		// outputs settle to zero; the partitions' envelope outputs to their resting voltages).
		if (yRef.size() != no)
			yRef.resize(no);
		bool loud = false;
		for (size_t k = 0; k < no; ++k) {
			const double y = yo[po[k].first];
			sig[po[k].second] = y;
			if (quiet == 0)
				yRef[k] = y;
			// a partition sleeping on its own judges by its jacks and what it passes on down the
			// voice, not by what it gives back to the partitions before it (an envelope's last
			// trickle into its VCA, which may still creep)
			if (!(ownSleep && backOut[k]))
				loud = loud || std::abs(y - yRef[k]) > floor;
		}
		if (kSleepDebug && loud && quiet > window / 2)
			std::fprintf(stderr, "%s: output loud after %d quiet samples\n", c.netlist().nodes.back().c_str(), quiet);
		// Sleep once the outputs have stayed quiet for the window and no timing capacitor (0.1 uF
		// and up; the small ones couple noise and the metal squares into transistor bases, where
		// they keep moving at rest) has moved more than 5 mV across it.
		if (loud) {
			quiet = 0;
			return;
		}
		const int nx = c.stateCount();
		if (quiet == 0) {
			ref.resize(size_t(nx));
			for (int k = 0; k < nx; ++k)
				ref[size_t(k)] = c.capVoltage(k);
		}
		if (++quiet < window)
			return;
		for (int k = 0; k < nx; ++k)
			if (c.capValue(k) >= 0.1e-6 && std::abs(c.capVoltage(k) - ref[size_t(k)]) > 5e-3) {
				if (kSleepDebug)
					std::fprintf(stderr, "%s: capacitor %d moved %.3g V\n", c.netlist().elements.empty() ? "?" : c.netlist().nodes.back().c_str(), k,
					             c.capVoltage(k) - ref[size_t(k)]);
				quiet = 0;
				return;
			}
		partRef.clear();
		for (int k : partIns)
			partRef.push_back(sig[k]);
		awake = false;
	}
};

// A current one partition takes back from another: `to` (an input of a current source) gets the
// voltage `from` (across a resistor) times `scale` (+-1/R), one sample late.
struct Coupling {
	int from, to;
	double scale;
};

// A partition sleeping on its own (.part ... sleep) does so once its jacks are within 0.3 mV,
// -90 dBFS: a hat's VCA under a closed hat played every 8th, the cymbal's high band in its long tail.
constexpr double kOwnFloor = 3e-4;

// A voice: its partitions, run in order, and the currents between them.
struct Unit {
	std::vector<Block> blocks;
	std::vector<Coupling> couplings;
	long steps = 0;

	bool awake() const { return on; }
	void wake()
	{
		on = true;
		quiet = 0;
		for (auto &b : blocks)
			b.wake();
	}
	void substeps(int n)
	{
		for (auto &b : blocks)
			b.c.substeps(n);
	}
	void edge(int n)
	{
		for (auto &b : blocks) {
			b.c.edge(n);
			b.c.jump();
		}
	}
	void set(const char *name, double v)
	{
		bool changed = false;
		for (auto &b : blocks)
			changed = b.set(name, v) || changed;
		if (changed)
			wake();
	}
	// Voices fed the white noise or the metal squares leak a little of them at rest (the snare
	// and clap 0.03-0.2 mV at the jack, the cowbell, cymbal and hats tens of microvolts through
	// their VCAs' reverse-biased diodes; all under -90 dBFS): their partitions count as quiet within
	// 1 mV or 0.1 mV.
	void quietFloor(double volts)
	{
		floorV = volts;
		for (auto &b : blocks)
			if (b.ownSleep)
				b.floor = std::max(volts, kOwnFloor);
	}
	// After the partitions are loaded: their jacks, and the blocks run whenever the unit does.
	void finish(Signals &sig, const std::vector<std::string> &jackNames)
	{
		jacks.clear();
		for (const auto &n : jackNames)
			jacks.push_back(sig.index("jack." + n));
		for (size_t k = 0; k < blocks.size(); ++k)
			for (size_t o = 0; o < blocks[k].outs.size(); ++o) {
				bool forward = false;
				for (size_t j = k + 1; j < blocks.size(); ++j) {
					const auto &in = blocks[j].partIns;
					forward = forward || std::find(in.begin(), in.end(), blocks[k].outs[o].second) != in.end();
				}
				blocks[k].backOut[o] = blocks[k].cutOut[o] && !forward;
			}
		static const bool noOwnSleep = std::getenv("TR808_NOPARTSLEEP") != nullptr; // tools
		for (auto &b : blocks) {
			b.sleeps = b.ownSleep && !noOwnSleep;
			b.floor = b.ownSleep ? std::max(floorV, kOwnFloor) : floorV;
		}
		on = true;
		quiet = 0;
		steps = 0;
	}
	void couple(double *sig) const
	{
		for (const auto &k : couplings)
			sig[k.to] = sig[k.from] * k.scale;
	}
	void settle(double *sig)
	{
		// the partitions' operating points, a few rounds to agree on the voltages at their cuts
		for (int round = 0; round < (blocks.size() > 1 ? 8 : 1); ++round) {
			for (auto &b : blocks)
				b.settle(sig);
			couple(sig);
		}
	}
	// The unit sleeps as a whole: once its jacks have stayed within the floor of where they were
	// for the window and none of its timing capacitors (0.1 uF and up) has moved over 5 mV. Its
	// partitions' own outputs (envelopes, currents) don't count.
	std::vector<int> jacks;
	double floorV = 1e-5;
	double capMove = 5e-3; // a timing capacitor moving more than this across the window keeps it awake
	int quiet = 0;
	std::vector<double> jRef, cRef;
	bool on = true;

	double seconds = 0; // time spent stepping (with TR808_PROFILE)
	void step(double *sig, int window)
	{
		if (!on)
			return;
		if (kProfile) {
			const auto t0 = std::chrono::steady_clock::now();
			stepOnce(sig, window);
			seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		} else {
			stepOnce(sig, window);
		}
	}
	void stepOnce(double *sig, int window)
	{
		++steps;
		// a partition sleeping on its own (a VCA whose envelope is down) needs a quarter of the
		// window: its outputs are the voice's (or feed them), already under the floor
		for (auto &b : blocks)
			b.step(sig, b.ownSleep ? window / 4 : window);
		couple(sig);
		// loud: a jack moved off where it was when the quiet window began
		if (jRef.size() != jacks.size())
			jRef.resize(jacks.size());
		bool loud = quiet == 0;
		for (size_t k = 0; k < jacks.size() && !loud; ++k)
			loud = std::abs(sig[jacks[k]] - jRef[k]) > floorV;
		if (loud) {
			for (size_t k = 0; k < jacks.size(); ++k)
				jRef[k] = sig[jacks[k]];
			quiet = 1;
			cRef.clear();
			return;
		}
		if (cRef.empty()) // the window's first quiet sample: the timing capacitors' voltages
			for (const auto &b : blocks)
				for (int k = 0; k < b.c.stateCount(); ++k)
					cRef.push_back(b.c.capVoltage(k));
		if (++quiet < window)
			return;
		size_t idx = 0;
		for (const auto &b : blocks)
			for (int k = 0; k < b.c.stateCount(); ++k, ++idx)
				if (b.c.capValue(k) >= 0.1e-6 && std::abs(b.c.capVoltage(k) - cRef[idx]) > capMove) {
					quiet = 0;
					return;
				}
		on = false;
	}
	circuit::Circuit::Stats stats() const
	{
		circuit::Circuit::Stats s;
		for (const auto &b : blocks) {
			const auto &t = b.c.stats();
			s.steps += t.steps;
			s.iterations += t.iterations;
			s.factorizations += t.factorizations;
			s.linearSteps += t.linearSteps;
			s.linearizations += t.linearizations;
			s.substepped += t.substepped;
			s.failures += t.failures;
		}
		return s;
	}
	int ports() const
	{
		int n = 0;
		for (const auto &b : blocks)
			n += b.c.portCount();
		return n;
	}
};

// An HD14584 Schmitt-trigger RC oscillator: a square wave between 0 and the rail, its edges
// band-limited (polyBLEP).
struct Square {
	double phase = 0, inc = 0, duty = 0.5, level = 4.7;
	void set(double hz, double rate, double d)
	{
		inc = hz / rate;
		duty = d;
	}
	static double blep(double t, double dt)
	{
		if (t < dt) {
			t /= dt;
			return t + t - t * t - 1;
		}
		if (t > 1 - dt) {
			t = (t - 1) / dt;
			return t * t + t + t + 1;
		}
		return 0;
	}
	double next()
	{
		// high for the first `duty` of the period (the capacitor charging)
		double v = phase < duty ? 1.0 : 0.0;
		v += 0.5 * blep(phase, inc);
		double t2 = phase - duty;
		if (t2 < 0)
			t2 += 1;
		v -= 0.5 * blep(t2, inc);
		phase += inc;
		if (phase >= 1)
			phase -= 1;
		return v * level;
	}
};

} // namespace

struct VoiceBoard::Impl {
	Signals sig;
	Block accent, mtrig, noise, metal, mix;
	std::array<Unit, kVoices> units; // CH's is empty: the hats are one unit, OH's
	Unit ma;                         // the maracas side of CP/MA
	std::array<Square, 6> osc;
	std::mt19937_64 rng{0x808u};
	std::normal_distribution<double> gauss{0.0, 1.0};
	double noiseScale = 0; // volts per sample of the white source
	bool lastPulse = false;
	double monoLeft = 0; // seconds of the common trig left
	double muteLeft = 0; // seconds of the power-on mute left (Q13)
	int sMute = 0;
	uint16_t data = 0;
	bool prevMono = false;
	int window = 9600;
	long samples = 0;
	int sMono = 0, sAcc = 0, sNz = 0, sOsc[6] = {}, sData[kVoices] = {}, sJack[kVoices] = {};
	// The group: the squares, the metal source and the voices they feed (CB, CY, the hats) run
	// `over` steps a board sample (2 below 150 kHz): their swing VCAs chop at up to 7 kHz, 14 samples
	// a cycle at 96 kHz, which left the cymbal and hats 1-3 dB low at 12-20 kHz against 8x. The
	// snare, the clap and the maracas run with them: at 96 kHz their 16-20 kHz were 0.7-1.4 dB low
	// against ngspice, the capacitors' warping; their noise (wn) comes up to the group's rate through
	// a half-band (wn.up). The mixer runs at that rate too: IC4a and IC4b each roll off at 22 kHz
	// (33k and 220p), which the trapezoidal rule pulls down to 19 kHz at 96 kHz (-1 dB at 18 kHz on
	// every voice). The other voices' jacks reach it through half-band interpolators, the group's
	// through a delay of the same length; the group's jacks also come down to the board rate
	// (voiceOut) through half-bands.
	int over = 1;
	static constexpr Voice kMetal[3] = {Voice::CB, Voice::CY, Voice::OH}; // the squares feed these
	static constexpr Voice kGroup[5] = {Voice::CB, Voice::CY, Voice::OH, Voice::SD, Voice::CP};
	static constexpr int kGroupJackCount = 6;
	static constexpr Voice kGroupJacks[kGroupJackCount] = {Voice::CB, Voice::CY, Voice::OH, Voice::CH, Voice::SD, Voice::CP};
	static constexpr int kUpHalf = 8; // the interpolators' half length: flat to 20 kHz, 15 samples' delay
	std::array<Decimator, kGroupJackCount> groupDec;
	Interpolator upWn; // the noise up to the group's rate
	int sWn = 0, sWnUp = 0;
	std::array<Interpolator, kVoices> up;
	std::array<std::vector<double>, kVoices> delay; // the group's jacks, delayed as the interpolators
	int delayPos = 0;
	std::array<double, kVoices> jackOut{}; // each jack at the board rate
	int sMixIn[kVoices] = {};
	static bool grouped(Voice v)
	{
		return v == Voice::CB || v == Voice::CY || v == Voice::OH || v == Voice::CH || v == Voice::SD || v == Voice::CP;
	}
	// wn at the group's rate for this board sample's `over` steps
	void noiseUp(const double *s, double *wn)
	{
		if (over > 1)
			upWn.process(s[sWn], wn);
		else
			wn[0] = s[sWn];
	}
	// One board sample of the group, and the mixer with it into work[0 .. over).
	void stepGroup(double *s, bool mixToo, float *work)
	{
		float buf[kGroupJackCount][2];
		double upv[kVoices][2], wn[2];
		noiseUp(s, wn);
		if (mixToo)
			for (int v = 0; v < kVoices; ++v)
				if (!grouped(Voice(v))) {
					if (over > 1)
						up[size_t(v)].process(s[sJack[v]], upv[v]);
					else
						upv[v][0] = s[sJack[v]];
				}
		for (int j = 0; j < over; ++j) {
			for (int o = 0; o < 6; ++o)
				s[sOsc[o]] = osc[size_t(o)].next();
			s[sWnUp] = wn[j];
			metal.step(s, window * over);
			for (Voice v : kGroup)
				unit(v, *controls).step(s, window * over);
			if (over > 1)
				for (int k = 0; k < kGroupJackCount; ++k)
					buf[k][j] = float(s[sJack[int(kGroupJacks[k])]]);
			if (!mixToo)
				continue;
			for (int v = 0; v < kVoices; ++v) {
				if (!grouped(Voice(v))) {
					s[sMixIn[v]] = upv[v][j];
				} else if (over > 1) {
					auto &d = delay[size_t(v)];
					s[sMixIn[v]] = d[size_t(delayPos)];
					d[size_t(delayPos)] = s[sJack[v]];
				} else {
					s[sMixIn[v]] = s[sJack[v]];
				}
			}
			if (over > 1 && ++delayPos == int(delay[0].size()))
				delayPos = 0;
			mix.step(s, window * over);
			work[j] = float(mix.c.output(0) / kVoltsFullScale);
		}
		for (int v = 0; v < kVoices; ++v)
			jackOut[size_t(v)] = s[sJack[v]];
		if (over > 1)
			for (int k = 0; k < kGroupJackCount; ++k)
				jackOut[size_t(kGroupJacks[k])] = groupDec[size_t(k)].process(buf[k]);
	}

	// The block's jobs (VoiceBoard::render): first the trigger circuits, the noise, the squares and
	// the metal source through the whole block, keeping what they give the voices at each sample
	// (sharedBuf at the board's rate, subBuf at the group's); then the voices, each through the
	// block on its own copy of the signals (jobSig), their jacks into jackBuf (the group's at its
	// rate); then the mixer. The voices share nothing else within a sample (checkJobs), so this is
	// the same arithmetic as stepping them all sample by sample (renderInterleaved), which short
	// blocks still use: a trigger pulse is rendered a sample at a time.
	static constexpr int kJobCount = 10;
	static constexpr Voice kJobs[kJobCount] = {Voice::CY, Voice::OH, Voice::CP, Voice::BD, Voice::CB,
	                                           Voice::RS, Voice::SD, Voice::LT, Voice::MT, Voice::HT}; // the heaviest first
	static constexpr int kJobsMin = 16; // board samples: shorter blocks run interleaved
	Parallel *parallel = nullptr;
	bool jobsOk = false;                // checkJobs passed
	std::vector<int> sharedIdx, subIdx; // the signals written before the voices, at the board's and the group's rate
	// of those, the ones each unit reads (ma's last) and the mixer reads: (place in sharedIdx or subIdx, signal)
	std::array<std::vector<std::pair<int, int>>, kVoices + 1> unitShared, unitSub;
	std::vector<std::pair<int, int>> mixShared, mixSub;
	std::vector<double> sharedBuf, subBuf;
	std::vector<uint8_t> edgeAt, mixWakeAt; // per board sample: a trigger edge; something wakes the mixer
	std::array<std::vector<double>, kJobCount> jobSig;
	std::array<bool, kJobCount> jobRan{}; // its unit stepped this block (else jobSig is stale)
	std::array<std::vector<uint8_t>, kJobCount> awakeAt; // whether the job's unit counts as awake for the mixer
	std::array<std::vector<double>, kVoices> jackBuf;
	std::array<std::vector<int>, kVoices + 1> writes; // the signals each unit writes (ma's last)
	int blockLength = 0;
	bool blockEdge = false; // a trigger edge somewhere in the block
	const Controls *controls = nullptr;
	bool checkJobs();
	void growJobs(int boardSamples);
	void runJob(int job);
	static void runJob(void *self, int job) { static_cast<Impl *>(self)->runJob(job); }
	// One board sample of the mixer from the voices' jacks in jackBuf (the mixer's part of stepGroup).
	void mixFromJobs(int j, double *s, float *work);

	Unit &unit(Voice v, const Controls &c)
	{
		if (v == Voice::CP && c.ma)
			return ma;
		if (v == Voice::CH)
			return units[size_t(Voice::OH)];
		return units[size_t(v)];
	}
	const Unit &unit(Voice v, const Controls &c) const { return const_cast<Impl *>(this)->unit(v, c); }
	static int jobOf(Voice v)
	{
		if (v == Voice::CH)
			v = Voice::OH;
		for (int k = 0; k < kJobCount; ++k)
			if (kJobs[k] == v)
				return k;
		return -1;
	}

	// The scope (VoiceBoard::setProbe): each probe's block and output (a netlist node) or signal, and
	// where render() reads it: after the trigger circuits and the noise (Shared), the squares and the
	// metal source (Sub), in a voice's job (Job), after the mixer (Mix). Interleaved, every probe is
	// read at the end of the board sample.
	enum class At { Off, Shared, Sub, Job, Mix };
	struct Probe {
		At at = At::Off;
		Block *block = nullptr;
		int output = -1, signal = -1, job = -1;
	};
	std::array<Probe, VoiceBoard::kProbes> probes;
	std::array<std::string, VoiceBoard::kProbes> probeNames; // set again after a prepare()
	std::array<std::vector<float>, VoiceBoard::kProbes> probeBuf;
	std::vector<uint16_t> gateBuf;
	bool probing = false;
	std::array<bool, kJobCount> jobProbed{};
	double tp4PerRms = 0; // TP4's rms for a noiseRms of 1 (VoiceBoard::tp4PerNoiseRms)
	float probeValue(const Probe &p, const double *s) const { return float(p.block ? p.block->c.output(p.output) : s[p.signal]); }
	void capture(At at, int j, const double *s)
	{
		for (size_t c = 0; c < probes.size(); ++c)
			if (probes[c].at == at)
				probeBuf[c][size_t(j)] = probeValue(probes[c], s);
	}
	void captureJob(int job, int j, const double *s)
	{
		for (size_t c = 0; c < probes.size(); ++c)
			if (probes[c].at == At::Job && probes[c].job == job)
				probeBuf[c][size_t(j)] = probeValue(probes[c], s);
	}
	void growProbes(int boardSamples)
	{
		if (int(gateBuf.size()) >= boardSamples)
			return;
		gateBuf.resize(size_t(boardSamples));
		for (auto &b : probeBuf)
			b.resize(size_t(boardSamples));
	}
};

VoiceBoard::VoiceBoard() : m_impl(std::make_unique<Impl>()) {}
VoiceBoard::~VoiceBoard() = default;

void VoiceBoard::setCalibration(const Calibration &c)
{
	m_cal = c;
	prepare(m_hostRate);
}

void VoiceBoard::prepare(double hostRate)
{
	m_hostRate = hostRate;
	m_factor = hostRate < 80000 ? 2 : 1;
	if (const char *f = std::getenv("TR808_FACTOR")) // tools: force the oversampling
		m_factor = std::max(1, std::atoi(f));
	m_rate = hostRate * m_factor;
	auto &im = *m_impl;
	im.over = m_rate < 150e3 ? 2 : 1;
	if (const char *g = std::getenv("TR808_GROUP")) // tools: the metal group's factor (1 or 2)
		im.over = std::clamp(std::atoi(g), 1, 2);
	const int stages = m_factor >= 8 ? 3 : m_factor >= 4 ? 2 : m_factor >= 2 ? 1 : 0;
	m_factor = 1 << stages;
	// the mixer runs at the metal group's rate: one more stage down from there
	m_decimator.setStages(std::min(3, stages + (im.over == 2 ? 1 : 0)));
	if (im.over == 2 && stages == 3)
		im.over = 1;
	m_work.assign(size_t(m_factor * im.over), 0.0f);
	for (auto &d : m_jackDecimators)
		d.setStages(stages);
	m_jackWork.assign(size_t(kVoices * m_factor), 0.0f);
	const auto &cal = m_cal;
	auto &sig = im.sig;
	std::map<std::string, double> sd, cp, rs;
	if (cal.lateSnare) {
		sd["c58"] = 0.056e-6;
		sd["c61"] = 0.015e-6;
		sd["r191"] = 10e3;
		sd["r200"] = 0.1;
	}
	if (cal.lateClap)
		cp["r200cp"] = 10e3;
	rs["r313"] = cal.claveR313;
	im.accent.load(netlistText("accent", netlists::accent), nullptr, sig);
	{
		std::map<std::string, double> mt;
		for (const auto &[k, v] : cal.parts)
			if (k.rfind("mtrig.", 0) == 0)
				mt[k.substr(6)] = v;
		im.mtrig.load(netlistText("mtrig", netlists::mtrig), nullptr, sig, mt);
	}
	{
		std::map<std::string, double> nz; // the first noise generator's parts (core/revisions.h)
		for (const auto &[k, v] : cal.parts)
			if (k.rfind("noise.", 0) == 0)
				nz[k.substr(6)] = v;
		im.noise.load(netlistText("noise", netlists::noise), nullptr, sig, nz);
	}
	im.metal.load(netlistText("metal", netlists::metal), nullptr, sig);
	{
		std::map<std::string, double> mx;
		for (const auto &[k, v] : cal.parts)
			if (k.rfind("mix.", 0) == 0)
				mx[k.substr(4)] = v;
		im.mix.load(netlistText("mix", netlists::mix), nullptr, sig, mx);
	}
	// the mixer's inputs are the jacks, at its rate (Impl::stepGroup)
	for (int v = 0; v < kVoices; ++v) {
		im.sJack[v] = sig.index(std::string("jack.") + kVoiceNames[v]);
		im.sMixIn[v] = sig.index(std::string("mixin.") + kVoiceNames[v]);
	}
	for (auto &[i, s] : im.mix.ins) {
		const std::string &n = im.mix.c.netlist().inputs[size_t(i)];
		for (int v = 0; v < kVoices; ++v)
			if (n == kVoiceNames[v])
				s = im.sMixIn[v];
	}
	// its LO output isn't the metal source's lo (the cymbal's input)
	for (auto &[o, s] : im.mix.outs)
		if (im.mix.c.netlist().outputs[size_t(o)].name == "lo")
			s = sig.index("mix.lo");
	auto single = [&](Unit &u, const char *text, const char *owner, const std::map<std::string, double> &params = {}) {
		u.blocks.assign(1, Block{});
		u.blocks[0].load(text, owner, sig, params);
		u.couplings.clear();
		u.steps = 0;
	};
	const bool full = std::getenv("TR808_FULL") != nullptr;
	// A voice from its netlist: in the partitions its .part lines declare, or whole (TR808_FULL, or
	// a netlist without them).
	auto voice = [&](Unit &u, const char *text, const char *owner, std::map<std::string, double> params = {}) {
		for (const auto &[k, v] : cal.parts)
			if (k.rfind(std::string(owner) + ".", 0) == 0)
				params[k.substr(std::string(owner).size() + 1)] = v;
		const auto split = full ? std::vector<circuit::Netlist::Partition>{} : circuit::Netlist::partitions(text);
		if (split.empty()) {
			single(u, text, owner, params);
			return;
		}
		u.blocks.assign(split.size(), Block{});
		for (size_t k = 0; k < split.size(); ++k) {
			u.blocks[k].load(split[k].text.c_str(), owner, sig, params);
			u.blocks[k].c.setName(std::string(owner) + ":" + split[k].name);
			u.blocks[k].ownSleep = split[k].sleeps;
		}
		u.couplings.clear();
		u.steps = 0;
	};
	auto &U = im.units;
	voice(U[size_t(Voice::BD)], netlistText("bd", netlists::bd), "bd");
	voice(U[size_t(Voice::SD)], netlistText("sd", netlists::sd), "sd", sd);
	voice(U[size_t(Voice::LT)], netlistText("lt", netlists::lt), "lt");
	voice(U[size_t(Voice::MT)], netlistText("mt", netlists::mt), "mt");
	voice(U[size_t(Voice::HT)], netlistText("ht", netlists::ht), "ht");
	voice(U[size_t(Voice::RS)], netlistText("rs", netlists::rs), "rs", rs);
	// The rim shot's and claves' op-amps' poles by the trapezoidal rule (Circuit::setPoleTrap):
	// backward Euler damps the claves' ring, which left an unaccented hit 5 dB low and its 8-20 kHz
	// 4-5 dB low against ngspice; trapezoidal, it's within 0.2 dB, with no ringing (above 22 kHz
	// as ngspice). The other voices measure the same either way (docs/model.md, "SPICE").
	// TR808_POLE_BE=1 keeps backward Euler (tools).
	if (!std::getenv("TR808_POLE_BE"))
		for (auto &b : U[size_t(Voice::RS)].blocks)
			b.c.setPoleTrap(true);
	voice(U[size_t(Voice::CP)], netlistText("cp", netlists::cp), "cp", cp);
	voice(im.ma, netlistText("ma", netlists::ma), "cp");
	voice(U[size_t(Voice::CB)], netlistText("cb", netlists::cb), "cb");
	voice(U[size_t(Voice::CY)], netlistText("cy", netlists::cy), "cy");
	voice(U[size_t(Voice::OH)], netlistText("hats", netlists::hats), "oh");
	U[size_t(Voice::CH)] = Unit{};
	for (int v = 0; v < kVoices; ++v)
		if (v != int(Voice::CH))
			U[size_t(v)].finish(sig, v == int(Voice::OH) ? std::vector<std::string>{"oh", "ch"} : std::vector<std::string>{kVoiceNames[v]});
	im.ma.finish(sig, {"cp"});
	for (Voice v : {Voice::SD, Voice::CP})
		U[size_t(v)].quietFloor(1e-3);
	im.ma.quietFloor(1e-3);
	// The cowbell's C9 runs down through D3/D4's knees for seconds after a hit, 0.3 V at 0.5 s and
	// creeping (20 mV a window at 0.8 s): a tail of a few hundred microvolts at the jack, under
	// -100 dBFS at the master by 0.7 s. Sleep once that is all that moves.
	U[size_t(Voice::CB)].quietFloor(1e-3);
	U[size_t(Voice::CB)].capMove = 25e-3;
	for (Voice v : {Voice::CY, Voice::OH})
		U[size_t(v)].quietFloor(1e-4);
	im.noise.sleeps = false;
	im.metal.sleeps = false;
	const double groupRate = m_rate * im.over;
	for (auto &d : im.groupDec)
		d.setStages(im.over == 2 ? 1 : 0, 8);
	for (auto &u : im.up)
		u.design(Impl::kUpHalf);
	im.upWn.design(Impl::kUpHalf);
	for (auto &d : im.delay)
		d.assign(size_t(im.up[0].delay()), 0.0);
	for (Block *b : {&im.accent, &im.mtrig, &im.noise})
		b->c.setRate(m_rate);
	im.metal.c.setRate(groupRate);
	im.mix.c.setRate(groupRate);
	for (int v = 0; v <= kVoices; ++v)
		for (auto &b : (v == kVoices ? im.ma : U[size_t(v)]).blocks)
			b.c.setRate(Impl::grouped(v < kVoices ? Voice(v) : Voice::CP) ? groupRate : m_rate); // the maracas are the clap's
	im.sMono = sig.index("mono");
	im.sMute = sig.index("mute");
	im.sAcc = sig.index("acc");
	im.sNz = sig.index("nz");
	// the snare's, the clap's and the maracas' noise at the group's rate
	im.sWn = sig.index("wn");
	im.sWnUp = sig.index("wn.up");
	for (int u : {int(Voice::SD), int(Voice::CP), kVoices})
		for (auto &b : (u == kVoices ? im.ma : U[size_t(u)]).blocks)
			for (auto &in : b.ins)
				if (in.second == im.sWn)
					in.second = im.sWnUp;
	for (int k = 0; k < 6; ++k)
		im.sOsc[k] = sig.index("o" + std::to_string(k + 1));
	for (int v = 0; v < kVoices; ++v)
		im.sData[v] = sig.index(std::string("data.") + kVoiceNames[v]);
	// The metal oscillators: TM1/TM2 trimmed, the other four from their parts.
	const double hz[6] = {cal.cbHz1, cal.cbHz2, cal.metalHz[0], cal.metalHz[1], cal.metalHz[2], cal.metalHz[3]};
	for (int k = 0; k < 6; ++k)
		im.osc[size_t(k)].set(hz[k], groupRate, cal.metalDuty);
	im.noiseScale = noiseScaleFor(cal.noiseRms, m_rate);
	{
		// What TP4 reads for a noiseRms of 1 (tp4PerNoiseRms): the noise circuit's impulse response
		// from Q35's emitter to IC24a's output, on a copy of it, at the board's rate
		circuit::Circuit c = im.noise.c;
		const int nx = c.stateCount();
		c.setState({std::vector<double>(size_t(nx), 0.0), std::vector<double>(size_t(nx), 0.0), std::vector<double>(size_t(c.portCount()), 0.0), {}});
		const int in = c.netlist().input("nz"), out = c.netlist().output("wn");
		double energy = 0;
		for (int k = 0; k < int(0.05 * m_rate) && in >= 0 && out >= 0; ++k) {
			c.setInput(in, k == 0 ? 1.0 : 0.0);
			c.step();
			energy += c.output(out) * c.output(out);
		}
		im.tp4PerRms = noiseScaleFor(1.0, m_rate, true) * std::sqrt(energy);
	}
	im.window = int(0.1 * m_rate);
	im.jobsOk = im.checkJobs() && !std::getenv("TR808_INTERLEAVED"); // tools: the voices sample by sample
	for (auto &js : im.jobSig)
		js.assign(size_t(sig.size()), 0.0);
	im.growJobs(4096);
	im.controls = &m_controls;
	setControls(m_controls);
	for (int c = 0; c < kProbes; ++c) // the scope's probes, on the new circuits
		setProbe(c, std::string(im.probeNames[size_t(c)]));
	reset();
}

void VoiceBoard::reset()
{
	auto &im = *m_impl;
	auto &sig = im.sig;
	sig.clear();
	sig[im.sMono] = 15;
	sig[im.sMute] = -15; // the detector's point A, settled: Q13 off
	im.muteLeft = 0;
	im.monoLeft = 0;
	im.data = 0;
	im.prevMono = false;
	im.lastPulse = false;
	for (int v = 0; v < kVoices; ++v)
		setPlugged(Voice(v), m_plugged[size_t(v)]);
	// the oscillators start low, from a discharged capacitor: settle the metal filters there
	for (int o = 0; o < 6; ++o) {
		im.osc[size_t(o)].phase = im.osc[size_t(o)].duty;
		sig[im.sOsc[o]] = 0;
	}
	for (Block *b : {&im.accent, &im.mtrig, &im.noise, &im.metal})
		b->settle(sig.data());
	for (auto &u : im.units)
		u.settle(sig.data());
	im.ma.settle(sig.data());
	// The metal squares never stop, and the swing VCAs' coupling capacitors (the cowbell's C32 and
	// C33, 10 uF) sit where the squares' rectified drive keeps them, a tenth of a volt off the
	// operating point of the squares' first instant: until they get there the VCAs hardly chop and
	// the cowbell is 12 dB down. Run the voices fed by the squares until they sleep, settled
	// (at most 2 s).
	double *s = sig.data();
	const long warm = long(2.0 * m_rate);
	for (long i = 0; i < warm; ++i) {
		im.stepGroup(s, false, nullptr);
		bool any = false;
		for (Voice v : Impl::kMetal)
			any = any || im.units[size_t(v)].awake();
		if (!any)
			break;
	}
	// the mixer, and the filters and delays into and out of the group's rate, from where the
	// voices settled
	for (int v = 0; v < kVoices; ++v) {
		const double j = s[im.sJack[v]];
		s[im.sMixIn[v]] = j;
		im.jackOut[size_t(v)] = j;
		double two[2];
		im.up[size_t(v)].reset();
		for (int n = 0; n < 4 * Impl::kUpHalf; ++n)
			im.up[size_t(v)].process(j, two);
		std::fill(im.delay[size_t(v)].begin(), im.delay[size_t(v)].end(), j);
	}
	im.delayPos = 0;
	{
		double two[2];
		im.upWn.reset();
		for (int n = 0; n < 4 * Impl::kUpHalf; ++n)
			im.upWn.process(s[im.sWn], two);
	}
	for (int k = 0; k < Impl::kGroupJackCount; ++k) {
		auto &d = im.groupDec[size_t(k)];
		d.reset();
		const float j = float(s[im.sJack[int(Impl::kGroupJacks[k])]]), two[2] = {j, j};
		for (int n = 0; n < 64; ++n)
			d.process(two);
	}
	im.mix.settle(s);
	m_decimator.reset();
	for (auto &d : m_jackDecimators)
		d.reset();
}

void VoiceBoard::coldStart()
{
	auto &im = *m_impl;
	im.sig.clear(); // every signal between the blocks at 0 V, as the discharged board's
	double *s = im.sig.data();
	im.muteLeft = 1.5;
	s[im.sMute] = 0;
	im.monoLeft = 0;
	im.prevMono = false;
	s[im.sMono] = 15;
	auto discharge = [](Block &b) {
		const auto &c = b.c;
		b.c.setState({std::vector<double>(size_t(c.stateCount()), 0.0), std::vector<double>(size_t(c.stateCount()), 0.0),
		              std::vector<double>(size_t(c.portCount()), 0.0), {}});
		b.wake();
	};
	for (Block *b : {&im.accent, &im.mtrig, &im.noise, &im.metal, &im.mix})
		discharge(*b);
	for (int v = 0; v <= kVoices; ++v) {
		Unit &u = v == kVoices ? im.ma : im.units[size_t(v)];
		for (auto &b : u.blocks)
			discharge(b);
		u.wake();
	}
}

void VoiceBoard::setControls(const Controls &c)
{
	m_controls = c;
	auto &im = *m_impl;
	// the knobs' ticks to their pots' rotation (Calibration::knobLaw)
	const auto k = [this](const char *name, double tick) { return m_cal.knob(name, tick); };
	auto &u = im.units;
	im.accent.set("accent", k("accent", c.accent));
	u[size_t(Voice::BD)].set("level", k("bdLevel", c.bdLevel));
	u[size_t(Voice::BD)].set("tone", k("bdTone", c.bdTone));
	u[size_t(Voice::BD)].set("decay", k("bdDecay", c.bdDecay));
	u[size_t(Voice::SD)].set("level", k("sdLevel", c.sdLevel));
	u[size_t(Voice::SD)].set("tone", k("sdTone", c.sdTone));
	u[size_t(Voice::SD)].set("snappy", k("sdSnappy", c.sdSnappy));
	u[size_t(Voice::LT)].set("level", k("ltLevel", c.ltLevel));
	u[size_t(Voice::LT)].set("tuning", k("ltTuning", c.ltTuning));
	u[size_t(Voice::LT)].set("conga", c.lc ? 1 : 0);
	u[size_t(Voice::MT)].set("level", k("mtLevel", c.mtLevel));
	u[size_t(Voice::MT)].set("tuning", k("mtTuning", c.mtTuning));
	u[size_t(Voice::MT)].set("conga", c.mc ? 1 : 0);
	u[size_t(Voice::HT)].set("level", k("htLevel", c.htLevel));
	u[size_t(Voice::HT)].set("tuning", k("htTuning", c.htTuning));
	u[size_t(Voice::HT)].set("conga", c.hc ? 1 : 0);
	u[size_t(Voice::RS)].set("level", k("rsLevel", c.rsLevel));
	u[size_t(Voice::RS)].set("claves", c.cl ? 1 : 0);
	u[size_t(Voice::CP)].set("level", k("cpLevel", c.cpLevel));
	u[size_t(Voice::CP)].set("offset", m_cal.cpOffset);
	im.ma.set("level", k("cpLevel", c.cpLevel));
	u[size_t(Voice::CB)].set("level", k("cbLevel", c.cbLevel));
	u[size_t(Voice::CY)].set("level", k("cyLevel", c.cyLevel));
	u[size_t(Voice::CY)].set("tone", k("cyTone", c.cyTone));
	u[size_t(Voice::CY)].set("decay", k("cyDecay", c.cyDecay));
	u[size_t(Voice::OH)].set("levelo", k("ohLevel", c.ohLevel));
	u[size_t(Voice::OH)].set("levelc", k("chLevel", c.chLevel));
	u[size_t(Voice::OH)].set("decay", k("ohDecay", c.ohDecay));
	im.mix.set("volume", c.volume);
}

void VoiceBoard::setPlugged(Voice v, bool plugged)
{
	static const char *const kPlug[kVoices] = {"plug_bd", "plug_sd", "plug_lt", "plug_mt", "plug_ht", "plug_rs",
	                                           "plug_cp", "plug_cb", "plug_cy", "plug_oh", "plug_ch"};
	auto &im = *m_impl;
	const double on = plugged ? 1 : 0;
	m_plugged[size_t(v)] = plugged;
	im.mix.set(kPlug[size_t(v)], on);
	if (v == Voice::OH || v == Voice::CH) {
		im.units[size_t(Voice::OH)].set(kPlug[size_t(v)], on); // the hats' netlist has both jacks
	} else {
		im.units[size_t(v)].set("plug", on);
		if (v == Voice::CP)
			im.ma.set("plug", on); // the maracas share the clap's jack
	}
}

void VoiceBoard::setLines(bool stepPulse, uint16_t data)
{
	auto &im = *m_impl;
	if (stepPulse && !im.lastPulse)
		im.monoLeft = m_cal.monoMs * 1e-3;
	im.lastPulse = stepPulse;
	im.data = data;
}

bool VoiceBoard::Impl::checkJobs()
{
	// Who writes each signal: the circuits before the voices (-2), the mixer (-3), a voice's unit
	// (its index; MA's is CP's, one job), or nothing (-1, a constant); none twice. Then each reads
	// only its own, the circuits' before it, or constants, and no voice off the board's rate reads
	// the group's (it would see them a sample late, interleaved).
	const int n = sig.size();
	std::vector<int> owner(size_t(n), -1);
	bool ok = true;
	auto claim = [&](int sg, int who) {
		if (owner[size_t(sg)] != -1 && owner[size_t(sg)] != who)
			ok = false;
		owner[size_t(sg)] = who;
	};
	std::vector<char> sub(size_t(n), 0);
	sharedIdx.clear();
	subIdx.clear();
	auto serial = [&](int sg, bool groupRate) {
		claim(sg, -2);
		if (groupRate)
			sub[size_t(sg)] = 1;
	};
	for (int sg : {sMono, sMute, sAcc, sNz})
		serial(sg, false);
	for (int v = 0; v < kVoices; ++v)
		serial(sData[v], false);
	for (const Block *b : {&accent, &mtrig, &noise})
		for (const auto &o : b->outs)
			serial(o.second, false);
	for (int k = 0; k < 6; ++k)
		serial(sOsc[k], true);
	for (const auto &o : metal.outs)
		serial(o.second, true);
	serial(sWnUp, true);
	auto unitAt = [&](int u) -> Unit & { return u == kVoices ? ma : units[size_t(u)]; };
	auto idOf = [](int u) { return u == kVoices ? int(Voice::CP) : u; };
	for (int u = 0; u <= kVoices; ++u) {
		auto &w = writes[size_t(u)];
		w.clear();
		for (const auto &b : unitAt(u).blocks)
			for (const auto &o : b.outs)
				w.push_back(o.second);
		for (const auto &c : unitAt(u).couplings)
			w.push_back(c.to);
		std::sort(w.begin(), w.end());
		w.erase(std::unique(w.begin(), w.end()), w.end());
		for (int sg : w)
			claim(sg, idOf(u));
	}
	for (const auto &o : mix.outs)
		claim(o.second, -3);
	for (int v = 0; v < kVoices; ++v)
		claim(sMixIn[v], -3);
	auto reads = [&](int sg, int who, bool groupRate) {
		const int o = owner[size_t(sg)];
		if (!(o == -1 || o == -2 || o == who) || (sub[size_t(sg)] && !groupRate))
			ok = false;
	};
	for (const Block *b : {&accent, &mtrig, &noise})
		for (const auto &in : b->ins)
			reads(in.second, -2, false);
	for (const auto &in : metal.ins)
		reads(in.second, -2, true);
	for (int u = 0; u <= kVoices; ++u) {
		const bool groupRate = grouped(u < kVoices ? Voice(u) : Voice::CP); // the maracas are the clap's
		for (const auto &b : unitAt(u).blocks)
			for (const auto &in : b.ins)
				reads(in.second, idOf(u), groupRate);
		for (const auto &c : unitAt(u).couplings)
			reads(c.from, idOf(u), groupRate);
	}
	for (const auto &in : mix.ins)
		reads(in.second, -3, true);
	for (int sg = 0; sg < n; ++sg)
		if (owner[size_t(sg)] == -2)
			(sub[size_t(sg)] ? subIdx : sharedIdx).push_back(sg);
	// which of them each job reads (a job's copy of the signals only needs those each sample)
	auto readList = [&](const std::vector<int> &readsOf, const std::vector<int> &idx) {
		std::vector<std::pair<int, int>> l;
		for (size_t k = 0; k < idx.size(); ++k)
			if (std::find(readsOf.begin(), readsOf.end(), idx[k]) != readsOf.end())
				l.push_back({int(k), idx[k]});
		return l;
	};
	for (int u = 0; u <= kVoices; ++u) {
		std::vector<int> r;
		for (const auto &b : unitAt(u).blocks)
			for (const auto &in : b.ins)
				r.push_back(in.second);
		for (const auto &c : unitAt(u).couplings)
			r.push_back(c.from);
		unitShared[size_t(u)] = readList(r, sharedIdx);
		unitSub[size_t(u)] = readList(r, subIdx);
	}
	{
		std::vector<int> r;
		for (const auto &in : mix.ins)
			r.push_back(in.second);
		mixShared = readList(r, sharedIdx);
		mixSub = readList(r, subIdx);
	}
	return ok;
}

void VoiceBoard::Impl::growJobs(int boardSamples)
{
	// a block larger than any before: grows once (prepare sizes them for 4096)
	if (int(edgeAt.size()) >= boardSamples)
		return;
	const size_t n = size_t(boardSamples);
	sharedBuf.assign(n * sharedIdx.size(), 0.0);
	subBuf.assign(n * size_t(over) * subIdx.size(), 0.0);
	edgeAt.assign(n, 0);
	mixWakeAt.assign(n, 0);
	for (auto &a : awakeAt)
		a.assign(n, 0);
	for (auto &b : jackBuf)
		b.assign(n * size_t(over), 0.0);
}

void VoiceBoard::Impl::runJob(int job)
{
	const Voice v = kJobs[job];
	Unit &u = unit(v, *controls);
	const int N = blockLength, ns = int(sharedIdx.size()), nsub = int(subIdx.size());
	const int ui = v == Voice::CP && controls->ma ? kVoices : int(v);
	const auto &readShared = unitShared[size_t(ui)], &readSub = unitSub[size_t(ui)];
	const bool group = grouped(v);
	uint8_t *awake = awakeAt[size_t(job)].data();
	// the voices whose trigger edges reach this unit (CH's is OH's, CP's is MA's with MARACAS on)
	bool hit = false;
	for (int w = 0; w < kVoices; ++w)
		hit = hit || ((data & (1u << w)) && &unit(Voice(w), *controls) == &u);
	if (!u.awake() && !(hit && blockEdge)) {
		// asleep through the block (its steps do nothing): its jacks hold
		const size_t len = size_t(N) * size_t(group ? over : 1);
		std::fill(awake, awake + N, uint8_t(0));
		std::fill(jackBuf[size_t(v)].begin(), jackBuf[size_t(v)].begin() + long(len), sig[sJack[int(v)]]);
		if (v == Voice::OH)
			std::fill(jackBuf[size_t(Voice::CH)].begin(), jackBuf[size_t(Voice::CH)].begin() + long(len), sig[sJack[int(Voice::CH)]]);
		jobRan[size_t(job)] = false;
		if (probing && jobProbed[size_t(job)])
			for (size_t c = 0; c < probes.size(); ++c)
				if (probes[c].at == At::Job && probes[c].job == job)
					std::fill(probeBuf[c].begin(), probeBuf[c].begin() + N, probeValue(probes[c], sig.data()));
		return;
	}
	const bool probed = probing && jobProbed[size_t(job)];
	jobRan[size_t(job)] = true;
	auto &js = jobSig[size_t(job)];
	std::copy(sig.data(), sig.data() + js.size(), js.begin()); // the board's signals are only read meanwhile
	double *s = js.data();
	for (int j = 0; j < N; ++j) {
		const double *sh = &sharedBuf[size_t(j) * size_t(ns)];
		for (const auto &[k, sg] : readShared)
			s[sg] = sh[k];
		if (hit && edgeAt[size_t(j)]) {
			// a trigger edge: wake the voice and step it finely through it, the first quarter steps
			// by backward Euler (Circuit::edge)
			for (int w = 0; w < kVoices; ++w)
				if ((data & (1u << w)) && &unit(Voice(w), *controls) == &u) {
					const int m = grouped(Voice(w)) ? over : 1; // the same time at the group's rate
					u.wake();
					u.substeps(kEdgeSubsteps * m);
					u.edge(kEdgeBackwardEuler * m);
				}
		}
		if (!group) {
			u.step(s, window);
			awake[j] = u.awake();
			jackBuf[size_t(v)][size_t(j)] = s[sJack[int(v)]];
			if (probed)
				captureJob(job, j, s);
			continue;
		}
		awake[j] = u.awake(); // the mixer wakes on the group's state before its step
		for (int jj = 0; jj < over; ++jj) {
			const double *sb = &subBuf[(size_t(j) * size_t(over) + size_t(jj)) * size_t(nsub)];
			for (const auto &[k, sg] : readSub)
				s[sg] = sb[k];
			u.step(s, window * over);
			const size_t at = size_t(j) * size_t(over) + size_t(jj);
			jackBuf[size_t(v)][at] = s[sJack[int(v)]];
			if (v == Voice::OH)
				jackBuf[size_t(Voice::CH)][at] = s[sJack[int(Voice::CH)]];
		}
		if (probed)
			captureJob(job, j, s);
	}
}

void VoiceBoard::Impl::mixFromJobs(int j, double *s, float *work)
{
	float buf[kGroupJackCount][2];
	double upv[kVoices][2];
	for (int v = 0; v < kVoices; ++v)
		if (!grouped(Voice(v))) {
			if (over > 1)
				up[size_t(v)].process(jackBuf[size_t(v)][size_t(j)], upv[v]);
			else
				upv[v][0] = jackBuf[size_t(v)][size_t(j)];
		}
	const int nsub = int(subIdx.size());
	for (int jj = 0; jj < over; ++jj) {
		const size_t at = size_t(j) * size_t(over) + size_t(jj);
		const double *sb = &subBuf[at * size_t(nsub)];
		for (const auto &[k, sg] : mixSub)
			s[sg] = sb[k];
		if (over > 1)
			for (int k = 0; k < kGroupJackCount; ++k)
				buf[k][jj] = float(jackBuf[size_t(kGroupJacks[k])][at]);
		for (int v = 0; v < kVoices; ++v) {
			if (!grouped(Voice(v))) {
				s[sMixIn[v]] = upv[v][jj];
			} else if (over > 1) {
				auto &d = delay[size_t(v)];
				s[sMixIn[v]] = d[size_t(delayPos)];
				d[size_t(delayPos)] = jackBuf[size_t(v)][at];
			} else {
				s[sMixIn[v]] = jackBuf[size_t(v)][at];
			}
		}
		if (over > 1 && ++delayPos == int(delay[0].size()))
			delayPos = 0;
		mix.step(s, window * over);
		work[jj] = float(mix.c.output(0) / kVoltsFullScale);
	}
	if (probing)
		capture(At::Mix, j, s);
	for (int v = 0; v < kVoices; ++v)
		jackOut[size_t(v)] = grouped(Voice(v)) ? jackBuf[size_t(v)][size_t(j) * size_t(over) + size_t(over - 1)] : jackBuf[size_t(v)][size_t(j)];
	if (over > 1)
		for (int k = 0; k < kGroupJackCount; ++k)
			jackOut[size_t(kGroupJacks[k])] = groupDec[size_t(k)].process(buf[k]);
}

void VoiceBoard::setParallel(Parallel *p)
{
	m_impl->parallel = p;
}

void VoiceBoard::render(float *out, int n, float *const *jacks)
{
	auto &im = *m_impl;
	const int N = n * m_factor; // board samples
	if (!im.jobsOk || N < Impl::kJobsMin) {
		renderInterleaved(out, n, jacks);
		return;
	}
	im.growJobs(N);
	if (im.probing)
		im.growProbes(N);
	const double dt = 1.0 / m_rate;
	double *s = im.sig.data();
	const int ns = int(im.sharedIdx.size()), nsub = int(im.subIdx.size()), over = im.over;
	bool blockEdge = false;
	// The common trig's monostable and the instrument data lines, the trigger circuits, the noise,
	// the squares and the metal source, through the block.
	for (int j = 0; j < N; ++j) {
		++im.samples;
		const bool mono = im.monoLeft > 0;
		if (mono)
			im.monoLeft -= dt;
		s[im.sMono] = mono ? 0.0 : 15.0;
		bool mixWake = false;
		if (im.muteLeft > 0) { // the power-on detector lets Q13 go
			im.muteLeft -= dt;
			if (im.muteLeft <= 0) {
				s[im.sMute] = -15;
				mixWake = true;
			}
		}
		const uint16_t data = im.data;
		s[im.sAcc] = data & kBitAC ? 5.0 : 0.0;
		for (int v = 0; v < kVoices; ++v)
			s[im.sData[v]] = data & (1u << v) ? 5.0 : 0.0;
		const bool edge = mono != im.prevMono;
		if (edge) {
			// a trigger edge: the trigger circuits stepped finely through it (the voices it reaches
			// in their jobs), and the step tick into the mixer (mix.cir)
			im.accent.wake();
			im.mtrig.wake();
			mixWake = true;
			im.accent.c.substeps(kEdgeSubsteps);
			im.mtrig.c.substeps(kEdgeSubsteps);
			im.accent.c.edge(kEdgeBackwardEuler);
			im.mtrig.c.edge(kEdgeBackwardEuler);
			im.accent.c.jump();
			im.mtrig.c.jump();
			im.prevMono = mono;
		}
		im.edgeAt[size_t(j)] = edge;
		blockEdge = blockEdge || edge;
		im.mixWakeAt[size_t(j)] = mixWake;
		im.accent.step(s, im.window);
		im.mtrig.step(s, im.window);
		s[im.sNz] = im.gauss(im.rng) * im.noiseScale;
		im.noise.step(s, im.window);
		double *sh = &im.sharedBuf[size_t(j) * size_t(ns)];
		for (int k = 0; k < ns; ++k)
			sh[k] = s[im.sharedIdx[size_t(k)]];
		double wn[2];
		im.noiseUp(s, wn);
		for (int jj = 0; jj < over; ++jj) {
			for (int o = 0; o < 6; ++o)
				s[im.sOsc[o]] = im.osc[size_t(o)].next();
			s[im.sWnUp] = wn[jj];
			im.metal.step(s, im.window * over);
			double *sb = &im.subBuf[(size_t(j) * size_t(over) + size_t(jj)) * size_t(nsub)];
			for (int k = 0; k < nsub; ++k)
				sb[k] = s[im.subIdx[size_t(k)]];
			if (im.probing && jj == over - 1)
				im.capture(Impl::At::Sub, j, s);
		}
		if (im.probing) {
			im.gateBuf[size_t(j)] = mono ? uint16_t(data & 0x0fff) : uint16_t(0);
			im.capture(Impl::At::Shared, j, s);
		}
	}
	// The voices: their jobs at once where there are threads for them; then what they wrote back
	// on the board.
	im.blockLength = N;
	im.blockEdge = blockEdge;
	im.controls = &m_controls;
	if (im.parallel)
		im.parallel->run(Impl::kJobCount, &Impl::runJob, &im);
	else
		for (int job = 0; job < Impl::kJobCount; ++job)
			im.runJob(job);
	for (int job = 0; job < Impl::kJobCount; ++job) {
		if (!im.jobRan[size_t(job)])
			continue;
		const Voice v = Impl::kJobs[job];
		const int u = v == Voice::CP && m_controls.ma ? kVoices : int(v);
		const double *js = im.jobSig[size_t(job)].data();
		for (int sg : im.writes[size_t(u)])
			s[sg] = js[sg];
	}
	// The mixer, and down to the host's rate.
	for (int i = 0, j = 0; i < n; ++i) {
		for (int k = 0; k < m_factor; ++k, ++j) {
			const double *sh = &im.sharedBuf[size_t(j) * size_t(ns)];
			for (const auto &[q, sg] : im.mixShared) // as the mixer saw them: the step ticks' trig, the mute
				s[sg] = sh[q];
			bool any = im.mixWakeAt[size_t(j)] != 0;
			for (int job = 0; job < Impl::kJobCount; ++job)
				any = any || im.awakeAt[size_t(job)][size_t(j)];
			if (any)
				im.mix.wake();
			im.mixFromJobs(j, s, &m_work[size_t(k * over)]);
			if (jacks)
				for (int v = 0; v < kVoices; ++v)
					m_jackWork[size_t(v * m_factor + k)] = float(im.jackOut[size_t(v)] / kVoltsFullScale);
		}
		out[i] = m_decimator.factor() == 1 ? m_work[0] : m_decimator.process(m_work.data());
		if (jacks)
			for (int v = 0; v < kVoices; ++v)
				if (jacks[v])
					jacks[v][i] = m_factor == 1 ? m_jackWork[size_t(v)] : m_jackDecimators[size_t(v)].process(&m_jackWork[size_t(v * m_factor)]);
	}
}

void VoiceBoard::renderInterleaved(float *out, int n, float *const *jacks)
{
	auto &im = *m_impl;
	const double dt = 1.0 / m_rate;
	double *s = im.sig.data();
	if (im.probing)
		im.growProbes(n * m_factor);
	for (int i = 0; i < n; ++i) {
		for (int k = 0; k < m_factor; ++k) {
			++im.samples;
			// The common trig's monostable and the instrument data lines.
			const bool mono = im.monoLeft > 0;
			if (mono)
				im.monoLeft -= dt;
			s[im.sMono] = mono ? 0.0 : 15.0;
			if (im.muteLeft > 0) { // the power-on detector lets Q13 go
				im.muteLeft -= dt;
				if (im.muteLeft <= 0) {
					s[im.sMute] = -15;
					im.mix.wake();
				}
			}
			const uint16_t data = im.data;
			s[im.sAcc] = data & kBitAC ? 5.0 : 0.0;
			for (int v = 0; v < kVoices; ++v)
				s[im.sData[v]] = data & (1u << v) ? 5.0 : 0.0;
			if (mono != im.prevMono) {
				// a trigger edge: wake the trigger circuits and the voices it reaches, and step them
				// finely through it, the first quarter steps by backward Euler (Circuit::edge)
				im.accent.wake();
				im.mtrig.wake();
				im.mix.wake(); // the step tick (mix.cir)
				im.accent.c.substeps(kEdgeSubsteps);
				im.mtrig.c.substeps(kEdgeSubsteps);
				im.accent.c.edge(kEdgeBackwardEuler);
				im.mtrig.c.edge(kEdgeBackwardEuler);
				im.accent.c.jump();
				im.mtrig.c.jump();
				for (int v = 0; v < kVoices; ++v)
					if (data & (1u << v)) {
						Unit &u = im.unit(Voice(v), m_controls);
						const int n = Impl::grouped(Voice(v)) ? im.over : 1; // the same time at the group's rate
						u.wake();
						u.substeps(kEdgeSubsteps * n);
						u.edge(kEdgeBackwardEuler * n);
					}
				im.prevMono = mono;
			}
			im.accent.step(s, im.window);
			im.mtrig.step(s, im.window);
			// noise and the metallic source run all the time
			s[im.sNz] = im.gauss(im.rng) * im.noiseScale;
			im.noise.step(s, im.window);
			bool any = false;
			for (int v = 0; v < kVoices; ++v) {
				if (Impl::grouped(Voice(v)))
					continue;
				Unit &u = im.unit(Voice(v), m_controls);
				u.step(s, im.window);
				any = any || u.awake();
			}
			for (Voice v : Impl::kGroup)
				any = any || im.unit(v, m_controls).awake();
			if (any)
				im.mix.wake();
			im.stepGroup(s, true, &m_work[size_t(k * im.over)]);
			if (jacks)
				for (int v = 0; v < kVoices; ++v)
					m_jackWork[size_t(v * m_factor + k)] = float(im.jackOut[size_t(v)] / kVoltsFullScale);
			if (im.probing) {
				const size_t at = size_t(i * m_factor + k);
				im.gateBuf[at] = mono ? uint16_t(data & 0x0fff) : uint16_t(0);
				for (size_t c = 0; c < im.probes.size(); ++c)
					if (im.probes[c].at != Impl::At::Off)
						im.probeBuf[c][at] = im.probeValue(im.probes[c], s);
			}
		}
		out[i] = m_decimator.factor() == 1 ? m_work[0] : m_decimator.process(m_work.data());
		if (jacks)
			for (int v = 0; v < kVoices; ++v)
				if (jacks[v])
					jacks[v][i] = m_factor == 1 ? m_jackWork[size_t(v)] : m_jackDecimators[size_t(v)].process(&m_jackWork[size_t(v * m_factor)]);
	}
}

double VoiceBoard::signal(const std::string &name) const
{
	const int i = m_impl->sig.find(name);
	return i < 0 ? -1e9 : m_impl->sig[i];
}

double VoiceBoard::voiceOut(Voice v) const
{
	return m_impl->jackOut[size_t(v)];
}

bool VoiceBoard::voiceAwake(Voice v) const
{
	return m_impl->unit(v, m_controls).awake();
}

bool VoiceBoard::asleep() const
{
	const auto &im = *m_impl;
	if (im.accent.awake || im.mtrig.awake || im.mix.awake || im.monoLeft > 0 || im.muteLeft > 0)
		return false;
	for (int v = 0; v < kVoices; ++v)
		if (voiceAwake(Voice(v)))
			return false;
	return true;
}

std::string VoiceBoard::blockReport() const
{
	const auto &im = *m_impl;
	std::string r;
	char line[256];
	auto one = [&](const Block &b) {
		const auto &t = b.c.stats();
		if (t.steps == 0)
			return;
		std::snprintf(line, sizeof line, "  %-12s %2d ports %2d states %2d inputs %8ld steps  %5.2f it/step  %5.2f fact/step  %5.1f %% linear (%ld)  %.3f s%s\n",
		              b.c.name().c_str(), b.c.portCount(), b.c.stateCount(), int(b.c.netlist().inputs.size()), t.steps, double(t.iterations) / double(t.steps),
		              double(t.factorizations) / double(t.steps), 100.0 * double(t.linearSteps) / double(t.steps), t.linearizations,
		              b.seconds,
		              (std::string(t.failures ? "  " + std::to_string(t.failures) + " failed" : "") +
		               (t.softened ? "  " + std::to_string(t.softened) + " softened" : "") +
		               (t.nyquist ? "  " + std::to_string(t.nyquist) + " nyquist" : ""))
		                  .c_str());
		r += line;
	};
	for (const Block *b : {&im.accent, &im.mtrig, &im.noise, &im.metal, &im.mix})
		one(*b);
	for (const auto &u : im.units)
		for (const auto &b : u.blocks)
			one(b);
	for (const auto &b : im.ma.blocks)
		one(b);
	return r;
}

VoiceBoard::Stats VoiceBoard::stats() const
{
	const auto &im = *m_impl;
	Stats s;
	s.samples = im.samples;
	for (int v = 0; v < kVoices; ++v) {
		if (v == int(Voice::CH))
			continue;
		const Unit &u = im.unit(Voice(v), m_controls);
		const auto t = u.stats();
		s.voiceSteps[size_t(v)] = u.steps;
		s.iterations[size_t(v)] = t.iterations;
		s.factorizations[size_t(v)] = t.factorizations;
		s.linearSteps[size_t(v)] = t.linearSteps;
		s.linearizations[size_t(v)] = t.linearizations;
		s.ports[size_t(v)] = u.ports();
		s.seconds[size_t(v)] = u.seconds;
		s.substepped += t.substepped;
		s.failures += t.failures;
	}
	return s;
}

// ====================================================================== SERVICE

void VoiceBoard::setTrims(double cbHz1, double cbHz2, double noiseRms, double cpOffset)
{
	auto &im = *m_impl;
	m_cal.cbHz1 = cbHz1;
	m_cal.cbHz2 = cbHz2;
	m_cal.noiseRms = noiseRms;
	m_cal.cpOffset = cpOffset;
	const double groupRate = m_rate * im.over;
	im.osc[0].set(cbHz1, groupRate, m_cal.metalDuty);
	im.osc[1].set(cbHz2, groupRate, m_cal.metalDuty);
	im.noiseScale = noiseScaleFor(noiseRms, m_rate);
	im.units[size_t(Voice::CP)].set("offset", cpOffset);
}

bool VoiceBoard::runsJobs() const
{
	return m_impl->jobsOk;
}

double VoiceBoard::tp4PerNoiseRms() const
{
	return m_impl->tp4PerRms;
}

bool VoiceBoard::setProbe(int channel, const std::string &where)
{
	auto &im = *m_impl;
	if (channel < 0 || channel >= kProbes)
		return false;
	im.probeNames[size_t(channel)] = where;
	using At = Impl::At;
	// where each probe is: a node of a netlist (its block, the node) or a board signal
	struct Want {
		At at = At::Off;
		Block *block = nullptr;
		int node = -1, signal = -1, job = -1;
	};
	std::array<Want, kProbes> want;
	std::array<bool, kProbes> found{};
	auto blocksOf = [&](const std::string &owner, At &at, int &job) -> std::vector<Block> * {
		job = -1;
		static std::vector<Block> none;
		if (owner == "accent" || owner == "mtrig" || owner == "noise" || owner == "metal" || owner == "mix")
			return nullptr; // single blocks, below
		if (owner == "ma") {
			at = At::Job, job = Impl::jobOf(Voice::CP);
			return &im.ma.blocks;
		}
		for (int v = 0; v < kVoices; ++v)
			if (v != int(Voice::CH) && owner == kVoiceNames[v]) {
				at = At::Job, job = Impl::jobOf(Voice(v));
				return &im.units[size_t(v)].blocks;
			}
		return &none;
	};
	for (int c = 0; c < kProbes; ++c) {
		const auto &name = im.probeNames[size_t(c)];
		found[size_t(c)] = name.empty();
		if (name.empty())
			continue;
		auto &w = want[size_t(c)];
		const auto dot = name.find('.');
		if (dot != std::string::npos) {
			// "<owner>.<node>": a node of that netlist, in the partition that works it out
			const std::string owner = name.substr(0, dot), node = name.substr(dot + 1);
			if (node == "0" || node == "gnd") { // ground: 0 V, nothing to record
				found[size_t(c)] = true;
				continue;
			}
			At at = At::Off;
			int job = -1;
			std::vector<Block *> blocks;
			if (auto *list = blocksOf(owner, at, job)) {
				for (auto &b : *list)
					blocks.push_back(&b);
			} else {
				Block *b = owner == "accent" ? &im.accent : owner == "mtrig" ? &im.mtrig : owner == "noise" ? &im.noise : owner == "metal" ? &im.metal : &im.mix;
				blocks.push_back(b);
				at = owner == "metal" ? At::Sub : owner == "mix" ? At::Mix : At::Shared;
			}
			// the partition that has the node as its own (not as another's, fed in)
			for (int pass = 0; pass < 2 && !found[size_t(c)]; ++pass)
				for (Block *b : blocks) {
					const auto &net = b->c.netlist();
					const int n = net.node(node);
					if (n > 0 && (pass == 1 || net.input("@" + node) < 0)) {
						w = {at, b, n, -1, job};
						found[size_t(c)] = true;
						break;
					}
				}
		}
		if (!found[size_t(c)] && (w.signal = im.sig.find(name)) >= 0) {
			// a board signal, read where it is written
			const int sg = w.signal;
			w.at = At::Shared;
			auto has = [sg](const std::vector<int> &l) { return std::find(l.begin(), l.end(), sg) != l.end(); };
			if (has(im.subIdx))
				w.at = At::Sub;
			else if (!has(im.sharedIdx)) {
				for (const auto &o : im.mix.outs)
					if (o.second == sg)
						w.at = At::Mix;
				for (int v = 0; v < kVoices; ++v)
					if (im.sMixIn[v] == sg)
						w.at = At::Mix;
				for (int u = 0; u <= kVoices && w.at == At::Shared; ++u)
					if (has(im.writes[size_t(u)]))
						w.at = At::Job, w.job = Impl::jobOf(u == kVoices ? Voice::CP : Voice(u));
			}
			found[size_t(c)] = true;
		}
	}
	// each block with the probes' outputs it needs, and the ones that had some without
	auto retap = [&](Block &b) {
		std::vector<int> nodes;
		for (int c = 0; c < kProbes; ++c)
			if (want[size_t(c)].block == &b)
				nodes.push_back(want[size_t(c)].node);
		b.retap(nodes);
	};
	for (Block *b : {&im.accent, &im.mtrig, &im.noise, &im.metal, &im.mix})
		retap(*b);
	for (auto &u : im.units)
		for (auto &b : u.blocks)
			retap(b);
	for (auto &b : im.ma.blocks)
		retap(b);
	im.probing = false;
	im.jobProbed.fill(false);
	for (int c = 0; c < kProbes; ++c) {
		const auto &w = want[size_t(c)];
		auto &p = im.probes[size_t(c)];
		p = {w.block || w.signal >= 0 ? w.at : At::Off, w.block, -1, w.block ? -1 : w.signal, w.job};
		if (w.block)
			for (size_t k = 0; k < w.block->probeNodes.size(); ++k)
				if (w.block->probeNodes[k] == w.node) {
					p.output = int(w.block->outs.size() + k);
					break;
				}
		if (p.at == At::Job && p.job >= 0)
			im.jobProbed[size_t(p.job)] = true;
		im.probing = im.probing || p.at != At::Off;
	}
	if (im.probing)
		im.growProbes(8192 * m_factor);
	return found[size_t(channel)];
}

const float *VoiceBoard::probeSamples(int channel) const
{
	const auto &im = *m_impl;
	if (channel < 0 || channel >= kProbes || im.probes[size_t(channel)].at == Impl::At::Off)
		return nullptr;
	return im.probeBuf[size_t(channel)].data();
}

const uint16_t *VoiceBoard::gateSamples() const
{
	const auto &im = *m_impl;
	return im.probing ? im.gateBuf.data() : nullptr;
}

namespace {
std::string partValue(char kind, double v, double taperMid = 0.5)
{
	char buf[32];
	if (kind == 'C') {
		if (v < 0.9995e-9)
			std::snprintf(buf, sizeof buf, "%gp", std::round(v * 1e12 * 100) / 100);
		else
			std::snprintf(buf, sizeof buf, "%gu", std::round(v * 1e6 * 1e4) / 1e4);
		return buf;
	}
	if (v >= 1e6)
		std::snprintf(buf, sizeof buf, "%gM", std::round(v / 1e6 * 1000) / 1000);
	else if (v >= 1e3)
		std::snprintf(buf, sizeof buf, "%gk", std::round(v / 1e3 * 1000) / 1000);
	else
		std::snprintf(buf, sizeof buf, "%g", v);
	std::string out = buf;
	if (kind == 'P')
		out += taperMid < 0.5 ? "(A)" : taperMid > 0.5 ? "(C)" : "(B)";
	return out;
}
} // namespace

const std::vector<VoiceBoard::Part> &VoiceBoard::parts()
{
	static const std::vector<Part> all = [] {
		std::vector<Part> out;
		struct Source {
			const char *owner, *name, *text, *board;
		};
		const Source sources[] = {
			{"accent", "accent", netlists::accent, "main"}, {"noise", "noise", netlists::noise, "main"}, {"bd", "bd", netlists::bd, "main"},
			{"sd", "sd", netlists::sd, "main"},             {"lt", "lt", netlists::lt, "main"},          {"mt", "mt", netlists::mt, "main"},
			{"ht", "ht", netlists::ht, "main"},             {"rs", "rs", netlists::rs, "main"},          {"cp", "cp", netlists::cp, "main"},
			{"ma", "ma", netlists::ma, "main"},             {"mtrig", "mtrig", netlists::mtrig, "voicing"}, {"metal", "metal", netlists::metal, "voicing"},
			{"cb", "cb", netlists::cb, "voicing"},          {"cy", "cy", netlists::cy, "voicing"},       {"oh", "hats", netlists::hats, "voicing"},
			{"mix", "mix", netlists::mix, "voicing"},
		};
		auto lower = [](std::string t) {
			for (auto &ch : t)
				ch = char(std::tolower(static_cast<unsigned char>(ch)));
			return t;
		};
		for (const auto &src : sources) {
			circuit::Netlist net;
			try {
				net = circuit::Netlist::parse(std::string(netlistText("models", netlists::models)) + "\n" + netlistText(src.name, src.text));
			} catch (...) {
				continue;
			}
			const std::string owner = src.owner;
			auto node = [&](const circuit::Netlist::Element &e, size_t k) { return owner + "." + net.nodes[size_t(e.nodes[k])]; };
			for (const auto &e : net.elements) {
				std::string name = e.name;
				for (auto &ch : name)
					ch = char(std::toupper(static_cast<unsigned char>(ch)));
				const auto digits = name.find_first_of("0123456789");
				if (digits == std::string::npos)
					continue;
				const std::string kind = name.substr(0, digits), rest = name.substr(digits);
				const auto num = rest.substr(0, rest.find_first_not_of("0123456789"));
				const std::string suffix = rest.substr(num.size());
				Part p{"", owner, src.board, "", {}};
				if ((kind == "R" || kind == "C" || kind == "SR") && suffix.empty()) { // SR: a mix resistor on its jack's contact
					if (e.nodes[0] == 0 && e.nodes[1] == 0)
						continue; // not fitted on this board (the first noise generator's R127)
					p.ref = kind == "SR" ? "R" + num : name;
					p.value = kind == "SR" ? partValue('R', e.ron) : partValue(kind[0], e.value);
					p.pins = {{"1", node(e, 0)}, {"2", node(e, 1)}};
					if (owner == "mix" && ((kind != "C" && std::stoi(num) <= 14) || name == "C1"))
						p.board = "jackb"; // the mix resistors on the jacks' contacts, the master out's parts
				} else if (kind == "D" && suffix.empty()) {
					p.ref = name;
					p.value = e.model;
					p.pins = {{"A", node(e, 0)}, {"K", node(e, 1)}};
				} else if (kind == "Q" && suffix.empty()) {
					p.ref = name;
					p.value = e.model;
					p.pins = {{"C", node(e, 0)}, {"B", node(e, 1)}, {"E", node(e, 2)}};
				} else if (kind == "J" && suffix.empty()) {
					p.ref = "Q" + num; // the FETs are Q on the boards
					p.value = e.model;
					p.pins = {{"D", node(e, 0)}, {"G", node(e, 1)}, {"S", node(e, 2)}};
				} else if (kind == "BC" && suffix.size() == 1) {
					p.ref = "IC" + num + lower(suffix);
					p.value = "comparator";
					p.pins = {{"+", node(e, 0)}, {"-", node(e, 1)}, {"out", node(e, 2)}};
				} else if (kind == "B" && suffix.empty() && e.nodes.size() >= 5) {
					p.ref = "IC" + num;
					p.value = "OTA";
					p.pins = {{"I abc", node(e, 0)}, {"+", node(e, 2)}, {"-", node(e, 3)}, {"out", node(e, 4)}};
				} else if (kind == "P" && suffix.empty()) {
					// the service trims are P elements too: TM3 (C.P. OFFSET) and TM4 (the noise)
					const std::string control = e.control >= 0 ? net.controls[size_t(e.control)] : "";
					p.ref = control == "offset" ? "TM3" : control == "tm4" ? "TM4" : "VR" + num;
					p.value = partValue('P', e.value, e.taperMid);
					p.pins = {{"1", node(e, 0)}, {"wiper", node(e, 1)}, {"3", node(e, 2)}};
				} else {
					continue;
				}
				out.push_back(p);
			}
			// the op-amps, which the parser makes into their poles' elements: from the text
			std::istringstream in(netlistText(src.name, src.text));
			for (std::string line; std::getline(in, line);) {
				std::istringstream ls(line);
				std::vector<std::string> t;
				for (std::string w; ls >> w;)
					t.push_back(w);
				if (t.size() < 5 || (t[0][0] != 'X' && t[0][0] != 'x') || lower(t[4]) != "opamp")
					continue;
				std::string name = t[0];
				for (auto &ch : name)
					ch = char(std::toupper(static_cast<unsigned char>(ch)));
				const auto num = name.substr(1, name.find_first_not_of("0123456789", 1) - 1);
				const std::string suffix = name.substr(1 + num.size());
				if (num.empty() || suffix.size() > 1)
					continue;
				out.push_back({"IC" + num + lower(suffix), owner, src.board, "op-amp",
				               {{"+", owner + "." + lower(t[1])}, {"-", owner + "." + lower(t[2])}, {"out", owner + "." + lower(t[3])}}});
			}
		}
		return out;
	}();
	return all;
}

} // namespace tr808
