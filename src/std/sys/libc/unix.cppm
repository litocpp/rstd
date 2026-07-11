module;
#include <rstd/macro.hpp>

#ifdef RSTD_OS_LINUX
#include <arpa/inet.h>
#include <linux/futex.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/timerfd.h>
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
inline constexpr auto _SYS_futex              = SYS_futex;
inline constexpr auto _FUTEX_WAIT_BITSET      = FUTEX_WAIT_BITSET;
inline constexpr auto _FUTEX_PRIVATE_FLAG     = FUTEX_PRIVATE_FLAG;
inline constexpr auto _FUTEX_WAKE             = FUTEX_WAKE;
inline constexpr auto _FUTEX_BITSET_MATCH_ANY = FUTEX_BITSET_MATCH_ANY;

inline constexpr auto _CLOCK_MONOTONIC = CLOCK_MONOTONIC;
inline constexpr auto _CLOCK_REALTIME  = CLOCK_REALTIME;

inline constexpr auto _ENOENT        = ENOENT;
inline constexpr auto _EACCES        = EACCES;
inline constexpr auto _EPERM         = EPERM;
inline constexpr auto _ECONNREFUSED  = ECONNREFUSED;
inline constexpr auto _ECONNRESET    = ECONNRESET;
inline constexpr auto _EHOSTUNREACH  = EHOSTUNREACH;
inline constexpr auto _ENETUNREACH   = ENETUNREACH;
inline constexpr auto _ECONNABORTED  = ECONNABORTED;
inline constexpr auto _ENOTCONN      = ENOTCONN;
inline constexpr auto _EADDRINUSE    = EADDRINUSE;
inline constexpr auto _EADDRNOTAVAIL = EADDRNOTAVAIL;
inline constexpr auto _ENETDOWN      = ENETDOWN;
inline constexpr auto _EPIPE         = EPIPE;
inline constexpr auto _EEXIST        = EEXIST;
inline constexpr auto _EAGAIN        = EAGAIN;
inline constexpr auto _EWOULDBLOCK   = EWOULDBLOCK;
inline constexpr auto _ENOTDIR       = ENOTDIR;
inline constexpr auto _EISDIR        = EISDIR;
inline constexpr auto _ENOTEMPTY     = ENOTEMPTY;
inline constexpr auto _EROFS         = EROFS;
inline constexpr auto _ELOOP         = ELOOP;
#ifdef ESTALE
inline constexpr auto _ESTALE     = ESTALE;
inline constexpr bool _HAS_ESTALE = true;
#else
inline constexpr auto _ESTALE     = -1;
inline constexpr bool _HAS_ESTALE = false;
#endif
inline constexpr auto _EINVAL    = EINVAL;
inline constexpr auto _ETIMEDOUT = ETIMEDOUT;
inline constexpr auto _ENOSPC    = ENOSPC;
inline constexpr auto _ESPIPE    = ESPIPE;
#ifdef EDQUOT
inline constexpr auto _EDQUOT     = EDQUOT;
inline constexpr bool _HAS_EDQUOT = true;
#else
inline constexpr auto _EDQUOT     = -2;
inline constexpr bool _HAS_EDQUOT = false;
#endif
inline constexpr auto _EFBIG        = EFBIG;
inline constexpr auto _EBUSY        = EBUSY;
inline constexpr auto _ETXTBSY      = ETXTBSY;
inline constexpr auto _EDEADLK      = EDEADLK;
inline constexpr auto _EXDEV        = EXDEV;
inline constexpr auto _EMLINK       = EMLINK;
inline constexpr auto _ENAMETOOLONG = ENAMETOOLONG;
inline constexpr auto _E2BIG        = E2BIG;
inline constexpr auto _EINTR        = EINTR;
inline constexpr auto _ENOSYS       = ENOSYS;
inline constexpr auto _EOPNOTSUPP   = EOPNOTSUPP;
inline constexpr auto _EAFNOSUPPORT = EAFNOSUPPORT;
inline constexpr auto _ENOMEM       = ENOMEM;
inline constexpr auto _ENOBUFS      = ENOBUFS;
inline constexpr auto _EIO          = EIO;
inline constexpr auto _EINPROGRESS  = EINPROGRESS;

