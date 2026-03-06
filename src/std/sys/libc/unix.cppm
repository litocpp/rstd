module;
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>
export module rstd:sys.libc.unix;

export namespace rstd::sys::libc
{

using ::syscall;
constexpr auto M_SYS_futex              = SYS_futex;
constexpr auto M_FUTEX_WAIT_BITSET      = FUTEX_WAIT_BITSET;
constexpr auto M_FUTEX_PRIVATE_FLAG     = FUTEX_PRIVATE_FLAG;
constexpr auto M_FUTEX_WAKE             = FUTEX_WAKE;
constexpr auto M_FUTEX_BITSET_MATCH_ANY = FUTEX_BITSET_MATCH_ANY;

} // namespace rstd::sys::lib::unix