module;
#include <errno.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <sched.h>

export module rstd:sys.libc.std;

export namespace rstd::sys::libc
{
using ::clock_gettime;
using ::timespec;
using ::nanosleep;
using ::sched_yield;
constexpr auto M_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
constexpr auto M_ETIMEDOUT       = ETIMEDOUT;
constexpr auto M_EINTR           = EINTR;

using ::abort;

// Memory management
using ::malloc;
using ::free;
using ::realloc;
using ::calloc;
using ::posix_memalign;

// Utilities
using ::memcpy;
using ::memset;

// Types
using ::max_align_t;

#undef errno
[[gnu::always_inline]]
inline auto errno() noexcept {
    return (*__errno_location());
};

} // namespace rstd::sys::libc
