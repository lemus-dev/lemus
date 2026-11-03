// cyrun: one crash or ride hit through its digital half (Cymbal, core/cymbals.*) and its analog half
// (cr.cir or rd.cir, run in its partitions as the voicing board runs them), to a WAV file; the
// clock's timing as its netlist (crclk.cir, rdclk.cir) gives it; the lines' events with their exact
// times, for the SPICE comparison (tools/spice/cases/cymbals.py); the CPU time.
//
//   tr909cyrun [--voice cr|rd] [--rom FILE] [--netlists DIR] [--rate HZ] [--seconds S] [--tune X]
//              [--level X] [--acc CODE] [--other-run] [--param NAME=VALUE]... [--smoothing sinc|box]
//              [--whole] [--pole-be] [--out FILE.wav] [--csv FILE] [--events FILE] [--edges FILE] [--timing]
//              [--clock-rate HZ] [--live-clock S]
//
// CODE is the accent DAC's code, 0-63 (default 10, the firmware's normal level at TOTAL ACCENT 0; 21
// is its accent there, 47 at full TOTAL ACCENT). --other-run holds the other cymbal's RUN line high
// (it plays along: its base network loads the shared accent DAC differently). The hit's trigger is at
// 1 ms. --out writes the MULTI OUT jack (jack) at the board's rate, float; --csv the jack, out and
// the lines per step. --whole runs the analog netlist as one circuit. --timing prints the clock's
// timing at the TUNE positions; --live-clock S runs the clock netlist along for S seconds at
// --clock-rate and compares its edges with Cymbal's.
#include "tr909/core/circuit.h"
#include "tr909/core/cymbals.h"
#include "tr909/netlists.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#ifndef TR909_ROM_DIR
#define TR909_ROM_DIR ""
#endif

using namespace tr909;
using circuit::Circuit;

// The op-amps' poles by the trapezoidal rule, as the voicing board runs this voice (--pole-be: by
// backward Euler, as before 2026-11-01, when it ran at 192 kHz)
bool g_poleTrap = true;
using circuit::Netlist;

namespace {

void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	FILE *f = std::fopen(path.c_str(), "wb");
	if (!f)
		return;
	auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
	auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };
	const uint32_t bytes = uint32_t(x.size() * 4);
	std::fwrite("RIFF", 1, 4, f);
	u32(36 + bytes);
	std::fwrite("WAVEfmt ", 1, 8, f);
	u32(16);
	u16(3);
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate * 4));
	u16(4);
	u16(32);
	std::fwrite("data", 1, 4, f);
	u32(bytes);
	std::fwrite(x.data(), 4, x.size(), f);
	std::fclose(f);
}

std::string readText(const std::string &dir, const std::string &name)
{
	if (!dir.empty()) {
		std::ifstream f(dir + "/" + name + ".cir");
		if (f) {
			std::stringstream s;
			s << f.rdbuf();
			return s.str();
		}
	}
	const char *t = netlists::find(name.c_str());
	return t ? t : "";
}

// The analog netlist in its partitions, run in order, the cut nodes passed on by name.
struct Board {
	struct Part {
		Circuit c;
		std::vector<std::pair<int, std::string>> ins, outs;
	};
	std::vector<Part> parts;
	std::map<std::string, double> sig;

