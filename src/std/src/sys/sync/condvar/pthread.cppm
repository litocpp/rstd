export module rstd:sys.sync.condvar.pthread;

export import :sys.pal;
export import :sys.sync.mutex.pthread;
export import :sys.sync.once_box;

namespace rstd::sys::sync::condvar::pthread
{

export class Condvar {
    OnceBox<pal::Condvar>             m_pal;
    rstd::sync::atomic::Atomic<usize> m_mutex;

    Condvar() noexcept;

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static auto make() noexcept -> Condvar;
    void        notify_one();
    void        notify_all();
    void        wait(mutex::pthread::Mutex& mutex);
    auto        wait_timeout(mutex::pthread::Mutex& mutex, rstd::time::Duration timeout) -> bool;

private:
    auto get() -> pal::Condvar&;
    void verify(pal::Mutex& mutex);
};

} // namespace rstd::sys::sync::condvar::pthread
