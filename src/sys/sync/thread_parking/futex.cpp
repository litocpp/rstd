module rstd;
import :sys.sync.thread_parking.futex;

using rstd::sync::atomic::Ordering;

namespace rstd::sys::sync::thread_parking::futex
{

Parker::Parker(): state(EMPTY) {}

void Parker::park() {
    if (state.fetch_sub(1, Ordering::Acquire) == NOTIFIED) {
        return;
    }

    while (true) {
        pal::futex::futex_wait(&state, PARKED, {});

        // Try to change NOTIFIED=>EMPTY and return if successful
        auto expected = NOTIFIED;
        if (state.compare_exchange_strong(expected, EMPTY, Ordering::Acquire, Ordering::Acquire)) {
            return;
        }
        // Spurious wakeup, loop to try again
    }
}

void Parker::park_timeout(cppstd::chrono::duration<double> timeout) {
    // Change NOTIFIED=>EMPTY or EMPTY=>PARKED, and directly return in the first case
    if (state.fetch_sub(1, Ordering::Acquire) == NOTIFIED) {
        return;
    }

    // Wait with timeout
    pal::futex::futex_wait(&state, PARKED, rstd::Some(timeout));

    // Try to detect if we were notified or just timed out
    [[maybe_unused]] i32 old = state.exchange(EMPTY, Ordering::Acquire);
    // Note: We don't differentiate between timeout and notification in this impl
    // TODO
}

void Parker::unpark() {
    // Change PARKED=>NOTIFIED, EMPTY=>NOTIFIED, or NOTIFIED=>NOTIFIED
    if (state.exchange(NOTIFIED, Ordering::Release) == PARKED) {
        pal::futex::futex_wake(&state);
    }
}

} // namespace rstd::sys::sync::thread_parking::futex