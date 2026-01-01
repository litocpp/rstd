module;
#ifdef __unix__
#    include <atomic>
#    include <chrono>
#    include <cstdint>
#    include <optional>
#endif
export module rstd.sys:pal.unix.futex;

#ifdef __unix__
export import rstd.core;

namespace rstd::sys::pal::unix
{

export using Duration = std::chrono::duration<double>;
export using Futex    = std::atomic_uint32_t;
/// Must be the underlying type of Futex
export using Primitive = std::uint32_t;
/// An atomic for use as a futex that is at least 8-bits but may be larger.
export using SmallFutex = std::atomic_uint32_t;
/// Must be the underlying type of SmallFutex
export using SmallPrimitive = std::uint32_t;

export bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout);
export bool futex_wake(Futex* futex);
export void futex_wake_all(Futex* futex);

} // namespace rstd::pal::unix
#endif