module;
#include <rstd/macro.hpp>
module rstd;
import :sys.sync.thread_parking.pthread;

#if RSTD_OS_UNIX && !RSTD_OS_LINUX

using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::thread_parking::pthread
{
Parker::Parker(): state(EMPTY), lock(sys::pal::Mutex::make()), cvar(Condvar::make()) {}

void Parker::park() {
    // Try fast path for already-notified state
    usize expected = NOTIFIED;
    if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
        return;
    }

    lock.lock();

    expected = EMPTY;
    if (! state.compare_exchange_strong(expected, PARKED, Ordering::Relaxed, Ordering::Relaxed)) {
        if (expected == NOTIFIED) {
            // Must read here for synchronization, see Rust comments
            [[maybe_unused]]
            usize old = state.exchange(EMPTY, Ordering::Acquire);
            assert(old == NOTIFIED, "park state changed unexpectedly");
            return;
        }
        panic { "inconsistent park state" };
    }

    while (true) {
        cvar.wait(lock);

        expected = NOTIFIED;
        if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
            break;
        }
    }
}

void Parker::park_timeout(rstd::time::Duration dur) {
    usize expected = NOTIFIED;
    if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Relaxed)) {
        return;
    }

    lock.lock();
    expected = EMPTY;
    if (! state.compare_exchange_strong(expected, PARKED, Ordering::Acquire, Ordering::Relaxed)) {
        if (expected == NOTIFIED) {
            [[maybe_unused]]
            usize old = state.exchange(EMPTY, Ordering::Acquire);
            assert(old == NOTIFIED && "park state changed unexpectedly");
            return;
        }
        panic { "inconsistent park_timeout state" };
    }

    cvar.wait_timeout(lock,
                      (i64)(dur.as_secs() * u64(rstd::time::NANOS_PER_SEC)) + (i64)dur.subsec_nanos());

    usize old = state.exchange(EMPTY, Ordering::Acquire);
    if (old != NOTIFIED && old != PARKED) {
        panic("inconsistent park_timeout state: {}", old);
    }
}

void Parker::unpark() {
    usize prev = state.exchange(NOTIFIED, Ordering::Relaxed);

    switch (prev) {
    case EMPTY:
    case NOTIFIED: return;
    case PARKED: break;
    default: panic { "inconsistent state in unpark" };
    }

    // Lock to ensure proper synchronization with the parking thread
    lock.lock();
    lock.unlock();
    cvar.notify_one();
}

} // namespace rstd::sys::sync::thread_parking::pthread
#endif
