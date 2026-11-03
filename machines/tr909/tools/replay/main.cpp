// replay: one partition of a voice, stepped alone on the inputs a run gave it (for the solver's
// speed and results without the rest of the board). TR909_RECORD=DIR on a run (tr909voicerun,
// tr909bench) writes DIR/<voice>_<partition>.rec for each partition (only those named in
// TR909_RECORDNAMES, comma-separated, if it is set): its netlist, parameters, rate, controls and
// state as it first steps, then each step's inputs and edge, jump and substep requests.
//
//   tr909replay FILE.rec [OUT.bin]
//
// Prints the steps, the CPU cycles and instructions a step (Apple Silicon; 0 elsewhere), and the
// solver's statistics; OUT.bin gets every step's outputs (doubles), to compare two builds bit for
// bit. REPEAT=N steps it N times (each from the recorded state), for a profiler.
#include "tr909/core/circuit.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>
#if defined(__APPLE__)
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

using tr909::circuit::Circuit;
using tr909::circuit::Netlist;

namespace {

FILE *f = nullptr;
uint64_t u64()
{
	uint64_t n = 0;
	if (std::fread(&n, 8, 1, f) != 1)
		return 0;
	return n;
}
std::string str()
{
	const size_t count = size_t(u64());
	std::string s(count, '\0');
	if (!s.empty() && std::fread(s.data(), 1, s.size(), f) != s.size())
		s.clear();
	return s;
}
std::vector<double> vec()
{
	const size_t count = size_t(u64());
	std::vector<double> v(count);
	if (!v.empty() && std::fread(v.data(), 8, v.size(), f) != v.size())
		v.clear();
	return v;
}

void counters(double &cycles, double &instructions)
{
	cycles = instructions = 0;
#if defined(__APPLE__)
	rusage_info_v4 r{};
	if (proc_pid_rusage(getpid(), RUSAGE_INFO_V4, reinterpret_cast<rusage_info_t *>(&r)) == 0) {
		cycles = double(r.ri_cycles);
		instructions = double(r.ri_instructions);
	}
#endif
}

struct Step {
	int32_t edge, sub, jump;
};

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2 || !(f = std::fopen(argv[1], "rb"))) {
		std::fprintf(stderr, "usage: tr909replay FILE.rec [OUT.bin]\n");
		return 1;
	}
	const std::string name = str(), text = str();
	std::map<std::string, double> params;
	for (uint64_t n = u64(); n > 0; --n) {
		const std::string k = str();
		double v = 0;
		if (std::fread(&v, 8, 1, f) == 1)
			params[k] = v;
	}
	double rate = 0;
	if (std::fread(&rate, 8, 1, f) != 1)
		return 1;
	const auto controls = vec();
	Circuit::State state;
	state.vc = vec();
	state.ic = vec();
	state.v = vec();
	state.u = vec();
	const uint64_t ni = u64();
	std::vector<Step> flags;
	std::vector<double> inputs;
	for (;;) {
		Step s{};
		if (std::fread(&s, 4, 3, f) != 3)
			break;
		std::vector<double> in(static_cast<size_t>(ni));
		if (std::fread(in.data(), 8, size_t(ni), f) != size_t(ni))
			break;
		flags.push_back(s);
		inputs.insert(inputs.end(), in.begin(), in.end());
	}
	std::fclose(f);
	const size_t n = flags.size();
	auto run = [&](Circuit &c, std::vector<double> *out) {
		c.setRate(rate);
		for (size_t k = 0; k < controls.size(); ++k)
			c.setControl(int(k), controls[k]);
		c.setState(state);
		const int ny = int(c.netlist().outputs.size());
		for (size_t k = 0; k < n; ++k) {
			for (uint64_t i = 0; i < ni; ++i)
				c.setInput(int(i), inputs[k * ni + i]);
			if (flags[k].edge > 0)
				c.edge(flags[k].edge);
			if (flags[k].jump)
				c.jump();
			if (flags[k].sub > 0)
				c.substeps(flags[k].sub);
			c.step();
			if (out)
				for (int o = 0; o < ny; ++o)
					out->push_back(c.output(o));
		}
	};
	const int repeat = std::getenv("REPEAT") ? std::atoi(std::getenv("REPEAT")) : 1;
	for (int r = 1; r < repeat; ++r) {
		Circuit c(Netlist::parse(text, params));
		c.setName(name);
		run(c, nullptr);
	}
	Circuit c(Netlist::parse(text, params));
	c.setName(name);
	std::vector<double> out;
	out.reserve(n * c.netlist().outputs.size());
	double c0, i0, c1, i1;
	counters(c0, i0);
	run(c, &out);
	counters(c1, i1);
	const auto &s = c.stats();
	const double steps = double(std::max<size_t>(n, 1));
	std::printf("%-14s %7zu steps %7.0f cyc/step %7.0f ins/step  lin %.3f its %.3f relin %.3f fail %ld [x %d p %d]\n", name.c_str(), n,
	            (c1 - c0) / steps, (i1 - i0) / steps, double(s.linearSteps) / steps, double(s.iterations) / steps,
	            double(s.linearizations) / steps, s.failures, c.stateCount(), c.portCount());
	if (argc > 2) {
		FILE *o = std::fopen(argv[2], "wb");
		if (o) {
			std::fwrite(out.data(), 8, out.size(), o);
			std::fclose(o);
		}
	}
	return 0;
}
