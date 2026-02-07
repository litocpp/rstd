module;
export module rstd:sys.pal.windows.futex;
export import rstd.core;

#ifndef _WIN32

using rstd::sync::atomic::Atomic;
namespace rstd::sys::pal::windows::futex
{
export {
    // Basic futex types
    using Duration       = cppstd::chrono::duration<double>;
    using Primitive      = u32;
    using Futex          = Atomic<Primitive>;
    using SmallPrimitive = u8;
    using SmallFutex     = Atomic<SmallPrimitive>;

    template<typename T>
    bool futex_wait(const Atomic<T>* futex, T expected, Option<Duration> timeout);

    template<typename T>
    bool futex_wake(const Atomic<T>* futex);

    template<typename T>
    void futex_wake_all(const Atomic<T>* futex);
}
} // namespace rstd::sys::pal::windows::futex
#endif