inline constexpr auto _SIGKILL = SIGKILL;

inline constexpr auto _O_CLOEXEC       = O_CLOEXEC;
inline constexpr auto _O_NONBLOCK      = O_NONBLOCK;
inline constexpr auto _O_RDONLY        = O_RDONLY;
inline constexpr auto _O_WRONLY        = O_WRONLY;
inline constexpr auto _O_RDWR          = O_RDWR;
inline constexpr auto _O_CREAT         = O_CREAT;
inline constexpr auto _O_EXCL          = O_EXCL;
inline constexpr auto _O_TRUNC         = O_TRUNC;
inline constexpr auto _O_APPEND        = O_APPEND;
inline constexpr auto _O_NOFOLLOW      = O_NOFOLLOW;
inline constexpr auto _O_DIRECTORY     = O_DIRECTORY;
inline constexpr auto _F_DUPFD_CLOEXEC = F_DUPFD_CLOEXEC;
inline constexpr auto _F_GETFL         = F_GETFL;
inline constexpr auto _F_SETFL         = F_SETFL;
inline constexpr auto _F_GETFD         = F_GETFD;
inline constexpr auto _F_SETFD         = F_SETFD;
inline constexpr auto _FD_CLOEXEC      = FD_CLOEXEC;
inline constexpr auto _SEEK_SET        = SEEK_SET;
inline constexpr auto _SEEK_CUR        = SEEK_CUR;
inline constexpr auto _SEEK_END        = SEEK_END;

inline constexpr auto _AF_INET      = AF_INET;
inline constexpr auto _AF_INET6     = AF_INET6;
inline constexpr auto _SOCK_STREAM  = SOCK_STREAM;
inline constexpr auto _SOL_SOCKET   = SOL_SOCKET;
inline constexpr auto _SO_REUSEADDR = SO_REUSEADDR;
inline constexpr auto _SO_ERROR     = SO_ERROR;
inline constexpr auto _IPPROTO_TCP  = IPPROTO_TCP;
inline constexpr auto _TCP_NODELAY  = TCP_NODELAY;
inline constexpr auto _SHUT_WR      = SHUT_WR;
#ifdef MSG_NOSIGNAL
inline constexpr auto _MSG_NOSIGNAL = MSG_NOSIGNAL;
#else
inline constexpr auto _MSG_NOSIGNAL = 0;
#endif

inline constexpr auto _EPOLL_CLOEXEC = EPOLL_CLOEXEC;
inline constexpr auto _EPOLLIN       = EPOLLIN;
inline constexpr auto _EPOLLOUT      = EPOLLOUT;
#ifdef EPOLLRDHUP
inline constexpr auto _EPOLLRDHUP     = EPOLLRDHUP;
inline constexpr bool _HAS_EPOLLRDHUP = true;
#else
inline constexpr auto _EPOLLRDHUP     = 0;
inline constexpr bool _HAS_EPOLLRDHUP = false;
#endif
inline constexpr auto _EPOLLHUP      = EPOLLHUP;
inline constexpr auto _EPOLLERR      = EPOLLERR;
inline constexpr auto _EPOLL_CTL_ADD = EPOLL_CTL_ADD;
inline constexpr auto _EPOLL_CTL_MOD = EPOLL_CTL_MOD;
inline constexpr auto _EPOLL_CTL_DEL = EPOLL_CTL_DEL;
inline constexpr auto _TFD_NONBLOCK  = TFD_NONBLOCK;
inline constexpr auto _TFD_CLOEXEC   = TFD_CLOEXEC;

inline constexpr auto _S_IFMT   = S_IFMT;
inline constexpr auto _S_IFREG  = S_IFREG;
inline constexpr auto _S_IFDIR  = S_IFDIR;
inline constexpr auto _S_IFLNK  = S_IFLNK;
inline constexpr auto _S_IFIFO  = S_IFIFO;
inline constexpr auto _S_IFBLK  = S_IFBLK;
inline constexpr auto _S_IFCHR  = S_IFCHR;
inline constexpr auto _S_IFSOCK = S_IFSOCK;

