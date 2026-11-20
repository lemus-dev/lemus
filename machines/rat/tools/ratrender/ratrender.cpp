// Command-line runner for the RAT model.
//
//   ratrender IN.wav OUT.wav [knobs] [options]     render through the plugin's signal chain
//   ratrender --dc [knobs]                          print the DC operating point
//   ratrender --response [knobs]                    small-signal response, input to output jack
//   ratrender --bench                               real-time cost per channel
//
// Knobs (rotation 0..1): --dist D --filter F --volume V
// Options:
//   --level DBU      0 dBFS = DBU dBu at the jacks (default +4)
//   --cal datasheet|captured      --opamp lm308|op07      --parts rat|rat2|china (rat: the 1984 RAT, default)
//   --direct         run the pedal at the input file's rate without oversampling; samples are
//                    volts (for comparisons with SPICE at a high rate)
//   --probe NAME     with --direct: write a node instead of the output jack (opout, clip,
//                    filter, source)
//   --stats          print Newton statistics
//   --set NAME=VALUE override a calibration field (repeatable; see setCalibrationField)
//   ratrender --netlist "SPEC" [knobs]              print the model as an ngspice netlist with
//                                                   input source SPEC (e.g. "PWL file=in.txt")
#include "rat/core/unit.h"
#include "wav.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

using namespace rat;

int main(int argc, char **argv)
{
	std::string in, out, cal = "datasheet", opamp = "lm308", parts = "rat", probe;
	Controls k;
	double level = kDefaultLevelDbu;
	bool direct = false, dc = false, response = false, bench = false, stats = false;
	std::string netlist;
	std::vector<std::pair<std::string, double>> sets;
	for (int a = 1; a < argc; ++a) {
		const std::string s = argv[a];
		auto next = [&]() -> const char * {
			if (a + 1 >= argc) {
				std::fprintf(stderr, "%s needs a value\n", s.c_str());
				std::exit(2);
			}
			return argv[++a];
		};
		if (s == "--dist")
			k.distortion = std::atof(next());
		else if (s == "--filter")
			k.filter = std::atof(next());
		else if (s == "--volume")
			k.volume = std::atof(next());
		else if (s == "--level")
			level = std::atof(next());
		else if (s == "--cal")
			cal = next();
		else if (s == "--opamp")
			opamp = next();
		else if (s == "--parts")
			parts = next();
		else if (s == "--probe")
			probe = next();
		else if (s == "--direct")
			direct = true;
		else if (s == "--dc")
			dc = true;
		else if (s == "--response")
			response = true;
		else if (s == "--bench")
			bench = true;
		else if (s == "--stats")
			stats = true;
		else if (s == "--netlist")
			netlist = next();
		else if (s == "--set") {
			const std::string kv = next();
			const auto eq = kv.find('=');
			if (eq == std::string::npos) {
				std::fprintf(stderr, "--set needs NAME=VALUE\n");
				return 2;
			}
			sets.push_back({kv.substr(0, eq), std::atof(kv.c_str() + eq + 1)});
		}
		else if (in.empty())
			in = s;
		else
			out = s;
	}
	Calibration c = cal == "captured" ? capturedCalibration() : datasheetCalibration();
	for (const auto &kv : sets)
		if (!setCalibrationField(c, kv.first, kv.second)) {
			std::fprintf(stderr, "unknown calibration field %s\n", kv.first.c_str());
			return 2;
		}
	const OpAmp op = opamp == "op07" ? OpAmp::OP07 : OpAmp::LM308;
	const Parts pv = parts == "china" ? chinaParts() : parts == "rat2" ? rat2Parts() : Parts{};

	if (!netlist.empty()) {
		Pedal p;
		p.setCalibration(c);
		p.setOpAmp(op);
		p.setParts(pv);
		p.setControls(k, true);
		std::printf("%s", p.spiceNetlist(netlist).c_str());
		return 0;
	}

	if (dc || response) {
		Pedal p;
		p.enableProbes(true);
		p.setCalibration(c);
		p.setOpAmp(op);
		p.setParts(pv);
		p.prepare(384000);
		p.setControls(k, true);
		p.reset();
		if (dc) {
			for (int i = 0; i < 2000; ++i)
				p.process(0);
			const auto &pr = p.probe();
			std::printf("op out %.4f V  clip %.4f V  filter %.4f V  source %.4f V\n", pr.opOut, pr.clip,
				pr.filter, pr.source);
		}
		if (response)
			for (double f = 10; f <= 40000; f *= 1.0592537) // 40 points per decade
				std::printf("%.2f %.3f\n", f, p.smallSignalDb(f));
		return 0;
	}

	if (bench) {
		Channel ch;
		ch.pedal().setCalibration(c);
		ch.pedal().setOpAmp(op);
		ch.pedal().setParts(pv);
		ch.pedal().setControls(k, true);
		ch.prepare(48000);
		const int n = 48000 * (std::getenv("RAT_BENCH_SECONDS") ? std::atoi(std::getenv("RAT_BENCH_SECONDS")) : 5);
		double acc = 0;
		const auto t0 = std::chrono::steady_clock::now();
		for (int i = 0; i < n; ++i)
			acc += ch.process(0.5 * std::sin(i * 2 * 3.14159265 * 220 / 48000));
		const double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
		std::printf("5 s of audio at 48 kHz (8x) in %.3f s: %.1f%% of one core per channel (%g)\n", sec,
			100 * sec * 48000 / n, acc);
		std::printf("Newton: %.2f iterations per sample, %ld failures\n",
			double(ch.pedal().iterations()) / std::max(1L, ch.pedal().samples()), ch.pedal().failures());
		return 0;
	}

	if (in.empty() || out.empty()) {
		std::fprintf(stderr, "usage: ratrender IN.wav OUT.wav [--dist D --filter F --volume V] [options]\n");
		return 2;
	}
	std::vector<double> x;
	double rate = 0;
	if (!wav::read(in, x, rate)) {
		std::fprintf(stderr, "cannot read %s\n", in.c_str());
		return 1;
	}
	std::vector<double> y(x.size());
	long fails = 0, iters = 0, samples = 0;
	if (direct) {
		Pedal p;
		p.enableProbes(!probe.empty());
		p.setCalibration(c);
		p.setOpAmp(op);
		p.setParts(pv);
		p.prepare(rate);
		p.setControls(k, true);
		p.reset();
		for (size_t i = 0; i < x.size(); ++i) {
			const double o = p.process(x[i]);
			const auto &pr = p.probe();
			y[i] = probe == "opout" ? pr.opOut : probe == "clip" ? pr.clip : probe == "filter" ? pr.filter
				: probe == "source" ? pr.source : o;
		}
		fails = p.failures();
		iters = p.iterations();
		samples = p.samples();
	} else {
		Channel ch;
		ch.pedal().setCalibration(c);
		ch.pedal().setOpAmp(op);
		ch.pedal().setParts(pv);
		ch.pedal().setControls(k, true);
		ch.setLevelDbu(level);
		ch.prepare(rate);
		for (size_t i = 0; i < x.size(); ++i)
			y[i] = ch.process(x[i]);
		fails = ch.pedal().failures();
		iters = ch.pedal().iterations();
		samples = ch.pedal().samples();
	}
	if (!wav::write(out, y, rate)) {
		std::fprintf(stderr, "cannot write %s\n", out.c_str());
		return 1;
	}
	if (stats)
		std::fprintf(stderr, "Newton: %.2f iterations per sample, %ld failures in %ld samples\n",
			double(iters) / std::max(1L, samples), fails, samples);
	return 0;
}
