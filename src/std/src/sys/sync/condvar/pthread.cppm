export module rstd:sys.sync.condvar.pthread;

export import :sys.pal;
export import :sys.sync.mutex.pthread;
export import :sys.sync.once_box;
import :forward;
import rstd.alloc;

using rstd::boxed::Box;
using rstd::sync::atomic::Atomic;
using rstd::sync::atomic::Ordering;
using rstd::sys::sync::OnceBox;

namespace rstd::sys::sync::condvar::pthread
{

export class Condvar {
    OnceBox<pal::Condvar> m_pal;
    Atomic<usize>         m_mutex;

    Condvar() noexcept: m_pal(OnceBox<pal::Condvar>::make()), m_mutex(usize {}) {}

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static auto make() noexcept -> Condvar { return {}; }

    void notify_one() { get().notify_one(); }

    void notify_all() { get().notify_all(); }

    void wait(mutex::pthread::Mutex& mutex) {
        auto& native = mutex.pal_mutex();
        verify(native);
        get().wait(native);
    }

    auto wait_timeout(mutex::pthread::Mutex& mutex, rstd::time::Duration timeout) -> bool {
        auto& native = mutex.pal_mutex();
        verify(native);
        return get().wait_timeout(native, timeout);
    }

private:
    auto get() -> pal::Condvar& {
        return m_pal.get_or_init([]() -> Box<pal::Condvar> {
            auto value = Box<pal::Condvar>::make();
            value->init();
            return value;
        });
    }

    void verify(pal::Mutex& mutex) {
        auto  address = usize(reinterpret_cast<rstd::uintptr_t>(&mutex));
        usize expected {};
        if (m_mutex.compare_exchange_strong(
                expected, address, Ordering::Relaxed, Ordering::Relaxed)) {
            return;
        }
        if (expected != address) {
            panic { "attempted to use a condition variable with two mutexes" };
        }
    }
};

} // namespace rstd::sys::sync::condvar::pthread
