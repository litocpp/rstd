module rstd;

import :sys.sync.mutex.futex;

namespace rstd::sys::sync::mutex::futex
{

constexpr State UNLOCKED {};
constexpr State LOCKED { static_cast<State::primitive_type>(1) };
constexpr State CONTENDED { static_cast<State::primitive_type>(2) };

Mutex::Mutex() noexcept: m_futex(UNLOCKED) {
}

auto Mutex::make() noexcept -> Mutex {
    return {};
}

bool Mutex::try_lock() noexcept {
    State expected = UNLOCKED;
    return m_futex.compare_exchange_strong(expected,
                                           LOCKED,
                                           rstd::sync::atomic::Ordering::Acquire,
                                           rstd::sync::atomic::Ordering::Relaxed);
}

void Mutex::lock() noexcept {
    State expected = UNLOCKED;
    if (! m_futex.compare_exchange_strong(expected,
                                          LOCKED,
                                          rstd::sync::atomic::Ordering::Acquire,
                                          rstd::sync::atomic::Ordering::Relaxed)) {
        lock_contended();
    }
}

void Mutex::unlock() noexcept {
    if (m_futex.exchange(UNLOCKED, rstd::sync::atomic::Ordering::Release) == CONTENDED) {
        wake();
    }
}

void Mutex::lock_contended() noexcept {
    State state = spin();

    if (state == UNLOCKED) {
        State expected = UNLOCKED;
        if (m_futex.compare_exchange_strong(expected,
                                            LOCKED,
                                            rstd::sync::atomic::Ordering::Acquire,
                                            rstd::sync::atomic::Ordering::Relaxed)) {
            return;
        }
        state = m_futex.load(rstd::sync::atomic::Ordering::Relaxed);
    }

    for (;;) {
        if (state != CONTENDED &&
            m_futex.exchange(CONTENDED, rstd::sync::atomic::Ordering::Acquire) == UNLOCKED) {
            return;
        }

        pal::futex::futex_wait(&m_futex, CONTENDED, {});
        state = spin();
    }
}

auto Mutex::spin() noexcept -> State {
    int spin_count = 100;
    for (;;) {
        const State state = m_futex.load(rstd::sync::atomic::Ordering::Relaxed);
        if (state != LOCKED || spin_count == 0) return state;

        rstd::hint::spin_loop();
        --spin_count;
    }
}

void Mutex::wake() noexcept {
    pal::futex::futex_wake(&m_futex);
}

} // namespace rstd::sys::sync::mutex::futex
