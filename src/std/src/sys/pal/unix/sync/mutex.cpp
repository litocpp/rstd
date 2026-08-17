module;
#include <rstd/macro.hpp>
module rstd;
import :sys.pal.unix.sync.mutex;

import :sys.libc.pthread;
import rstd.core;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::unix::sync::mutex
{

Mutex::Mutex() noexcept: inner(pthread_mutex_initializer()) {
}

Mutex::~Mutex() noexcept {
    pthread_mutex_destroy(&inner);
}

auto Mutex::make() noexcept -> Mutex {
    return {};
}

auto Mutex::raw() noexcept -> pthread_mutex_t* {
    return &inner;
}

void Mutex::lock() noexcept {
    auto r = pthread_mutex_lock(raw());
    if (r != 0) panic { "failed to lock mutex" };
}

auto Mutex::try_lock() noexcept -> bool {
    return pthread_mutex_trylock(raw()) == 0;
}

void Mutex::unlock() noexcept {
    [[maybe_unused]]
    auto r = pthread_mutex_unlock(raw());
    debug_assert_eq(r, 0);
}

} // namespace rstd::sys::pal::unix::sync::mutex
