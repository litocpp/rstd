module;
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <cassert>

module parking.pthread;

namespace parking::pthread
{
Parker::Parker(): state(EMPTY) {}

void Parker::park() {
    // Try fast path for already-notified state
    size_t expected = NOTIFIED;
    if (state.compare_exchange_strong(
            expected, EMPTY, std::memory_order_acquire, std::memory_order_relaxed)) {
        return;
    }

    std::unique_lock<std::mutex> guard(lock);
    expected = EMPTY;
    if (! state.compare_exchange_strong(
            expected, PARKED, std::memory_order_relaxed, std::memory_order_relaxed)) {
        if (expected == NOTIFIED) {
            // Must read here for synchronization, see Rust comments
            size_t old = state.exchange(EMPTY, std::memory_order_acquire);
            assert(old == NOTIFIED && "park state changed unexpectedly");
            return;
        }
        throw std::runtime_error("inconsistent park state");
    }

    while (true) {
        cvar.wait(guard);
        expected = NOTIFIED;
        if (state.compare_exchange_strong(
                expected, EMPTY, std::memory_order_acquire, std::memory_order_relaxed)) {
            break;
        }
    }
}

void Parker::park_timeout(const std::chrono::duration<double>& dur) {
    size_t expected = NOTIFIED;
    if (state.compare_exchange_strong(
            expected, EMPTY, std::memory_order_acquire, std::memory_order_relaxed)) {
        return;
    }

    std::unique_lock<std::mutex> guard(lock);
    expected = EMPTY;
    if (! state.compare_exchange_strong(
            expected, PARKED, std::memory_order_relaxed, std::memory_order_relaxed)) {
        if (expected == NOTIFIED) {
            size_t old = state.exchange(EMPTY, std::memory_order_acquire);
            assert(old == NOTIFIED && "park state changed unexpectedly");
            return;
        }
        throw std::runtime_error("inconsistent park_timeout state");
    }

    cvar.wait_for(guard, dur);

    size_t old = state.exchange(EMPTY, std::memory_order_acquire);
    if (old != NOTIFIED && old != PARKED) {
        throw std::runtime_error("inconsistent park_timeout state: " + std::to_string(old));
    }
}

void Parker::unpark() {
    size_t prev = state.exchange(NOTIFIED, std::memory_order_release);

    switch (prev) {
    case EMPTY:
    case NOTIFIED: return;
    case PARKED: break;
    default: throw std::runtime_error("inconsistent state in unpark");
    }

    // Lock to ensure proper synchronization with the parking thread
    std::unique_lock<std::mutex> guard(lock);
    guard.unlock();
    cvar.notify_one();
}

} // namespace parking. pthread