inline constexpr auto _DT_REG  = DT_REG;
inline constexpr auto _DT_DIR  = DT_DIR;
inline constexpr auto _DT_LNK  = DT_LNK;
inline constexpr auto _DT_FIFO = DT_FIFO;
inline constexpr auto _DT_BLK  = DT_BLK;
inline constexpr auto _DT_CHR  = DT_CHR;
inline constexpr auto _DT_SOCK = DT_SOCK;

inline constexpr auto _LOCK_SH = LOCK_SH;
inline constexpr auto _LOCK_EX = LOCK_EX;
inline constexpr auto _LOCK_NB = LOCK_NB;
inline constexpr auto _LOCK_UN = LOCK_UN;

inline constexpr auto _UTIME_OMIT = UTIME_OMIT;

#undef SYS_futex
#undef FUTEX_WAIT_BITSET
#undef FUTEX_PRIVATE_FLAG
#undef FUTEX_WAKE
#undef FUTEX_BITSET_MATCH_ANY
#undef CLOCK_MONOTONIC
#undef CLOCK_REALTIME
#undef ENOENT
#undef EACCES
#undef EPERM
#undef ECONNREFUSED
#undef ECONNRESET
#undef EHOSTUNREACH
#undef ENETUNREACH
#undef ECONNABORTED
#undef ENOTCONN
#undef EADDRINUSE
#undef EADDRNOTAVAIL
#undef ENETDOWN
#undef EPIPE
#undef EEXIST
#undef EAGAIN
#undef EWOULDBLOCK
#undef ENOTDIR
#undef EISDIR
#undef ENOTEMPTY
#undef EROFS
#undef ELOOP
#undef ESTALE
#undef EDQUOT
#undef ETIMEDOUT
#undef EINVAL
#undef ENOSPC
#undef ESPIPE
#undef EFBIG
#undef EBUSY
#undef ETXTBSY
#undef EDEADLK
#undef EXDEV
#undef EMLINK
#undef ENAMETOOLONG
#undef E2BIG
#undef EINTR
#undef ENOSYS
#undef EOPNOTSUPP
#undef EAFNOSUPPORT
#undef ENOMEM
#undef ENOBUFS
#undef EIO
#undef EINPROGRESS
#undef SIGKILL
#undef O_CLOEXEC
#undef O_NONBLOCK
#undef O_RDONLY
#undef O_WRONLY
#undef O_RDWR
#undef O_CREAT
#undef O_EXCL
#undef O_TRUNC
#undef O_APPEND
#undef O_NOFOLLOW
#undef O_DIRECTORY
#undef F_DUPFD_CLOEXEC
#undef F_GETFL
#undef F_SETFL
#undef F_GETFD
#undef F_SETFD
#undef FD_CLOEXEC
#undef SEEK_SET
#undef SEEK_CUR
#undef SEEK_END
#undef AF_INET
#undef AF_INET6
#undef SOCK_STREAM
#undef SOL_SOCKET
#undef SO_REUSEADDR
#undef SO_ERROR
#undef IPPROTO_TCP
#undef TCP_NODELAY
#undef SHUT_WR
#undef MSG_NOSIGNAL
#undef EPOLL_CLOEXEC
#undef EPOLLIN
#undef EPOLLOUT
#undef EPOLLRDHUP
#undef EPOLLHUP
#undef EPOLLERR
#undef EPOLL_CTL_ADD
#undef EPOLL_CTL_MOD
#undef EPOLL_CTL_DEL
#undef TFD_NONBLOCK
#undef TFD_CLOEXEC
#undef S_IFMT
#undef S_IFREG
#undef S_IFDIR
#undef S_IFLNK
#undef S_IFIFO
#undef S_IFBLK
#undef S_IFCHR
#undef S_IFSOCK
#undef DT_REG
#undef DT_DIR
#undef DT_LNK
#undef DT_FIFO
#undef DT_BLK
#undef DT_CHR
#undef DT_SOCK
#undef LOCK_SH
#undef LOCK_EX
#undef LOCK_NB
#undef LOCK_UN
#undef UTIME_OMIT

