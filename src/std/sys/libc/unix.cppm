module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_LINUX
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <sched.h>
#endif
export module rstd:sys.libc.unix;

#ifdef RSTD_OS_LINUX
export namespace rstd::sys::libc
{

using ::syscall;
using ::sched_yield;
using ::posix_memalign;

constexpr auto M_SYS_futex              = SYS_futex;
constexpr auto M_FUTEX_WAIT_BITSET      = FUTEX_WAIT_BITSET;
constexpr auto M_FUTEX_PRIVATE_FLAG     = FUTEX_PRIVATE_FLAG;
constexpr auto M_FUTEX_WAKE             = FUTEX_WAKE;
constexpr auto M_FUTEX_BITSET_MATCH_ANY = FUTEX_BITSET_MATCH_ANY;

using ::clock_gettime;
using ::nanosleep;
constexpr auto M_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
constexpr auto M_CLOCK_REALTIME = CLOCK_REALTIME;
constexpr auto M_ETIMEDOUT       = ETIMEDOUT;
constexpr auto M_EINTR           = EINTR;

} // namespace rstd::sys::lib::unix
#endif