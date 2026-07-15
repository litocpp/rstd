module;
#include <rstd/macro.hpp>

export module rstd:sys.socket;
export import :io.error;
export import :sys.fd;
export import :sys.libc;
export import rstd.core;

using namespace rstd::prelude;

using SocketOwnedFd   = rstd::sys::fd::OwnedFd;
using SocketRawFd     = rstd::sys::fd::RawFd;
using SocketError     = rstd::io::error::Error;
using SocketErrorKind = rstd::io::error::ErrorKind;
template<typename T>
using SocketResult    = rstd::io::Result<T>;
namespace socket_libc = rstd::sys::libc;

inline auto socket_unsupported() noexcept -> SocketError {
    return SocketError::from_kind(SocketErrorKind { SocketErrorKind::Unsupported });
}

#if RSTD_OS_UNIX
inline auto socket_last_error() noexcept -> SocketError {
    return SocketError::from_raw_os_error(rstd::sys::io::last_os_error());
}

inline auto socket_set_nonblocking(SocketRawFd fd, bool enabled) -> SocketResult<empty> {
    int flags = socket_libc::fcntl(fd, socket_libc::F_GETFL, 0);
    if (flags < 0) return rstd::Err(socket_last_error());

    if (enabled)
        flags |= socket_libc::O_NONBLOCK;
    else
        flags &= ~socket_libc::O_NONBLOCK;

    if (socket_libc::fcntl(fd, socket_libc::F_SETFL, flags) < 0)
        return rstd::Err(socket_last_error());
    return rstd::Ok(empty {});
}

inline auto socket_set_cloexec(SocketRawFd fd) -> SocketResult<empty> {
    int flags = socket_libc::fcntl(fd, socket_libc::F_GETFD, 0);
    if (flags < 0) return rstd::Err(socket_last_error());
    if (socket_libc::fcntl(fd, socket_libc::F_SETFD, flags | socket_libc::FD_CLOEXEC) < 0) {
        return rstd::Err(socket_last_error());
    }
    return rstd::Ok(empty {});
}
#endif

namespace rstd::sys::socket
{

export struct Ipv4Addr {
    u8 m_octets[4] {};

    static constexpr auto make(u8 a, u8 b, u8 c, u8 d) noexcept -> Ipv4Addr {
        return Ipv4Addr { { a, b, c, d } };
    }

    static constexpr auto loopback() noexcept -> Ipv4Addr { return make(127, 0, 0, 1); }
    static constexpr auto any() noexcept -> Ipv4Addr { return make(0, 0, 0, 0); }
};

export class SocketAddr {
#if RSTD_OS_UNIX
    socket_libc::sockaddr_storage m_storage {};
    socket_libc::socklen_t        m_len {};
#endif

    friend class Socket;

#if RSTD_OS_UNIX
    auto as_sockaddr() const noexcept -> const socket_libc::sockaddr* {
        return reinterpret_cast<const socket_libc::sockaddr*>(&m_storage);
    }

    auto as_sockaddr() noexcept -> socket_libc::sockaddr* {
        return reinterpret_cast<socket_libc::sockaddr*>(&m_storage);
    }

    auto len_ptr() noexcept -> socket_libc::socklen_t* { return &m_len; }
#endif

public:
    static auto ipv4(Ipv4Addr ip, u16 port) noexcept -> SocketAddr {
        auto out = SocketAddr {};
#if RSTD_OS_UNIX
        auto addr       = socket_libc::sockaddr_in {};
        addr.sin_family = socket_libc::AF_INET;
        addr.sin_port   = socket_libc::htons(port);
        addr.sin_addr.s_addr =
            socket_libc::htonl((u32(ip.m_octets[0]) << 24) | (u32(ip.m_octets[1]) << 16) |
                               (u32(ip.m_octets[2]) << 8) | u32(ip.m_octets[3]));
        *reinterpret_cast<socket_libc::sockaddr_in*>(&out.m_storage) = addr;
        out.m_len                                                    = sizeof(addr);
#else
        (void)ip;
        (void)port;
#endif
        return out;
    }

