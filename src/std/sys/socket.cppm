module;
#include <rstd/macro.hpp>

export module rstd:sys.socket;
export import :io.error;
export import :sys.fd;
export import :sys.libc;
export import rstd.core;

namespace rstd::sys::socket
{

using fd::OwnedFd;
using fd::RawFd;
using Error     = ::rstd::io::error::Error;
using ErrorKind = ::rstd::io::error::ErrorKind;
template<typename T>
using Result   = ::rstd::io::Result<T>;
namespace libc = rstd::sys::libc;

namespace detail
{

inline auto unsupported() noexcept -> Error {
    return Error::from_kind(ErrorKind { ErrorKind::Unsupported });
}

#if RSTD_OS_UNIX
inline auto last_error() noexcept -> Error {
    return Error::from_raw_os_error(sys::io::last_os_error());
}

inline auto set_nonblocking(RawFd fd, bool enabled) -> Result<empty> {
    int flags = libc::fcntl(fd, libc::F_GETFL, 0);
    if (flags < 0) return Err(last_error());

    if (enabled)
        flags |= libc::O_NONBLOCK;
    else
        flags &= ~libc::O_NONBLOCK;

    if (libc::fcntl(fd, libc::F_SETFL, flags) < 0) return Err(last_error());
    return Ok(empty {});
}

inline auto set_cloexec(RawFd fd) -> Result<empty> {
    int flags = libc::fcntl(fd, libc::F_GETFD, 0);
    if (flags < 0) return Err(last_error());
    if (libc::fcntl(fd, libc::F_SETFD, flags | libc::FD_CLOEXEC) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}
#endif

} // namespace detail

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
    libc::sockaddr_storage m_storage {};
    libc::socklen_t        m_len {};
#endif

    friend class Socket;

#if RSTD_OS_UNIX
    auto as_sockaddr() const noexcept -> const libc::sockaddr* {
        return reinterpret_cast<const libc::sockaddr*>(&m_storage);
    }

    auto as_sockaddr() noexcept -> libc::sockaddr* {
        return reinterpret_cast<libc::sockaddr*>(&m_storage);
    }

    auto len_ptr() noexcept -> libc::socklen_t* { return &m_len; }
#endif

public:
    static auto ipv4(Ipv4Addr ip, u16 port) noexcept -> SocketAddr {
        auto out = SocketAddr {};
#if RSTD_OS_UNIX
        auto addr       = libc::sockaddr_in {};
        addr.sin_family = libc::AF_INET;
        addr.sin_port   = libc::htons(port);
        addr.sin_addr.s_addr =
            libc::htonl((u32(ip.m_octets[0]) << 24) | (u32(ip.m_octets[1]) << 16) |
                        (u32(ip.m_octets[2]) << 8) | u32(ip.m_octets[3]));
        *reinterpret_cast<libc::sockaddr_in*>(&out.m_storage) = addr;
        out.m_len                                             = sizeof(addr);
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
        if (family() == libc::AF_INET) {
            auto const& addr = *reinterpret_cast<const libc::sockaddr_in*>(&m_storage);
            return libc::ntohs(addr.sin_port);
        }
#endif
        return 0;
    }

#if RSTD_OS_UNIX
    static auto from_native(const libc::sockaddr* addr, libc::socklen_t len) -> Result<SocketAddr> {
        if (addr == nullptr) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }

        auto out = SocketAddr {};
        if (addr->sa_family == libc::AF_INET) {
            if (len < sizeof(libc::sockaddr_in)) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            *reinterpret_cast<libc::sockaddr_in*>(&out.m_storage) =
                *reinterpret_cast<const libc::sockaddr_in*>(addr);
            out.m_len = sizeof(libc::sockaddr_in);
            return Ok(rstd::move(out));
        }

        if (addr->sa_family == libc::AF_INET6) {
            if (len < sizeof(libc::sockaddr_in6)) {
                return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
            }
            *reinterpret_cast<libc::sockaddr_in6*>(&out.m_storage) =
                *reinterpret_cast<const libc::sockaddr_in6*>(addr);
            out.m_len = sizeof(libc::sockaddr_in6);
            return Ok(rstd::move(out));
        }

        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    }
#endif
};

export class Socket {
    OwnedFd m_fd;

    explicit Socket(OwnedFd fd) noexcept: m_fd(rstd::move(fd)) {}

public:
    Socket(const Socket&)                        = delete;
    auto operator=(const Socket&)                = delete;
    Socket(Socket&&) noexcept                    = default;
    auto operator=(Socket&&) noexcept -> Socket& = default;

    static auto from_owned_fd(OwnedFd fd) noexcept -> Socket { return Socket { rstd::move(fd) }; }

    auto into_owned_fd() noexcept -> OwnedFd { return rstd::move(m_fd); }
    auto as_raw_fd() const noexcept -> RawFd { return m_fd.as_raw_fd(); }

    auto set_nonblocking(bool enabled) -> Result<empty> {
#if RSTD_OS_UNIX
        return detail::set_nonblocking(as_raw_fd(), enabled);
#else
        (void)enabled;
        return Err(detail::unsupported());
#endif
    }

