module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <limits.h>
#endif

export module rstd:sys.pal.windows.socket;
export import :io.error;
export import :net.socket_addr;
export import :os.socket;
import :sys.pal.poll.types;
import rstd.core;

namespace rstd::sys::pal::windows::socket
{

using rstd::io::Result;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::net::Ipv4Addr;
using rstd::net::Ipv6Addr;
using rstd::net::SocketAddr;
using rstd::os::socket::OwnedSocket;
using rstd::os::socket::RawSocket;
using rstd::sys::pal::poll::SocketAddress;

#if RSTD_OS_WINDOWS
struct WinsockState {
    int error {};

    WinsockState() {
        auto data = WSADATA {};
        error     = ::WSAStartup(MAKEWORD(2, 2), &data);
    }

    ~WinsockState() {
        if (error == 0) ::WSACleanup();
    }
};

auto ensure_started() -> Result<empty> {
    static auto state = WinsockState {};
    if (state.error != 0) return Err(Error::from_raw_os_error(i32(state.error)));
    return Ok(empty {});
}

auto native_socket(RawSocket socket) noexcept -> SOCKET {
    return reinterpret_cast<SOCKET>(socket);
}

auto raw_socket(SOCKET socket) noexcept -> RawSocket {
    return reinterpret_cast<RawSocket>(socket);
}

auto last_error() noexcept -> Error {
    return Error::from_raw_os_error(i32(::WSAGetLastError()));
}

struct NativeSocketAddr {
    sockaddr_storage storage {};
    int              len {};
};

auto addr_to_native(SocketAddr const& addr) noexcept -> NativeSocketAddr {
    auto out = NativeSocketAddr {};
    if (addr.is_ipv4()) {
        auto native            = sockaddr_in {};
        native.sin_family      = AF_INET;
        native.sin_port        = ::htons(addr.port().to_primitive());
        auto address           = (rstd::uint32_t(addr.octet(usize(0)).to_primitive()) << 24) |
                                 (rstd::uint32_t(addr.octet(usize(1)).to_primitive()) << 16) |
                                 (rstd::uint32_t(addr.octet(usize(2)).to_primitive()) << 8) |
                                 rstd::uint32_t(addr.octet(usize(3)).to_primitive());
        native.sin_addr.s_addr = ::htonl(address);
        *reinterpret_cast<sockaddr_in*>(&out.storage) = native;
        out.len                                       = sizeof(native);
        return out;
    }

    auto native          = sockaddr_in6 {};
    native.sin6_family   = AF_INET6;
    native.sin6_port     = ::htons(addr.port().to_primitive());
    native.sin6_flowinfo = ::htonl(addr.flowinfo().to_primitive());
    native.sin6_scope_id = addr.scope_id().to_primitive();
    for (rstd::size_t i = 0; i < 16; ++i) {
        native.sin6_addr.u.Byte[i] = addr.octet(usize(i)).to_primitive();
    }
    *reinterpret_cast<sockaddr_in6*>(&out.storage) = native;
    out.len                                        = sizeof(native);
    return out;
}

auto operation_addr_to_native(SocketAddress const& addr) noexcept -> NativeSocketAddr {
    auto out = NativeSocketAddr {};
    if (! addr.ipv6) {
        auto native            = sockaddr_in {};
        native.sin_family      = AF_INET;
        native.sin_port        = ::htons(addr.port.to_primitive());
        auto address           = (rstd::uint32_t(addr.octets[0].to_primitive()) << 24) |
                                 (rstd::uint32_t(addr.octets[1].to_primitive()) << 16) |
                                 (rstd::uint32_t(addr.octets[2].to_primitive()) << 8) |
                                 rstd::uint32_t(addr.octets[3].to_primitive());
        native.sin_addr.s_addr = ::htonl(address);
        *reinterpret_cast<sockaddr_in*>(&out.storage) = native;
        out.len                                       = sizeof(native);
        return out;
    }
    auto native          = sockaddr_in6 {};
    native.sin6_family   = AF_INET6;
    native.sin6_port     = ::htons(addr.port.to_primitive());
    native.sin6_flowinfo = ::htonl(addr.flowinfo.to_primitive());
    native.sin6_scope_id = addr.scope_id.to_primitive();
    for (rstd::size_t i = 0; i < 16; ++i) {
        native.sin6_addr.u.Byte[i] = addr.octets[i].to_primitive();
    }
    *reinterpret_cast<sockaddr_in6*>(&out.storage) = native;
    out.len                                        = sizeof(native);
    return out;
}

template<typename Function>
auto load_extension(SOCKET socket, GUID const& id) -> Result<Function> {
    Function function {};
    DWORD    bytes {};
    if (::WSAIoctl(socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   const_cast<GUID*>(rstd::addressof(id)),
                   sizeof(id),
                   &function,
                   sizeof(function),
                   &bytes,
                   nullptr,
                   nullptr) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(function);
}

export inline constexpr rstd::size_t ACCEPT_ADDRESS_BUFFER_SIZE =
    2 * (sizeof(sockaddr_storage) + 16);

export auto accept_address_buffer_size() noexcept -> usize {
    return usize(ACCEPT_ADDRESS_BUFFER_SIZE);
}

export auto start_connect(RawSocket socket, SocketAddress const& address, void* overlapped)
    -> Result<empty> {
    auto native = operation_addr_to_native(address);
    if (! address.ipv6) {
        auto local       = sockaddr_in {};
        local.sin_family = AF_INET;
        if (::bind(native_socket(socket),
                   reinterpret_cast<const sockaddr*>(&local),
                   sizeof(local)) == SOCKET_ERROR) {
            auto error = ::WSAGetLastError();
            if (error != WSAEINVAL) return Err(Error::from_raw_os_error(i32(error)));
        }
    } else {
        auto local        = sockaddr_in6 {};
        local.sin6_family = AF_INET6;
        if (::bind(native_socket(socket),
                   reinterpret_cast<const sockaddr*>(&local),
                   sizeof(local)) == SOCKET_ERROR) {
            auto error = ::WSAGetLastError();
            if (error != WSAEINVAL) return Err(Error::from_raw_os_error(i32(error)));
        }
    }

    auto loaded = load_extension<LPFN_CONNECTEX>(native_socket(socket), WSAID_CONNECTEX);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err_unchecked());
    DWORD sent {};
    if (! rstd::move(loaded).unwrap_unchecked()(native_socket(socket),
                                                reinterpret_cast<const sockaddr*>(&native.storage),
                                                native.len,
                                                nullptr,
                                                0,
                                                &sent,
                                                static_cast<OVERLAPPED*>(overlapped))) {
        auto error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING) return Err(Error::from_raw_os_error(i32(error)));
    }
    return Ok(empty {});
}

