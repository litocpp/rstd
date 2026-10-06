module;
#include <rstd/macro.hpp>
#if RSTD_OS_LINUX
#include <errno.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <unistd.h>
#if __has_include(<sys/pidfd.h>)
#include <sys/pidfd.h>
#elif __has_include(<linux/pidfd.h>)
#include <linux/pidfd.h>
#endif
#endif

export module rstd:sys.process.linux;

#if RSTD_OS_LINUX
export namespace rstd::sys::process
{

inline auto pidfd_channel(int descriptors[2]) -> int {
    return ::socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, descriptors);
}

// Acquire the descriptor in the child before exec, never by reopening its PID in the parent.
inline auto send_current_pidfd(int socket) -> int {
    int fd = -1;
#ifdef SYS_pidfd_open
    fd = static_cast<int>(::syscall(SYS_pidfd_open, ::getpid(), 0));
#endif
    char                                  payload = 0;
    struct iovec                          bytes { &payload, 1 };
    alignas(struct cmsghdr) unsigned char control[CMSG_SPACE(sizeof(int))] {};
    struct msghdr                         message {};
    message.msg_iov    = &bytes;
    message.msg_iovlen = 1;
    if (fd >= 0) {
        message.msg_control    = control;
        message.msg_controllen = sizeof(control);
        auto* header           = CMSG_FIRSTHDR(&message);
        header->cmsg_level     = SOL_SOCKET;
        header->cmsg_type      = SCM_RIGHTS;
        header->cmsg_len       = CMSG_LEN(sizeof(int));
        __builtin_memcpy(CMSG_DATA(header), &fd, sizeof(fd));
    }
    ssize_t sent;
    do {
        sent = ::sendmsg(socket, &message, MSG_NOSIGNAL);
    } while (sent < 0 && errno == EINTR);
    auto error = sent == 1 ? 0 : (sent < 0 ? errno : EIO);
    if (fd >= 0) ::close(fd);
    return error;
}

inline auto receive_pidfd(int socket, int& fd) -> int {
    fd = -1;
    char                                  payload;
    struct iovec                          bytes { &payload, 1 };
    alignas(struct cmsghdr) unsigned char control[CMSG_SPACE(sizeof(int))] {};
    struct msghdr                         message {};
    message.msg_iov        = &bytes;
    message.msg_iovlen     = 1;
    message.msg_control    = control;
    message.msg_controllen = sizeof(control);
    ssize_t received;
    do {
        received = ::recvmsg(socket, &message, MSG_CMSG_CLOEXEC);
    } while (received < 0 && errno == EINTR);
    if (received < 0) return errno;
    if (auto* header = CMSG_FIRSTHDR(&message); header && header->cmsg_level == SOL_SOCKET &&
                                                header->cmsg_type == SCM_RIGHTS &&
                                                header->cmsg_len == CMSG_LEN(sizeof(int)))
        __builtin_memcpy(&fd, CMSG_DATA(header), sizeof(fd));
    if (received != 1 || (message.msg_flags & (MSG_CTRUNC | MSG_TRUNC))) {
        if (fd >= 0) ::close(fd);
        fd = -1;
        return EIO;
    }
    return 0;
}

inline auto pidfd_send_signal(int fd, int signal, bool group) -> int {
#ifdef SYS_pidfd_send_signal
    // PIDFD_SIGNAL_PROCESS_GROUP is available since Linux 6.9.
    auto flags = group ? 4U : 0U;
    if (::syscall(SYS_pidfd_send_signal, fd, signal, nullptr, flags) == 0) return 0;
    return errno;
#else
    return ENOSYS;
#endif
}

struct PidFdWait {
    bool available {};
    bool signaled {};
    int  status {};
    int  error {};
};

inline auto pidfd_wait(int fd, bool nonblocking) -> PidFdWait {
    // Linux's P_PIDFD ABI value is absent from older libc headers.
    constexpr auto pidfd_idtype = static_cast<idtype_t>(3);
    siginfo_t      info {};
    int            result;
    do {
        result = ::waitid(
            pidfd_idtype, static_cast<id_t>(fd), &info, WEXITED | (nonblocking ? WNOHANG : 0));
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        auto error = errno;
#if defined(PIDFD_GET_INFO) && defined(PIDFD_INFO_EXIT)
        if (error == ECHILD) {
            struct pidfd_info saved {};
            saved.mask = PIDFD_INFO_EXIT;
            if (::ioctl(fd, PIDFD_GET_INFO, &saved) == 0) {
                auto status = saved.exit_code;
                return { true,
                         ! WIFEXITED(status),
                         WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status),
                         0 };
            }
        }
#endif
        return { false, false, 0, error };
    }
    return { info.si_pid != 0, info.si_code != CLD_EXITED, info.si_status, 0 };
}

} // namespace rstd::sys::process
#endif
