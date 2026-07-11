export module rstd:sync.condvar;
export import :sync.mutex;
export import :sys.sync.condvar;
import :time;

namespace rstd::sync
{

using sys_condvar_t = rstd::sys::sync::condvar::Condvar;

export struct WaitTimeoutResult {
    bool m_timed_out {};

    constexpr auto timed_out() const noexcept -> bool { return m_timed_out; }
};

export class Condvar {
    sys_condvar_t m_inner;

public:
    Condvar(): m_inner(sys_condvar_t::make()) {}

    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static auto make() -> Condvar { return {}; }

    template<typename T>
    void wait(MutexGuard<T>& guard) {
        m_inner.wait(guard.raw_lock());
    }

    template<typename T, typename F>
    void wait_while(MutexGuard<T>& guard, F condition) {
        while (condition(*guard)) {
            wait(guard);
        }
    }

    template<typename T>
    auto wait_timeout(MutexGuard<T>& guard, time::Duration timeout) -> WaitTimeoutResult {
        return WaitTimeoutResult { ! m_inner.wait_timeout(guard.raw_lock(), timeout) };
    }

    template<typename T, typename F>
    auto wait_timeout_while(MutexGuard<T>& guard, time::Duration timeout, F condition)
        -> WaitTimeoutResult {
        auto start = time::Instant::now();

        while (condition(*guard)) {
            auto remaining = timeout.checked_sub(start.elapsed());
            if (remaining.is_none()) {
                return WaitTimeoutResult { true };
            }

            (void)wait_timeout(guard, rstd::move(remaining).unwrap_unchecked());
        }

        return WaitTimeoutResult { false };
    }

    void notify_one() noexcept { m_inner.notify_one(); }

    void notify_all() noexcept { m_inner.notify_all(); }
};

} // namespace rstd::sync
