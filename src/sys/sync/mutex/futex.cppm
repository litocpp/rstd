export module rstd.sys:sync.mutex.futex;
export import :pal;


namespace rstd::sys::sync::mutex::futex
{

using Futex = pal::futex::SmallFutex;
using State = pal::futex::SmallPrimitive;

constexpr State UNLOCKED  = 0;
constexpr State LOCKED    = 1; // locked, no other threads waiting
constexpr State CONTENDED = 2; // locked, and other threads waiting (contended)

export class Mutex {
    Futex m_futex;

public:
    constexpr Mutex() noexcept: m_futex(UNLOCKED) {}

    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    [[nodiscard]] bool try_lock() noexcept {
        State expected = UNLOCKED;
        // Acquire on success, Relaxed on failure (match Rust)
        return m_futex.compare_exchange_strong(
            expected, LOCKED, rstd::memory_order::acquire, rstd::memory_order::relaxed);
    }

    void lock() noexcept {
        State expected = UNLOCKED;
        if (! m_futex.compare_exchange_strong(
                expected, LOCKED, rstd::memory_order::acquire, rstd::memory_order::relaxed)) {
            lock_contended();
        }
    }

    void unlock() noexcept {
        // Release, and if it was contended, wake one waiter
        if (m_futex.exchange(UNLOCKED, rstd::memory_order::release) == CONTENDED) {
            wake();
        }
    }

private:
    // “cold” 仅作提示：可按你工程习惯换成 [[gnu::cold]] / [[msvc::noinline]] 等
    void lock_contended() noexcept {
        // Spin first to speed things up if the lock is released quickly.
        State state = spin();

        // If it's unlocked now, attempt to take the lock without marking contended.
        if (state == UNLOCKED) {
        State expected = UNLOCKED;
            if (m_futex.compare_exchange_strong(
                    expected, LOCKED, rstd::memory_order::acquire, rstd::memory_order::relaxed)) {
                return; // Locked!
            }
            state = m_futex.load(rstd::memory_order::relaxed);
        }

        for (;;) {
            // Put the lock in contended state.
            // Avoid unnecessary write if already CONTENDED.
            if (state != CONTENDED &&
                m_futex.exchange(CONTENDED, rstd::memory_order::acquire) == UNLOCKED) {
                // UNLOCKED -> CONTENDED, so we successfully locked it.
                return;
            }

            // Wait for futex to change, assuming still CONTENDED.
            // timeout = None
            pal::futex::futex_wait(&m_futex, CONTENDED, {});

            // Spin again after waking up.
            state = spin();
        }
    }

    State spin() noexcept {
        int spin_count = 100;
        for (;;) {
            // Use load while spinning to be cache-friendly.
            const State state = m_futex.load(rstd::memory_order::relaxed);

            // Stop spinning when UNLOCKED, but also when CONTENDED.
            if (state != LOCKED || spin_count == 0) {
                return state;
            }

            rstd::hint::spin_loop();
            --spin_count;
        }
    }

    void wake() noexcept {
        // Wake one waiter (match Rust comment/behavior)
        pal::futex::futex_wake(&m_futex);
    }
};

} // namespace rstd::sys::sync::mutex::futex