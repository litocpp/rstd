module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.windows.sync.mutex;

#if RSTD_OS_WINDOWS
// SRWLOCK is used instead of CriticalSection because:
// 1. SRWLock is several times faster than CriticalSection
// 2. CriticalSection allows recursive locking while SRWLock deadlocks,
//    matching Unix pthread_mutex behavior for consistency
// 3. No fairness guarantees needed (matches Rust policy)

export import rstd.core;

// SRWLOCK is a pointer-sized opaque type
struct SRWLOCK {
    void* p;
};

extern "C" {
void __stdcall AcquireSRWLockExclusive(SRWLOCK*);
unsigned char __stdcall TryAcquireSRWLockExclusive(SRWLOCK*);
void __stdcall ReleaseSRWLockExclusive(SRWLOCK*);
}

namespace rstd::sys::pal::windows::sync::mutex
{

export class Mutex {
    SRWLOCK srwlock;

    constexpr Mutex() noexcept: srwlock { nullptr } {}

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
