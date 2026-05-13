module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_LINUX
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/types.h>
#include <unistd.h>
#include <sched.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <fcntl.h>
#include <dirent.h>
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
using ::gmtime_r;
constexpr auto M_CLOCK_MONOTONIC = CLOCK_MONOTONIC;
constexpr auto M_CLOCK_REALTIME = CLOCK_REALTIME;
constexpr auto M_ETIMEDOUT       = ETIMEDOUT;
constexpr auto M_EINTR           = EINTR;

inline auto gmtime_utc(::time_t secs) noexcept -> ::tm {
    ::tm out {};
    ::gmtime_r(&secs, &out);
    return out;
}

using ::isatty;

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
using ::dup;
using ::dup2;
using ::read;
using ::write;
using ::kill;
using ::lseek;
using ::pread;
using ::pwrite;
using ::fsync;
using ::fdatasync;
using ::ftruncate;
using ::unlink;
using ::rmdir;
using ::link;
using ::symlink;
using ::readlink;
using ::realpath;
using ::open;
using ::fcntl;
using ::chmod;
using ::fchmod;
using ::mkdir;
using ::futimens;
using ::utimensat;
using ::flock;
using ::stat;
using ::fstat;
using ::lstat;
using ::opendir;
using ::readdir;
using ::closedir;
using ::rename;
using ::free;

// ── Type aliases ─────────────────────────────────────────────────────────
using ::mode_t;
using ::off_t;
using ::ssize_t;
using ::time_t;
using ::DIR;
using ::dirent;
/// `struct stat` aliased to avoid clash with the `::stat()` function.
using stat_t     = struct ::stat;
/// `struct timespec` aliased to avoid the `struct` keyword leaking into call sites.
using timespec_t = struct ::timespec;

/// Returns an lvalue reference to the platform `errno`. Use to read and write.
inline auto get_errno() noexcept -> int& { return errno; }

// ── Open flags / seek whence ─────────────────────────────────────────────
constexpr auto M_O_RDONLY    = O_RDONLY;
constexpr auto M_O_WRONLY    = O_WRONLY;
constexpr auto M_O_RDWR      = O_RDWR;
constexpr auto M_O_CREAT     = O_CREAT;
constexpr auto M_O_EXCL      = O_EXCL;
constexpr auto M_O_TRUNC     = O_TRUNC;
constexpr auto M_O_APPEND    = O_APPEND;
constexpr auto M_O_NOFOLLOW  = O_NOFOLLOW;
constexpr auto M_O_DIRECTORY = O_DIRECTORY;
constexpr auto M_F_DUPFD_CLOEXEC = F_DUPFD_CLOEXEC;
constexpr auto M_SEEK_SET    = SEEK_SET;
constexpr auto M_SEEK_CUR    = SEEK_CUR;
constexpr auto M_SEEK_END    = SEEK_END;

// ── Stat mode masks ──────────────────────────────────────────────────────
constexpr auto M_S_IFMT   = S_IFMT;
constexpr auto M_S_IFREG  = S_IFREG;
constexpr auto M_S_IFDIR  = S_IFDIR;
constexpr auto M_S_IFLNK  = S_IFLNK;
constexpr auto M_S_IFIFO  = S_IFIFO;
constexpr auto M_S_IFBLK  = S_IFBLK;
constexpr auto M_S_IFCHR  = S_IFCHR;
constexpr auto M_S_IFSOCK = S_IFSOCK;

// ── Dirent type tags ─────────────────────────────────────────────────────
constexpr auto M_DT_REG  = DT_REG;
constexpr auto M_DT_DIR  = DT_DIR;
constexpr auto M_DT_LNK  = DT_LNK;
constexpr auto M_DT_FIFO = DT_FIFO;
constexpr auto M_DT_BLK  = DT_BLK;
constexpr auto M_DT_CHR  = DT_CHR;
constexpr auto M_DT_SOCK = DT_SOCK;

// ── flock ────────────────────────────────────────────────────────────────
constexpr auto M_LOCK_SH = LOCK_SH;
constexpr auto M_LOCK_EX = LOCK_EX;
constexpr auto M_LOCK_NB = LOCK_NB;
constexpr auto M_LOCK_UN = LOCK_UN;

// ── utimensat sentinel ───────────────────────────────────────────────────
constexpr auto M_UTIME_OMIT = UTIME_OMIT;

// ── Error codes ──────────────────────────────────────────────────────────
constexpr auto M_EINVAL      = EINVAL;
constexpr auto M_EWOULDBLOCK = EWOULDBLOCK;

constexpr auto M_SIGKILL   = SIGKILL;
constexpr auto M_O_CLOEXEC = O_CLOEXEC;

inline auto wait_exited(int status) -> bool  { return WIFEXITED(status); }
inline auto wait_exitstatus(int status) -> int { return WEXITSTATUS(status); }
inline auto wait_signaled(int status) -> bool { return WIFSIGNALED(status); }
inline auto wait_termsig(int status) -> int  { return WTERMSIG(status); }

} // namespace rstd::sys::libc
#endif