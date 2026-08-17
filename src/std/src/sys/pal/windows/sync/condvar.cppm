export module rstd:sys.pal.windows.sync.condvar;

import :sys.pal.windows.sync.mutex;
import :sys.libc.windows;
import rstd.core;

using namespace rstd::sys::libc;
using namespace rstd::sys::pal::windows::sync::mutex;

namespace rstd::sys::pal::windows::sync::condvar
{

export class Condvar {
    CONDITION_VARIABLE inner;

    constexpr Condvar() noexcept: inner {} {}

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static constexpr auto make() noexcept -> Condvar { return {}; }
    void                  notify_one() noexcept;
    void                  notify_all() noexcept;
    void                  wait(Mutex& mutex) noexcept;
    auto                  wait_timeout(Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool;
};

} // namespace rstd::sys::pal::windows::sync::condvar