	bool load(const std::string &models, const std::string &text, const std::map<std::string, double> &params, bool whole)
	{
		auto split = whole ? std::vector<Netlist::Partition>{} : Netlist::partitions(text);
		if (split.empty())
			split.push_back({"whole", text, false});
		parts.clear();
		parts.resize(split.size());
		for (size_t k = 0; k < split.size(); ++k) {
			try {
				parts[k].c.load(Netlist::parse(models + "\n" + split[k].text, params));
			} catch (const std::exception &e) {
				std::fprintf(stderr, "%s: %s\n", split[k].name.c_str(), e.what());
				return false;
			}
			parts[k].c.setName(split[k].name);
			const auto &net = parts[k].c.netlist();
			for (size_t i = 0; i < net.inputs.size(); ++i)
				parts[k].ins.push_back({int(i), net.inputs[i]});
			for (size_t o = 0; o < net.outputs.size(); ++o)
				parts[k].outs.push_back({int(o), net.outputs[o].name});
		}
		return true;
	}
	void setRate(double r)
	{
		for (auto &p : parts) {
			p.c.setPoleTrap(g_poleTrap);
			p.c.setRate(r);
		}
	}
	void control(const std::string &name, double v)
	{
		for (auto &p : parts) {
			const int i = p.c.netlist().control(name);
			if (i >= 0)
				p.c.setControl(i, v);
		}
	}
	void run(bool dc)
	{
		for (auto &p : parts) {
			for (const auto &[i, n] : p.ins)
				p.c.setInput(i, sig[n]);
			if (dc)
				p.c.solveDc();
			else
				p.c.step();
			for (const auto &[o, n] : p.outs)
				sig[n] = p.c.output(o);
		}
	}
	void stats(double seconds) const
	{
		for (const auto &p : parts) {
			const auto &s = p.c.stats();
			std::fprintf(stderr, "  %-6s %d ports, %d states: %.2f Newton iterations a step, %.1f %% linear, %ld substepped, %ld failures\n",
			             p.c.name().c_str(), p.c.portCount(), p.c.stateCount(), s.steps ? double(s.iterations) / double(s.steps) : 0.0,
			             s.steps ? 100.0 * double(s.linearSteps) / double(s.steps) : 0.0, s.substepped, s.failures);
		}
		(void)seconds;
	}
};

} // namespace

