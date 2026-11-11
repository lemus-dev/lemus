#include "parallel.h"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define TR808_PAUSE() _mm_pause()
#elif defined(__aarch64__) || defined(_M_ARM64)
#define TR808_PAUSE() __asm__ __volatile__("yield")
#else
#define TR808_PAUSE() ((void)0)
#endif

namespace tr808 {

Parallel::Parallel(int workers, std::function<void(int)> onStart, std::function<void(int)> onJoin)
	: m_onStart(std::move(onStart)), m_onJoin(std::move(onJoin))
{
	for (int k = 0; k < workers; ++k)
		m_threads.emplace_back([this, k] { worker(k); });
}

Parallel::~Parallel()
{
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_quit = true;
	}
	m_wake.notify_all();
	for (auto &t : m_threads)
		t.join();
}

void Parallel::take()
{
	for (;;) {
		const int job = m_next.fetch_add(1);
		if (job >= m_count.load())
			return;
		m_fn(m_ctx, job);
		m_left.fetch_sub(1);
	}
}

void Parallel::run(int n, void (*fn)(void *, int), void *ctx)
{
	if (n <= 0)
		return;
	if (m_threads.empty() || n == 1) {
		for (int job = 0; job < n; ++job)
			fn(ctx, job);
		return;
	}
	{
		std::lock_guard<std::mutex> lock(m_mutex);
		m_fn = fn;
		m_ctx = ctx;
		m_count.store(n);
		m_left.store(n);
		m_next.store(0);
		++m_generation;
	}
	m_wake.notify_all();
	take();
	// the jobs the workers took: they finish them within the block
	while (m_left.load() > 0)
		TR808_PAUSE();
	m_next.store(kParked);
}

void Parallel::worker(int index)
{
	if (m_onStart)
		m_onStart(index);
	int epoch = -1;
	long seen = 0;
	for (;;) {
		{
			std::unique_lock<std::mutex> lock(m_mutex);
			m_wake.wait(lock, [&] { return m_quit || m_generation != seen; });
			if (m_quit)
				return;
			seen = m_generation;
		}
		if (const int e = m_epoch.load(); e != epoch) {
			epoch = e;
			if (m_onJoin)
				m_onJoin(index);
		}
		take();
	}
}

} // namespace tr808
