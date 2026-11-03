// hhrun: plays hi-hat hits through the hi-hat's digital half (core/hihat.*) and its netlist
// (core/netlists/hh.cir) and writes the output to a WAV file; or writes the same hits as an
// ngspice deck with every D/A step at its exact time, for the timing measurements
// (docs/circuit/hh.md, "Rates and the D/A's timing"; tools/spice/hhtiming.py).
//
//   tr909hhrun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:ch|oh[:CODE]]... [--control NAME=V]...
//              [--param NAME=V]... [--smoothing sinc|box|none] [--clock HZ] [--trig MS] [--whole] [--pole-be] [--stats]
//              [--output out|jack] [--csv FILE] [--log FILE] [--spice DECK DATA [--spice-step S] [--spice-as-solver]]
//              [--rom FILE] [--netlists DIR]
//
// A hit starts the counter and holds trig at 5 V for --trig ms (2, one panel scan), with ohsel
// (5 V closed) and acc (5 V CODE / 63; default 32, the firmware's OH and CH-accent level at TOTAL
// ACCENT 0, CH normal 25) from its start, as the TRIG and accent latches do; those three reach the
// netlist the smoothing's latency later (8 steps Sinc), as the board should feed them. The trig and
// ohsel edges run as the board's trigger edges do (8 substeps, backward Euler, Circuit::jump).
// --whole runs the netlist as one circuit rather than in its .part partitions; --stats prints each
// partition's Newton work and time. --smoothing none gives the solver the latch's level at each
// step's end (a D/A step ramps across the step it falls in). --log writes IC68's and RUN/STOP's
// changes with their exact times. The CSV has the time and every .output of the netlist per step.
// The deck's D/A lines step in 10 ns at their exact times plus the smoothing's latency, so the two
// line up (--spice-as-solver: the lines as the solver had them, linear between its steps). Prints
// the CPU time per second of audio.
#include "tr909/core/circuit.h"
#include "tr909/core/hihat.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef TR909_ROM_PATH
#define TR909_ROM_PATH "hn61256p__c43.ic69"
#endif
#ifndef TR909_NETLIST_DIR
#define TR909_NETLIST_DIR "."
#endif

using namespace tr909;
using circuit::Circuit;

// The op-amps' poles by the trapezoidal rule, as the voicing board runs this voice (--pole-be: by
// backward Euler, as before 2026-11-01, when it ran at 192 kHz)
bool g_poleTrap = true;
using circuit::Netlist;

namespace {

std::string readFile(const std::string &path)
{
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		std::fprintf(stderr, "can't read %s\n", path.c_str());
		std::exit(1);
	}
	std::stringstream s;
	s << in.rdbuf();
	return s.str();
}

void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f) {
		std::fprintf(stderr, "can't write %s\n", path.c_str());
		std::exit(1);
	}
	auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(x.size() * 4);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3); // IEEE float
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate) * 4);
	u16(4);
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(x.data(), 4, x.size(), f);
	std::fclose(f);
}

// The netlist as its partitions (or whole), run in order each step, the voltages at the cuts
// passed on by name (as the voicing board does, without its sleep).
struct Runner {
	struct Part {
		Circuit c;
		double seconds = 0; // time in step() (--stats)
		std::vector<std::pair<int, int>> ins, outs; // (input or output index, signal)
	};
	std::vector<Part> parts;
	std::map<std::string, int> index;
	std::vector<double> sig;

	int signal(const std::string &name)
	{
		const auto it = index.find(name);
		if (it != index.end())
			return it->second;
		index[name] = int(sig.size());
		sig.push_back(0);
		return int(sig.size()) - 1;
	}
	void load(const std::string &models, const std::string &text, bool whole, const std::map<std::string, double> &params)
	{
		std::vector<std::string> texts;
		if (!whole)
			for (const auto &p : Netlist::partitions(text))
				texts.push_back(p.text);
		if (texts.empty())
			texts.push_back(text);
		for (const auto &t : texts) {
			Part p;
			p.c.load(Netlist::parse(models + "\n" + t, params));
			const auto &net = p.c.netlist();
			for (size_t i = 0; i < net.inputs.size(); ++i)
				p.ins.push_back({int(i), signal(net.inputs[i])});
			for (size_t o = 0; o < net.outputs.size(); ++o)
				p.outs.push_back({int(o), signal(net.outputs[o].name)});
			parts.push_back(std::move(p));
		}
	}
	void setRate(double r)
	{
		for (auto &p : parts) {
			p.c.setPoleTrap(g_poleTrap);
			p.c.setRate(r);
		}
	}
	void setControl(const std::string &name, double v)
	{
		for (auto &p : parts) {
			const int i = p.c.netlist().control(name);
			if (i >= 0)
				p.c.setControl(i, v);
		}
	}
	void settle()
	{
		for (int round = 0; round < (parts.size() > 1 ? 8 : 1); ++round)
			for (auto &p : parts) {
				for (const auto &[i, s] : p.ins)
					p.c.setInput(i, sig[size_t(s)]);
				p.c.solveDc();
				for (const auto &[o, s] : p.outs)
					sig[size_t(s)] = p.c.output(o);
			}
	}
	void edge()
	{
		for (auto &p : parts) {
			p.c.substeps(8);
			p.c.edge(8);
			p.c.jump();
		}
	}
	bool timing = false; // --stats: time each partition's steps
	void step()
	{
		for (auto &p : parts) {
			for (const auto &[i, s] : p.ins)
				p.c.setInput(i, sig[size_t(s)]);
			if (timing) {
				const auto t0 = std::chrono::steady_clock::now();
				p.c.step();
				p.seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			} else {
				p.c.step();
			}
			for (const auto &[o, s] : p.outs)
				sig[size_t(s)] = p.c.output(o);
		}
	}
};

