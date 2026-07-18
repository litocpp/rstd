module;
#include <rstd/macro.hpp>

export module rstd:sys.socket;
import :io.error;
import :net.socket_addr;
import :os.fd;
import :sys.libc;
import rstd.core;

using namespace rstd::prelude;

using SocketOwnedFd   = rstd::os::fd::OwnedFd;
using SocketRawFd     = rstd::os::fd::RawFd;
using SocketError     = rstd::io::error::Error;
using SocketErrorKind = rstd::io::error::ErrorKind;
template<typename T>
using SocketResult    = rstd::io::Result<T>;
namespace socket_libc = rstd::sys::libc;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;

inline auto socket_unsupported() noexcept -> SocketError {
    return SocketError::from_kind(SocketErrorKind { SocketErrorKind::Unsupported });
}

#if RSTD_OS_UNIX
inline auto socket_last_error() noexcept -> SocketError {
    return SocketError::last_os_error();
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

struct NativeSocketAddr {
    socket_libc::sockaddr_storage storage {};
    socket_libc::socklen_t        len {};
};

inline auto socket_addr_to_native(SocketAddr const& addr) noexcept -> NativeSocketAddr {
    auto out = NativeSocketAddr {};
    if (addr.is_ipv4()) {
        auto native       = socket_libc::sockaddr_in {};
        native.sin_family = socket_libc::AF_INET;
        native.sin_port   = socket_libc::htons(addr.port());
        native.sin_addr.s_addr =
            socket_libc::htonl((u32(addr.octet(0)) << 24) | (u32(addr.octet(1)) << 16) |
                               (u32(addr.octet(2)) << 8) | u32(addr.octet(3)));
        *reinterpret_cast<socket_libc::sockaddr_in*>(&out.storage) = native;
        out.len                                                    = sizeof(native);
        return out;
    }

    auto native          = socket_libc::sockaddr_in6 {};
    native.sin6_family   = socket_libc::AF_INET6;
    native.sin6_port     = socket_libc::htons(addr.port());
    native.sin6_flowinfo = socket_libc::htonl(addr.flowinfo());
    native.sin6_scope_id = addr.scope_id();
    for (usize i = 0; i < 16; ++i) {
        socket_libc::set_in6_addr_octet(native.sin6_addr, unsigned(i), addr.octet(i));
    }
    *reinterpret_cast<socket_libc::sockaddr_in6*>(&out.storage) = native;
    out.len                                                     = sizeof(native);
    return out;
}

inline auto socket_addr_from_native(const socket_libc::sockaddr* addr, socket_libc::socklen_t len)
    -> SocketResult<SocketAddr> {
    if (addr == nullptr) {
        return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
    }

    if (addr->sa_family == socket_libc::AF_INET) {
        if (len < sizeof(socket_libc::sockaddr_in)) {
            return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
        }
        auto const& native = *reinterpret_cast<const socket_libc::sockaddr_in*>(addr);
        auto        bits   = socket_libc::ntohl(native.sin_addr.s_addr);
        return Ok(SocketAddr::ipv4(
            Ipv4Addr::make(u8(bits >> 24), u8(bits >> 16), u8(bits >> 8), u8(bits)),
            socket_libc::ntohs(native.sin_port)));
    }

    if (addr->sa_family == socket_libc::AF_INET6) {
        if (len < sizeof(socket_libc::sockaddr_in6)) {
            return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::InvalidInput }));
        }
        auto const& native  = *reinterpret_cast<const socket_libc::sockaddr_in6*>(addr);
        auto        segment = [&](usize index) {
            auto high = socket_libc::in6_addr_octet(native.sin6_addr, unsigned(index * 2));
            auto low  = socket_libc::in6_addr_octet(native.sin6_addr, unsigned(index * 2 + 1));
            return u16((u16(high) << 8) | u16(low));
        };
        auto ip = Ipv6Addr::make(segment(0),
                                 segment(1),
                                 segment(2),
                                 segment(3),
                                 segment(4),
                                 segment(5),
                                 segment(6),
                                 segment(7));
        return Ok(SocketAddr::ipv6(ip,
                                   socket_libc::ntohs(native.sin6_port),
                                   socket_libc::ntohl(native.sin6_flowinfo),
                                   native.sin6_scope_id));
    }

    return Err(SocketError::from_kind(SocketErrorKind { SocketErrorKind::Unsupported }));
}
#endif

