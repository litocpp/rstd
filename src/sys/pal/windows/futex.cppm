#ifndef _WIN32
export module rstd.sys:pal.windows.futex;
#else
module;
#    include <atomic>
#    include <chrono>
#    include <cstdint>
export module rstd.sys:pal.windows.futex;
export import rstd.core;

namespace rstd::sys::pal::windows
{
export {
    // Basic futex types
    using Duration       = std::chrono::duration<double>;
    using Futex          = std::atomic_uint32_t;
    using Primitive      = std::uint32_t;
    using SmallFutex     = std::atomic_uint8_t;
    using SmallPrimitive = std::uint8_t;

    template<typename T>
    bool futex_wait(const std::atomic<T>* futex, T expected, Option<Duration> timeout);

    template<typename T>
    bool futex_wake(const std::atomic<T>* futex);

    template<typename T>
    void futex_wake_all(const std::atomic<T>* futex);
}
} // namespace rstd::pal::windows
#endif