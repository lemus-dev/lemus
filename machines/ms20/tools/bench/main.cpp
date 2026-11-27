// ms20bench: the workloads every round measures, their cost and golden renders.
//
//   ms20bench time [N]          cost per workload: share of one core at its rate (wall time, best
//                                 of N), the CPU time of every thread against the same (best of N;
//                                 the control side's thread too, Machine::setThreaded), the worst
//                                 64-sample block against its own duration (each block's fastest of
//                                 the N, then the slowest block), time asleep, and the 64-sample
//                                 blocks on two threads (MS20_THREADS=1: one)
//   ms20bench golden DIR [CASE] render every workload (or CASE) into DIR (mono float 32 WAV)
//   ms20bench check DIR [CASE]  render again and compare: the largest difference per workload in
//                                 dB re full scale ("exact" when bit-identical)
//   ms20bench list
//   ms20bench run NAME          one workload only, for /usr/bin/time -l's instruction count
//   ms20bench probe NAME A,B    one workload, printing the board's signals A, B... at each block's end
//   ms20bench threads           every workload and patched cases (one that keeps the control side on
//                                 the caller's thread: a cable from SIGNAL OUT into it), POWER's boot
//                                 and run-down, sleep, odd block sizes, on two threads and on one
//                                 (Machine::setThreaded): the output, capacitors and solver counts
//                                 must agree bit for bit
//   ms20bench boot [RATE] [key=K] [case=NAME] [volume=V]  POWER from cold (Machine::switchOn) at the
//                                 default controls (or workload NAME's; VOLUME at V), key K held
//                                 throughout if given, beside a warm start:
//                                 each block's cost while it boots, when SIGNAL OUT, the board's steady
//                                 signals and capacitors come within 1 mV (10 mV) of the warm start's,
//                                 the transient's peak, the solvers' failures; then POWER off: the
//                                 run-down's length, peak and blocks (MS20_BOOT_RUNS=N: the blocks'
//                                 cost the fastest of N runs, default 3)
//
// Each workload is the machine as the plugin runs it (core/machine.h): powered on, its controls,
// keys pressed and let go on a script, in 64-sample blocks.
#include "ms20/core/machine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <string>
#include <vector>

#include <sys/resource.h>

