// The check set's bench (plugin.cpp): viruscbench time | golden DIR | check DIR.
// And the board's timing, core only: every audio block timed while stepping through the factory programs.
//   viruscbench <rom> spikes [block frames, default 125 = 128 samples at 48 kHz] [programs per bank] [warm up: 0 or 1]
// Two passes over the same programs: on the first the recompiler meets code it hasn't compiled, on
// the second it has. Runs on a thread with a painted stack, to report the deepest stack used.
#include "virusc/core/machine.h"

#if defined(__APPLE__)
#include <pthread.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

constexpr size_t kStack = 32 * 1024 * 1024;
constexpr uint8_t kPaint = 0xa5;

struct Job {
	std::vector<char> rom;
	int blockFrames = 125, perBank = 128;
	bool warmUp = false;
};

#if defined(__APPLE__)
uint8_t *stackLow()
{
	const pthread_t self = pthread_self();
	return static_cast<uint8_t *>(pthread_get_stackaddr_np(self)) - pthread_get_stacksize_np(self);
}

// Everything below the current frame, less a guard margin at the far end.
__attribute__((noinline)) void paint()
{
	uint8_t here;
	uint8_t *low = stackLow() + 64 * 1024;
	uint8_t *top = &here - 4096;
	std::memset(low, kPaint, size_t(top - low));
}

__attribute__((noinline)) size_t deepest()
{
	uint8_t here;
	uint8_t *p = stackLow() + 64 * 1024;
	uint8_t *top = &here - 4096;
	while (p < top && *p == kPaint)
		++p;
	return size_t(stackLow() + pthread_get_stacksize_np(pthread_self()) - p);
}
#else
void paint() {}
size_t deepest() { return 0; }
#endif

struct Spike {
	double ms;
	int pass, bank, program, phase;
};

void *run(void *arg)
{
	auto &job = *static_cast<Job *>(arg);
	static virusc::Machine m;
	if (!m.setRom(reinterpret_cast<const uint8_t *>(job.rom.data()), job.rom.size())) {
		std::fprintf(stderr, "bad ROM\n");
		return nullptr;
	}
	paint();
	m.powerOn();
	auto t0 = Clock::now();
	while (m.frames() < uint64_t(60 * virusc::kSampleRate) && !(m.mcu().scon() & 0x10))
		m.runFrame();
	for (int i = 0; i < int(virusc::kSampleRate / 2); ++i)
		m.runFrame();
	if (job.warmUp)
		m.warmUp();
	std::printf("boot: %.2f s of machine in %.2f s, deepest stack %zu KB\n", double(m.frames()) / virusc::kSampleRate,
	            std::chrono::duration<double>(Clock::now() - t0).count(), deepest() / 1024);

	const double deadline = job.blockFrames / virusc::kSampleRate * 1000.0;
	const int block = job.blockFrames;
	for (int pass = 0; pass < 2; ++pass) {
		paint();
		std::vector<Spike> spikes;
		std::vector<float> all;
		uint64_t blocks = 0;
		double total = 0;
		const auto runBlocks = [&](int n, int bank, int program, int phase) {
			for (int b = 0; b < n; ++b) {
				const auto a = Clock::now();
				for (int f = 0; f < block; ++f)
					m.runFrame();
				const double ms = std::chrono::duration<double, std::milli>(Clock::now() - a).count();
				total += ms;
				++blocks;
				all.push_back(float(ms));
				if (ms > deadline * 0.25)
					spikes.push_back({ms, pass, bank, program, phase});
			}
		};
		const int perSecond = int(virusc::kSampleRate / block);
		for (int bank = 0; bank < 8; ++bank)
			for (int program = 0; program < job.perBank; ++program) {
				for (int d : {0xb0, 0x20, bank, 0xc0, program})
					m.midiIn(uint8_t(d));
				runBlocks(perSecond * 3 / 10, bank, program, 0); // the patch uploads
				for (int n = 0; n < 4; ++n)
					for (int d : {0x90, 48 + n * 7, 100})
						m.midiIn(uint8_t(d));
				runBlocks(perSecond / 4, bank, program, 1);
				for (int n = 0; n < 4; ++n)
					for (int d : {0x80, 48 + n * 7, 0})
						m.midiIn(uint8_t(d));
				runBlocks(perSecond / 10, bank, program, 2);
			}
		std::sort(all.begin(), all.end());
		std::sort(spikes.begin(), spikes.end(), [](const Spike &a, const Spike &b) { return a.ms > b.ms; });
		const auto pct = [&](double p) { return all[std::min(all.size() - 1, size_t(p * double(all.size())))]; };
		int over = 0;
		for (const auto &s : spikes)
			over += s.ms > deadline;
		std::printf("pass %d: %llu blocks of %.2f ms, mean %.3f ms, p50 %.3f, p99 %.3f, p99.9 %.3f, max %.3f ms, over the deadline: %d, deepest stack %zu KB\n",
		            pass + 1, (unsigned long long)blocks, deadline, total / double(blocks), pct(0.5), pct(0.99), pct(0.999), all.back(), over, deepest() / 1024);
		for (size_t i = 0; i < spikes.size() && i < 8; ++i)
			std::printf("    %.3f ms  bank %c program %d, %s\n", spikes[i].ms, 'A' + spikes[i].bank, spikes[i].program,
			            spikes[i].phase == 0 ? "patch change" : spikes[i].phase == 1 ? "note on" : "note off");
	}
	return nullptr;
}

} // namespace

int pluginBench(int argc, char **argv);

int main(int argc, char **argv)
{
	if (argc > 1 && (std::strcmp(argv[1], "time") == 0 || std::strcmp(argv[1], "golden") == 0 || std::strcmp(argv[1], "check") == 0 || std::strcmp(argv[1], "render") == 0))
		return pluginBench(argc, argv);
	if (argc < 3 || std::strcmp(argv[2], "spikes") != 0) {
		std::fprintf(stderr, "viruscbench time [RUNS [WORKLOAD]] | golden DIR | check DIR | render SCRIPT OUT.wav | <rom> spikes [block frames] [programs per bank] [warm up]\n");
		return 2;
	}
	static Job job;
	std::ifstream f(argv[1], std::ios::binary);
	job.rom.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
	if (argc > 3)
		job.blockFrames = std::atoi(argv[3]);
	if (argc > 4)
		job.perBank = std::atoi(argv[4]);

	job.warmUp = argc > 5 && std::atoi(argv[5]) != 0;

#if defined(__APPLE__)
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, kStack);
	pthread_t t;
	pthread_create(&t, &attr, run, &job);
	pthread_join(t, nullptr);
#else
	run(&job);
#endif
	return 0;
}
