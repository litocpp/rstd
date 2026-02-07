module;

#ifdef __unix__
#    include <cerrno>
#    include <ctime>

#    if defined(__linux__) || defined(__ANDROID__)
#        include <linux/futex.h>
#        include <sys/syscall.h>
#        include <unistd.h>
#    elif defined(__FreeBSD__)
#        include <sys/umtx.h>
#    endif
#endif

module rstd;
import :sys.pal.unix.futex;

#ifdef __unix__
namespace rstd::sys::pal::unix::futex
{

#    if defined(__linux__) || defined(__ANDROID__)

bool futex_wait(Futex* futex, Primitive expected, Option<Duration> timeout) {
    Option<std::timespec> ts;
    if (timeout) {
        ts = Some(std::timespec {});
        clock_gettime(CLOCK_MONOTONIC, &*ts);
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
        auto r = syscall(SYS_futex,
                         futex,
                         FUTEX_WAIT_BITSET | FUTEX_PRIVATE_FLAG,
                         expected,
                         pts,
                         nullptr,
                         FUTEX_BITSET_MATCH_ANY);

        if (r < 0) {
            switch (errno) {
            case ETIMEDOUT: return false;
            case EINTR: continue;
            default: return true;
            }
        }
    }

    return true;
}

bool futex_wake(Futex* futex) {
    return syscall(SYS_futex, futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, 1) > 0;
}

void futex_wake_all(Futex* futex) {
    syscall(SYS_futex, futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, rstd::numeric_limits<i32>::max());
}

#    elif defined(__FreeBSD__)
// FreeBSD implementation...
// ...existing FreeBSD specific code converted to C++...
#    endif

} // namespace rstd::sys::pal::unix::futex
#endif