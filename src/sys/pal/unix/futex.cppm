#ifndef __unix__
export module pal.windows.futex;
#else
module;
#    include <atomic>
#    include <chrono>
#    include <cstdint>
#    include <optional>
export module rstd.sys:pal.unix.futex;

namespace pal
{

export using Duration = std::chrono::duration<double>;
export using Futex    = std::atomic_uint32_t;
/// Must be the underlying type of Futex
export using Primitive = std::uint32_t;
/// An atomic for use as a futex that is at least 8-bits but may be larger.
export using SmallFutex = std::atomic_uint32_t;
/// Must be the underlying type of SmallFutex
export using SmallPrimitive = std::uint32_t;

export bool futex_wait(Futex* futex, Primitive expected, std::optional<Duration> timeout);
export bool futex_wake(Futex* futex);
export void futex_wake_all(Futex* futex);

} // namespace pal
#endif