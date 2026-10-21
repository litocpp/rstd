module;
#include <rstd/macro.hpp>
#if RSTD_OS_WINDOWS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#else
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>
#include <unistd.h>
#include <errno.h>
#if RSTD_VENDOR_APPLE
#include <net/if_dl.h>
#endif
#endif

module rstd.sys.socket;
import :datagram;
#if RSTD_OS_WINDOWS
import :windows;
#else
import :unix;
#endif

using namespace rstd::prelude;
#if RSTD_OS_WINDOWS
namespace native_socket = rstd::sys::pal::windows::socket;
#else
namespace native_socket = rstd::sys::pal::unix::socket;
#endif

namespace rstd::sys::socket
{
auto datagram_error() -> io::error::Error {
#if RSTD_OS_WINDOWS
    return io::error::Error::from_raw_os_error(i32(::WSAGetLastError()));
#else
    return io::error::Error::last_os_error();
#endif
}

auto native_datagram_socket(os::socket::RawSocket socket) noexcept {
#if RSTD_OS_WINDOWS
    return reinterpret_cast<SOCKET>(socket);
#else
    return socket;
#endif
}

auto datagram_option(os::socket::RawSocket socket, int level, int option, int value)
    -> io::Result<empty> {
    if (::setsockopt(native_datagram_socket(socket),
                     level,
                     option,
                     reinterpret_cast<const char*>(&value),
                     sizeof(value)) != 0)
        return Err(datagram_error());
    return Ok(empty {});
}

auto datagram_capabilities(net::SocketAddr address) noexcept -> net::DatagramCapabilities {
    if (address.is_ipv6()) {
#if defined(IPV6_PKTINFO) && (defined(IPV6_RECVPKTINFO) || RSTD_OS_WINDOWS)
        return { true, true, true };
#else
        return {};
#endif
    }
#if defined(IP_PKTINFO)
    return { true, true, true };
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
    return { true, true, false };
#else
    return {};
#endif
}

auto udp(net::SocketAddr address) -> io::Result<os::socket::OwnedSocket> {
    int family = address.is_ipv4() ? AF_INET : AF_INET6;
#if RSTD_OS_WINDOWS
    auto started = native_socket::ensure_started();
    if (started.is_err()) return Err(rstd::move(started).unwrap_err());
    auto raw = ::WSASocketW(family, SOCK_DGRAM, IPPROTO_UDP, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (raw == INVALID_SOCKET) return Err(datagram_error());
    auto result =
        os::socket::OwnedSocket::from_raw_socket(reinterpret_cast<os::socket::RawSocket>(raw));
#else
    auto raw = ::socket(family, SOCK_DGRAM, IPPROTO_UDP);
    if (raw < 0) return Err(datagram_error());
    auto result  = os::socket::OwnedSocket::from_raw_socket(raw);
    auto cloexec = native_socket::set_cloexec(raw);
    if (cloexec.is_err()) return Err(rstd::move(cloexec).unwrap_err());
#endif
    auto nonblocking = native_socket::set_nonblocking(result.as_raw_socket(), true);
    if (nonblocking.is_err()) return Err(rstd::move(nonblocking).unwrap_err());
    if (address.is_ipv6()) {
        auto only_v6 = datagram_option(result.as_raw_socket(), IPPROTO_IPV6, IPV6_V6ONLY, 1);
        if (only_v6.is_err()) return Err(rstd::move(only_v6).unwrap_err());
    }
    return Ok(rstd::move(result));
}

auto configure_datagram(os::socket::RawSocket socket, bool packet_info) -> io::Result<empty> {
    int type {};
#if RSTD_OS_WINDOWS
    int size = sizeof(type);
#else
    socklen_t size = sizeof(type);
#endif
    if (::getsockopt(native_datagram_socket(socket),
                     SOL_SOCKET,
                     SO_TYPE,
                     reinterpret_cast<char*>(&type),
                     &size) != 0)
        return Err(datagram_error());
    if (type != SOCK_DGRAM)
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    auto nonblocking = native_socket::set_nonblocking(socket, true);
    if (nonblocking.is_err()) return nonblocking;
    auto local = native_socket::local_addr(socket);
    if (local.is_err()) return Err(rstd::move(local).unwrap_err());
    if (! packet_info) return Ok(empty {});
    if (! datagram_capabilities(*local).destination_address)
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
    if (local->is_ipv6()) {
#if RSTD_OS_WINDOWS
        return datagram_option(socket, IPPROTO_IPV6, IPV6_PKTINFO, 1);
#elif defined(IPV6_RECVPKTINFO)
        return datagram_option(socket, IPPROTO_IPV6, IPV6_RECVPKTINFO, 1);
#else
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
    }
#if defined(IP_PKTINFO)
    return datagram_option(socket, IPPROTO_IP, IP_PKTINFO, 1);
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
    auto destination = datagram_option(socket, IPPROTO_IP, IP_RECVDSTADDR, 1);
    if (destination.is_err()) return destination;
    return datagram_option(socket, IPPROTO_IP, IP_RECVIF, 1);
#else
    return Err(
        io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
#endif
}

DatagramMessage::DatagramMessage(void* data, usize capacity, net::SocketAddr local)
    : local_(local), capacity_(capacity) {
#if RSTD_OS_WINDOWS
    buffer_.buf            = static_cast<char*>(data);
    buffer_.len            = static_cast<ULONG>(capacity.to_primitive());
    message_.name          = reinterpret_cast<sockaddr*>(&address_);
    message_.namelen       = sizeof(address_);
    message_.lpBuffers     = &buffer_;
    message_.dwBufferCount = 1;
    message_.Control.buf   = control_;
    message_.Control.len   = sizeof(control_);
#else
    buffer_.iov_base        = data;
    buffer_.iov_len         = capacity.to_primitive();
    message_.msg_name       = &address_;
    message_.msg_namelen    = sizeof(address_);
    message_.msg_iov        = &buffer_;
    message_.msg_iovlen     = 1;
    message_.msg_control    = control_;
    message_.msg_controllen = sizeof(control_);
#endif
}

auto DatagramMessage::prepare_send(net::DatagramTarget target) -> io::Result<empty> {
    if (target.peer.is_ipv6() != local_.is_ipv6() ||
        (target.source.is_some() &&
         (target.source->local.is_ipv6() != local_.is_ipv6() ||
          (target.source->local.port() != u16() && target.source->local.port() != local_.port()))))
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    auto native = native_socket::addr_to_native(target.peer);
    __builtin_memcpy(&address_, &native.storage, sizeof(address_));
#if RSTD_OS_WINDOWS
    message_.namelen     = native.len;
    message_.Control.len = 0;
#else
    message_.msg_namelen    = native.len;
    message_.msg_controllen = 0;
#endif
    if (target.source.is_none()) return Ok(empty {});
    if (! datagram_capabilities(local_).source_selection)
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::Unsupported }));
    auto source = native_socket::addr_to_native(target.source->local);
#if RSTD_OS_WINDOWS
    auto* header = reinterpret_cast<WSACMSGHDR*>(control_);
#define RSTD_CMSG_DATA  WSA_CMSG_DATA
#define RSTD_CMSG_LEN   WSA_CMSG_LEN
#define RSTD_CMSG_SPACE WSA_CMSG_SPACE
#else
    auto* header = reinterpret_cast<cmsghdr*>(control_);
#define RSTD_CMSG_DATA  CMSG_DATA
#define RSTD_CMSG_LEN   CMSG_LEN
#define RSTD_CMSG_SPACE CMSG_SPACE
#endif
    if (local_.is_ipv6()) {
#if defined(IPV6_PKTINFO)
        in6_pktinfo info {};
        info.ipi6_addr     = reinterpret_cast<const sockaddr_in6*>(&source.storage)->sin6_addr;
        info.ipi6_ifindex  = target.source->interface_index.to_primitive();
        header->cmsg_level = IPPROTO_IPV6;
        header->cmsg_type  = IPV6_PKTINFO;
        header->cmsg_len   = RSTD_CMSG_LEN(sizeof(info));
        __builtin_memcpy(RSTD_CMSG_DATA(header), &info, sizeof(info));
#if RSTD_OS_WINDOWS
        message_.Control.len = RSTD_CMSG_SPACE(sizeof(info));
#else
        message_.msg_controllen = RSTD_CMSG_SPACE(sizeof(info));
#endif
#endif
    } else {
#if defined(IP_PKTINFO)
        in_pktinfo info {};
#if RSTD_OS_WINDOWS
        info.ipi_addr = reinterpret_cast<const sockaddr_in*>(&source.storage)->sin_addr;
#else
        if (target.source->interface_index > u32(0x7fffffff))
            return Err(io::error::Error::from_kind(
                io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
        info.ipi_spec_dst = reinterpret_cast<const sockaddr_in*>(&source.storage)->sin_addr;
#endif
        info.ipi_ifindex   = target.source->interface_index.to_primitive();
        header->cmsg_level = IPPROTO_IP;
        header->cmsg_type  = IP_PKTINFO;
        header->cmsg_len   = RSTD_CMSG_LEN(sizeof(info));
        __builtin_memcpy(RSTD_CMSG_DATA(header), &info, sizeof(info));
#if RSTD_OS_WINDOWS
        message_.Control.len = RSTD_CMSG_SPACE(sizeof(info));
#else
        message_.msg_controllen = RSTD_CMSG_SPACE(sizeof(info));
#endif
#endif
    }
#undef RSTD_CMSG_DATA
#undef RSTD_CMSG_LEN
#undef RSTD_CMSG_SPACE
    return Ok(empty {});
}

auto DatagramMessage::result(usize transferred) -> io::Result<net::DatagramInfo> {
    if (transferred > capacity_)
        return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
#if RSTD_OS_WINDOWS
    auto peer  = native_socket::addr_from_native(message_.name, message_.namelen);
    auto flags = message_.dwFlags;
#define RSTD_FIRST_HEADER WSA_CMSG_FIRSTHDR
#define RSTD_NEXT_HEADER  WSA_CMSG_NXTHDR
#define RSTD_CMSG_DATA    WSA_CMSG_DATA
#define RSTD_CMSG_LEN     WSA_CMSG_LEN
#else
    auto peer  = native_socket::addr_from_native(reinterpret_cast<const sockaddr*>(&address_),
                                                 message_.msg_namelen);
    auto flags = message_.msg_flags;
#define RSTD_FIRST_HEADER CMSG_FIRSTHDR
#define RSTD_NEXT_HEADER  CMSG_NXTHDR
#define RSTD_CMSG_DATA    CMSG_DATA
#define RSTD_CMSG_LEN     CMSG_LEN
#endif
    if (peer.is_err()) return Err(rstd::move(peer).unwrap_err());
    net::DatagramInfo out {
        rstd::move(peer).unwrap(), None(), (flags & MSG_TRUNC) != 0, (flags & MSG_CTRUNC) != 0
    };
    for (auto* header = RSTD_FIRST_HEADER(&message_); header != nullptr;
         header       = RSTD_NEXT_HEADER(&message_, header)) {
        if (header->cmsg_level == IPPROTO_IP) {
#if defined(IP_PKTINFO)
            if (header->cmsg_type == IP_PKTINFO &&
                header->cmsg_len >= RSTD_CMSG_LEN(sizeof(in_pktinfo))) {
                in_pktinfo info {};
                __builtin_memcpy(&info, RSTD_CMSG_DATA(header), sizeof(info));
                sockaddr_in address {};
                address.sin_family = AF_INET;
                address.sin_port   = htons(local_.port().to_primitive());
                address.sin_addr   = info.ipi_addr;
                auto local         = native_socket::addr_from_native(
                    reinterpret_cast<const sockaddr*>(&address), sizeof(address));
                if (local.is_err()) return Err(rstd::move(local).unwrap_err());
                out.packet =
                    Some(net::PacketInfo { rstd::move(local).unwrap(), u32(info.ipi_ifindex) });
            }
#elif defined(IP_RECVDSTADDR) && defined(IP_RECVIF)
            if (out.packet.is_none()) out.packet = Some(net::PacketInfo { local_, u32() });
            if (header->cmsg_type == IP_RECVDSTADDR &&
                header->cmsg_len >= CMSG_LEN(sizeof(in_addr))) {
                sockaddr_in address {};
                address.sin_family = AF_INET;
                address.sin_port   = htons(local_.port().to_primitive());
                __builtin_memcpy(&address.sin_addr, CMSG_DATA(header), sizeof(in_addr));
                auto local = native_socket::addr_from_native(
                    reinterpret_cast<const sockaddr*>(&address), sizeof(address));
                if (local.is_err()) return Err(rstd::move(local).unwrap_err());
                out.packet->local = rstd::move(local).unwrap();
            }
#if RSTD_VENDOR_APPLE
            if (header->cmsg_type == IP_RECVIF &&
                header->cmsg_len >= CMSG_LEN(sizeof(sockaddr_dl))) {
                sockaddr_dl interface {};
                __builtin_memcpy(&interface, CMSG_DATA(header), sizeof(interface));
                out.packet->interface_index = u32(interface.sdl_index);
            }
#endif
#endif
        }
#if defined(IPV6_PKTINFO)
        if (header->cmsg_level == IPPROTO_IPV6 && header->cmsg_type == IPV6_PKTINFO &&
            header->cmsg_len >= RSTD_CMSG_LEN(sizeof(in6_pktinfo))) {
            in6_pktinfo info {};
            __builtin_memcpy(&info, RSTD_CMSG_DATA(header), sizeof(info));
            sockaddr_in6 address {};
            address.sin6_family = AF_INET6;
            address.sin6_port   = htons(local_.port().to_primitive());
            address.sin6_addr   = info.ipi6_addr;
            auto* octets        = reinterpret_cast<const unsigned char*>(&address.sin6_addr);
            if (octets[0] == 0xfe && (octets[1] & 0xc0) == 0x80)
                address.sin6_scope_id = info.ipi6_ifindex;
            auto local = native_socket::addr_from_native(
                reinterpret_cast<const sockaddr*>(&address), sizeof(address));
            if (local.is_err()) return Err(rstd::move(local).unwrap_err());
            out.packet =
                Some(net::PacketInfo { rstd::move(local).unwrap(), u32(info.ipi6_ifindex) });
        }
#endif
    }
#undef RSTD_FIRST_HEADER
#undef RSTD_NEXT_HEADER
#undef RSTD_CMSG_DATA
#undef RSTD_CMSG_LEN
    return Ok(rstd::move(out));
}

#if RSTD_OS_WINDOWS
auto receive_extension(SOCKET socket) -> io::Result<LPFN_WSARECVMSG> {
    GUID            id = WSAID_WSARECVMSG;
    LPFN_WSARECVMSG function {};
    DWORD           bytes {};
    if (::WSAIoctl(socket,
                   SIO_GET_EXTENSION_FUNCTION_POINTER,
                   &id,
                   sizeof(id),
                   &function,
                   sizeof(function),
                   &bytes,
                   nullptr,
                   nullptr) != 0)
        return Err(datagram_error());
    return Ok(function);
}

auto start_datagram(os::socket::RawSocket socket, void* message, bool receiving, void* overlapped)
    -> io::Result<empty> {
    auto native = native_datagram_socket(socket);
    int  result;
    if (receiving) {
        auto function = receive_extension(native);
        if (function.is_err()) return Err(rstd::move(function).unwrap_err());
        result = (*function)(native,
                             static_cast<WSAMSG*>(message),
                             nullptr,
                             static_cast<OVERLAPPED*>(overlapped),
                             nullptr);
    } else {
        result = ::WSASendMsg(native,
                              static_cast<WSAMSG*>(message),
                              0,
                              nullptr,
                              static_cast<OVERLAPPED*>(overlapped),
                              nullptr);
    }
    if (result == 0 || ::WSAGetLastError() == WSA_IO_PENDING) return Ok(empty {});
    return Err(datagram_error());
}

auto finish_truncated_datagram(void* message, usize transferred) noexcept -> usize {
    auto& native = *static_cast<WSAMSG*>(message);
    native.dwFlags |= MSG_TRUNC;
    return transferred == usize() ? usize(native.lpBuffers[0].len) : transferred;
}
#endif

auto DatagramMessage::try_receive(os::socket::RawSocket socket) -> io::Result<usize> {
#if RSTD_OS_WINDOWS
    auto function = receive_extension(native_datagram_socket(socket));
    if (function.is_err()) return Err(rstd::move(function).unwrap_err());
    DWORD transferred {};
    if ((*function)(native_datagram_socket(socket), &message_, &transferred, nullptr, nullptr) == 0)
        return Ok(usize(transferred));
    if (::WSAGetLastError() == WSAEMSGSIZE) {
        return Ok(finish_truncated_datagram(&message_, usize(transferred)));
    }
    return Err(datagram_error());
#else
    decltype(::recvmsg(socket, &message_, 0)) result;
    do {
        result = ::recvmsg(socket, &message_, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return Err(datagram_error());
    return Ok(usize(result));
#endif
}

auto DatagramMessage::try_send(os::socket::RawSocket socket) -> io::Result<usize> {
#if RSTD_OS_WINDOWS
    DWORD transferred {};
    if (::WSASendMsg(
            native_datagram_socket(socket), &message_, 0, &transferred, nullptr, nullptr) != 0)
        return Err(datagram_error());
    return Ok(usize(transferred));
#else
#ifdef MSG_NOSIGNAL
    constexpr int flags = MSG_NOSIGNAL;
#else
    constexpr int flags = 0;
#endif
    decltype(::sendmsg(socket, &message_, flags)) result;
    do {
        result = ::sendmsg(socket, &message_, flags);
    } while (result < 0 && errno == EINTR);
    if (result < 0) return Err(datagram_error());
    return Ok(usize(result));
#endif
}
} // namespace rstd::sys::socket
