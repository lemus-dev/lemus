#include "voiceboard.h"
#include "noise.h"
#include "cymbals.h"
#include "hihat.h"
#include "parallel.h"

#include "tr909/netlists.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

namespace tr909 {

// Tools: per-block timing and sleep diagnostics (read once).
const bool kProfile = std::getenv("TR909_PROFILE") != nullptr;
const bool kSleepDebug = std::getenv("TR909_SLEEPDEBUG") != nullptr;
// tools: the sample voices' op-amp poles by backward Euler as before 2026-11-01 (with TR909_GROUP=2:
// the old sample voices)
const bool kGroupBackwardEuler = std::getenv("TR909_GROUP_BE") != nullptr;

// Tools: TR909_RECORD=DIR writes, for each partition named in TR909_RECORDNAMES (comma-separated:
// "sd:snappy,lt:trig"; all if unset), DIR/<name>.rec: its netlist, parameters, rate, controls and
// state as it first steps, then each step's inputs and edge, jump and substep requests, for
// replaying it alone (the solver's speed and results on the inputs a run gave it).
const char *const kRecordDir = std::getenv("TR909_RECORD");

// The noise generator gives each sample's noise in this many parts, for a partition that takes the
// noise and runs faster than the board (.part ... over=N, N dividing it).
constexpr int kNoiseSub = 4;

// A trigger edge: the samples run in quarter steps, and the quarter steps of those by backward Euler.
constexpr int kEdgeSubsteps = 8;
const int kEdgeBackwardEuler = std::getenv("TR909_EDGEBE") ? std::atoi(std::getenv("TR909_EDGEBE")) : 8;

namespace {
// Tools: TR909_NETLISTS=DIR reads DIR/<name>.cir in place of a built-in netlist, where there is
// one (experiments without a rebuild). Null: no such netlist.
const char *netlistText(const char *name)
{
	const char *builtIn = netlists::find(name);
	static const char *dir = std::getenv("TR909_NETLISTS");
	if (dir == nullptr)
		return builtIn;
	static std::map<std::string, std::string> loaded;
	auto it = loaded.find(name);
	if (it == loaded.end()) {
		std::ifstream f(std::string(dir) + "/" + name + ".cir");
		std::stringstream text;
		if (f)
			text << f.rdbuf();
		if (!f && builtIn == nullptr)
			return nullptr;
		it = loaded.emplace(name, f ? text.str() : std::string(builtIn)).first;
	}
	return it->second.c_str();
}

const char *const kVoiceNames[kVoices] = {"bd", "sd", "lt", "mt", "ht", "rs", "hc", "hh", "cr", "rd"};

// Each voice's accent DAC (IC2-IC9 as 1-8): RS and HC share IC8, crash and ride IC9.
constexpr int kDac[kVoices] = {1, 2, 3, 4, 5, 6, 6, 7, 8, 8};

int voiceOf(const std::string &name)
{
	for (int v = 0; v < kVoices; ++v)
		if (name == kVoiceNames[v])
			return v;
	return -1;
}

// The board's signals, by name: each voice's trig line (trig.<voice>), each accent DAC's open-circuit
// voltage (acc.<1-8>), the noise, the CLOSED/OPEN line (ohsel), each voice's output into the jack
// board (jack.<voice>), the master outputs (out.l, out.r), and the signals between a voice's
// partitions and from its digital part (<voice>.<name>).
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

// A netlist's input or output name to the board's signal name, for a block of `owner` (a voice's
// name, or "mix"). A name with a dot in it is a board signal as it stands (one block's output into
// another's input: the toms' shared noise).
std::string signalName(const std::string &n, const std::string &owner)
{
	if (!n.empty() && n[0] == '@') // a node between a voice's partitions
		return owner + "." + n.substr(1);
	if (n.find('.') != std::string::npos || n == "noise" || n == "ohsel")
		return n;
	const int v = voiceOf(owner);
	if (owner == "mix") {
		if (voiceOf(n) >= 0) // the voices' outputs into the jack board
			return "jack." + n;
		if (n == "l" || n == "r")
			return "out." + n;
		return "mix." + n;
	}
	if (v >= 0 && n == "trig")
		return "trig." + owner;
	if (v >= 0 && n == "acc")
		return "acc." + std::to_string(kDac[v]);
	if (n == "out")
		return "jack." + owner;
	return owner + "." + n;
}

// One netlist running as a circuit, wired to the signals. A block sleeps (isn't stepped, its
// outputs held) once it has settled; wake() starts it again.
struct Block {
	circuit::Circuit c;
	std::vector<std::pair<int, int>> ins;  // (input index, signal)
	std::vector<std::pair<int, int>> outs; // (output index, signal)
	std::map<std::string, int> controls;
	bool awake = true;
	bool sleeps = true;    // false: always runs, or its Unit decides
	bool ownSleep = false; // a partition that may sleep within its awake voice (.part ... sleep)
	// A partition run at `over` times the voice's rate (.part ... over=N): each board sample is
	// `over` steps, the inputs moving in a straight line from the last sample's values, the noise
	// in its own parts of the sample (VoiceBoard's noiseSub); its outputs the last step's.
	int over = 1;
	int noiseIn = -1;          // the input that takes the noise (-1: none)
	std::vector<double> uLast; // the inputs at the last board sample
	std::vector<int> partIns;  // its inputs from the other partitions (signals)
	std::vector<bool> cutOut;  // which outputs go to the other partitions (not jacks)
	std::vector<bool> backOut; // and of those, which only to partitions run before it
	std::vector<double> partRef; // their values when it fell asleep
	double floor = 1e-5; // outputs under this (volts) count as quiet
	double capMove = 5e-3; // a timing capacitor moving more than this across the window keeps it awake
	int quiet = 0;       // samples the outputs have stayed quiet
	std::vector<double> ref;  // the timing capacitors' voltages at the start of the quiet window
	std::vector<double> yRef; // and the outputs'
	long steps = 0;
	double seconds = 0; // stepping time (TR909_PROFILE)
	// TR909_RECORD
	std::string recText;
	std::map<std::string, double> recParams;
	FILE *rec = nullptr;
	bool recChecked = false;
	int pendEdge = 0, pendSub = 0;
	bool pendJump = false;

