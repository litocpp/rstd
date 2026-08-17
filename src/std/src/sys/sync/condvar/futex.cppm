export module rstd:sys.sync.condvar.futex;
export import :sys.pal;
export import :sys.sync.mutex.futex;

namespace rstd::sys::sync::condvar::futex
{

using Futex = pal::futex::Futex;
using Mutex = mutex::futex::Mutex;

export class Condvar {
    Futex m_futex;

    Condvar() noexcept;

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static auto make() noexcept -> Condvar;
    void        notify_one() noexcept;
    void        notify_all() noexcept;
    void        wait(Mutex& mutex) noexcept;
    auto        wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool;

private:
    auto wait_optional_timeout(Mutex& mutex, Option<rstd::time::Duration> timeout) noexcept -> bool;
};

} // namespace rstd::sys::sync::condvar::futex
