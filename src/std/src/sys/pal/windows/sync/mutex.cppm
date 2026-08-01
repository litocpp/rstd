module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.windows.sync.mutex;

#if RSTD_OS_WINDOWS
export import rstd.core;
import :sys.libc.windows;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::windows::sync::mutex
{

export class Mutex {
    SRWLOCK srwlock;

    constexpr Mutex() noexcept: srwlock {} {}

public:
    Mutex(const Mutex&)            = delete;
    Mutex& operator=(const Mutex&) = delete;

    static constexpr auto make() noexcept -> Mutex { return {}; }

    auto raw() noexcept -> SRWLOCK* { return &srwlock; }

    void lock() noexcept { AcquireSRWLockExclusive(&srwlock); }

    auto try_lock() noexcept -> bool { return TryAcquireSRWLockExclusive(&srwlock) != 0; }

    void unlock() noexcept { ReleaseSRWLockExclusive(&srwlock); }
};

} // namespace rstd::sys::pal::windows::sync::mutex
#endif
