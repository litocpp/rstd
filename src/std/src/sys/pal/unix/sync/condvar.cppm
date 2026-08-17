module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.unix.sync.condvar;

export import :sys.libc.pthread;
export import :sys.libc.std;
export import :sys.libc.unix;
export import :sys.pal.unix.sync.mutex;
export import rstd.core;

using namespace rstd::sys::libc;
using namespace rstd::sys::pal::unix::sync::mutex;

namespace rstd::sys::pal::unix::sync::condvar
{

export class Condvar {
    pthread_cond_t inner;

public:
    Condvar() noexcept: inner(pthread_cond_initializer()) {}

    ~Condvar() noexcept {
        [[maybe_unused]]
        auto r = pthread_cond_destroy(&inner);
        debug_assert_eq(r, 0);
    }

    Condvar(const Condvar&)            = delete;
    Condvar(Condvar&&)                 = delete;
    Condvar& operator=(const Condvar&) = delete;
    Condvar& operator=(Condvar&&)      = delete;

    static auto make() noexcept -> Condvar { return {}; }

    auto raw() noexcept -> pthread_cond_t* { return &inner; }

    void init() noexcept {
#if RSTD_OS_APPLE
        auto r = pthread_cond_init(raw(), nullptr);
        rstd_assert_eq(r, 0);
#else
        pthread_condattr_t attr;
        auto               r = pthread_condattr_init(&attr);
        rstd_assert_eq(r, 0);

        r = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
        rstd_assert_eq(r, 0);

        r = pthread_cond_init(raw(), &attr);
        rstd_assert_eq(r, 0);

        r = pthread_condattr_destroy(&attr);
        rstd_assert_eq(r, 0);
#endif
    }

    // Signal one waiting thread
    void notify_one() noexcept {
        [[maybe_unused]]
        auto r = pthread_cond_signal(raw());
        debug_assert_eq(r, 0);
    }

    // Signal all waiting threads
    void notify_all() noexcept {
        [[maybe_unused]]
        auto r = pthread_cond_broadcast(raw());
        debug_assert_eq(r, 0);
    }

    // Wait on the condition variable
    // mutex must be locked by the current thread
    void wait(Mutex& mutex) noexcept {
        [[maybe_unused]]
        auto r = pthread_cond_wait(raw(), mutex.raw());
        debug_assert_eq(r, 0);
    }

    auto wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool {
        timespec ts {
            .tv_sec  = static_cast<time_t>(timeout.as_secs().to_primitive()),
            .tv_nsec = static_cast<long>(timeout.subsec_nanos().to_primitive()),
        };
#if RSTD_OS_APPLE
        auto r = pthread_cond_timedwait_relative_np(raw(), mutex.raw(), &ts);
#else
        clock_gettime(CLOCK_MONOTONIC, &ts);

        ts.tv_sec += static_cast<time_t>(timeout.as_secs().to_primitive());
        ts.tv_nsec += static_cast<long>(timeout.subsec_nanos().to_primitive());
        if (ts.tv_nsec >= static_cast<long>(rstd::time::NANOS_PER_SEC.to_primitive())) {
            ++ts.tv_sec;
            ts.tv_nsec -= static_cast<long>(rstd::time::NANOS_PER_SEC.to_primitive());
        }

        auto r = pthread_cond_timedwait(raw(), mutex.raw(), &ts);
#endif
        rstd_assert(r == ETIMEDOUT || r == 0, "pthread_cond_timedwait failed");
        return r == 0;
    }
};

} // namespace rstd::sys::pal::unix::sync::condvar