    static auto ipv4_loopback(u16 port) noexcept -> SocketAddr {
        return ipv4(Ipv4Addr::loopback(), port);
    }

    static auto ipv4_any(u16 port) noexcept -> SocketAddr { return ipv4(Ipv4Addr::any(), port); }

    auto family() const noexcept -> i32 {
#if RSTD_OS_UNIX
        return as_sockaddr()->sa_family;
#else
        return 0;
#endif
    }

    auto port() const noexcept -> u16 {
#if RSTD_OS_UNIX
        if (family() == socket_libc::AF_INET) {
            auto const& addr = *reinterpret_cast<const socket_libc::sockaddr_in*>(&m_storage);
            return socket_libc::ntohs(addr.sin_port);
        }
#endif
        return 0;
    }

#if RSTD_OS_UNIX
    static auto from_native(const socket_libc::sockaddr* addr, socket_libc::socklen_t len)
        -> SocketResult<SocketAddr> {
        if (addr == nullptr) {
            return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
        }

        auto out = SocketAddr {};
        if (addr->sa_family == socket_libc::AF_INET) {
            if (len < sizeof(socket_libc::sockaddr_in)) {
                return Err(
                    SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
            }
            *reinterpret_cast<socket_libc::sockaddr_in*>(&out.m_storage) =
                *reinterpret_cast<const socket_libc::sockaddr_in*>(addr);
            out.m_len = sizeof(socket_libc::sockaddr_in);
            return Ok(rstd::move(out));
        }

        if (addr->sa_family == socket_libc::AF_INET6) {
            if (len < sizeof(socket_libc::sockaddr_in6)) {
                return Err(
                    SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
            }
            *reinterpret_cast<socket_libc::sockaddr_in6*>(&out.m_storage) =
                *reinterpret_cast<const socket_libc::sockaddr_in6*>(addr);
            out.m_len = sizeof(socket_libc::sockaddr_in6);
            return Ok(rstd::move(out));
        }

        return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::Unsupported }));
    }
#endif
};

export class Socket {
    SocketOwnedFd m_fd;

    explicit Socket(SocketOwnedFd fd) noexcept: m_fd(rstd::move(fd)) {}

public:
    Socket(const Socket&)                        = delete;
    auto operator=(const Socket&)                = delete;
    Socket(Socket&&) noexcept                    = default;
    auto operator=(Socket&&) noexcept -> Socket& = default;

    static auto from_owned_fd(SocketOwnedFd fd) noexcept -> Socket {
        return Socket { rstd::move(fd) };
    }

    auto into_owned_fd() noexcept -> SocketOwnedFd { return rstd::move(m_fd); }
    auto as_raw_fd() const noexcept -> SocketRawFd { return m_fd.as_raw_fd(); }

    auto set_nonblocking(bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        return socket_set_nonblocking(as_raw_fd(), enabled);
#else
        (void)enabled;
        return Err(socket_unsupported());
#endif
    }

    static auto tcp(SocketAddr const& addr) -> SocketResult<Socket> {
#if RSTD_OS_UNIX
        int raw = socket_libc::socket(addr.family(), socket_libc::SOCK_STREAM, 0);
        if (raw < 0) return Err(socket_last_error());

        auto fd      = SocketOwnedFd::from_raw_fd(raw);
        auto cloexec = socket_set_cloexec(raw);
        if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

        auto nonblock = socket_set_nonblocking(raw, true);
        if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

        return Ok(Socket { rstd::move(fd) });
#else
        (void)addr;
        return Err(socket_unsupported());
#endif
    }

    auto set_reuseaddr(bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        int value = enabled ? 1 : 0;
        if (socket_libc::setsockopt(as_raw_fd(),
                                    socket_libc::SOL_SOCKET,
                                    socket_libc::SO_REUSEADDR,
                                    &value,
                                    sizeof(value)) < 0) {
            return Err(socket_last_error());
        }
        return Ok(empty {});
#else
        (void)enabled;
        return Err(socket_unsupported());
#endif
    }