    static auto tcp(SocketAddr const& addr) -> Result<Socket> {
#if RSTD_OS_UNIX
        int raw = libc::socket(addr.family(), libc::SOCK_STREAM, 0);
        if (raw < 0) return Err(detail::last_error());

        auto fd      = OwnedFd::from_raw_fd(raw);
        auto cloexec = detail::set_cloexec(raw);
        if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

        auto nonblock = detail::set_nonblocking(raw, true);
        if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

        return Ok(Socket { rstd::move(fd) });
#else
        (void)addr;
        return Err(detail::unsupported());
#endif
    }

    auto set_reuseaddr(bool enabled) -> Result<empty> {
#if RSTD_OS_UNIX
        int value = enabled ? 1 : 0;
        if (libc::setsockopt(
                as_raw_fd(), libc::SOL_SOCKET, libc::SO_REUSEADDR, &value, sizeof(value)) < 0) {
            return Err(detail::last_error());
        }
        return Ok(empty {});
#else
        (void)enabled;
        return Err(detail::unsupported());
#endif
    }

    auto set_nodelay(bool enabled) -> Result<empty> {
#if RSTD_OS_UNIX
        int value = enabled ? 1 : 0;
        if (libc::setsockopt(
                as_raw_fd(), libc::IPPROTO_TCP, libc::TCP_NODELAY, &value, sizeof(value)) < 0) {
            return Err(detail::last_error());
        }
        return Ok(empty {});
#else
        (void)enabled;
        return Err(detail::unsupported());
#endif
    }

    auto bind(SocketAddr const& addr) -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::bind(as_raw_fd(), addr.as_sockaddr(), addr.m_len) < 0) {
            return Err(detail::last_error());
        }
        return Ok(empty {});
#else
        (void)addr;
        return Err(detail::unsupported());
#endif
    }

    auto listen(i32 backlog = 128) -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::listen(as_raw_fd(), backlog) < 0) {
            return Err(detail::last_error());
        }
        return Ok(empty {});
#else
        (void)backlog;
        return Err(detail::unsupported());
#endif
    }

    auto connect(SocketAddr const& addr) -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::connect(as_raw_fd(), addr.as_sockaddr(), addr.m_len) < 0) {
            return Err(detail::last_error());
        }
        return Ok(empty {});
#else
        (void)addr;
        return Err(detail::unsupported());
#endif
    }

    auto accept() -> Result<tuple<Socket, SocketAddr>> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        int raw    = libc::accept(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr());
        if (raw < 0) return Err(detail::last_error());

        auto fd      = OwnedFd::from_raw_fd(raw);
        auto cloexec = detail::set_cloexec(raw);
        if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

        auto nonblock = detail::set_nonblocking(raw, true);
        if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

        return Ok(tuple<Socket, SocketAddr> { Socket { rstd::move(fd) }, rstd::move(addr) });
#else
        return Err(detail::unsupported());
#endif
    }

    auto recv(u8* buf, usize len) -> Result<usize> {
#if RSTD_OS_UNIX
        auto n = libc::recv(as_raw_fd(), buf, len, 0);
        if (n < 0) return Err(detail::last_error());
        return Ok(usize(n));
#else
        (void)buf;
        (void)len;
        return Err(detail::unsupported());
#endif
    }

    auto send(const u8* buf, usize len) -> Result<usize> {
#if RSTD_OS_UNIX
        auto n = libc::send(as_raw_fd(), buf, len, libc::MSG_NOSIGNAL);
        if (n < 0) return Err(detail::last_error());
        return Ok(usize(n));
#else
        (void)buf;
        (void)len;
        return Err(detail::unsupported());
#endif
    }

    auto shutdown_write() -> Result<empty> {
#if RSTD_OS_UNIX
        if (libc::shutdown(as_raw_fd(), libc::SHUT_WR) < 0) return Err(detail::last_error());
        return Ok(empty {});
#else
        return Err(detail::unsupported());
#endif
    }

    auto take_error() -> Result<Option<Error>> {
#if RSTD_OS_UNIX
        int             value = 0;
        libc::socklen_t len   = sizeof(value);
        if (libc::getsockopt(as_raw_fd(), libc::SOL_SOCKET, libc::SO_ERROR, &value, &len) < 0) {
            return Err(detail::last_error());
        }
        if (value == 0) return Ok(Option<Error> {});
        return Ok(Some(Error::from_raw_os_error(value)));
#else
        return Err(detail::unsupported());
#endif
    }

    auto local_addr() const -> Result<SocketAddr> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        if (libc::getsockname(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr()) < 0) {
            return Err(detail::last_error());
        }
        return Ok(rstd::move(addr));
#else
        return Err(detail::unsupported());
#endif
    }

    auto peer_addr() const -> Result<SocketAddr> {
#if RSTD_OS_UNIX
        auto addr  = SocketAddr {};
        addr.m_len = sizeof(addr.m_storage);
        if (libc::getpeername(as_raw_fd(), addr.as_sockaddr(), addr.len_ptr()) < 0) {
            return Err(detail::last_error());
        }
        return Ok(rstd::move(addr));
#else
        return Err(detail::unsupported());
#endif
    }
};

} // namespace rstd::sys::socket
