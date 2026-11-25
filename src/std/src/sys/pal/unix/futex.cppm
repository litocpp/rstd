export module rstd.sys.pal:unix.futex;

export import rstd.core;
using rstd::sync::atomic::Atomic;

namespace rstd::sys::pal::unix::futex
{

export using Duration = rstd::time::Duration;
/// Must be the underlying type of Futex
export using Primitive = u32;
export using Futex     = Atomic<Primitive>;
/// Must be the underlying type of SmallFutex
export using SmallPrimitive = u32;
/// An atomic for use as a futex that is at least 8-bits but may be larger.
export using SmallFutex = Atomic<SmallPrimitive>;

/// The futex must remain alive and at a stable address for the duration of the call.
export [[unsafe]]
bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout);
export [[unsafe]]
bool futex_wake(Futex* futex);
export [[unsafe]]
void futex_wake_all(Futex* futex);

} // namespace rstd::sys::pal::unix::futex
