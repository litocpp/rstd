module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_LINUX
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <fcntl.h>
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

// ── Process ──────────────────────────────────────────────────────────────
using ::pid_t;
using ::posix_spawn;
using ::posix_spawnp;
using ::posix_spawn_file_actions_t;
using ::posix_spawn_file_actions_init;
using ::posix_spawn_file_actions_destroy;
using ::posix_spawn_file_actions_addclose;
using ::posix_spawn_file_actions_adddup2;
using ::posix_spawn_file_actions_addopen;
using ::posix_spawnattr_t;
using ::posix_spawnattr_init;
using ::posix_spawnattr_destroy;
using ::waitpid;
using ::pipe2;
using ::close;
using ::dup2;
using ::read;
using ::write;
using ::kill;
constexpr auto M_SIGKILL   = SIGKILL;
constexpr auto M_O_CLOEXEC = O_CLOEXEC;

inline auto wait_exited(int status) -> bool  { return WIFEXITED(status); }
inline auto wait_exitstatus(int status) -> int { return WEXITSTATUS(status); }
inline auto wait_signaled(int status) -> bool { return WIFSIGNALED(status); }
inline auto wait_termsig(int status) -> int  { return WTERMSIG(status); }

} // namespace rstd::sys::libc
#endif