inline auto _rstd_make_dev(unsigned int ma, unsigned int mi) noexcept -> ::dev_t {
    return makedev(ma, mi);
}
inline auto _rstd_dev_major(::dev_t d) noexcept -> unsigned int {
    return major(d);
}
inline auto _rstd_dev_minor(::dev_t d) noexcept -> unsigned int {
    return minor(d);
}
#undef makedev
#undef major
#undef minor

export namespace rstd::sys::libc
{

using ::syscall;
using ::sched_yield;
using ::posix_memalign;
using ::htons;
using ::htonl;
using ::ntohs;

inline constexpr auto SYS_futex              = _SYS_futex;
inline constexpr auto FUTEX_WAIT_BITSET      = _FUTEX_WAIT_BITSET;
inline constexpr auto FUTEX_PRIVATE_FLAG     = _FUTEX_PRIVATE_FLAG;
inline constexpr auto FUTEX_WAKE             = _FUTEX_WAKE;
inline constexpr auto FUTEX_BITSET_MATCH_ANY = _FUTEX_BITSET_MATCH_ANY;

using ::clock_gettime;
using ::nanosleep;
using ::gmtime_r;
inline constexpr auto CLOCK_MONOTONIC = _CLOCK_MONOTONIC;
inline constexpr auto CLOCK_REALTIME  = _CLOCK_REALTIME;
inline constexpr auto EAGAIN          = _EAGAIN;
inline constexpr auto ENOENT          = _ENOENT;
inline constexpr auto EACCES          = _EACCES;
inline constexpr auto EPERM           = _EPERM;
inline constexpr auto ECONNREFUSED    = _ECONNREFUSED;
inline constexpr auto ECONNRESET      = _ECONNRESET;
inline constexpr auto EHOSTUNREACH    = _EHOSTUNREACH;
inline constexpr auto ENETUNREACH     = _ENETUNREACH;
inline constexpr auto ECONNABORTED    = _ECONNABORTED;
inline constexpr auto ENOTCONN        = _ENOTCONN;
inline constexpr auto EADDRINUSE      = _EADDRINUSE;
inline constexpr auto EADDRNOTAVAIL   = _EADDRNOTAVAIL;
inline constexpr auto ENETDOWN        = _ENETDOWN;
inline constexpr auto EPIPE           = _EPIPE;
inline constexpr auto EEXIST          = _EEXIST;
inline constexpr auto EWOULDBLOCK     = _EWOULDBLOCK;
inline constexpr auto ENOTDIR         = _ENOTDIR;
inline constexpr auto EISDIR          = _EISDIR;
inline constexpr auto ENOTEMPTY       = _ENOTEMPTY;
inline constexpr auto EROFS           = _EROFS;
inline constexpr auto ELOOP           = _ELOOP;
inline constexpr auto ESTALE          = _ESTALE;
inline constexpr auto HAS_ESTALE      = _HAS_ESTALE;
inline constexpr auto EINVAL          = _EINVAL;
inline constexpr auto ETIMEDOUT       = _ETIMEDOUT;
inline constexpr auto ENOSPC          = _ENOSPC;
inline constexpr auto ESPIPE          = _ESPIPE;
inline constexpr auto EDQUOT          = _EDQUOT;
inline constexpr auto HAS_EDQUOT      = _HAS_EDQUOT;
inline constexpr auto EFBIG           = _EFBIG;
inline constexpr auto EBUSY           = _EBUSY;
inline constexpr auto ETXTBSY         = _ETXTBSY;
inline constexpr auto EDEADLK         = _EDEADLK;
inline constexpr auto EXDEV           = _EXDEV;
inline constexpr auto EMLINK          = _EMLINK;
inline constexpr auto ENAMETOOLONG    = _ENAMETOOLONG;
inline constexpr auto E2BIG           = _E2BIG;
inline constexpr auto EINTR           = _EINTR;
inline constexpr auto ENOSYS          = _ENOSYS;
inline constexpr auto EOPNOTSUPP      = _EOPNOTSUPP;
inline constexpr auto EAFNOSUPPORT    = _EAFNOSUPPORT;
inline constexpr auto ENOMEM          = _ENOMEM;
inline constexpr auto ENOBUFS         = _ENOBUFS;
inline constexpr auto EIO             = _EIO;
inline constexpr auto EINPROGRESS     = _EINPROGRESS;

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
using ::environ;
using ::mkstemp;
using ::mkdtemp;
using ::waitpid;
using ::pipe;
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
using ::socket;
using ::setsockopt;
using ::bind;
using ::listen;
using ::connect;
using ::accept;
using ::recv;
using ::send;
using ::shutdown;
using ::getsockopt;
using ::getsockname;
using ::getpeername;
using ::epoll_create1;
using ::epoll_ctl;
using ::epoll_wait;
using ::timerfd_create;
using ::timerfd_settime;

// ── Type aliases ─────────────────────────────────────────────────────────
using ::mode_t;
using ::off_t;
using ::ssize_t;
using ::time_t;
using ::dev_t;
using ::DIR;
using ::dirent;
using ::sockaddr;
using ::sockaddr_storage;
using ::sockaddr_in;
using ::sockaddr_in6;
using ::socklen_t;
/// `struct stat` aliased to avoid clash with the `::stat()` function.
using stat_t = struct ::stat;
/// `struct timespec` aliased to avoid the `struct` keyword leaking into call sites.
using timespec_t   = struct ::timespec;
using itimerspec_t = struct ::itimerspec;
using epoll_event  = struct ::epoll_event;

inline constexpr auto SIGKILL    = _SIGKILL;
inline constexpr auto O_CLOEXEC  = _O_CLOEXEC;
inline constexpr auto O_NONBLOCK = _O_NONBLOCK;

// ── Open flags / seek whence ─────────────────────────────────────────────
inline constexpr auto O_RDONLY        = _O_RDONLY;
inline constexpr auto O_WRONLY        = _O_WRONLY;
inline constexpr auto O_RDWR          = _O_RDWR;
inline constexpr auto O_CREAT         = _O_CREAT;
inline constexpr auto O_EXCL          = _O_EXCL;
inline constexpr auto O_TRUNC         = _O_TRUNC;
inline constexpr auto O_APPEND        = _O_APPEND;
inline constexpr auto O_NOFOLLOW      = _O_NOFOLLOW;
inline constexpr auto O_DIRECTORY     = _O_DIRECTORY;
inline constexpr auto F_DUPFD_CLOEXEC = _F_DUPFD_CLOEXEC;
inline constexpr auto F_GETFL         = _F_GETFL;
inline constexpr auto F_SETFL         = _F_SETFL;
inline constexpr auto F_GETFD         = _F_GETFD;
inline constexpr auto F_SETFD         = _F_SETFD;
inline constexpr auto FD_CLOEXEC      = _FD_CLOEXEC;
inline constexpr auto SEEK_SET        = _SEEK_SET;
inline constexpr auto SEEK_CUR        = _SEEK_CUR;
inline constexpr auto SEEK_END        = _SEEK_END;

// ── Sockets / epoll ─────────────────────────────────────────────────────
inline constexpr auto AF_INET      = _AF_INET;
inline constexpr auto AF_INET6     = _AF_INET6;
inline constexpr auto SOCK_STREAM  = _SOCK_STREAM;
inline constexpr auto SOL_SOCKET   = _SOL_SOCKET;
inline constexpr auto SO_REUSEADDR = _SO_REUSEADDR;
inline constexpr auto SO_ERROR     = _SO_ERROR;
[[maybe_unused]]
inline constexpr auto IPPROTO_TCP = _IPPROTO_TCP;
inline constexpr auto TCP_NODELAY = _TCP_NODELAY;
[[maybe_unused]]
inline constexpr auto SHUT_WR = _SHUT_WR;
[[maybe_unused]]
inline constexpr auto MSG_NOSIGNAL = _MSG_NOSIGNAL;

[[maybe_unused]]
inline constexpr auto EPOLL_CLOEXEC  = _EPOLL_CLOEXEC;
inline constexpr auto EPOLLIN        = _EPOLLIN;
inline constexpr auto EPOLLOUT       = _EPOLLOUT;
inline constexpr auto EPOLLRDHUP     = _EPOLLRDHUP;
inline constexpr auto HAS_EPOLLRDHUP = _HAS_EPOLLRDHUP;
inline constexpr auto EPOLLHUP       = _EPOLLHUP;
inline constexpr auto EPOLLERR       = _EPOLLERR;
inline constexpr auto EPOLL_CTL_ADD  = _EPOLL_CTL_ADD;
inline constexpr auto EPOLL_CTL_MOD  = _EPOLL_CTL_MOD;
inline constexpr auto EPOLL_CTL_DEL  = _EPOLL_CTL_DEL;
[[maybe_unused]]
inline constexpr auto TFD_NONBLOCK = _TFD_NONBLOCK;
[[maybe_unused]]
inline constexpr auto TFD_CLOEXEC = _TFD_CLOEXEC;

// ── Stat mode masks ──────────────────────────────────────────────────────
inline constexpr auto S_IFMT   = _S_IFMT;
inline constexpr auto S_IFREG  = _S_IFREG;
inline constexpr auto S_IFDIR  = _S_IFDIR;
inline constexpr auto S_IFLNK  = _S_IFLNK;
inline constexpr auto S_IFIFO  = _S_IFIFO;
inline constexpr auto S_IFBLK  = _S_IFBLK;
inline constexpr auto S_IFCHR  = _S_IFCHR;
inline constexpr auto S_IFSOCK = _S_IFSOCK;

// ── Dirent type tags ─────────────────────────────────────────────────────
[[maybe_unused]]
inline constexpr auto DT_REG = _DT_REG;
[[maybe_unused]]
inline constexpr auto DT_DIR = _DT_DIR;
[[maybe_unused]]
inline constexpr auto DT_LNK = _DT_LNK;
[[maybe_unused]]
inline constexpr auto DT_FIFO = _DT_FIFO;
[[maybe_unused]]
inline constexpr auto DT_BLK = _DT_BLK;
[[maybe_unused]]
inline constexpr auto DT_CHR = _DT_CHR;
[[maybe_unused]]
inline constexpr auto DT_SOCK = _DT_SOCK;

// ── flock ────────────────────────────────────────────────────────────────
inline constexpr auto LOCK_SH = _LOCK_SH;
inline constexpr auto LOCK_EX = _LOCK_EX;
inline constexpr auto LOCK_NB = _LOCK_NB;
inline constexpr auto LOCK_UN = _LOCK_UN;

// ── utimensat sentinel ───────────────────────────────────────────────────
inline constexpr auto UTIME_OMIT = _UTIME_OMIT;

/// Returns an lvalue reference to the platform `errno`. Use to read and write.
inline auto get_errno() noexcept -> int& {
    return errno;
}

inline auto makedev(unsigned int ma, unsigned int mi) noexcept -> ::dev_t {
    return _rstd_make_dev(ma, mi);
}
inline auto major(::dev_t d) noexcept -> unsigned int {
    return _rstd_dev_major(d);
}
inline auto minor(::dev_t d) noexcept -> unsigned int {
    return _rstd_dev_minor(d);
}

inline auto wait_exited(int status) -> bool {
    return WIFEXITED(status);
}
inline auto wait_exitstatus(int status) -> int {
    return WEXITSTATUS(status);
}
inline auto wait_signaled(int status) -> bool {
    return WIFSIGNALED(status);
}
inline auto wait_termsig(int status) -> int {
    return WTERMSIG(status);
}

} // namespace rstd::sys::libc
#endif
