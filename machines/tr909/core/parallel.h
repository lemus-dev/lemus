#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace tr909 {

// Runs a block's jobs at once: the calling thread takes jobs too, and whatever the workers haven't
// started when it runs out it does itself, so a block never waits on a worker that is slow to wake.
// Real-time safe on the caller's side: no allocation; a short lock to wake the workers.
class Parallel {
public:
	// workers: threads besides the caller (0: everything on the caller). onStart runs on each worker
	// thread as it starts (its index), and onJoin each time a block finds the epoch changed (to join
	// the host's audio workgroup again): the plugin's hooks for the threads' priority.
	explicit Parallel(int workers, std::function<void(int)> onStart = {}, std::function<void(int)> onJoin = {});
	~Parallel();
	Parallel(const Parallel &) = delete;
	Parallel &operator=(const Parallel &) = delete;

	int workers() const { return int(m_threads.size()); }
	// Calls fn(ctx, job) for every job in 0..n-1, each once; returns when all have run.
	void run(int n, void (*fn)(void *, int), void *ctx);
	// The workers call onJoin before their next job (the host's workgroup changed).
	void rejoin() { m_epoch.fetch_add(1); }

private:
	void worker(int index);
	void take(); // runs jobs until none are left to start

	std::vector<std::thread> m_threads;
	std::function<void(int)> m_onStart, m_onJoin;
	std::mutex m_mutex;
	std::condition_variable m_wake;
	bool m_quit = false;
	long m_generation = 0; // under m_mutex: a new block's jobs
	// A job is claimed by m_next's fetch_add; m_count, m_fn and m_ctx are set before m_next is reset to
	// 0, so a claim below m_count sees them. Between blocks m_next is parked far past any count, so a
	// worker that wakes late claims nothing.
	static constexpr int kParked = 1 << 30;
	std::atomic<int> m_next{kParked}, m_left{0}, m_count{0};
	void (*m_fn)(void *, int) = nullptr;
	void *m_ctx = nullptr;
	std::atomic<int> m_epoch{0};
};

} // namespace tr909
