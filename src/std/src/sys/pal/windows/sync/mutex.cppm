export module rstd:sys.pal.windows.sync.mutex;

import rstd.core;
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
    auto                  raw() noexcept -> SRWLOCK*;
    void                  lock() noexcept;
    auto                  try_lock() noexcept -> bool;
    void                  unlock() noexcept;
};

} // namespace rstd::sys::pal::windows::sync::mutex
