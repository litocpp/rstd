#ifdef __unix__
module;

#    include <cerrno>
#    include <chrono>
#    include <cstdio>
#    include <atomic>
#    include <ctime>

#    if defined(__linux__) || defined(__ANDROID__)
#        include <linux/futex.h>
#        include <sys/syscall.h>
#        include <unistd.h>
#    elif defined(__FreeBSD__)
#        include <sys/umtx.h>
#    endif
module rstd.sys;
import :pal.unix.futex;

namespace pal
{

#    if defined(__linux__) || defined(__ANDROID__)

bool futex_wait(Futex* futex, Primitive expected, std::optional<Duration> timeout) {
    std::optional<std::timespec> ts;
    if (timeout) {
        ts.emplace();
        clock_gettime(CLOCK_MONOTONIC, &ts.value());
        auto secs  = std::chrono::duration_cast<std::chrono::seconds>(*timeout);
        auto nsecs = std::chrono::duration_cast<std::chrono::nanoseconds>(*timeout - secs);
        ts->tv_sec += secs.count();
        ts->tv_nsec += nsecs.count();
    }
    auto pts = ts ? &ts.value() : nullptr;

    for (;;) {
        if (futex->load(std::memory_order::relaxed) != expected) {
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
    syscall(SYS_futex, futex, FUTEX_WAKE | FUTEX_PRIVATE_FLAG, INT32_MAX);
}

#    elif defined(__FreeBSD__)
// FreeBSD implementation...
// ...existing FreeBSD specific code converted to C++...
#    endif

} // namespace pal
#endif