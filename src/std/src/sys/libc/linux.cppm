module;
#include <rstd/macro.hpp>

#if RSTD_OS_LINUX
#include <linux/futex.h>
#include <linux/io_uring.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/timerfd.h>
#include <unistd.h>

inline constexpr auto _SYS_futex              = SYS_futex;
inline constexpr auto _SYS_io_uring_setup     = SYS_io_uring_setup;
inline constexpr auto _SYS_io_uring_enter     = SYS_io_uring_enter;
inline constexpr auto _SYS_io_uring_register  = SYS_io_uring_register;
inline constexpr auto _FUTEX_WAIT_BITSET      = FUTEX_WAIT_BITSET;
inline constexpr auto _FUTEX_PRIVATE_FLAG     = FUTEX_PRIVATE_FLAG;
inline constexpr auto _FUTEX_WAKE             = FUTEX_WAKE;
inline constexpr auto _FUTEX_BITSET_MATCH_ANY = FUTEX_BITSET_MATCH_ANY;

inline constexpr auto _EPOLL_CLOEXEC = EPOLL_CLOEXEC;
inline constexpr auto _EPOLLIN       = EPOLLIN;
inline constexpr auto _EPOLLOUT      = EPOLLOUT;
inline constexpr auto _EPOLLONESHOT  = EPOLLONESHOT;
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
inline constexpr auto _EFD_NONBLOCK  = EFD_NONBLOCK;
inline constexpr auto _EFD_CLOEXEC   = EFD_CLOEXEC;
inline constexpr auto _TFD_NONBLOCK  = TFD_NONBLOCK;
inline constexpr auto _TFD_CLOEXEC   = TFD_CLOEXEC;
inline constexpr auto _MAP_POPULATE  = MAP_POPULATE;

inline constexpr auto _IORING_OFF_SQ_RING      = IORING_OFF_SQ_RING;
inline constexpr auto _IORING_OFF_CQ_RING      = IORING_OFF_CQ_RING;
inline constexpr auto _IORING_OFF_SQES         = IORING_OFF_SQES;
inline constexpr auto _IORING_FEAT_SINGLE_MMAP = IORING_FEAT_SINGLE_MMAP;
inline constexpr auto _IO_URING_OP_SUPPORTED   = IO_URING_OP_SUPPORTED;

#undef SYS_futex
#undef SYS_io_uring_setup
#undef SYS_io_uring_enter
#undef SYS_io_uring_register
#undef FUTEX_WAIT_BITSET
#undef FUTEX_PRIVATE_FLAG
#undef FUTEX_WAKE
#undef FUTEX_BITSET_MATCH_ANY
#undef EPOLL_CLOEXEC
#undef EPOLLIN
#undef EPOLLOUT
#undef EPOLLONESHOT
#undef EPOLLRDHUP
#undef EPOLLHUP
#undef EPOLLERR
#undef EPOLL_CTL_ADD
#undef EPOLL_CTL_MOD
#undef EPOLL_CTL_DEL
#undef EFD_NONBLOCK
#undef EFD_CLOEXEC
#undef TFD_NONBLOCK
#undef TFD_CLOEXEC
#undef MAP_POPULATE
#undef IORING_OFF_SQ_RING
#undef IORING_OFF_CQ_RING
#undef IORING_OFF_SQES
#undef IORING_FEAT_SINGLE_MMAP
#undef IO_URING_OP_SUPPORTED
#endif

export module rstd:sys.libc.linux;

