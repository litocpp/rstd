module;
#include <rstd/macro.hpp>
#include <cstddef>
module rstd.sys.socket;
import :unix;

template<typename T>
using IOResult = rstd::io::Result<T>;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;
namespace libc = rstd::sys::libc;

namespace rstd::sys::pal::unix::socket
{

auto last_error() noexcept -> Error {
    return Error::last_os_error();
}

auto addr_to_native(const rstd::sys::pal::poll::SocketAddress& address) noexcept
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

auto addr_to_native(const rstd::sys::pal::poll::SocketAddress& address) noexcept
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

auto addr_from_native(const libc::sockaddr* addr, libc::socklen_t len) -> IOResult<SocketAddr> {
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

auto set_nonblocking(RawSocket socket, bool enabled) -> IOResult<empty> {
    int flags = libc::fcntl(socket, libc::F_GETFL, 0);
    if (flags < 0) return Err(last_error());
    flags = enabled ? flags | libc::O_NONBLOCK : flags & ~libc::O_NONBLOCK;
    if (libc::fcntl(socket, libc::F_SETFL, flags) < 0) return Err(last_error());
    return Ok(empty {});
}

auto set_cloexec(RawSocket socket) -> IOResult<empty> {
    int flags = libc::fcntl(socket, libc::F_GETFD, 0);
    if (flags < 0) return Err(last_error());
    if (libc::fcntl(socket, libc::F_SETFD, flags | libc::FD_CLOEXEC) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto tcp(SocketAddr const& addr) -> IOResult<OwnedSocket> {
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

auto unix_stream() -> IOResult<OwnedSocket> {
    int raw = libc::socket(libc::AF_UNIX, libc::SOCK_STREAM, 0);
    if (raw < 0) return Err(last_error());
    auto owned = OwnedSocket::from_raw_socket(raw);
    auto flags = set_cloexec(raw);
    if (flags.is_err()) return Err(rstd::move(flags).unwrap_err());
    flags = set_nonblocking(raw, true);
    if (flags.is_err()) return Err(rstd::move(flags).unwrap_err());
    return Ok(rstd::move(owned));
}
auto connect_unix(RawSocket socket, slice<byte> name, bool abstract) -> IOResult<empty> {
    auto addr = libc::sockaddr_un {};
    if (name.is_empty() || name.len() >= usize(sizeof(addr.sun_path)))
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
#if ! RSTD_OS_LINUX
    if (abstract) return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
#endif
    addr.sun_family = libc::AF_UNIX;
    auto offset     = abstract ? 1U : 0U;
    for (usize i; i < name.len(); ++i) {
        auto value = static_cast<char>(name[i]);
        if (! abstract && value == 0)
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        addr.sun_path[i.to_primitive() + offset] = value;
    }
    auto length = static_cast<libc::socklen_t>(offsetof(libc::sockaddr_un, sun_path) +
                                               name.len().to_primitive() + 1);
#if RSTD_VENDOR_APPLE
    addr.sun_len = length;
#endif
    if (libc::connect(socket, reinterpret_cast<const libc::sockaddr*>(&addr), length) < 0)
        return Err(last_error());
    return Ok(empty {});
}

auto prepare_accept(SocketAddr const&) -> IOResult<Option<OwnedSocket>> {
    return Ok(None<OwnedSocket>());
}

auto accept_address_buffer_size() noexcept -> usize {
    return usize();
}

auto set_reuseaddr(RawSocket socket, bool enabled) -> IOResult<empty> {
    int value = enabled ? 1 : 0;
    if (libc::setsockopt(socket, libc::SOL_SOCKET, libc::SO_REUSEADDR, &value, sizeof(value)) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto set_nodelay(RawSocket socket, bool enabled) -> IOResult<empty> {
    int value = enabled ? 1 : 0;
    if (libc::setsockopt(socket, libc::IPPROTO_TCP, libc::TCP_NODELAY, &value, sizeof(value)) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto set_keepalive(RawSocket socket, bool enabled, Option<u32> idle, Option<u32> interval)
    -> IOResult<empty> {
    if ((idle.is_some() && libc::TCP_KEEP_IDLE == 0) ||
        (interval.is_some() && libc::TCP_KEEP_INTERVAL == 0))
        return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
    int value = enabled ? 1 : 0;
    if (libc::setsockopt(socket, libc::SOL_SOCKET, libc::SO_KEEPALIVE, &value, sizeof(value)) < 0)
        return Err(last_error());
    if (idle.is_some()) {
        value = static_cast<int>(idle->to_primitive());
        if (libc::setsockopt(
                socket, libc::IPPROTO_TCP, libc::TCP_KEEP_IDLE, &value, sizeof(value)) < 0)
            return Err(last_error());
    }
    if (interval.is_some()) {
        value = static_cast<int>(interval->to_primitive());
        if (libc::setsockopt(
                socket, libc::IPPROTO_TCP, libc::TCP_KEEP_INTERVAL, &value, sizeof(value)) < 0)
            return Err(last_error());
    }
    return Ok(empty {});
}
auto keepalive(RawSocket socket) -> IOResult<bool> {
    int             value {};
    libc::socklen_t size = sizeof(value);
    if (libc::getsockopt(socket, libc::SOL_SOCKET, libc::SO_KEEPALIVE, &value, &size) < 0)
        return Err(last_error());
    return Ok(value != 0);
}

auto keepalive_timing_supported() -> bool {
    return libc::TCP_KEEP_IDLE != 0 && libc::TCP_KEEP_INTERVAL != 0;
}

auto bind(RawSocket socket, SocketAddr const& addr) -> IOResult<empty> {
    auto native = addr_to_native(addr);
    if (libc::bind(socket, reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len) <
        0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto listen(RawSocket socket, i32 backlog) -> IOResult<empty> {
    if (libc::listen(socket, static_cast<int>(backlog.to_primitive())) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto connect(RawSocket socket, SocketAddr const& addr) -> IOResult<empty> {
    auto native = addr_to_native(addr);
    if (libc::connect(
            socket, reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len) < 0) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto accept(RawSocket socket) -> IOResult<tuple<OwnedSocket, SocketAddr>> {
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

auto recv(RawSocket socket, mut_ref<byte[]> buf) -> IOResult<usize> {
    auto n = libc::recv(socket, buf.as_raw_ptr(), buf.len().to_primitive(), 0);
    if (n < 0) return Err(last_error());
    return Ok(usize(n));
}

auto send(RawSocket socket, slice<byte> buf) -> IOResult<usize> {
    auto n = libc::send(socket, buf.as_raw_ptr(), buf.len().to_primitive(), libc::MSG_NOSIGNAL);
    if (n < 0) return Err(last_error());
    return Ok(usize(n));
}

auto shutdown_write(RawSocket socket) -> IOResult<empty> {
    if (libc::shutdown(socket, libc::SHUT_WR) < 0) return Err(last_error());
    return Ok(empty {});
}

auto shutdown_both(RawSocket socket) -> IOResult<empty> {
    if (libc::shutdown(socket, libc::SHUT_RDWR) < 0) return Err(last_error());
    return Ok(empty {});
}

auto take_error(RawSocket socket) -> IOResult<Option<Error>> {
    int             value = 0;
    libc::socklen_t len   = sizeof(value);
    if (libc::getsockopt(socket, libc::SOL_SOCKET, libc::SO_ERROR, &value, &len) < 0) {
        return Err(last_error());
    }
    if (value == 0) return Ok(Option<Error> {});
    return Ok(Some(Error::from_raw_os_error(i32(value))));
}

auto local_addr(RawSocket socket) -> IOResult<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (libc::getsockname(socket, reinterpret_cast<libc::sockaddr*>(&native.storage), &native.len) <
        0) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len);
}

auto peer_addr(RawSocket socket) -> IOResult<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (libc::getpeername(socket, reinterpret_cast<libc::sockaddr*>(&native.storage), &native.len) <
        0) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const libc::sockaddr*>(&native.storage), native.len);
}
} // namespace rstd::sys::pal::unix::socket