namespace rstd::sys::socket
{

export auto set_nonblocking(SocketRawFd fd, bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    return socket_set_nonblocking(fd, enabled);
#else
    (void)fd;
    (void)enabled;
    return Err(socket_unsupported());
#endif
}

export auto tcp(SocketAddr const& addr) -> SocketResult<SocketOwnedFd> {
#if RSTD_OS_UNIX
    int family = addr.is_ipv4() ? socket_libc::AF_INET : socket_libc::AF_INET6;
    int raw    = socket_libc::socket(family, socket_libc::SOCK_STREAM, 0);
    if (raw < 0) return Err(socket_last_error());

    auto fd      = SocketOwnedFd::from_raw_fd(raw);
    auto cloexec = socket_set_cloexec(raw);
    if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

    auto nonblock = socket_set_nonblocking(raw, true);
    if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

    return Ok(rstd::move(fd));
#else
    (void)addr;
    return Err(socket_unsupported());
#endif
}

export auto set_reuseaddr(SocketRawFd fd, bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    int value = enabled ? 1 : 0;
    if (socket_libc::setsockopt(
            fd, socket_libc::SOL_SOCKET, socket_libc::SO_REUSEADDR, &value, sizeof(value)) < 0) {
        return Err(socket_last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)enabled;
    return Err(socket_unsupported());
#endif
}

export auto set_nodelay(SocketRawFd fd, bool enabled) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    int value = enabled ? 1 : 0;
    if (socket_libc::setsockopt(
            fd, socket_libc::IPPROTO_TCP, socket_libc::TCP_NODELAY, &value, sizeof(value)) < 0) {
        return Err(socket_last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)enabled;
    return Err(socket_unsupported());
#endif
}

export auto bind(SocketRawFd fd, SocketAddr const& addr) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    auto native = socket_addr_to_native(addr);
    if (socket_libc::bind(
            fd, reinterpret_cast<const socket_libc::sockaddr*>(&native.storage), native.len) < 0) {
        return Err(socket_last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)addr;
    return Err(socket_unsupported());
#endif
}

export auto listen(SocketRawFd fd, i32 backlog = 128) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    if (socket_libc::listen(fd, backlog) < 0) {
        return Err(socket_last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)backlog;
    return Err(socket_unsupported());
#endif
}

export auto connect(SocketRawFd fd, SocketAddr const& addr) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    auto native = socket_addr_to_native(addr);
    if (socket_libc::connect(
            fd, reinterpret_cast<const socket_libc::sockaddr*>(&native.storage), native.len) < 0) {
        return Err(socket_last_error());
    }
    return Ok(empty {});
#else
    (void)fd;
    (void)addr;
    return Err(socket_unsupported());
#endif
}

export auto accept(SocketRawFd fd) -> SocketResult<tuple<SocketOwnedFd, SocketAddr>> {
#if RSTD_OS_UNIX
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    int raw     = socket_libc::accept(
        fd, reinterpret_cast<socket_libc::sockaddr*>(&native.storage), &native.len);
    if (raw < 0) return Err(socket_last_error());

    auto accepted_fd = SocketOwnedFd::from_raw_fd(raw);
    auto addr        = socket_addr_from_native(
        reinterpret_cast<const socket_libc::sockaddr*>(&native.storage), native.len);
    if (addr.is_err()) return Err(rstd::move(addr).unwrap_err_unchecked());
    auto cloexec = socket_set_cloexec(raw);
    if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());

    auto nonblock = socket_set_nonblocking(raw, true);
    if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());

    return Ok(tuple<SocketOwnedFd, SocketAddr> { rstd::move(accepted_fd),
                                                 rstd::move(addr).unwrap_unchecked() });
#else
    (void)fd;
    return Err(socket_unsupported());
#endif
}

export auto recv(SocketRawFd fd, u8* buf, usize len) -> SocketResult<usize> {
#if RSTD_OS_UNIX
    auto n = socket_libc::recv(fd, buf, len, 0);
    if (n < 0) return Err(socket_last_error());
    return Ok(usize(n));
#else
    (void)fd;
    (void)buf;
    (void)len;
    return Err(socket_unsupported());
#endif
}

export auto send(SocketRawFd fd, const u8* buf, usize len) -> SocketResult<usize> {
#if RSTD_OS_UNIX
    auto n = socket_libc::send(fd, buf, len, socket_libc::MSG_NOSIGNAL);
    if (n < 0) return Err(socket_last_error());
    return Ok(usize(n));
#else
    (void)fd;
    (void)buf;
    (void)len;
    return Err(socket_unsupported());
#endif
}

export auto shutdown_write(SocketRawFd fd) -> SocketResult<empty> {
#if RSTD_OS_UNIX
    if (socket_libc::shutdown(fd, socket_libc::SHUT_WR) < 0) return Err(socket_last_error());
    return Ok(empty {});
#else
    (void)fd;
    return Err(socket_unsupported());
#endif
}

export auto take_error(SocketRawFd fd) -> SocketResult<Option<SocketError>> {
#if RSTD_OS_UNIX
    int                    value = 0;
    socket_libc::socklen_t len   = sizeof(value);
    if (socket_libc::getsockopt(fd, socket_libc::SOL_SOCKET, socket_libc::SO_ERROR, &value, &len) <
        0) {
        return Err(socket_last_error());
    }
    if (value == 0) return Ok(Option<SocketError> {});
    return Ok(Some(SocketError::from_raw_os_error(value)));
#else
    (void)fd;
    return Err(socket_unsupported());
#endif
}

export auto local_addr(SocketRawFd fd) -> SocketResult<SocketAddr> {
#if RSTD_OS_UNIX
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (socket_libc::getsockname(
            fd, reinterpret_cast<socket_libc::sockaddr*>(&native.storage), &native.len) < 0) {
        return Err(socket_last_error());
    }
    return socket_addr_from_native(reinterpret_cast<const socket_libc::sockaddr*>(&native.storage),
                                   native.len);
#else
    (void)fd;
    return Err(socket_unsupported());
#endif
}

export auto peer_addr(SocketRawFd fd) -> SocketResult<SocketAddr> {
#if RSTD_OS_UNIX
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (socket_libc::getpeername(
            fd, reinterpret_cast<socket_libc::sockaddr*>(&native.storage), &native.len) < 0) {
        return Err(socket_last_error());
    }
    return socket_addr_from_native(reinterpret_cast<const socket_libc::sockaddr*>(&native.storage),
                                   native.len);
#else
    (void)fd;
    return Err(socket_unsupported());
#endif
}

} // namespace rstd::sys::socket