struct Hit {
	double t;
	bool closed;
	int code;
};

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: tr909hhrun OUT.wav [--rate HZ] [--seconds S] [--hit TIME:ch|oh[:CODE]]... [--control N=V]... "
		                     "[--param N=V]... [--smoothing sinc|box|none] [--clock HZ] [--trig MS] [--whole] [--pole-be] [--output out|jack] "
		                     "[--csv FILE] [--spice DECK DATA [--spice-step S]] [--rom FILE] [--netlists DIR]\n");
		return 2;
	}
	const std::string outPath = argv[1];
	double rate = 96000, seconds = 0.5, trigMs = 2, clockHz = 0, spiceStep = 0.1e-6;
	std::vector<Hit> hits;
	std::vector<std::pair<std::string, double>> controls;
	std::map<std::string, double> params;
	std::string smoothing = "sinc", output = "out", csvPath, deckPath, dataPath, logPath;
	std::string romPath = TR909_ROM_PATH, netDir = TR909_NETLIST_DIR;
	bool whole = false, spiceAsSolver = false, stats = false;
	for (int a = 2; a < argc; ++a) {
		const std::string arg = argv[a];
		auto next = [&]() -> std::string {
			if (a + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", arg.c_str());
				std::exit(2);
			}
			return argv[++a];
		};
		auto keyValue = [](const std::string &s) {
			const auto eq = s.find('=');
			return std::make_pair(s.substr(0, eq), eq == std::string::npos ? std::string() : s.substr(eq + 1));
		};
		if (arg == "--rate") rate = std::atof(next().c_str());
		else if (arg == "--seconds") seconds = std::atof(next().c_str());
		else if (arg == "--trig") trigMs = std::atof(next().c_str());
		else if (arg == "--clock") clockHz = std::atof(next().c_str());
		else if (arg == "--smoothing") smoothing = next();
		else if (arg == "--output") output = next();
		else if (arg == "--whole") whole = true;
		else if (arg == "--pole-be") g_poleTrap = false;
		else if (arg == "--stats") stats = true;
		else if (arg == "--csv") csvPath = next();
		else if (arg == "--log") logPath = next();
		else if (arg == "--rom") romPath = next();
		else if (arg == "--netlists") netDir = next();
		else if (arg == "--spice-step") spiceStep = std::atof(next().c_str());
		else if (arg == "--spice-as-solver") spiceAsSolver = true;
		else if (arg == "--spice") {
			deckPath = next();
			dataPath = next();
		} else if (arg == "--control") {
			const auto [k, v] = keyValue(next());
			controls.push_back({k, std::atof(v.c_str())});
		} else if (arg == "--param") {
			const auto [k, v] = keyValue(next());
			params[k] = circuit::parseValue(v);
		} else if (arg == "--hit") {
			std::stringstream ss(next());
			std::string t, kind, code;
			std::getline(ss, t, ':');
			std::getline(ss, kind, ':');
			std::getline(ss, code, ':');
			const bool closed = kind != "oh";
			hits.push_back({std::atof(t.c_str()), closed, code.empty() ? 32 : std::atoi(code.c_str())});
		} else {
			std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
			return 2;
		}
	}
	if (hits.empty())
		hits.push_back({0.01, false, 32});
	std::sort(hits.begin(), hits.end(), [](const Hit &x, const Hit &y) { return x.t < y.t; });

	const std::string rom = readFile(romPath);
	HiHat hh;
	hh.setRom(reinterpret_cast<const uint8_t *>(rom.data()), rom.size());
	hh.setRate(rate);
	const bool smooth = smoothing != "none";
	hh.setSmoothing(smoothing == "box" ? HiHat::Smoothing::Box : HiHat::Smoothing::Sinc);
	if (clockHz > 0)
		hh.setClock(clockHz);
	hh.reset();
	std::vector<HiHat::Event> log;
	hh.setLog(&log);

	Runner run;
	try {
		run.load(readFile(netDir + "/models.cir"), readFile(netDir + "/hh.cir"), whole, params);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "hh.cir: %s\n", e.what());
		return 1;
	}
	run.setRate(rate);
	run.timing = stats;
	for (const auto &[k, v] : controls)
		run.setControl(k, v);
	const char *lineName[6] = {"d0", "d1", "d2", "d3", "d4", "d5"};
	int sd[6];
	for (int k = 0; k < 6; ++k)
		sd[k] = run.signal(lineName[k]);
	const int sRun = run.signal("run"), sTrig = run.signal("trig"), sOhsel = run.signal("ohsel"), sAcc = run.signal("acc");
	const int sOut = run.signal(output);
	// power-on as the firmware leaves it: a closed hat at rest, the accent DAC at 0
	const auto &l0 = hh.lines();
	for (int k = 0; k < 6; ++k)
		run.sig[size_t(sd[k])] = l0.d[k];
	run.sig[size_t(sRun)] = l0.run;
	run.sig[size_t(sOhsel)] = 5;
	run.sig[size_t(sAcc)] = 0;
	run.settle();

	// The inputs as the solver saw them, for the deck: trig, ohsel and acc step at the start of
	// their step; the D/A lines and RUN from the log.
	std::vector<std::pair<double, double>> wTrig{{0, 0}}, wOhsel{{0, 5}}, wAcc{{0, 0}};
	auto stepTo = [](std::vector<std::pair<double, double>> &w, double t, double v) {
		if (w.back().second == v)
			return;
		w.push_back({t, w.back().second});
		w.push_back({t + 1e-8, v});
	};

	const long n = long(seconds * rate + 0.5);
	std::vector<std::vector<std::pair<double, double>>> given(7); // --spice-as-solver: the lines as the solver had them
	for (int b = 0; b < 7; ++b)
		given[size_t(b)].push_back({0, run.sig[size_t(b < 6 ? sd[b] : sRun)]});
	std::vector<float> out;
	out.reserve(size_t(n));
	// the CSV: every .output of the netlist (out, jack and any probes)
	std::vector<std::pair<std::string, int>> probes;
	for (const auto &p : run.parts)
		for (const auto &o : p.c.netlist().outputs)
			if (o.name[0] != '@')
				probes.push_back({o.name, run.signal(o.name)});
	FILE *csv = csvPath.empty() ? nullptr : std::fopen(csvPath.c_str(), "w");
	if (csv) {
		std::fprintf(csv, "time");
		for (const auto &pr : probes)
			std::fprintf(csv, ",%s", pr.first.c_str());
		std::fprintf(csv, "\n");
	}
	size_t next = 0;
	long trigOff = -1;
	const long trigLen = long(trigMs * 1e-3 * rate + 0.5);
	// trig, ohsel and acc reach the netlist the lines' latency later (whole steps), so that they line
	// up with the D/A as on the board
	const long lag = smooth ? long(hh.latency()) : 0;
	std::vector<std::pair<long, Hit>> toNet;
	const auto t0 = std::chrono::steady_clock::now();
	for (long k = 0; k < n; ++k) {
		const double tStart = double(k) / rate; // this step runs from tStart to tStart + 1 / rate
		bool edge = false;
		while (next < hits.size() && hits[next].t <= tStart + 0.5 / rate) {
			const Hit &h = hits[next++];
			hh.start(h.closed);
			toNet.push_back({k + lag, h});
		}
		for (size_t q = 0; q < toNet.size();) {
			if (toNet[q].first != k) {
				++q;
				continue;
			}
			const Hit h = toNet[q].second;
			toNet.erase(toNet.begin() + long(q));
			run.sig[size_t(sTrig)] = 5;
			run.sig[size_t(sOhsel)] = h.closed ? 5 : 0;
			run.sig[size_t(sAcc)] = 5.0 * h.code / 63;
			stepTo(wTrig, tStart, 5);
			stepTo(wOhsel, tStart, h.closed ? 5 : 0);
			stepTo(wAcc, tStart, 5.0 * h.code / 63);
			trigOff = k + trigLen;
			edge = true;
		}
		if (k == trigOff) {
			run.sig[size_t(sTrig)] = 0;
			stepTo(wTrig, tStart, 0);
			edge = true;
		}
		const auto &l = hh.step();
		if (smooth) {
			for (int b = 0; b < 6; ++b)
				run.sig[size_t(sd[b])] = l.d[b];
			run.sig[size_t(sRun)] = l.run;
		} else {
			for (int b = 0; b < 6; ++b)
				run.sig[size_t(sd[b])] = ((hh.code() >> b) & 1) * HiHat::kVdd;
			run.sig[size_t(sRun)] = hh.running() ? HiHat::kVdd : 0;
		}
		if (spiceAsSolver)
			for (int b = 0; b < 7; ++b)
				given[size_t(b)].push_back({tStart + 1 / rate, run.sig[size_t(b < 6 ? sd[b] : sRun)]});
		if (edge)
			run.edge();
		run.step();
		out.push_back(float(run.sig[size_t(sOut)]));
		if (csv) {
			std::fprintf(csv, "%.9g", tStart + 1 / rate);
			for (const auto &pr : probes)
				std::fprintf(csv, ",%.10g", run.sig[size_t(pr.second)]);
			std::fprintf(csv, "\n");
		}
	}
	const double cpu = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	if (csv)
		std::fclose(csv);
	writeWav(outPath, out, int(rate + 0.5));
	if (!logPath.empty()) {
		FILE *f = std::fopen(logPath.c_str(), "w");
		if (f) {
			std::fprintf(f, "time,code,run\n");
			for (const auto &e : log)
				std::fprintf(f, "%.12g,%d,%d\n", e.t, e.code, e.run ? 1 : 0);
			std::fclose(f);
		}
	}
	long steps = 0, iterations = 0, substepped = 0, failures = 0;
	for (const auto &p : run.parts) {
		const auto &s = p.c.stats();
		if (stats)
			std::fprintf(stderr, "  partition: %d states, %d ports: %.2f Newton iterations a step, %.1f %% linear, %.2f s\n", p.c.stateCount(),
			             p.c.portCount(), s.steps ? double(s.iterations) / double(s.steps) : 0.0,
			             s.steps ? 100.0 * double(s.linearSteps) / double(s.steps) : 0.0, p.seconds);
		steps += s.steps;
		iterations += s.iterations;
		substepped += s.substepped;
		failures += s.failures;
	}
	std::fprintf(stderr, "%s: %ld steps at %.0f Hz in %zu partition(s), %.2f Newton iterations a step, %ld substepped, %ld failures; "
	                     "CPU %.1f ms per second of audio; D/A latency %.2f steps\n",
	             outPath.c_str(), n, rate, run.parts.size(), steps ? double(iterations) / double(steps) : 0.0, substepped, failures,
	             1000 * cpu / seconds, smooth ? hh.latency() : 0.0);

	if (!deckPath.empty()) {
		// the whole netlist with the inputs as PWL: the D/A lines and RUN at the logic's exact
		// times, delayed by the smoothing's latency
		Netlist net = Netlist::parse(readFile(netDir + "/models.cir") + "\n" + readFile(netDir + "/hh.cir"), params);
		Circuit c(net);
		c.setPoleTrap(g_poleTrap);
		for (const auto &[k, v] : controls)
			if (net.control(k) >= 0)
				c.setControl(net.control(k), v);
		const double delay = smooth && !spiceAsSolver ? hh.latency() / rate : 0.0;
		std::vector<std::vector<std::pair<double, double>>> waves(net.inputs.size());
		auto put = [&](const char *name, const std::vector<std::pair<double, double>> &w) {
			const int i = net.input(name);
			if (i >= 0)
				waves[size_t(i)] = w;
		};
		put("trig", wTrig);
		put("ohsel", wOhsel);
		put("acc", wAcc);
		HiHat ref;
		ref.setRom(reinterpret_cast<const uint8_t *>(rom.data()), rom.size());
		ref.reset();
		std::vector<std::vector<std::pair<double, double>>> wd(7);
		for (int b = 0; b < 6; ++b)
			wd[size_t(b)].push_back({0, ((ref.code() >> b) & 1) * HiHat::kVdd});
		wd[6].push_back({0, 0});
		for (const auto &e : log)
			for (int b = 0; b < 7; ++b) {
				const double v = b < 6 ? ((e.code >> b) & 1) * HiHat::kVdd : (e.run ? HiHat::kVdd : 0.0);
				stepTo(wd[size_t(b)], e.t + delay, v);
			}
		if (spiceAsSolver)
			wd = given;
		for (int b = 0; b < 6; ++b)
			put(lineName[b], wd[size_t(b)]);
		put("run", wd[6]);
		for (size_t i = 0; i < waves.size(); ++i)
			c.setInput(int(i), waves[i].empty() ? 0 : waves[i].front().second);
		c.solveDc();
		std::ofstream deck(deckPath);
		deck << c.toSpice(waves, seconds, spiceStep, dataPath);
	}
	return 0;
}
