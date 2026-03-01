module;
#include <errno.h>
#include <time.h>
#include <stdlib.h>
export module rstd:sys.libc.std;

export namespace rstd::sys::libc
{
using ::clock_gettime;
using ::timespec;
constexpr auto M_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
constexpr auto M_ETIMEDOUT       = ETIMEDOUT;
constexpr auto M_EINTR           = EINTR;

using ::abort;

#undef errno
[[gnu::always_inline]]
inline auto errno() noexcept {
    return (*__errno_location());
};

} // namespace rstd::sys::libc