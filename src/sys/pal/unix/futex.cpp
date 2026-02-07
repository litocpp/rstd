module;
module rstd;
import :sys.pal.unix.futex;
import :sys.libc.std;
#ifdef __unix__
import :sys.libc.unix;
#endif
using namespace rstd::sys::libc;


#ifdef __unix__
namespace rstd::sys::pal::unix::futex
{

#    if defined(__linux__) || defined(__ANDROID__)

bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout) {
    Option<libc::timespec> ts;
    if (timeout) {
        ts = Some(libc::timespec {});
        libc::clock_gettime(libc::M_CLOCK_MONOTONIC, &*ts);
        auto secs  = cppstd::chrono::duration_cast<cppstd::chrono::seconds>(*timeout);
        auto nsecs = cppstd::chrono::duration_cast<cppstd::chrono::nanoseconds>(
            cppstd::chrono::operator-(*timeout, secs));
        ts->tv_sec += secs.count();
        ts->tv_nsec += nsecs.count();
    }
    auto pts = ts ? &*ts : nullptr;

    for (;;) {
        if (futex->load(rstd::memory_order::relaxed) != expected) {
            return true;
        }
        auto r = syscall(M_SYS_futex,
                         futex,
                         M_FUTEX_WAIT_BITSET | M_FUTEX_PRIVATE_FLAG,
                         expected,
                         pts,
                         nullptr,
                         M_FUTEX_BITSET_MATCH_ANY);

        if (r < 0) {
            switch (libc::errno()) {
            case libc::M_ETIMEDOUT: return false;
            case libc::M_EINTR: continue;
            default: return true;
            }
        }
    }

    return true;
}

bool futex_wake(Futex* futex) {
    return syscall(M_SYS_futex, futex, M_FUTEX_WAKE | M_FUTEX_PRIVATE_FLAG, 1) > 0;
}

void futex_wake_all(Futex* futex) {
    syscall(
        M_SYS_futex, futex, M_FUTEX_WAKE | M_FUTEX_PRIVATE_FLAG, rstd::numeric_limits<i32>::max());
}

#    endif

} // namespace rstd::sys::pal::unix::futex
#endif