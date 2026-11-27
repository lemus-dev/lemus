// One thread beside the caller's, for Machine::render's control side (machine.cpp, "Two threads").
// It sleeps on a semaphore between jobs (no lock on either side), takes a posted job with the
// caller's floating-point mode (the flush-to-zero the host's audio thread runs with, so the two
// threads round alike), and runs it to the end. The caller can take a job back that the thread
// hasn't claimed yet (a thread slow to wake) and run it itself.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace ms20 {

class Worker {
public:
	// Hooks, each on the thread: onStart as it starts (its priority; without one, the class the
	// audio thread has, as tr909's workers), onJoin before its next job each time rejoin() is called
	// (the host's audio workgroup), onStop as it ends (leaving the workgroup).
	struct Hooks {
		std::function<void()> onStart, onJoin, onStop;
	};
	explicit Worker(Hooks hooks = {});
	~Worker();
	Worker(const Worker &) = delete;
	Worker &operator=(const Worker &) = delete;

	// Posts fn(ctx) while idle(); the thread claims it and runs it to the end.
	void post(void (*fn)(void *), void *ctx);
	// Posted and not claimed yet; claimed and running.
	bool posted() const { return m_state.load(std::memory_order_acquire) == kPosted; }
	bool claimed() const { return m_state.load(std::memory_order_acquire) == kRunning; }
	// Takes back a job the thread hasn't claimed: true if taken back (it won't run).
	bool cancel();
	// No job posted or running.
	bool idle() const { return m_state.load(std::memory_order_acquire) == kIdle; }
	void rejoin() { m_epoch.fetch_add(1, std::memory_order_relaxed); }

	// The calling thread's floating-point mode (FPCR on ARM, MXCSR's control bits on x86), and
	// setting it.
	static uint64_t fpMode();
	static void setFpMode(uint64_t mode);
	// A spin-wait's pause.
	static void pause();

private:
	void run();

	enum { kIdle, kPosted, kRunning };
	Hooks m_hooks;
	[[maybe_unused]] char m_pad[64] = {}; // (m_state on a cache line of its own, as near as padding gets it)
	std::atomic<int> m_state{kIdle};
	std::atomic<int> m_epoch{0};
	std::atomic<bool> m_quit{false};
	void (*m_fn)(void *) = nullptr;
	void *m_ctx = nullptr;
	uint64_t m_fp = 0;
	void *m_sem = nullptr; // the platform's semaphore (worker.cpp)
	std::thread m_thread;
};

} // namespace ms20
