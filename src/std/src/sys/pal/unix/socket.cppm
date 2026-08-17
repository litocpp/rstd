export module rstd:sys.pal.unix.socket;
export import :io.error;
export import :net.socket_addr;
export import :os.socket;
import :sys.libc;
import :sys.pal.poll.types;
import rstd.core;

namespace rstd::sys::pal::unix::socket
{

using rstd::io::Result;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;
namespace libc = rstd::sys::libc;

export struct NativeSocketAddr {
    libc::sockaddr_storage storage {};
    libc::socklen_t        len {};
};

auto last_error() noexcept -> Error {
    return Error::last_os_error();
}

export auto addr_to_native(const rstd::sys::pal::poll::SocketAddress& address) noexcept
    -> NativeSocketAddr;

auto addr_to_native(SocketAddr const& addr) noexcept -> NativeSocketAddr {
    auto portable = rstd::sys::pal::poll::SocketAddress {
        .ipv6     = addr.is_ipv6(),
        .port     = addr.port(),
        .flowinfo = addr.flowinfo(),
        .scope_id = addr.scope_id(),
    };
    for (rstd::size_t i = 0; i < 16; ++i) {
        portable.octets[i] = addr.octet(usize(i));
    }
    return addr_to_native(portable);
}

export auto addr_to_native(const rstd::sys::pal::poll::SocketAddress& address) noexcept
    -> NativeSocketAddr {
    auto out = NativeSocketAddr {};
    if (! address.ipv6) {
        auto native            = libc::sockaddr_in {};
        native.sin_family      = libc::AF_INET;
        native.sin_port        = libc::htons(address.port.to_primitive());
        auto bits              = (rstd::uint32_t(address.octets[0].to_primitive()) << 24) |
                                 (rstd::uint32_t(address.octets[1].to_primitive()) << 16) |
                                 (rstd::uint32_t(address.octets[2].to_primitive()) << 8) |
                                 rstd::uint32_t(address.octets[3].to_primitive());
        native.sin_addr.s_addr = libc::htonl(bits);
        *reinterpret_cast<libc::sockaddr_in*>(&out.storage) = native;
        out.len                                             = sizeof(native);
        return out;
    }

    auto native          = libc::sockaddr_in6 {};
    native.sin6_family   = libc::AF_INET6;
    native.sin6_port     = libc::htons(address.port.to_primitive());
    native.sin6_flowinfo = libc::htonl(address.flowinfo.to_primitive());
    native.sin6_scope_id = address.scope_id.to_primitive();
    for (rstd::size_t i = 0; i < 16; ++i) {
        libc::set_in6_addr_octet(
            native.sin6_addr, static_cast<unsigned int>(i), address.octets[i].to_primitive());
    }
    *reinterpret_cast<libc::sockaddr_in6*>(&out.storage) = native;
    out.len                                              = sizeof(native);
    return out;
}

auto addr_from_native(const libc::sockaddr* addr, libc::socklen_t len) -> Result<SocketAddr> {
    if (addr == nullptr) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    if (addr->sa_family == libc::AF_INET) {
        if (len < sizeof(libc::sockaddr_in)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        auto const& native = *reinterpret_cast<const libc::sockaddr_in*>(addr);
        auto        bits   = libc::ntohl(native.sin_addr.s_addr);
        return Ok(SocketAddr::ipv4(
            Ipv4Addr::make(u8(bits >> 24), u8(bits >> 16), u8(bits >> 8), u8(bits)),
            u16(libc::ntohs(native.sin_port))));
    }
    if (addr->sa_family == libc::AF_INET6) {
        if (len < sizeof(libc::sockaddr_in6)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        auto const& native  = *reinterpret_cast<const libc::sockaddr_in6*>(addr);
        auto        segment = [&](rstd::size_t index) {
            auto high =
                libc::in6_addr_octet(native.sin6_addr, static_cast<unsigned int>(index * 2));
            auto low =
                libc::in6_addr_octet(native.sin6_addr, static_cast<unsigned int>(index * 2 + 1));
            return u16((rstd::uint16_t(high) << 8) | low);
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
                                   u16(libc::ntohs(native.sin6_port)),
                                   u32(libc::ntohl(native.sin6_flowinfo)),
                                   u32(native.sin6_scope_id)));
    }
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
}

export auto set_nonblocking(RawSocket socket, bool enabled) -> Result<empty> {
    int flags = libc::fcntl(socket, libc::F_GETFL, 0);
    if (flags < 0) return Err(last_error());
    flags = enabled ? flags | libc::O_NONBLOCK : flags & ~libc::O_NONBLOCK;
    if (libc::fcntl(socket, libc::F_SETFL, flags) < 0) return Err(last_error());
    return Ok(empty {});
}

auto set_cloexec(RawSocket socket) -> Result<empty> {
    int flags = libc::fcntl(socket, libc::F_GETFD, 0);
    if (flags < 0) return Err(last_error());
    if (libc::fcntl(socket, libc::F_SETFD, flags | libc::FD_CLOEXEC) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto tcp(SocketAddr const& addr) -> Result<OwnedSocket> {
    int family = addr.is_ipv4() ? libc::AF_INET : libc::AF_INET6;
    int raw    = libc::socket(family, libc::SOCK_STREAM, 0);
    if (raw < 0) return Err(last_error());

    auto socket  = OwnedSocket::from_raw_socket(raw);
    auto cloexec = set_cloexec(raw);
    if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());
    auto nonblock = set_nonblocking(raw, true);
    if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());
    return Ok(rstd::move(socket));
}

export auto prepare_accept(SocketAddr const&) -> Result<Option<OwnedSocket>> {
    return Ok(None<OwnedSocket>());
}

export auto accept_address_buffer_size() noexcept -> usize {
    return usize();
}

export auto set_reuseaddr(RawSocket socket, bool enabled) -> Result<empty> {
    int value = enabled ? 1 : 0;
    if (libc::setsockopt(socket, libc::SOL_SOCKET, libc::SO_REUSEADDR, &value, sizeof(value)) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto set_nodelay(RawSocket socket, bool enabled) -> Result<empty> {
    int value = enabled ? 1 : 0;
    if (libc::setsockopt(socket, libc::IPPROTO_TCP, libc::TCP_NODELAY, &value, sizeof(value)) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto bind(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    auto native = addr_to_native(addr);
    if (libc::bind(socket, reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len) <
        0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto listen(RawSocket socket, i32 backlog) -> Result<empty> {
    if (libc::listen(socket, static_cast<int>(backlog.to_primitive())) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto connect(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    auto native = addr_to_native(addr);
    if (libc::connect(
            socket, reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto accept(RawSocket socket) -> Result<tuple<OwnedSocket, SocketAddr>> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    int raw = libc::accept(socket, reinterpret_cast<libc::sockaddr*>(&native.storage), &native.len);
    if (raw < 0) return Err(last_error());

    auto accepted = OwnedSocket::from_raw_socket(raw);
    auto addr =
        addr_from_native(reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len);
    if (addr.is_err()) return Err(rstd::move(addr).unwrap_err_unchecked());
    auto cloexec = set_cloexec(raw);
    if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err_unchecked());
    auto nonblock = set_nonblocking(raw, true);
    if (nonblock.is_err()) return Err(rstd::move(nonblock).unwrap_err_unchecked());
    return Ok(tuple<OwnedSocket, SocketAddr> { rstd::move(accepted),
                                               rstd::move(addr).unwrap_unchecked() });
}

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> Result<usize> {
    auto n = libc::recv(socket, buf.as_raw_ptr(), buf.len().to_primitive(), 0);
    if (n < 0) return Err(last_error());
    return Ok(usize(n));
}

export auto send(RawSocket socket, slice<byte> buf) -> Result<usize> {
    auto n = libc::send(socket, buf.as_raw_ptr(), buf.len().to_primitive(), libc::MSG_NOSIGNAL);
    if (n < 0) return Err(last_error());
    return Ok(usize(n));
}

export auto shutdown_write(RawSocket socket) -> Result<empty> {
    if (libc::shutdown(socket, libc::SHUT_WR) < 0) return Err(last_error());
    return Ok(empty {});
}

export auto take_error(RawSocket socket) -> Result<Option<Error>> {
    int             value = 0;
    libc::socklen_t len   = sizeof(value);
    if (libc::getsockopt(socket, libc::SOL_SOCKET, libc::SO_ERROR, &value, &len) < 0) {
        return Err(last_error());
    }
    if (value == 0) return Ok(Option<Error> {});
    return Ok(Some(Error::from_raw_os_error(i32(value))));
}

export auto local_addr(RawSocket socket) -> Result<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (libc::getsockname(socket, reinterpret_cast<libc::sockaddr*>(&native.storage), &native.len) <
        0) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len);
}

export auto peer_addr(RawSocket socket) -> Result<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (libc::getpeername(socket, reinterpret_cast<libc::sockaddr*>(&native.storage), &native.len) <
        0) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len);
}
} // namespace rstd::sys::pal::unix::socket
