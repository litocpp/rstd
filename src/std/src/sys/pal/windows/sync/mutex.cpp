module rstd;
import :sys.pal.windows.sync.mutex;

import rstd.core;
import :sys.libc.windows;

using namespace rstd::sys::libc;

namespace rstd::sys::pal::windows::sync::mutex
{

auto Mutex::raw() noexcept -> SRWLOCK* {
    return &srwlock;
}

void Mutex::lock() noexcept {
    AcquireSRWLockExclusive(&srwlock);
}

auto Mutex::try_lock() noexcept -> bool {
    return TryAcquireSRWLockExclusive(&srwlock) != 0;
}

void Mutex::unlock() noexcept {
    ReleaseSRWLockExclusive(&srwlock);
}

} // namespace rstd::sys::pal::windows::sync::mutex