int main(int argc, char **argv)
{
	std::string voice = "cr", romPath, netDir, outPath, csvPath, eventsPath, edgesPath;
	double rate = 96000, seconds = 1.5, tune = 0.5, level = 0.5, liveClock = 0, clockRate = Cymbal::kClockRate;
	int acc = 10;
	bool otherRun = false, whole = false, timing = false;
	Cymbal::Smoothing smoothing = Cymbal::Smoothing::Sinc;
	std::map<std::string, double> params;
	for (int a = 1; a < argc; ++a) {
		const std::string arg = argv[a];
		auto next = [&]() -> std::string {
			if (a + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", arg.c_str());
				std::exit(2);
			}
			return argv[++a];
		};
		if (arg == "--voice")
			voice = next();
		else if (arg == "--rom")
			romPath = next();
		else if (arg == "--netlists")
			netDir = next();
		else if (arg == "--rate")
			rate = std::atof(next().c_str());
		else if (arg == "--seconds")
			seconds = std::atof(next().c_str());
		else if (arg == "--tune")
			tune = std::atof(next().c_str());
		else if (arg == "--level")
			level = std::atof(next().c_str());
		else if (arg == "--acc")
			acc = std::atoi(next().c_str());
		else if (arg == "--other-run")
			otherRun = true;
		else if (arg == "--param") {
			const std::string kv = next();
			const auto eq = kv.find('=');
			params[kv.substr(0, eq)] = circuit::parseValue(kv.substr(eq + 1));
		} else if (arg == "--smoothing")
			smoothing = next() == "box" ? Cymbal::Smoothing::Box : Cymbal::Smoothing::Sinc;
		else if (arg == "--pole-be")
			g_poleTrap = false;
		else if (arg == "--whole")
			whole = true;
		else if (arg == "--out")
			outPath = next();
		else if (arg == "--csv")
			csvPath = next();
		else if (arg == "--events")
			eventsPath = next();
		else if (arg == "--edges")
			edgesPath = next();
		else if (arg == "--timing")
			timing = true;
		else if (arg == "--clock-rate")
			clockRate = std::atof(next().c_str());
		else if (arg == "--live-clock")
			liveClock = std::atof(next().c_str());
		else {
			std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
			return 2;
		}
	}
	if (voice != "cr" && voice != "rd") {
		std::fprintf(stderr, "--voice is cr or rd\n");
		return 2;
	}
	const std::string other = voice == "cr" ? "rd" : "cr";
	if (romPath.empty())
		romPath = std::string(TR909_ROM_DIR) + (voice == "cr" ? "/hn61256p__c42.ic62" : "/hn61256p__c44.ic54");
	std::vector<uint8_t> rom;
	{
		std::ifstream f(romPath, std::ios::binary);
		rom.assign(std::istreambuf_iterator<char>(f), {});
		if (rom.size() != size_t(Cymbal::kRomSize))
			std::fprintf(stderr, "warning: %s: %zu bytes (no ROM: the voice is silent)\n", romPath.c_str(), rom.size());
	}
	const std::string models = readText(netDir, "models");
	const std::string clockText = models + "\n" + readText(netDir, voice + "clk");
	const std::string text = readText(netDir, voice);
	if (models.empty() || text.empty()) {
		std::fprintf(stderr, "no netlist %s\n", voice.c_str());
		return 1;
	}

	Cymbal cy;
	cy.setRom(rom.size() == size_t(Cymbal::kRomSize) ? rom.data() : nullptr, rom.size());
	{
		const auto t0 = std::chrono::steady_clock::now();
		if (!cy.setClock(clockText, params)) {
			std::fprintf(stderr, "the clock netlist %sclk doesn't run\n", voice.c_str());
			return 1;
		}
		std::fprintf(stderr, "clock timed at %d TUNE positions in %.3f s of CPU\n", Cymbal::kTunePoints,
		             std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
	}
	if (timing) {
		std::printf("tune   sample_Hz   ab_us   ba_us  reset_us  first_us (A/B)\n");
		for (int k = 0; k <= 20; ++k) {
			cy.setTune(k / 20.0);
			const auto &t = cy.timing();
			std::printf("%.2f %10.1f %7.4f %7.4f %8.4f  %c %.4f %.4f %.4f %.4f\n", k / 20.0, cy.sampleRate(), t.ab * 1e6, t.ba * 1e6,
			            t.reset * 1e6, t.firstIsA ? 'A' : 'B', t.first[0] * 1e6, t.first[1] * 1e6, t.first[2] * 1e6, t.first[3] * 1e6);
		}
	}
	cy.setTune(tune);
	cy.setRate(rate);
	cy.setSmoothing(smoothing);
	cy.reset();
	std::vector<Cymbal::Event> events;
	std::vector<Cymbal::Edge> edges;
	if (!eventsPath.empty())
		cy.setLog(&events);
	if (!edgesPath.empty() || liveClock > 0)
		cy.setEdgeLog(&edges);

	Board board;
	if (!board.load(models, text, params, whole))
		return 1;
	board.setRate(rate);
	board.control("level", level);
	auto put = [&](const Cymbal::Lines &l) {
		for (int k = 0; k < 6; ++k) {
			board.sig["d" + std::to_string(k)] = l.d[k];
			board.sig["e" + std::to_string(k)] = l.e[k];
		}
		board.sig["run"] = l.run;
	};
	board.sig["acc"] = 5.0 * acc / 63;
	board.sig[other + ".run"] = otherRun ? 5.0 : 0.0;
	put(cy.lines());
	board.run(true);
	board.run(true);

	const long n = long(seconds * rate + 0.5);
	const long trigAt = long(1e-3 * rate);
	std::vector<float> jack;
	jack.reserve(size_t(n));
	FILE *csv = csvPath.empty() ? nullptr : std::fopen(csvPath.c_str(), "w");
	if (csv)
		std::fprintf(csv, "time,jack,out,run,code,envelope\n");
	double cpu = 0;
	for (long k = 0; k < n; ++k) {
		if (k == trigAt)
			cy.start(0);
		const auto t0 = std::chrono::steady_clock::now();
		put(cy.step());
		board.run(false);
		cpu += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		jack.push_back(float(board.sig["jack"]));
		if (csv)
			std::fprintf(csv, "%.9g,%.9g,%.9g,%.6g,%d,%u\n", double(k + 1) / rate, board.sig["jack"], board.sig["out"], cy.lines().run,
			             cy.code(), (cy.count() >> 9) & 63u);
	}
	if (csv)
		std::fclose(csv);
	std::fprintf(stderr, "%s at %.0f Hz, TUNE %.2f (sample clock %.1f Hz), %s smoothing (latency %.1f steps): %.3f s of CPU a second\n",
	             voice.c_str(), rate, tune, cy.sampleRate(), smoothing == Cymbal::Smoothing::Box ? "box" : "sinc", cy.latency(), cpu / seconds);
	board.stats(seconds);
	if (!outPath.empty())
		writeWav(outPath, jack, int(rate));
	if (!eventsPath.empty()) {
		FILE *f = std::fopen(eventsPath.c_str(), "w");
		std::fprintf(f, "time,code,envelope,run\n");
		for (const auto &e : events)
			std::fprintf(f, "%.12g,%d,%d,%d\n", e.t, e.code, e.envelope, e.run ? 1 : 0);
		std::fclose(f);
	}
	if (!edgesPath.empty()) {
		FILE *f = std::fopen(edgesPath.c_str(), "w");
		std::fprintf(f, "time,a\n");
		for (const auto &e : edges)
			std::fprintf(f, "%.12g,%d\n", e.t, e.a ? 1 : 0);
		std::fclose(f);
	}
	if (liveClock > 0) {
		// The clock netlist run along from the same start (TRIG at 1 ms, RUN rising with it) for
		// liveClock seconds: its rising edges against those Cymbal placed.
		Netlist net = Netlist::parse(clockText, params);
		Circuit c(net);
		c.setPoleTrap(g_poleTrap);
		c.setRate(clockRate);
		c.setControl(net.control("tune"), tune);
		const int iTrig = net.input("trig"), iRun = net.input("run"), oA = net.output("osca"), oB = net.output("oscb");
		c.setInput(iTrig, 0);
		c.setInput(iRun, 0);
		c.solveDc();
		const double t0 = double(trigAt) / rate, dt = 1 / clockRate;
		std::vector<Cymbal::Edge> live;
		double a0 = c.output(oA), b0 = c.output(oB);
		const long m = long(liveClock * clockRate);
		for (long k = 1; k <= m; ++k) {
			if (k == 1) {
				c.setInput(iTrig, 5);
				c.setInput(iRun, 5);
				c.jump();
			}
			c.step();
			const double t = t0 + double(k) * dt, a1 = c.output(oA), b1 = c.output(oB);
			if (a0 < Cymbal::kThreshold && a1 >= Cymbal::kThreshold)
				live.push_back({t - dt * (a1 - Cymbal::kThreshold) / (a1 - a0), true});
			if (b0 < Cymbal::kThreshold && b1 >= Cymbal::kThreshold)
				live.push_back({t - dt * (b1 - Cymbal::kThreshold) / (b1 - b0), false});
			a0 = a1;
			b0 = b1;
		}
		double worst = 0, sum = 0;
		size_t count = 0;
		for (size_t k = 0; k < live.size() && k < edges.size(); ++k, ++count) {
			if (live[k].a != edges[k].a) {
				std::fprintf(stderr, "live clock: edge %zu is %c live, %c placed\n", k, live[k].a ? 'A' : 'B', edges[k].a ? 'A' : 'B');
				break;
			}
			const double d = edges[k].t - live[k].t;
			worst = std::max(worst, std::abs(d));
			sum += d;
		}
		std::fprintf(stderr, "live clock at %.0f Hz: %zu edges in %.4f s; placed against live: worst %.4f us, mean %.4f us, last %.4f us\n",
		             clockRate, live.size(), liveClock, worst * 1e6, count ? sum / double(count) * 1e6 : 0.0,
		             count ? (edges[count - 1].t - live[count - 1].t) * 1e6 : 0.0);
	}
	return 0;
}
