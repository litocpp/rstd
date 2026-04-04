module;
#include <rstd/macro.hpp>
export module rstd:sys.pal.windows.sync.condvar;

#if RSTD_OS_WINDOWS
export import :sys.pal.windows.sync.mutex;
export import rstd.core;

struct CONDITION_VARIABLE {
    void* p;
};

extern "C" {
unsigned char __stdcall SleepConditionVariableSRW(
    CONDITION_VARIABLE*, SRWLOCK*, unsigned long dwMilliseconds, unsigned long Flags);
void __stdcall WakeConditionVariable(CONDITION_VARIABLE*);
void __stdcall WakeAllConditionVariable(CONDITION_VARIABLE*);
}

using namespace rstd::sys::pal::windows::sync::mutex;

namespace rstd::sys::pal::windows::sync::condvar
{

constexpr unsigned long M_INFINITE = 0xFFFFFFFF;

static auto dur2timeout(rstd::time::Duration dur) noexcept -> unsigned long {
    // Saturate at ~50 days (INFINITE - 1)
    auto ms = dur.as_millis();
    if (ms >= M_INFINITE) return M_INFINITE - 1;
    return static_cast<unsigned long>(ms);
}

export class Condvar {
    CONDITION_VARIABLE inner;

    constexpr Condvar() noexcept: inner { nullptr } {}

public:
    Condvar(const Condvar&)            = delete;
    Condvar& operator=(const Condvar&) = delete;

    static constexpr auto make() noexcept -> Condvar { return {}; }

    void notify_one() noexcept { WakeConditionVariable(&inner); }

    void notify_all() noexcept { WakeAllConditionVariable(&inner); }

    void wait(Mutex& mutex) noexcept {
        [[maybe_unused]]
        auto r = SleepConditionVariableSRW(&inner, mutex.raw(), M_INFINITE, 0);
        debug_assert(r != 0);
    }

    // Returns true if notified, false if timed out
    auto wait_timeout(Mutex& mutex, u64 timeout_ns) noexcept -> bool {
        auto dur = rstd::time::Duration::from_nanos(timeout_ns);
        auto r   = SleepConditionVariableSRW(&inner, mutex.raw(), dur2timeout(dur), 0);
        return r != 0;
    }
};

} // namespace rstd::sys::pal::windows::sync::condvar
#endif