namespace {

using namespace ms20;
constexpr int kBlock = 64;

struct Key {
	int key;
	double on, off;
};
struct Case {
	const char *name;
	double rate, seconds;
	std::function<void(Controls &)> set;
	std::vector<Key> keys;
	bool rev2 = false; // the Rev 2 filters (Machine::setFilterRev)
};

std::vector<Key> melody(double from, double step, int n, const std::vector<int> &notes, double len = 0.8)
{
	std::vector<Key> k;
	for (int i = 0; i < n; ++i)
		k.push_back({notes[size_t(i) % notes.size()], from + i * step, from + i * step + step * len});
	return k;
}

std::vector<Case> cases()
{
	const std::vector<int> riff = {12, 12, 24, 19, 15, 12, 22, 17};
	std::vector<Case> all = {
		{"idle-48k", 48000, 4, [](Controls &) {}, {}},
		{"note-48k", 48000, 3, [](Controls &) {}, {{12, 0.1, 2.0}}},
		{"bass-48k", 48000, 4,
		 [](Controls &c) {
			 c.vco1Scale = 1;
			 c.lpfCutoff = 4;
			 c.lpfPeak = 7;
			 c.lpfEg2 = 7;
			 c.eg2Decay = 3;
			 c.eg2Sustain = 2;
		 },
		 melody(0.05, 0.25, 15, riff)},
		{"scream-48k", 48000, 4,
		 [](Controls &c) {
			 c.vco2Level = 8;
			 c.vco2Wave = Vco2Wave::Square;
			 c.hpfCutoff = 4;
			 c.hpfPeak = 8;
			 c.lpfCutoff = 6;
			 c.lpfPeak = 9;
			 c.lpfMg = 4;
			 c.mgFreq = 3;
		 },
		 melody(0.1, 1.0, 3, {7, 12, 0})},
		{"everything-44k", 44100, 5,
		 [](Controls &c) {
			 c.vco1Wave = Vco1Wave::Pulse;
			 c.vco1Pw = 4;
			 c.vco2Wave = Vco2Wave::Ring;
			 c.vco2Level = 7;
			 c.vco2Pitch = 3;
			 c.portamento = 4;
			 c.fmMg = 3;
			 c.fmEg1 = 2;
			 c.eg1Attack = 4;
			 c.hpfCutoff = 3;
			 c.hpfPeak = 6;
			 c.hpfMg = 5;
			 c.lpfPeak = 8;
			 c.lpfEg2 = 5;
			 c.mgFreq = 7;
		 },
		 melody(0.1, 0.5, 9, {0, 7, 12, 24, 19, 31, 36, 12, 5})},
		{"selfosc-48k", 48000, 3,
		 [](Controls &c) {
			 c.vco1Level = 0;
			 c.lpfCutoff = 5;
			 c.lpfPeak = 10;
		 },
		 {{12, 0.1, 2.5}}},
	};
	// the same with the Rev 2 filters (KLM-307)
	const std::pair<const char *, const char *> rev2[] = {
		{"scream-48k", "scream-rev2-48k"}, {"everything-44k", "everything-rev2-44k"}, {"selfosc-48k", "selfosc-rev2-48k"}};
	const size_t n = all.size();
	for (const auto &[from, to] : rev2)
		for (size_t k = 0; k < n; ++k)
			if (std::string(all[k].name) == from) {
				Case c = all[k];
				c.name = to;
				c.rev2 = true;
				all.push_back(c);
			}
	return all;
}

struct Result {
	std::vector<float> out;
	std::vector<double> blocks; // each 64-sample block's time against its duration
	double seconds = 0, took = 0, worstBlock = 0, worstAt = 0, asleep = 0;
	double cpu = 0;  // every thread's CPU time (getrusage)
	double split = 0; // share of the blocks rendered on two threads
	Machine::ThreadStats threads;
};

double cpuSeconds()
{
	rusage u{};
	getrusage(RUSAGE_SELF, &u);
	return double(u.ru_utime.tv_sec + u.ru_stime.tv_sec) + 1e-6 * double(u.ru_utime.tv_usec + u.ru_stime.tv_usec);
}

// probe mode: these signals at each block's end, one line a block (signal names: Board)
std::vector<std::string> g_probe;

Result run(const Case &c)
{
	Machine m;
	if (c.rev2)
		m.setFilterRev(Machine::FilterRev::Rev2);
	Controls ctl;
	c.set(ctl);
	m.setControls(ctl);
	m.prepare(c.rate);
	m.powerOn();
	Result r;
	const int total = int(c.seconds * c.rate);
	r.out.assign(size_t(total), 0.0f);
	r.seconds = c.seconds;
	int asleep = 0;
	const double c0 = cpuSeconds();
	const auto t0 = std::chrono::steady_clock::now();
	for (int pos = 0; pos < total; pos += kBlock) {
		const int n = std::min(kBlock, total - pos);
		const double t = pos / c.rate, t1 = (pos + n) / c.rate;
		for (const auto &k : c.keys) {
			if (k.on >= t && k.on < t1)
				m.setKey(k.key, true);
			if (k.off >= t && k.off < t1)
				m.setKey(k.key, false);
		}
		static const bool profile = std::getenv("MS20_PROFILE") != nullptr;
		const auto before = profile ? m.board().blockSeconds() : std::vector<double>();
		const auto b0 = std::chrono::steady_clock::now();
		m.render(r.out.data() + pos, n, nullptr);
		const double took = std::chrono::duration<double>(std::chrono::steady_clock::now() - b0).count();
		if (profile && took / (n / c.rate) > 1.0) { // a block over its duration: where the time went
			const auto after = m.board().blockSeconds();
			const auto names = m.board().blockNames();
			std::fprintf(stderr, "block at %.4f s: %.0f %%:", t, 100 * took / (n / c.rate));
			for (size_t b = 0; b < names.size(); ++b)
				if (after[b] - before[b] > 50e-6)
					std::fprintf(stderr, " %s %.0f us", names[b].c_str(), 1e6 * (after[b] - before[b]));
			std::fprintf(stderr, "\n");
		}
		r.blocks.push_back(took / (n / c.rate));
		if (took / (n / c.rate) > r.worstBlock) {
			r.worstBlock = took / (n / c.rate);
			r.worstAt = t;
		}
		if (m.asleep())
			asleep += n;
		if (!g_probe.empty()) {
			std::printf("%.6f", t1);
			for (const auto &sig : g_probe)
				std::printf(" %.12g", m.board().signal(sig));
			std::printf("\n");
		}
	}
	r.took = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	r.cpu = cpuSeconds() - c0;
	r.asleep = double(asleep) / total;
	r.threads = m.threadStats();
	r.split = double(r.threads.split) / double((total + kBlock - 1) / kBlock);
	if (std::getenv("MS20_THREADSTATS") != nullptr)
		std::fprintf(stderr,
		             "%s: stretches on two threads %ld (the thread too late for %ld; %.1f samples a stretch before it came), on one %ld; "
		             "the caller waited %.3f s for the thread (%.1f %% of the run)\n",
		             c.name, r.threads.split, r.threads.takenBack, r.threads.split ? double(r.threads.early) / double(r.threads.split) : 0.0,
		             r.threads.single, r.threads.waited, 100 * r.threads.waited / r.took);
	if (std::getenv("MS20_PROFILE") != nullptr) // per block: time, worst sample, the solver's counts
		m.board().report();
	return r;
}

// POWER from cold beside a warm start (ms20bench boot).
int boot(double rate, int key, const Case *c, double volume)
{
	Controls ctl;
	if (c != nullptr)
		c->set(ctl);
	if (volume >= 0)
		ctl.volume = volume;
	auto make = [&](Machine &m) {
		m.setControls(ctl);
		m.prepare(rate);
		if (key >= 0)
			m.setKey(key, true);
	};
	// both kept awake (a whisper at the plugin's input, which no cable takes): the boot measured
	// whatever SIGNAL OUT does
	std::vector<float> in(kBlock, 2e-6f), wo(kBlock), co(kBlock);
	const std::vector<std::string> names = {"sigout", "vout", "kbdcv", "kbdtrig", "trigout", "eg1", "eg2", "eg2vcf", "eg1vco", "tmod",
	                                        "hcv", "lcv", "mix", "hpf", "lpf", "mg", "mgsq", "c6", "ic13a", "q16c", "cp19", "o3", "o7", "o4"};
	const double seconds = 12, downSeconds = 5;
	const int blocks = int(seconds * rate / kBlock), downBlocks = int(downSeconds * rate / kBlock);
	std::vector<std::vector<double>> ws(names.size()), cs(names.size()), wc, cc; // per signal, per block
	// each block's cost against its duration: the fastest of the runs (the runs are identical; a
	// block another process preempted in one doesn't count)
	std::vector<double> cost(static_cast<size_t>(blocks), 1e30), downCost(static_cast<size_t>(downBlocks), 1e30);
	double peak = 0, warmPeak = 0, outSettled = 0, downPeak = 0, lastLoud = 0, offAfter = -1;
	SolverStats bootStats, downStats;
	const int runs = std::getenv("MS20_BOOT_RUNS") ? std::atoi(std::getenv("MS20_BOOT_RUNS")) : 3;
	for (int run = 0; run < runs; ++run) {
		Machine warm, cold;
		make(warm);
		make(cold);
		cold.powerOff();
		cold.switchOn();
		for (int b = 0; b < blocks; ++b) {
			if (run == 0)
				warm.render(wo.data(), kBlock, in.data());
			const auto t0 = std::chrono::steady_clock::now();
			cold.render(co.data(), kBlock, in.data());
			cost[size_t(b)] = std::min(cost[size_t(b)], std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / (kBlock / rate));
			if (run > 0)
				continue;
			for (int i = 0; i < kBlock; ++i) {
				peak = std::max(peak, double(std::fabs(co[size_t(i)])));
				warmPeak = std::max(warmPeak, double(std::fabs(wo[size_t(i)])));
				if (std::fabs(co[size_t(i)] - wo[size_t(i)]) >= 1e-3)
					outSettled = (b * kBlock + i + 1) / rate;
			}
			for (size_t k = 0; k < names.size(); ++k) {
				ws[k].push_back(warm.board().signal(names[k]));
				cs[k].push_back(cold.board().signal(names[k]));
			}
			wc.push_back(warm.board().capVoltages());
			cc.push_back(cold.board().capVoltages());
			// MS20_BOOT_TRACE=1: the cold start's block ends on stderr (the output's peak in the
			// block, the rails, the signals)
			static const bool trace = std::getenv("MS20_BOOT_TRACE") != nullptr;
			if (trace && b < int(2 * rate / kBlock)) {
				float pk = 0;
				for (float v : co)
					pk = std::max(pk, std::fabs(v));
				std::fprintf(stderr, "%.4f out %.4f rails %.3f", (b + 1) * kBlock / rate, double(pk), cold.rails());
				for (size_t k = 0; k < names.size(); ++k)
					std::fprintf(stderr, " %s %.4g", names[k].c_str(), cs[k].back());
				std::fprintf(stderr, "\n");
			}
		}
		bootStats = cold.solverStats();
		// switched off
		cold.switchOff();
		for (int b = 0; b < downBlocks; ++b) {
			const bool on = cold.powered();
			const auto t0 = std::chrono::steady_clock::now();
			cold.render(co.data(), kBlock, in.data());
			downCost[size_t(b)] = std::min(downCost[size_t(b)], on ? std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() / (kBlock / rate) : 0.0);
			if (run > 0)
				continue;
			for (int i = 0; i < kBlock; ++i) {
				downPeak = std::max(downPeak, double(std::fabs(co[size_t(i)])));
				if (std::fabs(co[size_t(i)]) >= 1e-3)
					lastLoud = (b * kBlock + i + 1) / rate;
			}
			if (on && !cold.powered())
				offAfter = (b + 1) * kBlock / rate;
		}
		if (run == 0) {
			downStats = cold.solverStats();
			downStats.failures -= bootStats.failures;
			downStats.substepped -= bootStats.substepped;
		}
	}
	// a signal (a capacitor) counts if the warm start holds it within 1 mV (10 mV) over the last
	// 10 s (the MG, the VCOs' audio through the filters don't: they run)
	auto settledAfter = [&](const std::vector<double> &w, const std::vector<double> &cv, double tol, bool &steady) {
		const size_t from = size_t(2 * rate / kBlock);
		double lo = 1e30, hi = -1e30;
		for (size_t j = from; j < w.size(); ++j) {
			lo = std::min(lo, w[j]);
			hi = std::max(hi, w[j]);
		}
		steady = hi - lo < tol;
		double after = 0;
		for (size_t j = 0; j < w.size(); ++j)
			if (std::abs(cv[j] - w[j]) >= tol)
				after = (j + 1) * kBlock / rate;
		return after;
	};
	std::printf("boot at %.0f Hz%s%s, VOLUME %.1f: SIGNAL OUT within 1 mV of a warm start's after %.3f s; peak %.4f (warm %.4f)\n", rate,
	            key >= 0 ? (", key " + std::to_string(key) + " held").c_str() : "", c != nullptr ? (std::string(", ") + c->name).c_str() : "",
	            ctl.volume, outSettled, peak, warmPeak);
	double sigWorst = 0, capWorst = 0;
	std::string sigLast, skipped;
	for (size_t k = 0; k < names.size(); ++k) {
		bool steady = false;
		const double t = settledAfter(ws[k], cs[k], 1e-3, steady);
		if (!steady) {
			skipped += " " + names[k];
			continue;
		}
		std::printf("  %-8s within 1 mV after %6.3f s (warm %.6g V)\n", names[k].c_str(), t, ws[k].back());
		if (t > sigWorst) {
			sigWorst = t;
			sigLast = names[k];
		}
	}
	int caps = 0, capSkipped = 0, capIdx = -1;
	for (size_t j = 0; j < wc.front().size(); ++j) {
		std::vector<double> w, cv;
		for (int b = 0; b < blocks; ++b) {
			w.push_back(wc[size_t(b)][j]);
			cv.push_back(cc[size_t(b)][j]);
		}
		bool steady = false;
		const double t = settledAfter(w, cv, 1e-2, steady);
		if (!steady) {
			++capSkipped;
			continue;
		}
		++caps;
		if (t > capWorst) {
			capWorst = t;
			capIdx = int(j);
		}
	}
	std::printf("  steady signals within 1 mV after %.3f s (last: %s); not steady in the warm start:%s\n", sigWorst, sigLast.c_str(), skipped.c_str());
	std::printf("  %d capacitors within 10 mV after %.3f s (the last: #%d); %d not steady in the warm start\n", caps, capWorst, capIdx, capSkipped);
	auto worstOf = [&](const std::vector<double> &cv, int from, int to, double &at, int &over) {
		double w = 0;
		over = 0;
		for (int b = from; b < to && b < int(cv.size()); ++b) {
			over += cv[size_t(b)] > 1;
			if (cv[size_t(b)] > w) {
				w = cv[size_t(b)];
				at = b * kBlock / rate;
			}
		}
		return w;
	};
	double at1 = 0, at2 = 0, at3 = 0, at4 = 0;
	int over1 = 0, over2 = 0, over3 = 0, over4 = 0;
	const int b20 = int(0.02 * rate / kBlock), b1 = int(1.0 * rate / kBlock);
	const double w1 = worstOf(cost, 0, b20, at1, over1), w2 = worstOf(cost, b20, b1, at2, over2), w3 = worstOf(cost, b1, blocks, at3, over3);
	std::printf("  64-sample blocks (fastest of %d runs): worst %.0f %% of real time at %.4f s while the rails rise (the first 20 ms, %d of %d "
	            "over), %.0f %% at %.3f s to 1 s (%d of %d over), %.0f %% at %.3f s after (%d over)\n",
	            runs, 100 * w1, at1, over1, b20, 100 * w2, at2, over2, b1 - b20, 100 * w3, at3, over3);
	std::printf("  solves %ld, substepped %ld, failures %ld, dc failures %ld\n", bootStats.solves, bootStats.substepped, bootStats.failures,
	            bootStats.dcFailures);
	const double w4 = worstOf(downCost, 0, downBlocks, at4, over4);
	std::printf("run-down: off after %.3f s%s; peak %.4f, over 1 mV until %.3f s; worst block %.0f %% at %.3f s (%d over); substepped %ld, failures %ld\n",
	            offAfter, offAfter < 0 ? " (not yet)" : "", downPeak, lastLoud, 100 * w4, at4, over4, downStats.substepped, downStats.failures);
	return 0;
}

// ms20bench threads: a scenario rendered on two threads and on one.
struct Scenario {
	std::string name;
	double rate, seconds;
	std::function<void(Machine &)> setup;                       // controls, cables (before prepare)
	std::function<void(Machine &, double, double)> events;      // keys, POWER, cables in [t, t1)
	bool input = false;                                         // a tone at the plugin's input
	bool splits = true;                                         // expected to run on two threads at all
};

struct Rendered {
	std::vector<float> out;
	std::vector<double> caps;
	SolverStats solver;
	Machine::ThreadStats threads;
};

Rendered renderScenario(const Scenario &s, bool threaded)
{
	Machine m;
	m.setThreaded(threaded);
	s.setup(m);
	m.prepare(s.rate);
	Rendered r;
	const int total = int(s.seconds * s.rate);
	r.out.assign(size_t(total), 0.0f);
	std::vector<float> in(static_cast<size_t>(total));
	for (int i = 0; i < total; ++i)
		in[size_t(i)] = float(0.3 * std::sin(2 * M_PI * 220.0 * i / s.rate) + 0.1 * std::sin(2 * M_PI * 1830.0 * i / s.rate));
	// block sizes as hosts and MIDI events cut them: under the shortest stretch worth two threads,
	// at and around the longest, past it
	static const int sizes[] = {64, 1, 7, 15, 16, 17, 64, 100, 255, 256, 257, 700, 1024, 3, 64, 128, 31};
	unsigned lcg = 12345;
	for (int pos = 0; pos < total;) {
		lcg = lcg * 1103515245u + 12345u;
		const int n = std::min(sizes[(lcg >> 16) % (sizeof(sizes) / sizeof(sizes[0]))], total - pos);
		if (s.events)
			s.events(m, pos / s.rate, (pos + n) / s.rate);
		m.render(r.out.data() + pos, n, s.input ? in.data() + pos : nullptr);
		pos += n;
	}
	r.caps = m.board().capVoltages();
	r.solver = m.solverStats();
	r.threads = m.threadStats();
	return r;
}

// Asleep, the VCOs and MG run on (Machine::wakeUp): a note, the machine asleep through a gap, a note
// again, against the same kept awake (Machine::keepAwake). In the second note the MG's triangle
// should cross zero when the awake one's does: the offset in samples and as a share of its cycle.
int sleepCheck()
{
	const double rate = 48000;
	const int key = 18;
	std::printf("%-22s %8s %10s %12s %10s %11s\n", "MG FREQ / WAVE, gap", "slept s", "cycle", "offset", "of cycle", "a cycle");
	int bad = 0;
	// upward zero crossings of a signal, between samples
	auto ups = [](const std::vector<double> &x, size_t from) {
		std::vector<double> t;
		for (size_t i = std::max<size_t>(from, 1); i < x.size(); ++i)
			if (x[i - 1] < 0 && x[i] >= 0)
				t.push_back(double(i - 1) + x[i - 1] / (x[i - 1] - x[i]));
		return t;
	};
	for (const double freq : {1.5, 5.0, 8.5, 10.0})
		for (const double wave : {0.5, 5.0, 9.5})
			for (const double gap : {2.0, 7.3}) {
				Controls ctl;
				ctl.mgFreq = freq;
				ctl.mgWave = wave;
				ctl.fmMg = 3;
				Machine a, b;
				for (Machine *m : {&a, &b}) {
					m->setDrift(false); // (a random path: what the sleep does, not the drift)
					m->setControls(ctl);
					m->prepare(rate);
				}
				b.keepAwake(true);
				const long n1 = long(0.5 * rate), n2 = n1 + long(gap * rate), n3 = n2 + long(1.5 * rate);
				std::vector<double> ma(static_cast<size_t>(n3)), mb(static_cast<size_t>(n3));
				float oa = 0, ob = 0;
				double slept = 0;
				for (long k = 0; k < n3; ++k) {
					if (k == 0 || k == n2)
						for (Machine *m : {&a, &b})
							m->setKey(key, true);
					if (k == n1)
						for (Machine *m : {&a, &b})
							m->setKey(key, false);
					a.render(&oa, 1, nullptr);
					b.render(&ob, 1, nullptr);
					if (a.asleep())
						slept += 1 / rate;
					ma[size_t(k)] = a.board().signal("mg");
					mb[size_t(k)] = b.board().signal("mg");
				}
				const auto tb = ups(mb, size_t(n1)), ta = ups(ma, size_t(n2) + 1), tb2 = ups(mb, size_t(n2) + 1);
				char label[64];
				std::snprintf(label, sizeof label, "%.1f / %.1f, %.1f s", freq, wave, gap);
				if (tb.size() < 2 || ta.empty() || tb2.empty()) {
					std::printf("%-22s %8.2f   (too few cycles)\n", label, slept);
					continue;
				}
				const double cycle = (tb.back() - tb.front()) / double(tb.size() - 1);
				double off = ta.front() - tb2.front();
				off -= cycle * std::round(off / cycle);
				// the offset spread over the cycles slept: the free-running cycle's error (a real MG's
				// rate moves more than 0.01 % with a degree's warming)
				const double cycles = slept * rate / cycle, perCycle = 100 * off / cycle / std::max(1.0, cycles);
				std::printf("%-22s %8.2f %8.1f sm %9.2f sm %9.3f %% %10.4f %%\n", label, slept, cycle, off, 100 * off / cycle, perCycle);
				if (slept < 0.5 || std::abs(perCycle) > 0.01)
					++bad;
			}
	// the VCOs, FM off (a steady pitch while asleep): VCO 1's switch common crossing upward
	for (const double gap : {2.0, 7.3}) {
		Controls ctl;
		Machine a, b;
		for (Machine *m : {&a, &b}) {
			m->setDrift(false);
			m->setControls(ctl);
			m->prepare(rate);
		}
		b.keepAwake(true);
		const long n1 = long(0.5 * rate), n2 = n1 + long(gap * rate), n3 = n2 + long(0.2 * rate);
		std::vector<double> va(static_cast<size_t>(n3)), vb(static_cast<size_t>(n3));
		float oa = 0, ob = 0;
		double slept = 0;
		for (long k = 0; k < n3; ++k) {
			if (k == 0 || k == n2)
				for (Machine *m : {&a, &b})
					m->setKey(key, true);
			if (k == n1)
				for (Machine *m : {&a, &b})
					m->setKey(key, false);
			a.render(&oa, 1, nullptr);
			b.render(&ob, 1, nullptr);
			if (a.asleep())
				slept += 1 / rate;
			va[size_t(k)] = a.board().signal("vco1");
			vb[size_t(k)] = b.board().signal("vco1");
		}
		// the common's mean taken out (the saw sits off zero)
		auto centred = [&](std::vector<double> x) {
			double m = 0;
			for (long k = n2; k < n3; ++k)
				m += x[size_t(k)];
			m /= double(n3 - n2);
			for (auto &y : x)
				y -= m;
			return x;
		};
		const auto ta = ups(centred(va), size_t(n2) + 480), tb = ups(centred(vb), size_t(n2) + 480);
		char label[64];
		std::snprintf(label, sizeof label, "VCO 1, %.1f s", gap);
		if (ta.size() < 2 || tb.empty()) {
			std::printf("%-22s %8.2f   (too few cycles)\n", label, slept);
			++bad;
			continue;
		}
		const double cycle = (ta.back() - ta.front()) / double(ta.size() - 1);
		double off = ta.front() - tb.front();
		off -= cycle * std::round(off / cycle);
		const double cycles = slept * rate / cycle, perCycle = 100 * off / cycle / std::max(1.0, cycles);
		std::printf("%-22s %8.2f %8.1f sm %9.2f sm %9.3f %% %10.4f %%\n", label, slept, cycle, off, 100 * off / cycle, perCycle);
		if (slept < 0.5 || std::abs(perCycle) > 0.01)
			++bad;
	}
	std::printf(bad == 0 ? "all within 0.01 %% a cycle slept\n" : "%d over 0.01 %% a cycle slept, or never asleep\n", bad);
	return bad == 0 ? 0 : 1;
}

int threadsCheck()
{
	std::vector<Scenario> all;
	auto keysOf = [](std::vector<Key> keys) {
		return [keys](Machine &m, double t, double t1) {
			for (const auto &k : keys) {
				if (k.on >= t && k.on < t1)
					m.setKey(k.key, true);
				if (k.off >= t && k.off < t1)
					m.setKey(k.key, false);
			}
		};
	};
	for (const auto &c : cases())
		all.push_back({c.name, c.rate, c.seconds,
		               [set = c.set](Machine &m) {
			               Controls ctl;
			               set(ctl);
			               m.setControls(ctl);
		               },
		               keysOf(c.keys), false, true});
	const auto riff = melody(0.05, 0.3, 9, {0, 12, 7, 24, 5, 19, 31, 12, 3});
	auto patched = [&](const char *name, std::function<void(Controls &)> set, std::vector<std::pair<Jack, int>> cables, bool input, bool splits) {
		all.push_back({name, 48000, 3,
		               [set, cables](Machine &m) {
			               Controls ctl;
			               if (set)
				               set(ctl);
			               m.setControls(ctl);
			               Patch p;
			               for (const auto &[in, out] : cables)
				               p.connect(in, out);
			               m.setPatch(p);
		               },
		               keysOf(riff), input, splits});
	};
	auto everything = [](Controls &c) {
		c.vco2Wave = Vco2Wave::Ring;
		c.vco2Level = 7;
		c.lpfPeak = 8;
		c.hpfPeak = 6;
		c.mgFreq = 7;
		c.eg1Attack = 3;
	};
	// control signals into the audio path: two threads
	patched("patch MG, EG 1 into the filters", everything,
	        {{Jack::HpfExtCutoff, int(Jack::MgTriangle)}, {Jack::LpfExtCutoff, int(Jack::Eg1RevOut)}, {Jack::InitialGain, int(Jack::Wheel)}}, false, true);
	patched("patch input into EXT SIGNAL IN", nullptr, {{Jack::ExtSigIn, kHostInput}}, true, true);
	patched("patch input through the ESP", nullptr,
	        {{Jack::EspSignalIn, kHostInput}, {Jack::LpfExtCutoff, int(Jack::EspEnvOut)}, {Jack::VcoCvIn, int(Jack::EspFvOut)},
	         {Jack::TrigIn, int(Jack::EspTrigOut)}},
	        true, true);
	patched("patch MG through the MVCA", [](Controls &c) { c.mgFreq = 6; },
	        {{Jack::MvcaIn, int(Jack::MgTriangle)}, {Jack::TotalExtMod, int(Jack::MvcaOut)}}, false, true);
	patched("patch noise, S&H", [](Controls &c) { c.vco1Wave = Vco1Wave::Noise; },
	        {{Jack::ShIn, int(Jack::PinkNoise)}, {Jack::ShClock, int(Jack::MgRectangle)}, {Jack::VcoCvIn, int(Jack::ShOut)}}, false, true);
	// SIGNAL OUT into the control side: one thread
	patched("patch SIGNAL OUT into the ESP", nullptr, {{Jack::EspSignalIn, int(Jack::SignalOut)}, {Jack::HpfExtCutoff, int(Jack::EspEnvOut)}},
	        false, false);
	patched("patch SIGNAL OUT into the S&H", nullptr,
	        {{Jack::ShIn, int(Jack::SignalOut)}, {Jack::ShClock, int(Jack::MgRectangle)}, {Jack::VcoCvIn, int(Jack::ShOut)}}, false, false);
	// a cable in and out while it plays (the board rewired between blocks), the wheel and MOMENTARY
	all.push_back({"cables and the wheel while playing", 48000, 3, [](Machine &m) { m.setControls(Controls()); },
	               [k = keysOf(riff)](Machine &m, double t, double t1) {
		               k(m, t, t1);
		               auto at = [&](double x) { return x >= t && x < t1; };
		               Patch p = m.patch();
		               if (at(0.8))
			               p.connect(Jack::EspSignalIn, int(Jack::SignalOut));
		               if (at(1.6))
			               p.disconnect(Jack::EspSignalIn);
		               if (at(2.0))
			               p.connect(Jack::LpfExtCutoff, int(Jack::Wheel));
		               m.setPatch(p);
		               m.setManual({std::sin(7 * t), t > 2.4 && t < 2.6});
	               },
	               false, true});
	// POWER from cold with a key held, off while it plays, on again while it runs down, off for good
	all.push_back({"POWER: boot, run-down, boot", 48000, 5, [](Machine &m) { m.setControls(Controls()); },
	               [](Machine &m, double t, double t1) {
		               auto at = [&](double x) { return x >= t && x < t1; };
		               if (t == 0) {
			               m.setKey(12, true);
			               m.powerOff();
			               m.switchOn();
		               }
		               if (at(2.0))
			               m.switchOff();
		               if (at(2.1))
			               m.switchOn();
		               if (at(4.0))
			               m.switchOff();
	               },
	               false, true});
	// asleep after a note, woken by a key and by the input
	all.push_back({"sleep and wake", 48000, 4, [](Machine &m) { m.setControls(Controls()); },
	               keysOf({{12, 0.1, 0.3}, {19, 2.0, 2.2}}), false, true});
	int bad = 0;
	for (const auto &sc : all) {
		const auto a = renderScenario(sc, true), b = renderScenario(sc, false);
		size_t diff = 0;
		for (size_t i = 0; i < a.out.size(); ++i)
			diff += a.out[i] != b.out[i] || std::signbit(a.out[i]) != std::signbit(b.out[i]);
		const bool same = diff == 0 && a.caps == b.caps && a.solver.solves == b.solver.solves && a.solver.substepped == b.solver.substepped &&
		                  a.solver.held == b.solver.held && a.solver.failures == b.solver.failures;
		const bool splitOk = sc.splits ? a.threads.split > 0 : a.threads.split == 0;
		bad += !same || !splitOk;
		std::printf("%-40s %s  stretches: %ld on two threads (the thread too late for %ld; %.1f samples a stretch before it "
		            "came), %ld on one%s\n",
		            sc.name.c_str(), same ? "exact" : ("DIFFERS (" + std::to_string(diff) + " samples)").c_str(), a.threads.split,
		            a.threads.takenBack, a.threads.split ? double(a.threads.early) / double(a.threads.split) : 0.0, a.threads.single,
		            splitOk ? "" : (sc.splits ? "  NONE ON TWO" : "  EXPECTED ONE"));
	}
	std::printf("%s\n", bad ? "FAIL" : "all exact");
	return bad;
}

void writeWav(const std::string &path, const std::vector<float> &x, int rate)
{
	std::ofstream f(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { f.write(reinterpret_cast<const char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { f.write(reinterpret_cast<const char *>(&v), 2); };
	const uint32_t bytes = uint32_t(x.size() * 4);
	f.write("RIFF", 4);
	u32(36 + bytes);
	f.write("WAVEfmt ", 8);
	u32(16);
	u16(3);
	u16(1);
	u32(uint32_t(rate));
	u32(uint32_t(rate * 4));
	u16(4);
	u16(32);
	f.write("data", 4);
	u32(bytes);
	f.write(reinterpret_cast<const char *>(x.data()), std::streamsize(bytes));
}

std::vector<float> readWav(const std::string &path)
{
	std::ifstream f(path, std::ios::binary);
	std::vector<char> d((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
	if (d.size() < 44)
		return {};
	std::vector<float> x((d.size() - 44) / 4);
	std::memcpy(x.data(), d.data() + 44, x.size() * 4);
	return x;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::fprintf(stderr, "ms20bench time [N] | golden DIR [CASE] | check DIR [CASE] | list | run NAME | probe NAME SIGNALS | boot | threads | sleep\n");
		return 1;
	}
	const std::string mode = argv[1];
	const auto all = cases();
	if (mode == "list") {
		for (const auto &c : all)
			std::printf("%s\n", c.name);
		return 0;
	}
	if (mode == "probe" && argc > 3) {
		for (std::string rest = argv[3]; !rest.empty();) {
			const auto comma = rest.find(',');
			g_probe.push_back(rest.substr(0, comma));
			rest = comma == std::string::npos ? "" : rest.substr(comma + 1);
		}
		for (const auto &c : all)
			if (c.name == std::string(argv[2]))
				run(c);
		return 0;
	}
	if (mode == "boot") {
		double rate = 48000, volume = -1;
		int key = -1;
		const Case *c = nullptr;
		for (int a = 2; a < argc; ++a) {
			const std::string arg = argv[a];
			const auto eq = arg.find('=');
			const std::string k = arg.substr(0, eq), v = eq == std::string::npos ? "" : arg.substr(eq + 1);
			if (k == "key")
				key = std::atoi(v.c_str());
			else if (k == "volume")
				volume = std::atof(v.c_str());
			else if (k == "case") {
				for (const auto &w : all)
					if (w.name == v)
						c = &w;
			} else
				rate = std::atof(arg.c_str());
		}
		return boot(rate, key, c, volume);
	}
	if (mode == "threads")
		return threadsCheck();
	if (mode == "sleep")
		return sleepCheck();
	if (mode == "run" && argc > 2) {
		for (const auto &c : all)
			if (c.name == std::string(argv[2]))
				run(c);
		return 0;
	}
	if (mode == "time") {
		const int n = argc > 2 ? std::atoi(argv[2]) : 3;
		std::printf("%-18s %8s %8s %10s %8s %8s %8s\n", "workload", "core %", "cpu %", "worst blk", "at s", "asleep", "2 thr");
		for (const auto &c : all) {
			// the worst block is each block's fastest over the N runs, then the slowest of those:
			// a block another process preempted in one run doesn't count (the runs are identical)
			Result best;
			std::vector<double> fastest;
			double cpu = 1e30;
			for (int i = 0; i < n; ++i) {
				auto r = run(c);
				cpu = std::min(cpu, r.cpu);
				if (i == 0)
					fastest = r.blocks;
				for (size_t b = 0; b < fastest.size() && b < r.blocks.size(); ++b)
					fastest[b] = std::min(fastest[b], r.blocks[b]);
				if (i == 0 || r.took < best.took)
					best = std::move(r);
			}
			best.worstBlock = 0;
			for (size_t b = 0; b < fastest.size(); ++b)
				if (fastest[b] > best.worstBlock) {
					best.worstBlock = fastest[b];
					best.worstAt = double(b * kBlock) / c.rate;
				}
			std::printf("%-18s %7.2f%% %7.2f%% %9.1f%% %8.3f %7.0f%% %7.0f%%\n", c.name, 100 * best.took / best.seconds, 100 * cpu / best.seconds,
			            100 * best.worstBlock, best.worstAt, 100 * best.asleep, 100 * best.split);
		}
		return 0;
	}
	if ((mode == "golden" || mode == "check") && argc > 2) {
		const std::string dir = argv[2];
		const std::string only = argc > 3 ? argv[3] : "";
		int bad = 0;
		for (const auto &c : all) {
			if (!only.empty() && only != c.name)
				continue;
			const auto r = run(c);
			const std::string path = dir + "/" + c.name + ".wav";
			if (mode == "golden") {
				writeWav(path, r.out, int(c.rate));
				std::printf("%s\n", path.c_str());
				continue;
			}
			const auto ref = readWav(path);
			if (ref.size() != r.out.size()) {
				std::printf("%-18s missing or different length\n", c.name);
				++bad;
				continue;
			}
			double worst = 0;
			for (size_t i = 0; i < ref.size(); ++i)
				worst = std::max(worst, double(std::fabs(ref[i] - r.out[i])));
			if (worst == 0)
				std::printf("%-18s exact\n", c.name);
			else
				std::printf("%-18s %7.1f dB\n", c.name, 20 * std::log10(worst));
		}
		return bad;
	}
	std::fprintf(stderr, "unknown mode\n");
	return 1;
}
