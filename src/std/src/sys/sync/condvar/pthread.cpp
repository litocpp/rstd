module rstd;

import :sys.sync.condvar.pthread;
import rstd.alloc;

using rstd::boxed::Box;
using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::condvar::pthread
{

Condvar::Condvar() noexcept: m_pal(OnceBox<pal::Condvar>::make()), m_mutex(usize {}) {
}

auto Condvar::make() noexcept -> Condvar {
    return {};
}

void Condvar::notify_one() {
    get().notify_one();
}

void Condvar::notify_all() {
    get().notify_all();
}

void Condvar::wait(mutex::pthread::Mutex& mutex) {
    auto& native = mutex.pal_mutex();
    verify(native);
    get().wait(native);
}

auto Condvar::wait_timeout(mutex::pthread::Mutex& mutex, rstd::time::Duration timeout) -> bool {
    auto& native = mutex.pal_mutex();
    verify(native);
    return get().wait_timeout(native, timeout);
}

auto Condvar::get() -> pal::Condvar& {
    return m_pal.get_or_init([]() -> Box<pal::Condvar> {
        auto value = Box<pal::Condvar>::make();
        value->init();
        return value;
    });
}

void Condvar::verify(pal::Mutex& mutex) {
    auto  address = usize(reinterpret_cast<rstd::uintptr_t>(&mutex));
    usize expected {};
    if (m_mutex.compare_exchange_strong(expected, address, Ordering::Relaxed, Ordering::Relaxed)) {
        return;
    }
    if (expected != address) {
        panic { "attempted to use a condition variable with two mutexes" };
    }
}

} // namespace rstd::sys::sync::condvar::pthread