export auto finish_connect(RawSocket socket) -> Result<empty> {
    if (::setsockopt(native_socket(socket), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0) ==
        SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto start_accept(RawSocket listener,
                         RawSocket accepted,
                         void*     address_buffer,
                         usize     address_buffer_len,
                         void*     overlapped) -> Result<empty> {
    if (address_buffer_len.to_primitive() < ACCEPT_ADDRESS_BUFFER_SIZE) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto loaded = load_extension<LPFN_ACCEPTEX>(native_socket(listener), WSAID_ACCEPTEX);
    if (loaded.is_err()) return Err(rstd::move(loaded).unwrap_err_unchecked());
    DWORD received {};
    auto  address_len = static_cast<DWORD>(sizeof(sockaddr_storage) + 16);
    if (! rstd::move(loaded).unwrap_unchecked()(native_socket(listener),
                                                native_socket(accepted),
                                                address_buffer,
                                                0,
                                                address_len,
                                                address_len,
                                                &received,
                                                static_cast<OVERLAPPED*>(overlapped))) {
        auto error = ::WSAGetLastError();
        if (error != WSA_IO_PENDING) return Err(Error::from_raw_os_error(i32(error)));
    }
    return Ok(empty {});
}

export auto finish_accept(RawSocket listener, RawSocket accepted) -> Result<empty> {
    auto native_listener = native_socket(listener);
    if (::setsockopt(native_socket(accepted),
                     SOL_SOCKET,
                     SO_UPDATE_ACCEPT_CONTEXT,
                     reinterpret_cast<const char*>(&native_listener),
                     sizeof(native_listener)) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

auto addr_from_native(const sockaddr* addr, int len) -> Result<SocketAddr> {
    if (addr == nullptr) return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    if (addr->sa_family == AF_INET) {
        if (len < sizeof(sockaddr_in)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        auto const& native = *reinterpret_cast<const sockaddr_in*>(addr);
        auto        bits   = ::ntohl(native.sin_addr.s_addr);
        return Ok(SocketAddr::ipv4(
            Ipv4Addr::make(u8(bits >> 24), u8(bits >> 16), u8(bits >> 8), u8(bits)),
            u16(::ntohs(native.sin_port))));
    }
    if (addr->sa_family == AF_INET6) {
        if (len < sizeof(sockaddr_in6)) {
            return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
        }
        auto const& native  = *reinterpret_cast<const sockaddr_in6*>(addr);
        auto        segment = [&](rstd::size_t index) {
            auto high = native.sin6_addr.u.Byte[index * 2];
            auto low  = native.sin6_addr.u.Byte[index * 2 + 1];
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
                                   u16(::ntohs(native.sin6_port)),
                                   u32(::ntohl(native.sin6_flowinfo)),
                                   u32(native.sin6_scope_id)));
    }
    return Err(Error::from_kind(ErrorKind { ErrorKind::Unsupported }));
}

export auto set_nonblocking(RawSocket socket, bool enabled) -> Result<empty> {
    auto started = ensure_started();
    if (started.is_err()) return started;
    u_long value = enabled ? 1 : 0;
    if (::ioctlsocket(native_socket(socket), FIONBIO, &value) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto tcp(SocketAddr const& addr) -> Result<OwnedSocket> {
    auto started = ensure_started();
    if (started.is_err()) return Err(rstd::move(started).unwrap_err_unchecked());
    int  family = addr.is_ipv4() ? AF_INET : AF_INET6;
    auto socket = ::WSASocketW(family, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket == INVALID_SOCKET) return Err(last_error());
    return Ok(OwnedSocket::from_raw_socket(raw_socket(socket)));
}

export auto prepare_accept(SocketAddr const& addr) -> Result<Option<OwnedSocket>> {
    auto socket = tcp(addr);
    if (socket.is_err()) return Err(rstd::move(socket).unwrap_err_unchecked());
    return Ok(Some(rstd::move(socket).unwrap_unchecked()));
}

export auto set_reuseaddr(RawSocket socket, bool enabled) -> Result<empty> {
    BOOL value = enabled ? TRUE : FALSE;
    if (::setsockopt(native_socket(socket),
                     SOL_SOCKET,
                     SO_REUSEADDR,
                     reinterpret_cast<const char*>(&value),
                     sizeof(value)) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto set_nodelay(RawSocket socket, bool enabled) -> Result<empty> {
    BOOL value = enabled ? TRUE : FALSE;
    if (::setsockopt(native_socket(socket),
                     IPPROTO_TCP,
                     TCP_NODELAY,
                     reinterpret_cast<const char*>(&value),
                     sizeof(value)) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto bind(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    auto native = addr_to_native(addr);
    if (::bind(native_socket(socket),
               reinterpret_cast<const sockaddr*>(&native.storage),
               native.len) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto listen(RawSocket socket, i32 backlog) -> Result<empty> {
    if (::listen(native_socket(socket), static_cast<int>(backlog.to_primitive())) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto connect(RawSocket socket, SocketAddr const& addr) -> Result<empty> {
    auto native = addr_to_native(addr);
    if (::connect(native_socket(socket),
                  reinterpret_cast<const sockaddr*>(&native.storage),
                  native.len) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return Ok(empty {});
}

export auto accept(RawSocket socket) -> Result<tuple<OwnedSocket, SocketAddr>> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    auto accepted =
        ::accept(native_socket(socket), reinterpret_cast<sockaddr*>(&native.storage), &native.len);
    if (accepted == INVALID_SOCKET) return Err(last_error());

    auto owned = OwnedSocket::from_raw_socket(raw_socket(accepted));
    auto addr  = addr_from_native(reinterpret_cast<const sockaddr*>(&native.storage), native.len);
    if (addr.is_err()) return Err(rstd::move(addr).unwrap_err_unchecked());
    return Ok(
        tuple<OwnedSocket, SocketAddr> { rstd::move(owned), rstd::move(addr).unwrap_unchecked() });
}

export auto recv(RawSocket socket, mut_ref<byte[]> buf) -> Result<usize> {
    if (buf.len().to_primitive() > static_cast<rstd::size_t>(INT_MAX)) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto n = ::recv(native_socket(socket),
                    reinterpret_cast<char*>(buf.as_raw_ptr()),
                    static_cast<int>(buf.len().to_primitive()),
                    0);
    if (n == SOCKET_ERROR) return Err(last_error());
    return Ok(usize(n));
}

export auto send(RawSocket socket, slice<byte> buf) -> Result<usize> {
    if (buf.len().to_primitive() > static_cast<rstd::size_t>(INT_MAX)) {
        return Err(Error::from_kind(ErrorKind { ErrorKind::InvalidInput }));
    }
    auto n = ::send(native_socket(socket),
                    reinterpret_cast<const char*>(buf.as_raw_ptr()),
                    static_cast<int>(buf.len().to_primitive()),
                    0);
    if (n == SOCKET_ERROR) return Err(last_error());
    return Ok(usize(n));
}

export auto shutdown_write(RawSocket socket) -> Result<empty> {
    if (::shutdown(native_socket(socket), SD_SEND) == SOCKET_ERROR) return Err(last_error());
    return Ok(empty {});
}

export auto take_error(RawSocket socket) -> Result<Option<Error>> {
    int value = 0;
    int len   = sizeof(value);
    if (::getsockopt(
            native_socket(socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&value), &len) ==
        SOCKET_ERROR) {
        return Err(last_error());
    }
    if (value == 0) return Ok(Option<Error> {});
    return Ok(Some(Error::from_raw_os_error(i32(value))));
}

export auto local_addr(RawSocket socket) -> Result<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (::getsockname(native_socket(socket),
                      reinterpret_cast<sockaddr*>(&native.storage),
                      &native.len) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const sockaddr*>(&native.storage), native.len);
}

export auto peer_addr(RawSocket socket) -> Result<SocketAddr> {
    auto native = NativeSocketAddr {};
    native.len  = sizeof(native.storage);
    if (::getpeername(native_socket(socket),
                      reinterpret_cast<sockaddr*>(&native.storage),
                      &native.len) == SOCKET_ERROR) {
        return Err(last_error());
    }
    return addr_from_native(reinterpret_cast<const sockaddr*>(&native.storage), native.len);
}
#endif

} // namespace rstd::sys::pal::windows::socket
