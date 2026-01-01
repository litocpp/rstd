module;
#include <rstd/assert.hpp>
export module rstd.sys:pal.unix.sync.mutex;
export import :lib.pthread;
export import rstd.core;

using namespace rstd::sys::lib::pthread;

namespace rstd::sys::pal::unix::sync::mutex
{

export class Mutex {
    pthread_mutex_t inner;

    Mutex() noexcept: inner(pthread_mutex_initializer()) {}

public:
    ~Mutex() noexcept {
        // always destroy here even move is copy
        // only valid when destroy on not used mutex
        pthread_mutex_destroy(&inner);
    }
    Mutex(Mutex&& o) noexcept: inner(o.inner) {}

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex& operator=(Mutex&&)      = delete;

    static auto make() noexcept -> Mutex { return {}; }

    auto raw() noexcept -> pthread_mutex_t* { return &inner; }

    void lock() noexcept {
        auto r = pthread_mutex_lock(raw());
        if (r != 0) {
            // TODO: from_raw_os_error
            // error = Error::from_raw_os_error(r);
            panic("failed to lock mutex");
        }
    }

    auto try_lock() noexcept -> bool { return pthread_mutex_trylock(raw()) == 0; }

    void unlock() noexcept {
        auto r = pthread_mutex_unlock(raw());
        debug_assert_eq(r, 0);
    }
};

} // namespace rstd::sys::pal::unix::sync::mutex