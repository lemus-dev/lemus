#include "worker.h"

#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#include <pthread.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <semaphore.h>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define MS20_X86 1
#elif defined(_M_ARM64) && defined(_MSC_VER)
#include <intrin.h>
#endif

namespace ms20 {

namespace {

// The platform's counting semaphore: posting never blocks or allocates.
void *semCreate()
{
#if defined(__APPLE__)
	return dispatch_semaphore_create(0);
#elif defined(_WIN32)
	return CreateSemaphoreW(nullptr, 0, 0x7fffffff, nullptr);
#else
	auto *s = new sem_t;
	sem_init(s, 0, 0);
	return s;
#endif
}

void semPost(void *s)
{
#if defined(__APPLE__)
	dispatch_semaphore_signal(static_cast<dispatch_semaphore_t>(s));
#elif defined(_WIN32)
	ReleaseSemaphore(static_cast<HANDLE>(s), 1, nullptr);
#else
	sem_post(static_cast<sem_t *>(s));
#endif
}

void semWait(void *s)
{
#if defined(__APPLE__)
	dispatch_semaphore_wait(static_cast<dispatch_semaphore_t>(s), DISPATCH_TIME_FOREVER);
#elif defined(_WIN32)
	WaitForSingleObject(static_cast<HANDLE>(s), INFINITE);
#else
	while (sem_wait(static_cast<sem_t *>(s)) != 0 && errno == EINTR) {
	}
#endif
}

void semDestroy(void *s)
{
#if defined(__APPLE__)
	dispatch_release(static_cast<dispatch_semaphore_t>(s));
#elif defined(_WIN32)
	CloseHandle(static_cast<HANDLE>(s));
#else
	sem_destroy(static_cast<sem_t *>(s));
	delete static_cast<sem_t *>(s);
#endif
}

// Without the plugin's hook: the priority the host's audio threads have (as tr909's workers).
void audioPriority()
{
#if defined(__APPLE__)
	pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#elif defined(_WIN32)
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
#endif
}

} // namespace

uint64_t Worker::fpMode()
{
#if defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
	uint64_t v = 0;
	__asm__ __volatile__("mrs %0, fpcr" : "=r"(v));
	return v;
#elif defined(MS20_X86)
	return uint64_t(_mm_getcsr() & ~0x3Fu); // the control bits (the low six are sticky exception flags)
#else
	return 0; // (MSVC on ARM: the default mode on both threads)
#endif
}

void Worker::setFpMode(uint64_t mode)
{
#if defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
	__asm__ __volatile__("msr fpcr, %0" : : "r"(mode));
#elif defined(MS20_X86)
	_mm_setcsr(unsigned(mode) | (_mm_getcsr() & 0x3Fu));
#else
	(void)mode;
#endif
}

void Worker::pause()
{
#if defined(MS20_X86)
	_mm_pause();
#elif defined(__aarch64__) && (defined(__clang__) || defined(__GNUC__))
	__asm__ __volatile__("yield");
#elif defined(_M_ARM64)
	__yield();
#endif
}

Worker::Worker(Hooks hooks) : m_hooks(std::move(hooks))
{
	m_sem = semCreate();
	m_thread = std::thread([this] { run(); });
}

Worker::~Worker()
{
	m_quit.store(true, std::memory_order_release);
	semPost(m_sem);
	if (m_thread.joinable())
		m_thread.join();
	semDestroy(m_sem);
}

void Worker::post(void (*fn)(void *), void *ctx)
{
	m_fn = fn;
	m_ctx = ctx;
	m_fp = fpMode();
	m_state.store(kPosted, std::memory_order_release);
	semPost(m_sem);
}

bool Worker::cancel()
{
	int s = kPosted;
	return m_state.compare_exchange_strong(s, kIdle, std::memory_order_acq_rel);
}

void Worker::run()
{
	if (m_hooks.onStart)
		m_hooks.onStart();
	else
		audioPriority();
	int epoch = -1;
	uint64_t fp = fpMode();
	for (;;) {
		semWait(m_sem);
		if (m_quit.load(std::memory_order_acquire))
			break;
		// a post's count can outlive its job (taken back, or claimed on an earlier wake)
		int s = kPosted;
		if (!m_state.compare_exchange_strong(s, kRunning, std::memory_order_acq_rel))
			continue;
		if (const int e = m_epoch.load(std::memory_order_relaxed); e != epoch) {
			epoch = e;
			if (m_hooks.onJoin)
				m_hooks.onJoin();
		}
		if (m_fp != fp) {
			fp = m_fp;
			setFpMode(fp);
		}
		m_fn(m_ctx);
		m_state.store(kIdle, std::memory_order_release);
	}
	if (m_hooks.onStop)
		m_hooks.onStop();
}

} // namespace ms20
