export module rstd:sys.sync.condvar.futex;
export import :sys.pal;
export import :sys.sync.mutex.futex;

namespace rstd::sys::sync::condvar::futex
{

using Futex = pal::futex::Futex;
using Mutex = mutex::futex::Mutex;

export class Condvar {
    Futex m_futex;

    constexpr Condvar() noexcept: m_futex(0) {}

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static auto make() noexcept -> Condvar { return {}; }

    void notify_one() noexcept {
        m_futex.fetch_add(1, rstd::sync::atomic::Ordering::Relaxed);
        pal::futex::futex_wake(&m_futex);
    }

    void notify_all() noexcept {
        m_futex.fetch_add(1, rstd::sync::atomic::Ordering::Relaxed);
        pal::futex::futex_wake_all(&m_futex);
    }

    void wait(Mutex& mutex) noexcept { (void)wait_optional_timeout(mutex, None()); }

    auto wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool {
        return wait_optional_timeout(mutex, Some(timeout));
    }

private:
    auto wait_optional_timeout(Mutex& mutex, Option<rstd::time::Duration> timeout) noexcept
        -> bool {
        auto value = m_futex.load(rstd::sync::atomic::Ordering::Relaxed);

        mutex.unlock();
        auto notified = pal::futex::futex_wait(&m_futex, value, timeout);
        mutex.lock();

        return notified;
    }
};

} // namespace rstd::sys::sync::condvar::futex
