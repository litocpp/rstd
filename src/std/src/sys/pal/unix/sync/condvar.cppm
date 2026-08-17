export module rstd:sys.pal.unix.sync.condvar;

import :sys.libc.pthread;
import :sys.pal.unix.sync.mutex;
import rstd.core;

using namespace rstd::sys::libc;
using namespace rstd::sys::pal::unix::sync::mutex;

namespace rstd::sys::pal::unix::sync::condvar
{

export class Condvar {
    pthread_cond_t inner;

public:
    Condvar() noexcept;
    ~Condvar() noexcept;

    Condvar(const Condvar&)            = delete;
    Condvar(Condvar&&)                 = delete;
    Condvar& operator=(const Condvar&) = delete;
    Condvar& operator=(Condvar&&)      = delete;

    static auto make() noexcept -> Condvar;
    auto        raw() noexcept -> pthread_cond_t*;
    void        init() noexcept;
    void        notify_one() noexcept;
    void        notify_all() noexcept;
    void        wait(Mutex& mutex) noexcept;
    auto        wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool;
};

} // namespace rstd::sys::pal::unix::sync::condvar
