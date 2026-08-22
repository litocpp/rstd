module;
#include <rstd/macro.hpp>
module rstd;
import :sys.pal.windows.sync.condvar;

import :sys.pal.windows.sync.mutex;
import rstd.core;
import :sys.libc.windows;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::windows::sync::condvar
{

static auto dur2timeout(rstd::time::Duration dur) noexcept -> DWORD {
    auto ms = dur.as_millis().to_primitive();
    if (ms >= M_INFINITE) return M_INFINITE - 1;
    return static_cast<DWORD>(ms);
}

void Condvar::notify_one() noexcept {
    WakeConditionVariable(&inner);
}

void Condvar::notify_all() noexcept {
    WakeAllConditionVariable(&inner);
}

void Condvar::wait(mutex::Mutex& mutex) noexcept {
    [[maybe_unused]]
    auto r = SleepConditionVariableSRW(&inner, mutex.raw(), M_INFINITE, 0);
    debug_assert(r != 0);
}

auto Condvar::wait_timeout(mutex::Mutex& mutex, rstd::time::Duration timeout) noexcept -> bool {
    auto r = SleepConditionVariableSRW(&inner, mutex.raw(), dur2timeout(timeout), 0);
    return r != 0;
}

} // namespace rstd::sys::pal::windows::sync::condvar
