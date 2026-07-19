module;
#include <rstd/macro.hpp>
module rstd;
import :sys.pal.unix.futex;
import :sys.libc.std;
#if RSTD_OS_UNIX
import :sys.libc.unix;
#endif
using namespace rstd::sys::libc;

#if RSTD_OS_UNIX
namespace rstd::sys::pal::unix::futex
{

#if RSTD_OS_LINUX

bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout) {
    Option<libc::timespec> ts;
    if (timeout) {
        ts = Some(libc::timespec {});
        libc::clock_gettime(libc::CLOCK_MONOTONIC, &*ts);
        ts->tv_sec += static_cast<long>(timeout->as_secs().to_primitive());
        ts->tv_nsec += static_cast<long>(timeout->subsec_nanos().to_primitive());
        auto nanos_per_sec = static_cast<long>(rstd::time::NANOS_PER_SEC.to_primitive());
        if (ts->tv_nsec >= nanos_per_sec) {
            ts->tv_nsec -= nanos_per_sec;
            ts->tv_sec += 1;
        }
    }
    auto pts = ts ? &*ts : nullptr;

    for (;;) {
        if (futex->load(rstd::sync::atomic::Ordering::Relaxed) != expected) {
            return true;
        }
        auto r = syscall(SYS_futex,
                         futex->as_native_ptr(),
                         FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG,
                         expected.to_primitive(),
                         pts,
                         nullptr,
                         FUTEX_BITSET_MATCH_ANY);

        if (r < 0) {
            switch (libc::errno()) {
            case libc::ETIMEDOUT: return false;
            case libc::EINTR: continue;
            default: return true;
            }
        }
    }

    return true;
}

bool futex_wake(Futex* futex) {
    return syscall(SYS_futex, futex->as_native_ptr(), FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1) > 0;
}

void futex_wake_all(Futex* futex) {
    syscall(SYS_futex,
            futex->as_native_ptr(),
            FUTEX_WAKE | FUTEX_PRIVATE_FLAG,
            i32::MAX.to_primitive());
}

#endif

} // namespace rstd::sys::pal::unix::futex
#endif