    auto set_nodelay(bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        int value = enabled ? 1 : 0;
        if (socket_libc::setsockopt(as_raw_fd(),
                                    socket_libc::IPPROTO_TCP,
                                    socket_libc::TCP_NODELAY,
                                    &value,
                                    sizeof(value)) < 0) {
            return Err(socket_last_error());
        }
        return Ok(empty {});
#else
        (void)enabled;
        return Err(socket_unsupported());
#endif
    }

    auto bind(SocketAddr const& addr) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        if (socket_libc::bind(as_raw_fd(), addr.as_sockaddr(), addr.m_len) < 0) {
            return Err(socket_last_error());
        }
        return Ok(empty {});
#else
        (void)addr;
        return Err(socket_unsupported());
#endif
    }

    auto listen(i32 backlog = 128) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        if (socket_libc::listen(as_raw_fd(), backlog) < 0) {
            return Err(socket_last_error());
        }
        return Ok(empty {});
#else
        (void)backlog;
        return Err(socket_unsupported());
#endif
    }

    auto connect(SocketAddr const& addr) -> SocketResult<empty> {
#if RSTD_OS_UNIX
        if (socket_libc::connect(as_raw_fd(), addr.as_sockaddr(), addr.m_len) < 0) {
            return Err(socket_last_error());
        }
        return Ok(empty {});
#else
        (void)addr;
        return Err(socket_unsupported());
#endif
    }

    auto accept() -> SocketResult<tuple<Socket, SocketAddr>> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        int raw    = socket_libc::accept(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr());
        if (raw < 0) return Err(socket_last_error());

        auto fd      = SocketOwnedFd::from_raw_fd(raw);
        auto cloexec = socket_set_cloexec(raw);
        if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

        auto nonblock = socket_set_nonblocking(raw, true);
        if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

        return Ok(tuple<Socket, SocketAddr> { Socket { rstd::move(fd) }, rstd::move(addr) });
#else
        return Err(socket_unsupported());
#endif
    }

    auto recv(u8* buf, usize len) -> SocketResult<usize> {
#if RSTD_OS_UNIX
        auto n = socket_libc::recv(as_raw_fd(), buf, len, 0);
        if (n < 0) return Err(socket_last_error());
        return Ok(usize(n));
#else
        (void)buf;
        (void)len;
        return Err(socket_unsupported());
#endif
    }

    auto send(const u8* buf, usize len) -> SocketResult<usize> {
#if RSTD_OS_UNIX
        auto n = socket_libc::send(as_raw_fd(), buf, len, socket_libc::MSG_NOSIGNAL);
        if (n < 0) return Err(socket_last_error());
        return Ok(usize(n));
#else
        (void)buf;
        (void)len;
        return Err(socket_unsupported());
#endif
    }

    auto shutdown_write() -> SocketResult<empty> {
#if RSTD_OS_UNIX
        if (socket_libc::shutdown(as_raw_fd(), socket_libc::SHUT_WR) < 0)
            return Err(socket_last_error());
        return Ok(empty {});
#else
        return Err(socket_unsupported());
#endif
    }

    auto take_error() -> SocketResult<Option<SocketError>> {
#if RSTD_OS_UNIX
        int                    value = 0;
        socket_libc::socklen_t len   = sizeof(value);
        if (socket_libc::getsockopt(
                as_raw_fd(), socket_libc::SOL_SOCKET, socket_libc::SO_ERROR, &value, &len) < 0) {
            return Err(socket_last_error());
        }
        if (value == 0) return Ok(Option<SocketError> {});
        return Ok(Some(SocketError::from_raw_os_error(value)));
#else
        return Err(socket_unsupported());
#endif
    }

    auto local_addr() const -> SocketResult<SocketAddr> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        if (socket_libc::getsockname(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr()) < 0) {
            return Err(socket_last_error());
        }
        return Ok(rstd::move(addr));
#else
        return Err(socket_unsupported());
#endif
    }

    auto peer_addr() const -> SocketResult<SocketAddr> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        if (socket_libc::getpeername(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr()) < 0) {
            return Err(socket_last_error());
        }
        return Ok(rstd::move(addr));
#else
        return Err(socket_unsupported());
#endif
    }
};

} // namespace rstd::sys::socket
