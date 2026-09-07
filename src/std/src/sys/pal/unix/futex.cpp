module;
#include <stdint.h>
#include <errno.h>
#if defined(__APPLE__)
#include <os/os_sync_wait_on_address.h>
#endif
module rstd;
import :sys.pal.unix.futex;
import :sys.libc.std;
#if defined(__linux__)
import :sys.libc.linux;
#endif
import :sys.libc.unix;
using namespace rstd::sys::libc;

namespace rstd::sys::pal::unix::futex
{

#if defined(__APPLE__)
bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout) {
    for (;;) {
        if (futex->load(rstd::sync::atomic::Ordering::Relaxed) != expected) return true;
        int r;
        if (timeout.is_some()) {
            if (timeout->is_zero()) return false;
            auto nanos = static_cast<uint64_t>(timeout->as_secs().to_primitive()) *
                             1000000000ull +
                         static_cast<uint64_t>(timeout->subsec_nanos().to_primitive());
            r = os_sync_wait_on_address_with_timeout(
                futex->as_native_ptr(),
                static_cast<uint64_t>(expected.to_primitive()),
                sizeof(*futex->as_native_ptr()),
                OS_SYNC_WAIT_ON_ADDRESS_NONE,
                OS_CLOCK_MACH_ABSOLUTE_TIME,
                nanos);
        } else {
            r = os_sync_wait_on_address(
                futex->as_native_ptr(),
                static_cast<uint64_t>(expected.to_primitive()),
                sizeof(*futex->as_native_ptr()),
                OS_SYNC_WAIT_ON_ADDRESS_NONE);
        }
        if (r >= 0) return true;
        auto e = errno;
        // Transient conditions documented by the API: retry the wait.
        if (e == EINTR || e == EFAULT || e == ENOMEM) continue;
        if (timeout.is_some() && e == ETIMEDOUT) return false;
        return true;
    }
}

bool futex_wake(Futex* futex) {
    return os_sync_wake_by_address_any(
               futex->as_native_ptr(), sizeof(*futex->as_native_ptr()),
               OS_SYNC_WAKE_BY_ADDRESS_NONE) >= 0;
}

void futex_wake_all(Futex* futex) {
    (void)os_sync_wake_by_address_all(
        futex->as_native_ptr(), sizeof(*futex->as_native_ptr()),
        OS_SYNC_WAKE_BY_ADDRESS_NONE);
}
#else
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
