module;
#include <atomic>
#include <chrono>
module rstd.sys;
import :sync.thread_parking.futex;



namespace parking::futex
{

Parker::Parker(): state(EMPTY) {}

void Parker::park() {
    if (state.fetch_sub(1, std::memory_order_acquire) == NOTIFIED) {
        return;
    }

    while (true) {
        pal::futex_wait(&state, PARKED, {});

        // Try to change NOTIFIED=>EMPTY and return if successful
        auto expected = NOTIFIED;
        if (state.compare_exchange_strong(
                expected, EMPTY, std::memory_order_acquire, std::memory_order_acquire)) {
            return;
        }
        // Spurious wakeup, loop to try again
    }
}

void Parker::park_timeout(std::chrono::duration<double> timeout) {
    // Change NOTIFIED=>EMPTY or EMPTY=>PARKED, and directly return in the first case
    if (state.fetch_sub(1, std::memory_order_acquire) == NOTIFIED) {
        return;
    }

    // Wait with timeout
    pal::futex_wait(&state, PARKED, timeout);

    // Try to detect if we were notified or just timed out
    int32_t old = state.exchange(EMPTY, std::memory_order_acquire);
    // Note: We don't differentiate between timeout and notification in this impl
}

void Parker::unpark() {
    // Change PARKED=>NOTIFIED, EMPTY=>NOTIFIED, or NOTIFIED=>NOTIFIED
    if (state.exchange(NOTIFIED, std::memory_order_release) == PARKED) {
        pal::futex_wake(&state);
    }
}

} // namespace futex