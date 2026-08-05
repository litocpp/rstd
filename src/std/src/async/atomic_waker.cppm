export module rstd:async.atomic_waker;
export import :async.forward;

using namespace rstd;

namespace rstd::async
{

/// Stores one task waker and coordinates concurrent registration and wake-up.
export class AtomicWaker {
    static constexpr usize WAITING {};
    static constexpr usize REGISTERING { static_cast<usize::primitive_type>(0b01) };
    static constexpr usize WAKING { static_cast<usize::primitive_type>(0b10) };

    sync::atomic::Atomic<usize> m_state { WAITING };
    Option<task::Waker>         m_waker {};

public:
    /// Creates an empty waker slot.
    AtomicWaker() = default;

    AtomicWaker(const AtomicWaker&)                    = delete;
    auto operator=(const AtomicWaker&) -> AtomicWaker& = delete;
    AtomicWaker(AtomicWaker&&)                         = delete;
    auto operator=(AtomicWaker&&) -> AtomicWaker&      = delete;

    ~AtomicWaker() = default;

    /// Registers the waker to notify on the next wake-up.
    void register_waker(const task::Waker& waker) {
        auto expected = WAITING;
        if (m_state.compare_exchange_strong(expected,
                                            REGISTERING,
                                            sync::atomic::Ordering::Acquire,
                                            sync::atomic::Ordering::Acquire)) {
            auto old_waker = Option<task::Waker> {};
            auto wake_now  = Option<task::Waker> {};

            {
                auto next = waker.clone();
                old_waker = m_waker.take();
                m_waker   = Some(rstd::move(next));
            }

            expected = REGISTERING;
            if (m_state.compare_exchange_strong(expected,
                                                WAITING,
                                                sync::atomic::Ordering::AcqRel,
                                                sync::atomic::Ordering::Acquire)) {
                old_waker = None();
                return;
            }

            wake_now = m_waker.take();
            m_state.exchange(WAITING, sync::atomic::Ordering::AcqRel);
            old_waker = None();

            if (wake_now.is_some()) {
                rstd::move(*wake_now).wake();
            }
        } else if (expected == WAKING) {
            waker.wake_by_ref();
            hint::spin_loop();
        }
    }

    /// Registers the waker carried by a polling context.
    void register_context(task::Context& cx) { register_waker(cx.waker()); }

private:
    auto take_waker() -> Option<task::Waker> {
        auto previous = m_state.fetch_or(WAKING, sync::atomic::Ordering::AcqRel);
        if (previous == WAITING) {
            auto waker = m_waker.take();
            m_state.exchange(WAITING, sync::atomic::Ordering::Release);
            return waker;
        }

        return None();
    }

public:
    /// Wakes and removes the currently registered waker, if any.
    void wake() {
        auto waker = take_waker();
        if (waker.is_some()) {
            rstd::move(*waker).wake();
        }
    }
};

} // namespace rstd::async