#if RSTD_OS_LINUX
export namespace rstd::sys::libc
{

using ::syscall;

inline constexpr auto SYS_futex              = _SYS_futex;
inline constexpr auto FUTEX_WAIT_BITSET      = _FUTEX_WAIT_BITSET;
inline constexpr auto FUTEX_PRIVATE_FLAG     = _FUTEX_PRIVATE_FLAG;
inline constexpr auto FUTEX_WAKE             = _FUTEX_WAKE;
inline constexpr auto FUTEX_BITSET_MATCH_ANY = _FUTEX_BITSET_MATCH_ANY;

using epoll_event = struct ::epoll_event;
using ::epoll_create1;
using ::epoll_ctl;
using ::epoll_wait;
using ::eventfd;
using ::timerfd_create;
using ::timerfd_settime;

[[maybe_unused]]
inline constexpr auto EPOLL_CLOEXEC  = _EPOLL_CLOEXEC;
inline constexpr auto EPOLLIN        = _EPOLLIN;
inline constexpr auto EPOLLOUT       = _EPOLLOUT;
inline constexpr auto EPOLLONESHOT   = _EPOLLONESHOT;
inline constexpr auto EPOLLRDHUP     = _EPOLLRDHUP;
inline constexpr auto HAS_EPOLLRDHUP = _HAS_EPOLLRDHUP;
inline constexpr auto EPOLLHUP       = _EPOLLHUP;
inline constexpr auto EPOLLERR       = _EPOLLERR;
inline constexpr auto EPOLL_CTL_ADD  = _EPOLL_CTL_ADD;
inline constexpr auto EPOLL_CTL_MOD  = _EPOLL_CTL_MOD;
inline constexpr auto EPOLL_CTL_DEL  = _EPOLL_CTL_DEL;
[[maybe_unused]]
inline constexpr auto EFD_NONBLOCK = _EFD_NONBLOCK;
[[maybe_unused]]
inline constexpr auto EFD_CLOEXEC = _EFD_CLOEXEC;
[[maybe_unused]]
inline constexpr auto TFD_NONBLOCK = _TFD_NONBLOCK;
[[maybe_unused]]
inline constexpr auto TFD_CLOEXEC  = _TFD_CLOEXEC;
inline constexpr auto MAP_POPULATE = _MAP_POPULATE;

using io_uring_sqe      = struct ::io_uring_sqe;
using io_uring_cqe      = struct ::io_uring_cqe;
using io_uring_params   = struct ::io_uring_params;
using io_uring_probe    = struct ::io_uring_probe;
using io_uring_probe_op = struct ::io_uring_probe_op;

inline constexpr auto IORING_OP_READ          = ::IORING_OP_READ;
inline constexpr auto IORING_OP_WRITE         = ::IORING_OP_WRITE;
inline constexpr auto IORING_OP_RECV          = ::IORING_OP_RECV;
inline constexpr auto IORING_OP_SEND          = ::IORING_OP_SEND;
inline constexpr auto IORING_OP_CONNECT       = ::IORING_OP_CONNECT;
inline constexpr auto IORING_OP_ACCEPT        = ::IORING_OP_ACCEPT;
inline constexpr auto IORING_OP_ASYNC_CANCEL  = ::IORING_OP_ASYNC_CANCEL;
inline constexpr auto IORING_REGISTER_EVENTFD = ::IORING_REGISTER_EVENTFD;
inline constexpr auto IORING_REGISTER_PROBE   = ::IORING_REGISTER_PROBE;
inline constexpr auto IORING_OFF_SQ_RING      = _IORING_OFF_SQ_RING;
inline constexpr auto IORING_OFF_CQ_RING      = _IORING_OFF_CQ_RING;
inline constexpr auto IORING_OFF_SQES         = _IORING_OFF_SQES;
inline constexpr auto IORING_FEAT_SINGLE_MMAP = _IORING_FEAT_SINGLE_MMAP;
inline constexpr auto IO_URING_OP_SUPPORTED   = _IO_URING_OP_SUPPORTED;

inline auto io_uring_setup(unsigned int entries, io_uring_params* params) -> long {
    return ::syscall(_SYS_io_uring_setup, entries, params);
}

inline auto io_uring_enter(int           fd,
                           unsigned int  to_submit,
                           unsigned int  min_complete,
                           unsigned int  flags,
                           void*         signal_mask,
                           unsigned long signal_mask_size) -> long {
    return ::syscall(
        _SYS_io_uring_enter, fd, to_submit, min_complete, flags, signal_mask, signal_mask_size);
}

inline auto io_uring_register(int fd, unsigned int opcode, void* argument, unsigned int count)
    -> long {
    return ::syscall(_SYS_io_uring_register, fd, opcode, argument, count);
}

} // namespace rstd::sys::libc
#endif
