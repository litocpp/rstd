module;
export module rstd.sys:pal.windows.futex;
export import rstd.core;

#ifndef _WIN32
namespace rstd::sys::pal::windows::futex
{
export {
    // Basic futex types
    using Duration       = cppstd::chrono::duration<double>;
    using Primitive      = u32;
    using Futex          = rstd::atomic<Primitive>;
    using SmallPrimitive = u8;
    using SmallFutex     = rstd::atomic<SmallPrimitive>;

    template<typename T>
    bool futex_wait(const rstd::atomic<T>* futex, T expected, Option<Duration> timeout);

    template<typename T>
    bool futex_wake(const rstd::atomic<T>* futex);

    template<typename T>
    void futex_wake_all(const rstd::atomic<T>* futex);
}
} // namespace rstd::sys::pal::windows
#endif