	void load(const char *text, const std::string &owner, Signals &sig, const std::map<std::string, double> &params = {})
	{
		const std::string all = std::string(netlistText("models")) + "\n" + text;
		if (kRecordDir) {
			recText = all;
			recParams = params;
		}
		c.load(circuit::Netlist::parse(all, params));
		c.setName(owner);
		probeNodes.clear();
		const auto &net = c.netlist();
		ins.clear();
		outs.clear();
		partIns.clear();
		noiseIn = -1;
		for (size_t i = 0; i < net.inputs.size(); ++i) {
			if (net.inputs[i] == "noise")
				noiseIn = int(i);
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
		floor = 1e-5;
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
	void wake()
	{
		awake = true;
		quiet = 0;
	}
	void takeInputs(const double *sig)
	{
		for (const auto &[i, s] : ins)
			c.setInput(i, sig[s]);
		if (over > 1) {
			uLast.resize(ins.size());
			for (size_t k = 0; k < ins.size(); ++k)
				uLast[k] = sig[ins[k].second];
		}
	}
	// over > 1: the board sample's steps
	void stepOver(const double *sig, const double *noiseSub)
	{
		uLast.resize(ins.size());
		for (int k = 1; k <= over; ++k) {
			const double a = double(k) / over;
			for (size_t n = 0; n < ins.size(); ++n) {
				const auto &[i, sg] = ins[n];
				double u;
				if (i == noiseIn && noiseSub) { // the noise's parts in this step: their mean
					const int r = kNoiseSub / over;
					double m = 0;
					for (int q = (k - 1) * r; q < k * r; ++q)
						m += noiseSub[q];
					u = m / r;
				} else {
					u = uLast[n] + (sig[sg] - uLast[n]) * a;
				}
				c.setInput(i, u);
			}
			c.step();
		}
		for (size_t n = 0; n < ins.size(); ++n)
			uLast[n] = sig[ins[n].second];
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
	void step(double *sig, int window, const double *noiseSub = nullptr)
	{
		if (!awake) {
			// a partition asleep on its own wakes when what the others give it moves (20 mV: an
			// envelope's trigger, not its creep)
			bool moved = false;
			for (size_t k = 0; k < partIns.size() && k < partRef.size(); ++k)
				moved = moved || std::abs(sig[partIns[k]] - partRef[k]) > 20e-3;
			if (!moved)
				return;
			wake();
		}
		if (over > 1) {
			stepOver(sig, noiseSub);
		} else {
			takeInputs(sig);
			if (kRecordDir)
				record(sig);
			if (kProfile) {
				const auto t0 = std::chrono::steady_clock::now();
				c.step();
				seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			} else {
				c.step();
			}
		}
		++steps;
		if (!sleeps) { // its outputs only: its unit judges its sleep
			for (const auto &[o, sg] : outs)
				sig[sg] = c.output(o);
			return;
		}
		// Quiet: every output within `floor` of where it was when the quiet window began.
		if (quiet == 0)
			yRef.resize(outs.size());
		bool loud = false;
		for (size_t k = 0; k < outs.size(); ++k) {
			const double y = c.output(outs[k].first);
			sig[outs[k].second] = y;
			if (quiet == 0)
				yRef[k] = y;
			// a partition sleeping on its own judges by its jacks and what it passes on down the
			// voice, not by what it gives back to the partitions before it
			if (!(ownSleep && backOut[k]))
				loud = loud || std::abs(y - yRef[k]) > floor;
		}
		// Sleep once the outputs have stayed quiet for the window and no timing capacitor (0.1 uF
		// and up) has moved more than 5 mV across it.
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
			if (c.capValue(k) >= 0.1e-6 && std::abs(c.capVoltage(k) - ref[size_t(k)]) > capMove) {
				if (kSleepDebug)
					std::fprintf(stderr, "%s: capacitor %d moved %.3g V\n", c.name().c_str(), k, c.capVoltage(k) - ref[size_t(k)]);
				quiet = 0;
				return;
			}
		partRef.clear();
		for (int k : partIns)
			partRef.push_back(sig[k]);
		awake = false;
	}
	void record(const double *sig)
	{
		if (!recChecked) {
			recChecked = true;
			const char *names = std::getenv("TR909_RECORDNAMES");
			bool want = names == nullptr;
			if (names) {
				std::stringstream list(names);
				std::string n;
				while (std::getline(list, n, ','))
					want = want || n == c.name();
			}
			if (want) {
				std::string file = c.name();
				std::replace(file.begin(), file.end(), ':', '_');
				rec = std::fopen((std::string(kRecordDir) + "/" + file + ".rec").c_str(), "wb");
			}
			if (rec) {
				auto putStr = [this](const std::string &t) {
					const uint64_t n = t.size();
					std::fwrite(&n, 8, 1, rec);
					std::fwrite(t.data(), 1, t.size(), rec);
				};
				auto putVec = [this](const std::vector<double> &v) {
					const uint64_t n = v.size();
					std::fwrite(&n, 8, 1, rec);
					std::fwrite(v.data(), 8, v.size(), rec);
				};
				putStr(c.name());
				putStr(recText);
				const uint64_t np = recParams.size();
				std::fwrite(&np, 8, 1, rec);
				for (const auto &[k, v] : recParams) {
					putStr(k);
					std::fwrite(&v, 8, 1, rec);
				}
				const double rate = c.rate();
				std::fwrite(&rate, 8, 1, rec);
				std::vector<double> ctl;
				for (size_t k = 0; k < c.netlist().controls.size(); ++k)
					ctl.push_back(c.controlValue(int(k)));
				putVec(ctl);
				const auto st = c.state();
				putVec(st.vc);
				putVec(st.ic);
				putVec(st.v);
				putVec(st.u);
				const uint64_t ni = ins.size();
				std::fwrite(&ni, 8, 1, rec);
			}
		}
		if (!rec)
			return;
		const int32_t flags[3] = {pendEdge, pendSub, pendJump ? 1 : 0};
		std::fwrite(flags, 4, 3, rec);
		for (const auto &[i, sgl] : ins)
			std::fwrite(&sig[sgl], 8, 1, rec);
		pendEdge = pendSub = 0;
		pendJump = false;
	}
};

// A partition sleeping on its own (.part ... sleep) does so once its jacks are within 0.3 mV.
constexpr double kOwnFloor = 3e-4;

// A voice: its partitions, run in order.
struct Unit {
	std::vector<Block> blocks;
	long steps = 0;
	std::vector<int> jacks;
	double floorV = 1e-5;
	double capMove = 5e-3; // a timing capacitor moving more than this across the window keeps it awake
	int quiet = 0;
	std::vector<double> jRef, cRef;
	bool on = true;
	double seconds = 0; // time spent stepping (with TR909_PROFILE)

	bool loaded() const { return !blocks.empty(); }
	bool awake() const { return on && loaded(); }
	void wake()
	{
		on = true;
		quiet = 0;
		for (auto &b : blocks)
			b.wake();
	}
	void substeps(int n)
	{
		for (auto &b : blocks) {
			b.c.substeps(n);
			b.pendSub = std::max(b.pendSub, n);
		}
	}
	void edge(int n)
	{
		for (auto &b : blocks) {
			b.c.edge(n);
			b.c.jump();
			b.pendEdge = std::max(b.pendEdge, n);
			b.pendJump = true;
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
	void quietFloor(double volts)
	{
		floorV = volts;
		for (auto &b : blocks)
			b.floor = b.ownSleep ? std::max(volts, kOwnFloor) : volts;
	}
	// Capacitors that carry the noise (coupling caps, not timing ones) move by its swing across any
	// window: a voice fed the noise judges its capacitors within `volts`.
	void capacitorMove(double volts)
	{
		capMove = volts;
		for (auto &b : blocks)
			b.capMove = volts;
	}
	// A voice from its netlist: in the partitions its .part lines declare, or whole (TR909_FULL, or a
	// netlist without them). False if there is no such netlist.
	// A netlist that doesn't parse (an unknown model, a bad value) leaves the voice silent, with the
	// error on stderr, rather than taking the host down.
	bool load(const char *name, const std::string &owner, Signals &sig, const std::map<std::string, double> &params)
	{
		try {
			return loadOrThrow(name, owner, sig, params);
		} catch (const std::exception &e) {
			std::fprintf(stderr, "tr909: netlist %s: %s\n", name, e.what());
			blocks.clear();
			return false;
		}
	}
	bool loadOrThrow(const char *name, const std::string &owner, Signals &sig, const std::map<std::string, double> &params)
	{
		blocks.clear();
		const char *text = netlistText(name);
		if (text == nullptr)
			return false;
		static const bool full = std::getenv("TR909_FULL") != nullptr;
		const auto split = full ? std::vector<circuit::Netlist::Partition>{} : circuit::Netlist::partitions(text);
		if (split.empty()) {
			blocks.assign(1, Block{});
			blocks[0].load(text, owner, sig, params);
		} else {
			blocks.assign(split.size(), Block{});
			for (size_t k = 0; k < split.size(); ++k) {
				blocks[k].load(split[k].text.c_str(), owner, sig, params);
				blocks[k].c.setName(owner + ":" + split[k].name);
				blocks[k].ownSleep = split[k].sleeps;
				blocks[k].over = split[k].over;
				if (blocks[k].over > 1 && blocks[k].noiseIn >= 0 && kNoiseSub % blocks[k].over != 0)
					throw std::runtime_error("partition " + split[k].name + ": a partition that takes the noise runs at over=2 or 4");
			}
		}
		steps = 0;
		return true;
	}
	// After the partitions are loaded: their jacks, and which of their outputs go back.
	void finish(Signals &sig, const std::vector<std::string> &jackSignals)
	{
		jacks.clear();
		for (const auto &n : jackSignals)
			jacks.push_back(sig.index(n));
		for (size_t k = 0; k < blocks.size(); ++k)
			for (size_t o = 0; o < blocks[k].outs.size(); ++o) {
				bool forward = false;
				for (size_t j = k + 1; j < blocks.size(); ++j) {
					const auto &in = blocks[j].partIns;
					forward = forward || std::find(in.begin(), in.end(), blocks[k].outs[o].second) != in.end();
				}
				blocks[k].backOut[o] = blocks[k].cutOut[o] && !forward;
			}
		static const bool noOwnSleep = std::getenv("TR909_NOPARTSLEEP") != nullptr; // tools
		for (auto &b : blocks) {
			b.sleeps = b.ownSleep && !noOwnSleep;
			b.floor = b.ownSleep ? std::max(floorV, kOwnFloor) : floorV;
		}
		on = true;
		quiet = 0;
		steps = 0;
	}
	// The partitions' operating points, a few rounds to agree on the voltages at their cuts. A voice
	// with a free-running oscillator (the BD's VCO runs at rest) has none: it then runs from its
	// power-on state for `warm` samples with its inputs at rest, as a 909 that has been on a while.
	// Switched on: every capacitor at 0 V (the rails up at once), charging to its operating point as
	// the unit runs.
	void cold()
	{
		for (auto &b : blocks) {
			auto s = b.c.state();
			std::fill(s.vc.begin(), s.vc.end(), 0.0);
			std::fill(s.ic.begin(), s.ic.end(), 0.0);
			b.c.setState(s);
		}
		wake();
	}
	void settle(double *sig, long warm = 0)
	{
		bool ok = true;
		for (int round = 0; round < (blocks.size() > 1 ? 8 : 1); ++round)
			for (auto &b : blocks) {
				b.takeInputs(sig);
				ok = b.c.solveDc() && ok;
				b.putOutputs(sig);
				b.wake();
			}
		if (!ok)
			for (long i = 0; i < warm; ++i)
				for (auto &b : blocks)
					b.step(sig, 1 << 30);
		wake();
	}
	// The unit sleeps as a whole: once its jacks have stayed within the floor of where they were
	// for the window and none of its timing capacitors (0.1 uF and up) has moved over capMove.
	void step(double *sig, int window, const double *noiseSub = nullptr)
	{
		if (!on)
			return;
		const auto t0 = kProfile ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
		struct Timer {
			bool on;
			std::chrono::steady_clock::time_point t0;
			double &acc;
			~Timer()
			{
				if (on)
					acc += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			}
		} timer{kProfile, t0, seconds};
		++steps;
		for (auto &b : blocks)
			b.step(sig, b.ownSleep ? window / 4 : window, noiseSub);
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
		if (cRef.empty())
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
};

// The accent DACs (IC2-IC9, 4174 latches into RM0621 arrays: 5k, 10k ... 160k from D6 down to D1,
// to one node): the node's open-circuit voltage for a code, with each latch output's 455 ohms
// (HD14174 at 5 V, typical) in series with its resistor (docs/circuit/interface.md). Its source
// resistance, 2.69k, is the voice netlists' rdac.
double dacVolts(int code)
{
	double on = 0, all = 0;
	for (int b = 0; b < 6; ++b) {
		const double g = 1.0 / (5e3 * double(1 << (5 - b)) + 455.0);
		all += g;
		if (code >> b & 1)
			on += g;
	}
	return 5.0 * on / all;
}
constexpr double kDacOhms = 2691.0;
constexpr double kNetlistTm = 0.255; // hc.cir's .param tm: the service notes' adjustment on the model

} // namespace

double Calibration::knob(const char *name, double rotation) const
{
	const double r = std::clamp(rotation, 0.0, 1.0);
	const auto it = knobLaws.find(name);
	if (it == knobLaws.end() || it->second.empty())
		return r;
	const auto &pts = it->second;
	if (r <= pts.front().first)
		return pts.front().second;
	for (size_t k = 1; k < pts.size(); ++k)
		if (r <= pts[k].first) {
			const auto &a = pts[k - 1], &b = pts[k];
			return b.first > a.first ? a.second + (b.second - a.second) * (r - a.first) / (b.first - a.first) : b.second;
		}
	return pts.back().second;
}

Calibration unitCalibration()
{
	// The AudioRealism unit (a late board; its per-knob recordings, docs/reference.md), fitted
	// within the parts' tolerances, each value checked on the knob settings it wasn't fitted to and
	// on Rob Roy's unit (docs/model.md, "Calibration"; each voice's in docs/circuit/<voice>.md).
	Calibration c;
	c.parts = {
		// the BD: C14 +3.6 % and R27 +6 % put the pitch's end and the sweep's start on the unit's
		// (49.2-49.5 Hz); C8 0.40 uF (+21 %, the edge of a tantalum's +-20 %) its tail at full
		// DECAY; R44 36k (+64 %, outside its tolerance) stands for an unidentified factor in the
		// click's path: the click is 4.3-4.5 dB strong against the body at every ATTACK, TUNE and
		// DECAY, on both units (docs/circuit/bd.md, "Calibration")
		{"bd.c14", 0.0342e-6},
		{"bd.r27", 1.59e6},
		{"bd.c8", 0.40e-6},
		{"bd.r44", 36e3},
		// the rim shot: its output is IC50a's, not IC50b's high-pass (gain 1): the level against the
		// other voices (+7.0 -> -0.5 dB), the waveform (correlation 0.64 -> 0.998), the -40 dB time
		// and the slow lobe the units' recordings show all follow, on the unit and held out on two
		// others; the service notes and NAVA draw IC50b (docs/circuit/rshc.md, "Calibration": against
		// the drawing). And its three resonators on the unit's lines (C112/C113 +2.7 %, C115/C116
		// -2.0 %, C117/C118 +3.2 %)
		{"rs.r421", 1e-3}, {"rs.c121", 100e-6}, {"rs.c122", 100e-6},
		{"rs.c112", 0.01027e-6}, {"rs.c113", 0.01027e-6},
		{"rs.c115", 0.02646e-6}, {"rs.c116", 0.02646e-6},
		{"rs.c117", 0.00485e-6}, {"rs.c118", 0.00485e-6},
		// the clap: TM at 0.30 (the OTA's pulses level with the other voices; the service notes'
		// 2 Vp-p gives 0.255), C65 -17 % (the teeth's spacing), C57 -19 % (the reverb's level), both
		// electrolytic
		{"hc.tm", 0.30},
		{"hc.c65", 0.39e-6},
		{"hc.c57", 0.38e-6},
		// the toms: LT's TUNE pot +6 % and MT's -2.5 % (each tom's span over the turn); VCO1's or
		// VCO2's capacitor on each tom's ratio (LT 1.471, MT 1.481: the drawn values give 1.50), and
		// HT's two +3-4 % (60-80 cents sharp at every TUNE); the three DECAY pots +20 %, the edge of
		// their tolerance (the tail at DECAY above 0; all three alike is suspicious: one batch, or
		// standing in for something else; docs/circuit/toms.md, "Calibration")
		{"lt.vr10", 10.6e3}, {"mt.vr13", 9.75e3},
		{"lt.c18", 22.4e-9}, {"mt.c33", 26.7e-9},
		{"ht.c97", 15.5e-9}, {"ht.c102", 22.9e-9},
		{"lt.vr11", 600e3}, {"mt.vr14", 600e3}, {"ht.vr17", 600e3},
		// the snare: VCO-1 3.4 % sharp, and with it the two VCOs lock at 3:2 as the unit's do; TONE's
		// pot (500k) 20 % long: the snappy's decay 10-13 % short from TONE 0.25 up, right at 0
		{"sd.c69", 0.01035e-6},
		{"sd.vr7", 600e3},
		// the crash's and ride's clocks (TC4011UBP astables, IC59 and IC57): one fit for both gates,
		// jointly with the hi-hat's astable (31.57 kHz on both units), the drive 1.72x the CD4011UB's
		// typical (above every published typical: docs/circuit/cymbals.md, "Calibration") and a
		// 2.15 pF gate-drain capacitance; VR25 -16 %, VR27 -10 %, C130 -4 % (the ride's top 4 %
		// above the crash's): the clocks within 15 cents rms of the unit's TUNE sweeps, against
		// 250-290 as printed
		{"cr.w4011n", 931e-6}, {"cr.w4011p", 483.6e-6}, {"cr.cgd", 2.15e-12},
		{"rd.w4011n", 931e-6}, {"rd.w4011p", 483.6e-6}, {"rd.cgd", 2.15e-12},
		{"cr.vr25", 8355},
		{"rd.vr27", 9031},
		{"rd.c130", 450.6e-12},
	};
	// The toms' TUNE (VR10, VR13, VR16): the unit's pitch barely moves over the first 8 % and the
	// last 6.5 % of the turn; one law for the three (a law each did no better on held-out settings)
	for (const char *k : {"ltTune", "mtTune", "htTune"})
		c.knobLaws[k] = {{0.08, 0}, {0.935, 1}};
	// The hi-hat's sample clock, the unit's (the median of 32 takes; its parts give 31.571 kHz)
	c.hhClockHz = 31577;
	// OH DECAY (VR23, 1M A): its track reaches 15 % at a third of the turn and 34 % at three
	// quarters (the netlist's a0.15: 15 % at half, 57.5 % at three quarters); the unit's sweep,
	// scherer.de's unit agrees (docs/circuit/hh.md, "Calibration")
	c.knobLaws["ohDecay"] = {{0, 0}, {0.335, 0.5}, {0.76, 0.612}, {1, 1}};
	// The BD's TUNE (VR2) and DECAY (VR5), 100k and 1M A pots: their tracks at the knob's quarters,
	// from its sweep and tail (the parts list's two-segment A put TUNE's 75 % at 57.5 % of the track)
	c.knobLaws["bdTune"] = {{0, 0.02}, {0.25, 0.293}, {0.5, 0.514}, {0.75, 0.579}, {1, 1}};
	c.knobLaws["bdDecay"] = {{0, 0}, {0.25, 0.442}, {0.5, 0.527}, {0.75, 0.605}, {1, 1}};
	// The crash's and ride's TUNE: their pots' dead ends (the clocks' periods straight between)
	c.knobLaws["crTune"] = {{0.073, 0}, {0.956, 1}};
	c.knobLaws["rdTune"] = {{0.0745, 0}, {0.9456, 1}};
	return c;
}

struct VoiceBoard::Impl {
	Signals sig;
	std::array<Unit, kVoices> units;
	Unit tomnoise; // the toms' shared noise (IC27, Q33/Q34): keyed by all three, into all three
	Unit mix;
	NoiseGenerator noise;
	// The sample voices: their digital halves (the counters, ROMs, latches and D/A lines), and their
	// netlists run `over` steps a board sample (2 below 150 kHz: their D/A images pass the filters,
	// docs/model.md "Rates"), their jacks brought down to the board's rate for the jack board.
	static constexpr Voice kGroup[3] = {Voice::HH, Voice::CR, Voice::RD};
	static bool grouped(int v) { return v == int(Voice::HH) || v == int(Voice::CR) || v == int(Voice::RD); }
	std::array<Cymbal, 2> cymbals; // crash, ride
	std::array<bool, 2> cymbalOk{};
	std::array<std::array<int, 13>, 2> sCymbal{}; // d0..d5, e0..e5, run
	HiHat hihat;
	bool hihatOk = false;
	std::array<int, 7> sHihat{}; // d0..d5, run
	// The hi-hat's trig, CLOSED/OPEN and accent lines reach its netlist HiHat::latency() steps late,
	// in line with its D/A lines (their band-limited steps are centred that late): rings of them.
	static constexpr int kDelay = 32;
	std::array<std::array<double, kDelay>, 3> hhRing{};
	int hhPos = 0, hhLatency = 8;
	std::array<int, 3> sHhIn{}, sHhLate{}; // trig.hh, ohsel, acc.7, and their late copies
	bool hhTrig = false, hhClosed = true;
	int over = 1;
	std::array<Decimator, 3> groupDec;

	// The block's jobs (render): the voices, each with its own copy of the noise signal, their jacks
	// at the board's rate into jackBuf, then the jack board. They share nothing else within a block:
	// the latches' lines only change between render calls.
	enum Job { JobToms, JobSD, JobBD, JobHH, JobCymbals, JobHC, JobRS, JobCount }; // the heaviest first
	std::array<int, kVoices> sNoiseOf{}; // each voice's copy of the noise (the toms': the tom noise's)
	std::vector<double> noiseBuf;
	std::vector<double> noiseSub; // the noise in kNoiseSub parts of each sample
	std::array<std::vector<double>, kVoices> jackBuf;
	int blockLength = 0;
	Parallel *parallel = nullptr;
	void stepVoice(int v, double *s, int j);
	void stepGrouped(size_t g, double *s, float *buf, int j);
	void runJob(int job);
	static void runJob(void *self, int job) { static_cast<Impl *>(self)->runJob(job); }
	// The jobs in order of what each took in the last block, the longest first: the caller starts
	// on job 0 before the workers wake, so it takes the longest and finishes last rather than
	// waiting on a worker (the order changes nothing else: the jobs share nothing).
	std::array<int, JobCount> jobOrder{JobToms, JobSD, JobBD, JobHH, JobCymbals, JobHC, JobRS};
	std::array<int64_t, JobCount> jobTime{};
	static void runOrdered(void *self, int k)
	{
		auto &im = *static_cast<Impl *>(self);
		const int job = im.jobOrder[size_t(k)];
		const auto t0 = std::chrono::steady_clock::now();
		im.runJob(job);
		im.jobTime[size_t(job)] = (std::chrono::steady_clock::now() - t0).count();
	}
	std::array<std::vector<uint8_t>, 3> roms; // hh, crash, ride
	int window = 9600;
	long samples = 0;
	int sNoise = 0, sOhsel = 0, sPon = 0;
	std::array<int, 9> sAcc{};
	std::array<int, kVoices> sTrig{}, sJack{}, sMulti{};
	int sOutL = 0, sOutR = 0;
	std::array<bool, kVoices> trig{}, prevTrig{};
	bool accChanged = false;

	// The scope's probes (VoiceBoard::setProbe): a node of a unit's netlist (the voices 0-9,
	// kTomNoise, kMix), recorded after that unit's step each board sample, or a board signal,
	// recorded in the jack board's loop.
	static constexpr int kTomNoise = kVoices, kMix = kVoices + 1;
	struct Probe {
		int unit = -1, block = -1, output = -1, signal = -1;
	};
	std::array<Probe, VoiceBoard::kProbes> probes;
	std::array<std::string, VoiceBoard::kProbes> probeNames; // set again after a prepare()
	std::array<std::vector<float>, VoiceBoard::kProbes> probeBuf;
	bool probing = false;
	Unit &unit(int u) { return u == kTomNoise ? tomnoise : u == kMix ? mix : units[size_t(u)]; }
	void capture(int u, int j)
	{
		for (size_t c = 0; c < probes.size(); ++c)
			if (probes[c].unit == u)
				probeBuf[c][size_t(j)] = float(unit(u).blocks[size_t(probes[c].block)].c.output(probes[c].output));
	}
};

VoiceBoard::VoiceBoard() : m_impl(std::make_unique<Impl>()) {}
VoiceBoard::~VoiceBoard() = default;

void VoiceBoard::setSampleRoms(const SampleRoms &roms)
{
	auto copy = [](const uint8_t *p) { return p ? std::vector<uint8_t>(p, p + 32768) : std::vector<uint8_t>{}; };
	m_impl->roms = {copy(roms.hihat), copy(roms.crash), copy(roms.ride)};
}

void VoiceBoard::setCalibration(const Calibration &c)
{
	m_cal = c;
	prepare(m_hostRate);
}

void VoiceBoard::prepare(double hostRate)
{
	m_hostRate = hostRate;
	m_factor = hostRate < 80000 ? 2 : 1;
	if (const char *f = std::getenv("TR909_FACTOR")) // tools: force the oversampling
		m_factor = std::max(1, std::atoi(f));
	const int stages = m_factor >= 8 ? 3 : m_factor >= 4 ? 2 : m_factor >= 2 ? 1 : 0;
	m_factor = 1 << stages;
	m_rate = hostRate * m_factor;
	m_decL.setStages(stages);
	m_decR.setStages(stages);
	m_workL.assign(size_t(m_factor), 0.0f);
	m_workR.assign(size_t(m_factor), 0.0f);
	for (auto &d : m_jackDecimators)
		d.setStages(stages);
	m_jackWork.assign(size_t(kVoices * m_factor), 0.0f);

	auto &im = *m_impl;
	auto &sig = im.sig;
	auto params = [this](const std::string &owner) {
		std::map<std::string, double> p{{"rdac", kDacOhms}};
		for (const auto &[k, v] : m_cal.parts)
			if (k.rfind(owner + ".", 0) == 0)
				p[k.substr(owner.size() + 1)] = v;
		return p;
	};
	// The sample voices at the board's rate, their op-amps' poles by the trapezoidal rule (below):
	// against ngspice as close as at twice the rate with backward-Euler poles, closer below 15 kHz,
	// -0.3 dB at 18-20 kHz, for 7 % less CPU on the busy kit (2026-11-01, docs/model.md "Rates").
	im.over = 1;
	if (const char *g = std::getenv("TR909_GROUP")) // tools: the sample voices' factor (1 or 2)
		im.over = std::clamp(std::atoi(g), 1, 2);
	for (auto &d : im.groupDec)
		d.setStages(im.over == 2 ? 1 : 0, 8);
	for (int v = 0; v < kVoices; ++v) {
		auto &u = im.units[size_t(v)];
		if (u.load(kVoiceNames[v], kVoiceNames[v], sig, params(kVoiceNames[v])))
			u.finish(sig, {std::string("jack.") + kVoiceNames[v]});
		for (auto &b : u.blocks) {
			b.c.setRate((Impl::grouped(v) ? m_rate * im.over : m_rate) * b.over);
			// checked on the hat: no ringing near Nyquist (the other voices keep backward Euler)
			b.c.setPoleTrap(Impl::grouped(v) && !kGroupBackwardEuler);
		}
	}
	{
		auto &hh = im.hihat;
		const auto &rom = im.roms[0];
		hh.setRom(rom.empty() ? nullptr : rom.data(), rom.size());
		im.hihatOk = !rom.empty();
		hh.setRate(m_rate * im.over);
		hh.setSmoothing(HiHat::Smoothing::Sinc);
		if (m_cal.hhClockHz > 0)
			hh.setClock(m_cal.hhClockHz);
		im.hhLatency = std::clamp(int(std::lround(hh.latency())), 0, Impl::kDelay - 1);
		for (int l = 0; l < 7; ++l)
			im.sHihat[size_t(l)] = sig.index(l < 6 ? "hh.d" + std::to_string(l) : std::string("hh.run"));
		const char *in[3] = {"trig.hh", "ohsel", "acc.7"}, *late[3] = {"hh.trig.late", "hh.ohsel.late", "hh.acc.late"};
		for (int k = 0; k < 3; ++k) {
			im.sHhIn[size_t(k)] = sig.index(in[k]);
			im.sHhLate[size_t(k)] = sig.index(late[k]);
		}
		// the hi-hat's netlist takes the late copies
		for (auto &b : im.units[size_t(Voice::HH)].blocks)
			for (auto &[i, sg] : b.ins)
				for (int k = 0; k < 3; ++k)
					if (sg == im.sHhIn[size_t(k)])
						sg = im.sHhLate[size_t(k)];
	}
	for (int k = 0; k < 2; ++k) {
		const std::string owner = k == 0 ? "cr" : "rd";
		auto &cy = im.cymbals[size_t(k)];
		const auto &rom = im.roms[size_t(1 + k)];
		const char *clock = netlistText((owner + "clk").c_str());
		cy.setRom(rom.empty() ? nullptr : rom.data(), rom.size());
		im.cymbalOk[size_t(k)] = !rom.empty() && clock != nullptr
		                         && cy.setClock(std::string(netlistText("models")) + "\n" + clock, params(owner));
		cy.setRate(m_rate * im.over);
		cy.setSmoothing(Cymbal::Smoothing::Sinc);
		for (int l = 0; l < 13; ++l)
			im.sCymbal[size_t(k)][size_t(l)] = sig.index(owner + "." + (l < 6 ? "d" + std::to_string(l) : l < 12 ? "e" + std::to_string(l - 6) : "run"));
	}
	if (im.tomnoise.load("tomnoise", "tomnoise", sig, params("tomnoise"))) {
		im.tomnoise.finish(sig, {"tomnoise.tn"});
		for (auto &b : im.tomnoise.blocks)
			b.c.setRate(m_rate * b.over);
	}
	// The BD's and toms' VCOs run at rest (their VCAs shut) and each trigger resets them: the
	// voices sleep once their jacks have settled, whatever their VCOs' capacitors do.
	for (Voice v : {Voice::LT, Voice::MT, Voice::HT})
		im.units[size_t(v)].capacitorMove(1e9);
	// The snare's two VCOs run at rest too, and their tones leak through Q50/Q51 as the envelope
	// capacitors creep down (0.46 mV at the jack 0.7 s after a hit, 0.13 mV at 1.8 s): it counts as
	// quiet within 1 mV (-80 dBV), as the 808's snare does.
	im.units[size_t(Voice::SD)].capacitorMove(1e9);
	im.units[size_t(Voice::SD)].quietFloor(1e-3);
	// The BD's VCO runs at rest (about 50 Hz, its VCAs shut) and each trigger resets it (Q7, Q11):
	// the voice sleeps once its jack has settled, whatever its VCO's capacitors do.
	im.units[size_t(Voice::BD)].capacitorMove(1e9);
	// The clap's reverb VCA, Q37, closes ever more slowly after a hit (C61 drains only through its
	// base): about 1.3 mV p-p of noise at the jack at 1 s, 0.3 mV at 2 s; it counts as quiet within
	// 2 mV. Its reverb path's C55 and C57 (0.47 uF) carry the noise itself.
	im.units[size_t(Voice::HC)].quietFloor(2e-3);
	im.units[size_t(Voice::HC)].capacitorMove(2.0);
	if (im.mix.load("mix", "mix", sig, params("mix"))) {
		im.mix.finish(sig, {"out.l", "out.r"});
		for (auto &b : im.mix.blocks)
			b.c.setRate(m_rate * b.over);
	}
	im.sNoise = sig.index("noise");
	im.sPon = sig.index("mix.pon");
	// each voice's own copy of the noise, so the voices' jobs can run at once (the toms and their tom
	// noise share one: they run as one job)
	for (int v = 0; v < kVoices; ++v) {
		const bool tom = v == int(Voice::LT) || v == int(Voice::MT) || v == int(Voice::HT);
		im.sNoiseOf[size_t(v)] = sig.index(tom ? std::string("noise.toms") : std::string("noise.") + kVoiceNames[v]);
	}
	auto ownNoise = [&](Unit &u, int signal) {
		for (auto &b : u.blocks)
			for (auto &in : b.ins)
				if (in.second == im.sNoise)
					in.second = signal;
	};
	for (int v = 0; v < kVoices; ++v)
		ownNoise(im.units[size_t(v)], im.sNoiseOf[size_t(v)]);
	ownNoise(im.tomnoise, im.sNoiseOf[size_t(Voice::LT)]);
	im.sOhsel = sig.index("ohsel");
	for (int d = 1; d <= 8; ++d)
		im.sAcc[size_t(d)] = sig.index("acc." + std::to_string(d));
	for (int v = 0; v < kVoices; ++v) {
		im.sTrig[size_t(v)] = sig.index(std::string("trig.") + kVoiceNames[v]);
		im.sJack[size_t(v)] = sig.index(std::string("jack.") + kVoiceNames[v]);
		// the MULTI OUT jack itself, after the jack board's 1k (and C500-C512 on the late board)
		im.sMulti[size_t(v)] = sig.index(std::string("mix.m") + kVoiceNames[v]);
	}
	im.sOutL = sig.index("out.l");
	im.sOutR = sig.index("out.r");
	im.noise.setRate(m_rate);
	im.noise.setClock(m_cal.noiseHz > 0 ? m_cal.noiseHz : NoiseGenerator::kClockHz);
	im.noise.setHigh(NoiseGenerator::kHighLoaded); // the four voices' inputs load the 4006's pin
	im.window = int(0.1 * m_rate);
	// the blocks' buffers, for host blocks up to 8192 samples (a larger one grows them once)
	im.noiseBuf.assign(size_t(8192 * m_factor), 0.0);
	im.noiseSub.assign(size_t(8192 * m_factor * kNoiseSub), 0.0);
	for (auto &b : im.jackBuf)
		b.assign(size_t(8192 * m_factor), 0.0);
	setControls(m_controls);
	for (int v = 0; v < kVoices; ++v)
		setPlugged(Voice(v), m_plugged[size_t(v)]);
	for (int c = 0; c < kProbes; ++c) // the scope's probes, on the new circuits
		setProbe(c, std::string(im.probeNames[size_t(c)]));
	reset();
}

void VoiceBoard::reset()
{
	auto &im = *m_impl;
	auto &sig = im.sig;
	sig.clear();
	m_latch.fill(0);
	im.trig.fill(false);
	im.prevTrig.fill(false);
	im.noise.reset();
	{
		im.hihat.reset();
		const auto &l = im.hihat.step();
		for (int i = 0; i < 6; ++i)
			sig[im.sHihat[size_t(i)]] = l.d[i];
		sig[im.sHihat[6]] = l.run;
		im.hhTrig = false;
		im.hhClosed = true;
		sig[im.sOhsel] = 5.0; // CLOSED high, as the latch's power-on state leaves the hi-hat
		for (int k = 0; k < 3; ++k) {
			im.hhRing[size_t(k)].fill(sig[im.sHhIn[size_t(k)]]);
			sig[im.sHhLate[size_t(k)]] = sig[im.sHhIn[size_t(k)]];
		}
		im.hhPos = 0;
	}
	for (int k = 0; k < 2; ++k) {
		auto &cy = im.cymbals[size_t(k)];
		cy.reset();
		const auto &l = cy.step();
		for (int i = 0; i < 6; ++i) {
			sig[im.sCymbal[size_t(k)][size_t(i)]] = l.d[i];
			sig[im.sCymbal[size_t(k)][size_t(6 + i)]] = l.e[i];
		}
		sig[im.sCymbal[size_t(k)][12]] = l.run;
	}
	sig[im.sPon] = 15; // C143 charged: the power-on mute (Q82/Q83) let go
	for (int d = 1; d <= 8; ++d)
		sig[im.sAcc[size_t(d)]] = dacVolts(0);
	for (auto &u : im.units)
		u.settle(sig.data(), long(1.0 * m_rate));
	im.tomnoise.settle(sig.data(), long(0.1 * m_rate));
	im.mix.settle(sig.data());
	m_decL.reset();
	m_decR.reset();
	for (auto &d : m_jackDecimators)
		d.reset();
	for (size_t k = 0; k < im.groupDec.size(); ++k) {
		auto &d = im.groupDec[k];
		d.reset();
		const float j = float(sig[im.sJack[size_t(Impl::kGroup[k])]]), two[2] = {j, j};
		for (int n = 0; n < 64; ++n)
			d.process(two);
	}
}

void VoiceBoard::powerOn()
{
	auto &im = *m_impl;
	for (int reg = 1; reg <= 10; ++reg)
		write(reg, 0); // RESET clears IC1-IC10 (SN p.6)
	im.noise.powerOn();
	// the hi-hat's, crash's and ride's analog stages from 0 V: their MULTI OUTs thump (1.7-1.8 V, the
	// crash's and ride's settling over 1.6 s). The other voices measure the same from their operating
	// points (their outputs move by microvolts, a hit within 0.03 dB from 10 s; the clap's 0.3-0.4 dB
	// in its first 3 s is under the mute), and from 0 V the BD's and SD's capacitors would keep them
	// awake for over a minute: they start at their operating points (docs/circuit/power.md).
	for (Voice v : {Voice::HH, Voice::CR, Voice::RD})
		im.units[size_t(v)].cold();
	if (!im.mix.loaded())
		return;
	im.sig[im.sPon] = -15; // the rails off: C143 discharged
	im.mix.settle(im.sig.data());
	im.sig[im.sPon] = 15; // and on: C143's negative end jumps up and holds Q82/Q83 on while it charges
	im.mix.wake();
	im.mix.substeps(kEdgeSubsteps);
	im.mix.edge(kEdgeBackwardEuler);
}

void VoiceBoard::setControls(const Controls &c)
{
	m_controls = c;
	auto &u = m_impl->units;
	// each knob through its law (Calibration::knobLaws) to its pot's control
	auto set = [&u, this](Voice v, const char *control, const char *knob, double x) { u[size_t(v)].set(control, m_cal.knob(knob, x)); };
	set(Voice::BD, "tune", "bdTune", c.bdTune);
	set(Voice::BD, "level", "bdLevel", c.bdLevel);
	set(Voice::BD, "attack", "bdAttack", c.bdAttack);
	set(Voice::BD, "decay", "bdDecay", c.bdDecay);
	set(Voice::SD, "tune", "sdTune", c.sdTune);
	set(Voice::SD, "level", "sdLevel", c.sdLevel);
	set(Voice::SD, "tone", "sdTone", c.sdTone);
	set(Voice::SD, "snappy", "sdSnappy", c.sdSnappy);
	set(Voice::LT, "tune", "ltTune", c.ltTune);
	set(Voice::LT, "level", "ltLevel", c.ltLevel);
	set(Voice::LT, "decay", "ltDecay", c.ltDecay);
	set(Voice::MT, "tune", "mtTune", c.mtTune);
	set(Voice::MT, "level", "mtLevel", c.mtLevel);
	set(Voice::MT, "decay", "mtDecay", c.mtDecay);
	set(Voice::HT, "tune", "htTune", c.htTune);
	set(Voice::HT, "level", "htLevel", c.htLevel);
	set(Voice::HT, "decay", "htDecay", c.htDecay);
	set(Voice::RS, "level", "rsLevel", c.rsLevel);
	set(Voice::HC, "level", "hcLevel", c.hcLevel);
	{
		const auto cal = m_cal.parts.find("hc.tm");
		u[size_t(Voice::HC)].set("tm", c.hcTm >= 0 ? c.hcTm : cal != m_cal.parts.end() ? cal->second : kNetlistTm);
	}
	set(Voice::HH, "level", "hhLevel", c.hhLevel);
	set(Voice::HH, "chdecay", "chDecay", c.chDecay);
	set(Voice::HH, "ohdecay", "ohDecay", c.ohDecay);
	set(Voice::CR, "level", "crLevel", c.crLevel);
	set(Voice::RD, "level", "rdLevel", c.rdLevel);
	// TUNE is the cymbals' clocks' (crclk.cir, rdclk.cir: Cymbal times them)
	m_impl->cymbals[0].setTune(m_cal.knob("crTune", c.crTune));
	m_impl->cymbals[1].setTune(m_cal.knob("rdTune", c.rdTune));
	m_impl->mix.set("volume", c.volume);
}

void VoiceBoard::setPlugged(Voice v, bool plugged)
{
	static const char *const kPlug[kVoices] = {"plug_bd", "plug_sd", "plug_lt", "plug_mt", "plug_ht",
	                                           "plug_rs", "plug_hc", "plug_hh", "plug_cr", "plug_rd"};
	m_plugged[size_t(v)] = plugged;
	m_impl->mix.set(kPlug[size_t(v)], plugged ? 1.0 : 0.0);
}

void VoiceBoard::write(int reg, uint8_t value)
{
	auto &im = *m_impl;
	m_latch[size_t(reg & 15)] = value;
	auto bit = [value](int b) { return ((value >> b) & 1) != 0; };
	if (reg >= 1 && reg <= 8) {
		im.sig[im.sAcc[size_t(reg)]] = dacVolts((value >> 1) & 63);
		im.accChanged = true;
	} else if (reg == 9) {
		const Voice v[5] = {Voice::BD, Voice::SD, Voice::LT, Voice::MT, Voice::HT};
		for (int k = 0; k < 5; ++k)
			im.trig[size_t(v[k])] = bit(k + 1);
	} else if (reg == 10) {
		im.trig[size_t(Voice::RS)] = bit(1);
		im.trig[size_t(Voice::HH)] = bit(2);
		im.sig[im.sOhsel] = bit(3) ? 5.0 : 0.0;
		im.trig[size_t(Voice::CR)] = bit(4);
		im.trig[size_t(Voice::RD)] = bit(5);
		im.trig[size_t(Voice::HC)] = bit(6);
	}
}

// One board sample of a voice that isn't one of the sample voices, on its own copy of the noise.
void VoiceBoard::Impl::stepVoice(int v, double *s, int j)
{
	s[sNoiseOf[size_t(v)]] = noiseBuf[size_t(j)];
	units[size_t(v)].step(s, window, &noiseSub[size_t(j) * kNoiseSub]);
	jackBuf[size_t(v)][size_t(j)] = s[sJack[size_t(v)]];
	if (probing)
		capture(v, j);
}

// One step of a sample voice (kGroup[g]) at the group's rate: its digital half, then its netlist.
void VoiceBoard::Impl::stepGrouped(size_t g, double *s, float *buf, int o)
{
	const int v = int(kGroup[g]);
	auto &u = units[size_t(v)];
	if (v == int(Voice::HH)) {
		// CLOSED/OPEN changing on its own (while a hat plays)
		const bool closed = s[sOhsel] > 2.5;
		if (closed != hhClosed && hihatOk)
			hihat.setClosed(closed);
		hhClosed = closed;
		// the late lines, and the edges as the netlist meets them
		bool edge = false;
		for (int k = 0; k < 3; ++k) {
			auto &ring = hhRing[size_t(k)];
			ring[size_t(hhPos)] = s[sHhIn[size_t(k)]];
			const double late = ring[size_t((hhPos - hhLatency + kDelay) % kDelay)];
			edge = edge || (k < 2 && std::abs(late - s[sHhLate[size_t(k)]]) > 1.0);
			s[sHhLate[size_t(k)]] = late;
		}
		hhPos = (hhPos + 1) % kDelay;
		if (edge) {
			u.wake();
			u.substeps(kEdgeSubsteps);
			u.edge(kEdgeBackwardEuler);
		}
		if (hihatOk && (u.awake() || !hihat.idle())) {
			const auto &l = hihat.step();
			for (int i = 0; i < 6; ++i)
				s[sHihat[size_t(i)]] = l.d[i];
			s[sHihat[6]] = l.run;
			if (!u.awake() && l.run > 2.5)
				u.wake();
		}
	}
	const int k = v == int(Voice::CR) ? 0 : v == int(Voice::RD) ? 1 : -1;
	if (k >= 0 && cymbalOk[size_t(k)] && (u.awake() || !cymbals[size_t(k)].idle())) {
		const auto &l = cymbals[size_t(k)].step();
		const auto &sc = sCymbal[size_t(k)];
		for (int i = 0; i < 6; ++i) {
			s[sc[size_t(i)]] = l.d[i];
			s[sc[size_t(6 + i)]] = l.e[i];
		}
		s[sc[12]] = l.run;
		if (!u.awake() && l.run > 2.5)
			u.wake();
	}
	u.step(s, window * over);
	buf[o] = float(s[sJack[size_t(v)]]);
}

void VoiceBoard::Impl::runJob(int job)
{
	double *s = sig.data();
	const int n = blockLength;
	switch (job) {
	case JobToms:
		for (int j = 0; j < n; ++j) {
			s[sNoiseOf[size_t(Voice::LT)]] = noiseBuf[size_t(j)];
			for (Voice v : {Voice::LT, Voice::MT, Voice::HT})
				units[size_t(v)].step(s, window);
			// the toms' shared noise, after them (it takes this sample's gates, they take its bus a
			// sample late), while any of them runs; R194 leaks the noise into it all the time, so it
			// sleeps with them, not on its own
			if (tomnoise.loaded()) {
				if (units[size_t(Voice::LT)].awake() || units[size_t(Voice::MT)].awake() || units[size_t(Voice::HT)].awake()) {
					tomnoise.wake();
					tomnoise.step(s, 1 << 30, &noiseSub[size_t(j) * kNoiseSub]);
				} else {
					tomnoise.on = false;
				}
			}
			for (Voice v : {Voice::LT, Voice::MT, Voice::HT})
				jackBuf[size_t(v)][size_t(j)] = s[sJack[size_t(v)]];
			if (probing)
				for (int u : {int(Voice::LT), int(Voice::MT), int(Voice::HT), kTomNoise})
					capture(u, j);
		}
		break;
	case JobSD:
	case JobBD:
	case JobHC:
	case JobRS: {
		const int v = int(job == JobSD ? Voice::SD : job == JobBD ? Voice::BD : job == JobHC ? Voice::HC : Voice::RS);
		for (int j = 0; j < n; ++j)
			stepVoice(v, s, j);
		break;
	}
	case JobHH:
	case JobCymbals:
		// the sample voices at their rate (the crash and ride together: each takes the other's run
		// line), their jacks down to the board's
		for (int j = 0; j < n; ++j) {
			float buf[3][2];
			for (int o = 0; o < over; ++o)
				for (size_t g = job == JobHH ? 0 : 1; g < (job == JobHH ? 1u : 3u); ++g)
					stepGrouped(g, s, buf[g], o);
			for (size_t g = job == JobHH ? 0 : 1; g < (job == JobHH ? 1u : 3u); ++g) {
				jackBuf[size_t(kGroup[g])][size_t(j)] = over > 1 ? groupDec[g].process(buf[g]) : buf[g][0];
				if (probing)
					capture(int(kGroup[g]), j);
			}
		}
		break;
	default:
		break;
	}
}

void VoiceBoard::setParallel(Parallel *p)
{
	m_impl->parallel = p;
}

void VoiceBoard::render(float *left, float *right, int n, float *const *jacks)
{
	auto &im = *m_impl;
	double *s = im.sig.data();
	const int N = n * m_factor; // board samples
	im.samples += N;
	// The latches' lines, as they stand for the whole call (the processor renders up to each write),
	// and their edges.
	for (int v = 0; v < kVoices; ++v) {
		const bool t = im.trig[size_t(v)];
		s[im.sTrig[size_t(v)]] = t ? 5.0 : 0.0;
		if (t == im.prevTrig[size_t(v)])
			continue;
		im.prevTrig[size_t(v)] = t;
		auto &u = im.units[size_t(v)];
		if (v == int(Voice::HH)) { // its netlist sees the edge late (Impl::stepGrouped)
			if (t && im.hihatOk) // TRIG's rising edge, with CLOSED/OPEN as it is
				im.hihat.start(s[im.sOhsel] > 2.5);
			u.wake();
			continue;
		}
		// a trigger edge: wake the voice and step it finely through it, the first quarter steps by
		// backward Euler (Circuit::edge)
		const int steps = Impl::grouped(v) ? im.over : 1; // the same time at the group's rate
		u.wake();
		u.substeps(kEdgeSubsteps * steps);
		u.edge(kEdgeBackwardEuler * steps);
		const int k = v == int(Voice::CR) ? 0 : v == int(Voice::RD) ? 1 : -1;
		if (k >= 0 && t && im.cymbalOk[size_t(k)]) // TRIG's rising edge into C50 / C40
			im.cymbals[size_t(k)].start();
	}
	// the noise for the block, then the voices: their jobs at once where there are threads for them
	if (int(im.noiseBuf.size()) < N) {
		im.noiseBuf.resize(size_t(N)); // a block larger than prepare's: grows once
		for (auto &b : im.jackBuf)
			b.resize(size_t(N));
	}
	if (int(im.noiseSub.size()) < N * kNoiseSub)
		im.noiseSub.resize(size_t(N * kNoiseSub));
	for (int j = 0; j < N; ++j)
		im.noiseBuf[size_t(j)] = im.noise.next(kNoiseSub, &im.noiseSub[size_t(j * kNoiseSub)]);
	im.blockLength = N;
	if (im.probing)
		for (auto &b : im.probeBuf)
			if (int(b.size()) < N)
				b.resize(size_t(N));
	bool any = false;
	for (const auto &u : im.units)
		any = any || u.awake();
	if (im.parallel) {
		static const bool fixedOrder = std::getenv("TR909_JOBORDER_FIXED") != nullptr; // tools
		void (*fn)(void *, int) = &Impl::runOrdered;
		if (fixedOrder)
			fn = &Impl::runJob;
		im.parallel->run(Impl::JobCount, fn, &im);
		if (!fixedOrder)
			std::stable_sort(im.jobOrder.begin(), im.jobOrder.end(),
			                 [&](int a, int b) { return im.jobTime[size_t(a)] > im.jobTime[size_t(b)]; });
	} else
		for (int job = 0; job < Impl::JobCount; ++job)
			im.runJob(job);
	for (const auto &u : im.units)
		any = any || u.awake();
	// the jack board, and down to the host's rate
	for (int i = 0, j = 0; i < n; ++i) {
		for (int k = 0; k < m_factor; ++k, ++j) {
			for (int v = 0; v < kVoices; ++v)
				s[im.sJack[size_t(v)]] = im.jackBuf[size_t(v)][size_t(j)];
			if (any)
				im.mix.wake();
			if (im.mix.loaded()) {
				im.mix.step(s, im.window);
				m_workL[size_t(k)] = float(s[im.sOutL] / kVoltsFullScale);
				m_workR[size_t(k)] = float(s[im.sOutR] / kVoltsFullScale);
				if (im.probing)
					im.capture(Impl::kMix, j);
			} else { // no jack board: every voice into both sides
				double sum = 0;
				for (int v = 0; v < kVoices; ++v)
					sum += s[im.sJack[size_t(v)]];
				m_workL[size_t(k)] = m_workR[size_t(k)] = float(sum / kVoltsFullScale);
			}
			if (jacks)
				for (int v = 0; v < kVoices; ++v)
					m_jackWork[size_t(v * m_factor + k)] = float(s[im.mix.loaded() ? im.sMulti[size_t(v)] : im.sJack[size_t(v)]] / kVoltsFullScale);
			if (im.probing)
				for (size_t c = 0; c < im.probes.size(); ++c)
					if (im.probes[c].signal >= 0)
						im.probeBuf[c][size_t(j)] = float(s[im.probes[c].signal]);
		}
		left[i] = m_factor == 1 ? m_workL[0] : m_decL.process(m_workL.data());
		right[i] = m_factor == 1 ? m_workR[0] : m_decR.process(m_workR.data());
		if (jacks)
			for (int v = 0; v < kVoices; ++v)
				if (jacks[v])
					jacks[v][i] = m_factor == 1 ? m_jackWork[size_t(v)] : m_jackDecimators[size_t(v)].process(&m_jackWork[size_t(v * m_factor)]);
	}
}

bool VoiceBoard::setProbe(int channel, const std::string &where)
{
	auto &im = *m_impl;
	if (channel < 0 || channel >= kProbes)
		return false;
	im.probeNames[size_t(channel)] = where;
	// where each probe is: (unit, block, node) for a netlist node, else a board signal
	struct At {
		int unit = -1, block = -1, node = -1, signal = -1;
	};
	std::array<At, kProbes> at;
	std::array<bool, kProbes> found{};
	for (int c = 0; c < kProbes; ++c) {
		const auto &name = im.probeNames[size_t(c)];
		found[size_t(c)] = name.empty();
		const auto dot = name.find('.');
		if (!name.empty() && dot != std::string::npos) {
			// "<unit>.<node>": a node of that unit's netlist, in the partition that works it out
			const std::string owner = name.substr(0, dot), node = name.substr(dot + 1);
			if (node == "0" || node == "gnd") { // ground: 0 V, nothing to record
				found[size_t(c)] = true;
				continue;
			}
			int u = owner == "tomnoise" ? Impl::kTomNoise : owner == "mix" ? Impl::kMix : -1;
			for (int v = 0; v < kVoices && u < 0; ++v)
				if (owner == kVoiceNames[v])
					u = v;
			if (u >= 0) {
				const auto &blocks = im.unit(u).blocks;
				for (size_t b = 0; b < blocks.size() && !found[size_t(c)]; ++b) {
					const int n = blocks[b].c.netlist().node(node);
					if (n > 0) {
						at[size_t(c)] = {u, int(b), n, -1};
						found[size_t(c)] = true;
					}
				}
			}
		}
		if (!found[size_t(c)] && !name.empty() && (at[size_t(c)].signal = im.sig.find(name)) >= 0)
			found[size_t(c)] = true;
	}
	// each partition with the probes' outputs it needs, and the ones that had some without
	for (int u = 0; u <= Impl::kMix; ++u)
		for (size_t b = 0; b < im.unit(u).blocks.size(); ++b) {
			std::vector<int> nodes;
			for (int c = 0; c < kProbes; ++c)
				if (at[size_t(c)].unit == u && at[size_t(c)].block == int(b))
					nodes.push_back(at[size_t(c)].node);
			auto &blk = im.unit(u).blocks[b];
			blk.retap(nodes);
		}
	im.probing = false;
	for (int c = 0; c < kProbes; ++c) {
		auto &p = im.probes[size_t(c)];
		const auto &a = at[size_t(c)];
		p = {a.unit, a.block, -1, a.signal};
		if (a.unit >= 0) {
			const auto &blk = im.unit(a.unit).blocks[size_t(a.block)];
			for (size_t k = 0; k < blk.probeNodes.size(); ++k)
				if (blk.probeNodes[k] == a.node) {
					p.output = int(blk.outs.size() + k);
					break;
				}
		}
		im.probing = im.probing || a.unit >= 0 || a.signal >= 0;
	}
	return found[size_t(channel)];
}

const std::vector<VoiceBoard::Part> &VoiceBoard::parts()
{
	static const std::vector<Part> all = [] {
		std::vector<Part> out;
		std::map<std::string, size_t> gates; // a CMOS gate's N and P devices, as one part
		auto lower = [](std::string t) {
			for (auto &ch : t)
				ch = char(std::tolower(static_cast<unsigned char>(ch)));
			return t;
		};
		std::vector<std::string> owners(kVoiceNames, kVoiceNames + kVoices);
		owners.push_back("tomnoise");
		owners.push_back("mix");
		for (const auto &owner : owners) {
			const char *text = netlistText(owner.c_str());
			if (text == nullptr)
				continue;
			std::istringstream in(text);
			std::string line;
			while (std::getline(in, line)) {
				std::istringstream ls(line);
				std::vector<std::string> t;
				for (std::string w; ls >> w;)
					t.push_back(w);
				if (t.size() < 3 || t[0][0] == '*' || t[0][0] == '.' || t[0][0] == '+')
					continue;
				std::string name = t[0];
				for (auto &ch : name)
					ch = char(std::toupper(static_cast<unsigned char>(ch)));
				auto node = [&](size_t k) { return owner + "." + lower(t[k]); };
				if (name == "PTM" && t.size() >= 5) { // the clap's trimmer
					out.push_back({"TM", owner, t[4], {{"-15 V end", node(1)}, {"wiper", node(2)}, {"ground end", node(3)}}});
					continue;
				}
				const auto digits = name.find_first_of("0123456789");
				if (digits == std::string::npos)
					continue;
				const std::string kind = name.substr(0, digits), rest = name.substr(digits);
				const auto num = rest.substr(0, rest.find_first_not_of("0123456789"));
				const std::string suffix = rest.substr(num.size());
				Part p{"", owner, "", {}};
				if ((kind == "R" || kind == "C") && suffix.empty() && t.size() >= 4) {
					p.ref = name;
					p.value = t[3];
					p.pins = {{"1", node(1)}, {"2", node(2)}};
				} else if (kind == "D" && suffix.empty()) {
					p.ref = name;
					p.value = t.size() > 3 ? t[3] : "";
					p.pins = {{"A", node(1)}, {"K", node(2)}};
				} else if (kind == "Q" && suffix.empty() && t.size() >= 5) {
					p.ref = name;
					p.value = t[4];
					p.pins = {{"C", node(1)}, {"B", node(2)}, {"E", node(3)}};
				} else if (kind == "X" && suffix.size() == 1 && t.size() >= 4) {
					p.ref = "IC" + num + lower(suffix);
					p.value = "op-amp";
					p.pins = {{"+", node(1)}, {"-", node(2)}, {"out", node(3)}};
				} else if (kind == "BC" && suffix.size() == 1 && t.size() >= 5) {
					p.ref = "IC" + num + lower(suffix);
					p.value = "comparator";
					p.pins = {{"+", node(1)}, {"-", node(2)}, {"out", node(3)}};
				} else if (kind == "B" && suffix.empty() && t.size() >= 6) {
					p.ref = "IC" + num;
					p.value = "OTA";
					p.pins = {{"I abc", node(1)}, {"+", node(3)}, {"-", node(4)}, {"out", node(5)}};
				} else if (kind == "M" && (suffix.size() == 2 || suffix.size() == 3) && suffix[1] == 'N') {
					// a CMOS gate section's N device: its gate is the input, its drain the output (a
					// NAND's two inputs are two N devices: N1, N2)
					const std::string ref = "IC" + num + lower(suffix.substr(0, 1));
					const auto it = gates.find(owner + ref);
					if (it == gates.end()) {
						gates[owner + ref] = out.size();
						out.push_back({ref, owner, "CMOS gate", {{"in", node(2)}, {"out", node(1)}}});
					} else {
						out[it->second].pins.insert(out[it->second].pins.begin() + 1, {"in 2", node(2)});
						out[it->second].pins[0].first = "in 1";
					}
					continue;
				} else if (kind == "P" && t.size() >= 5) {
					p.ref = "VR" + num + lower(suffix);
					p.value = t[4];
					p.pins = {{"1", node(1)}, {"wiper", node(2)}, {"3", node(3)}};
				} else {
					continue;
				}
				out.push_back(p);
			}
		}
		return out;
	}();
	return all;
}

const float *VoiceBoard::probeSamples(int channel) const
{
	const auto &b = m_impl->probeBuf[size_t(channel)];
	return b.empty() ? nullptr : b.data();
}

double VoiceBoard::signal(const std::string &name) const
{
	const int i = m_impl->sig.find(name);
	return i < 0 ? -1e9 : m_impl->sig[i];
}

double VoiceBoard::voiceOut(Voice v) const
{
	return m_impl->sig[m_impl->sJack[size_t(v)]];
}

bool VoiceBoard::voiceAwake(Voice v) const
{
	return m_impl->units[size_t(v)].awake();
}

bool VoiceBoard::asleep() const
{
	const auto &im = *m_impl;
	if (im.mix.awake())
		return false;
	for (int v = 0; v < kVoices; ++v)
		if (im.trig[size_t(v)] || voiceAwake(Voice(v)))
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
		              b.c.name().c_str(), b.c.portCount(), b.c.stateCount(), int(b.c.netlist().inputs.size()), t.steps,
		              double(t.iterations) / double(t.steps), double(t.factorizations) / double(t.steps),
		              100.0 * double(t.linearSteps) / double(t.steps), t.linearizations, b.seconds,
		              t.failures ? ("  " + std::to_string(t.failures) + " failed").c_str() : "");
		r += line;
	};
	for (const auto &u : im.units)
		for (const auto &b : u.blocks)
			one(b);
	for (const auto &b : im.mix.blocks)
		one(b);
	return r;
}

} // namespace tr909
