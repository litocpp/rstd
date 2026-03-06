module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.sync.condvar;
export import :sys.libc.pthread;
export import :sys.libc.std;
export import :sys.pal.unix.sync.mutex;
export import rstd.core;

using namespace rstd::sys::libc;
using namespace rstd::sys::pal::unix::sync::mutex;

namespace rstd::sys::pal::unix::sync::condvar
{

export class Condvar {
    pthread_cond_t inner;

    Condvar() noexcept: inner(pthread_cond_initializer()) {}

public:
    ~Condvar() noexcept {
        [[maybe_unused]] auto r = pthread_cond_destroy(&inner);
        debug_assert_eq(r, 0);
    }

    Condvar(Condvar&& o) noexcept: inner(o.inner) {}

    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;
    Condvar& operator=(Condvar&&)      = delete;

    static auto make() noexcept -> Condvar { return {}; }

    auto raw() noexcept -> pthread_cond_t* { return &inner; }

    // Initialize the condition variable with CLOCK_MONOTONIC
    void init() noexcept {
        pthread_condattr_t attr;
        auto r = pthread_condattr_init(&attr);
        assert_eq(r, 0);

        r = pthread_condattr_setclock(&attr, M_CLOCK_MONOTONIC);
        assert_eq(r, 0);

        r = pthread_cond_init(raw(), &attr);
        assert_eq(r, 0);

        r = pthread_condattr_destroy(&attr);
        assert_eq(r, 0);
    }

    // Signal one waiting thread
    void notify_one() noexcept {
        [[maybe_unused]] auto r = pthread_cond_signal(raw());
        debug_assert_eq(r, 0);
    }

    // Signal all waiting threads
    void notify_all() noexcept {
        [[maybe_unused]] auto r = pthread_cond_broadcast(raw());
        debug_assert_eq(r, 0);
    }

    // Wait on the condition variable
    // mutex must be locked by the current thread
    void wait(Mutex& mutex) noexcept {
        [[maybe_unused]] auto r = pthread_cond_wait(raw(), mutex.raw());
        debug_assert_eq(r, 0);
    }

    // Wait on the condition variable with a timeout
    // Returns true if notified, false if timed out
    // mutex must be locked by the current thread
    auto wait_timeout(Mutex& mutex, u64 timeout_ns) noexcept -> bool {
        timespec ts;

        // Get current time with CLOCK_MONOTONIC
        clock_gettime(M_CLOCK_MONOTONIC, &ts);

        // Add timeout
        const u64 NSEC_PER_SEC = 1'000'000'000;
        u64 total_nsec = ts.tv_nsec + timeout_ns;
        ts.tv_sec += total_nsec / NSEC_PER_SEC;
        ts.tv_nsec = total_nsec % NSEC_PER_SEC;

        auto r = pthread_cond_timedwait(raw(), mutex.raw(), &ts);
        assert(r == M_ETIMEDOUT || r == 0, "pthread_cond_timedwait failed");
        return r == 0;
    }
};

} // namespace rstd::sys::pal::unix::sync::condvar