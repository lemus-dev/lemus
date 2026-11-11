// circuitrun: runs a netlist through tr808::circuit (the solver the voices use) and writes its
// outputs, and the same netlist as an ngspice deck, so the two can be compared
// (tools/spice/compare_circuit.py).
//
//   circuitrun NETLIST[,NETLIST...] [--rate HZ] [--seconds S] [--control NAME=VALUE]... [--param NAME=VALUE]...
//              [--input NAME=t,v,t,v,...]... [--pole-trap] [--out FILE.csv] [--spice DECK.cir DATA.txt]
//
// --pole-trap integrates the op-amps' internal poles by the trapezoidal rule (Circuit::setPoleTrap).
//
// Inputs are piecewise linear (as SPICE's PWL), held after their last point. The CSV has the time
// and each .output, one row per sample, starting at the DC operating point (t = 0).
#include "tr808/core/circuit.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using tr808::circuit::Circuit;
using tr808::circuit::Netlist;

namespace {

double pwl(const std::vector<std::pair<double, double>> &w, double t)
{
	if (w.empty())
		return 0;
	if (t <= w.front().first)
		return w.front().second;
	for (size_t k = 1; k < w.size(); ++k)
		if (t <= w[k].first) {
			const auto &a = w[k - 1], &b = w[k];
			return b.first == a.first ? b.second : a.second + (b.second - a.second) * (t - a.first) / (b.first - a.first);
		}
	return w.back().second;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "usage: circuitrun NETLIST [--rate HZ] [--seconds S] [--control N=V] [--param N=V] "
		                     "[--input N=t,v,...] [--edge-substeps N [--edge-be N] [--edge-inputs A,B]] [--pole-trap] [--out FILE.csv] [--spice DECK.cir DATA.txt]\n");
		return 2;
	}
	// NETLIST may be several files joined with commas (e.g. models.cir,bd.cir): read as one.
	std::stringstream text;
	{
		std::stringstream names(argv[1]);
		std::string name;
		while (std::getline(names, name, ',')) {
			std::ifstream in(name);
			if (!in) {
				std::fprintf(stderr, "can't read %s\n", name.c_str());
				return 1;
			}
			text << in.rdbuf() << "\n";
		}
	}

	double rate = 192000, seconds = 0.1;
	int edgeSubsteps = 0; // samples run in quarter steps after an input jumps by 0.5 V or more
	int edgeBe = 0;       // and of those, the quarter steps run by backward Euler (Circuit::edge)
	std::string edgeInputs; // the inputs whose jumps count as edges, comma-separated (default all)
	std::map<std::string, double> params;
	std::vector<std::pair<std::string, double>> controls;
	std::vector<std::pair<std::string, std::vector<std::pair<double, double>>>> inputs;
	std::string outPath, deckPath, dataPath;
	bool poleTrap = false;
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
		if (arg == "--rate") {
			rate = std::atof(next().c_str());
		} else if (arg == "--seconds") {
			seconds = std::atof(next().c_str());
		} else if (arg == "--control") {
			const auto [k, v] = keyValue(next());
			controls.push_back({k, std::atof(v.c_str())});
		} else if (arg == "--param") {
			const auto [k, v] = keyValue(next());
			params[k] = tr808::circuit::parseValue(v);
		} else if (arg == "--input") {
			const auto [k, v] = keyValue(next());
			std::vector<std::pair<double, double>> w;
			std::stringstream ss(v);
			std::string t, val;
			while (std::getline(ss, t, ',') && std::getline(ss, val, ','))
				w.push_back({std::atof(t.c_str()), std::atof(val.c_str())});
			inputs.push_back({k, w});
		} else if (arg == "--edge-substeps") {
			edgeSubsteps = std::atoi(next().c_str());
		} else if (arg == "--edge-be") {
			edgeBe = std::atoi(next().c_str());
		} else if (arg == "--edge-inputs") {
			edgeInputs = "," + next() + ",";
		} else if (arg == "--pole-trap") {
			poleTrap = true;
		} else if (arg == "--out") {
			outPath = next();
		} else if (arg == "--spice") {
			deckPath = next();
			dataPath = next();
		} else {
			std::fprintf(stderr, "unknown argument %s\n", arg.c_str());
			return 2;
		}
	}

	Netlist net;
	try {
		net = Netlist::parse(text.str(), params);
	} catch (const std::exception &e) {
		std::fprintf(stderr, "%s: %s\n", argv[1], e.what());
		return 1;
	}
	Circuit c(net);
	c.setRate(rate);
	c.setPoleTrap(poleTrap);
	if (const char *tol = std::getenv("TR808_TOL"))
		c.setTolerance(std::atof(tol));
	for (const auto &[k, v] : controls) {
		const int i = net.control(k);
		if (i < 0) {
			std::fprintf(stderr, "no control %s\n", k.c_str());
			return 1;
		}
		c.setControl(i, v);
	}
	std::vector<std::vector<std::pair<double, double>>> waves(net.inputs.size());
	for (const auto &[k, w] : inputs) {
		const int i = net.input(k);
		if (i < 0) {
			std::fprintf(stderr, "no input %s\n", k.c_str());
			return 1;
		}
		waves[size_t(i)] = w;
	}
	for (size_t i = 0; i < waves.size(); ++i)
		c.setInput(int(i), pwl(waves[i], 0));
	if (!c.solveDc())
		std::fprintf(stderr, "warning: the DC operating point didn't converge\n");
	if (!deckPath.empty()) {
		std::ofstream deck(deckPath);
		deck << c.toSpice(waves, seconds, 1.0 / rate, dataPath);
	}
	FILE *out = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "w");
	if (out == nullptr) {
		std::fprintf(stderr, "can't write %s\n", outPath.c_str());
		return 1;
	}
	std::fprintf(out, "time");
	for (const auto &o : net.outputs)
		std::fprintf(out, ",%s", o.name.c_str());
	std::fprintf(out, "\n");
	auto row = [&](double t) {
		std::fprintf(out, "%.9g", t);
		for (int o = 0; o < int(net.outputs.size()); ++o)
			std::fprintf(out, ",%.10g", c.output(o));
		std::fprintf(out, "\n");
	};
	row(0);
	const long n = long(seconds * rate + 0.5);
	for (long k = 1; k <= n; ++k) {
		const double t = double(k) / rate;
		for (size_t i = 0; i < waves.size(); ++i)
			if (!waves[i].empty()) {
				const double v = pwl(waves[i], t);
				const bool edgeInput = edgeInputs.empty() || edgeInputs.find("," + net.inputs[i] + ",") != std::string::npos;
				if (edgeSubsteps > 0 && edgeInput && std::abs(v - c.inputValue(int(i))) >= 0.5) {
					c.substeps(edgeSubsteps);
					c.edge(edgeBe);
					c.jump();
				}
				c.setInput(int(i), v);
			}
		c.step();
		row(t);
	}
	if (out != stdout)
		std::fclose(out);
	const auto &s = c.stats();
	std::fprintf(stderr, "%ld steps, %.2f Newton iterations per step, %.2f factorisations, %ld substepped, %ld failures, worst residual %.3g; %d states, %d ports\n",
	             s.steps, s.steps ? double(s.iterations) / double(s.steps) : 0.0, s.steps ? double(s.factorizations) / double(s.steps) : 0.0, s.substepped, s.failures, s.worstResidual, c.stateCount(),
	             c.portCount());
	return 0;
}
