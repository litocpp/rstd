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
#endif

export module rstd.sys.socket:datagram;
export import rstd.net.datagram;
export import rstd.os.socket;
export import rstd.io.error;

namespace rstd::sys::socket
{
export auto udp(net::SocketAddr address) -> io::Result<os::socket::OwnedSocket>;
export auto datagram_capabilities(net::SocketAddr address) noexcept -> net::DatagramCapabilities;
export auto configure_datagram(os::socket::RawSocket socket, bool packet_info) -> io::Result<empty>;

export auto
multicast_membership(os::socket::RawSocket socket, net::SocketAddr group, u32 index, bool join)
    -> io::Result<empty>;
export auto datagram_ttl(os::socket::RawSocket socket, bool ipv6, u32 ttl, bool multicast)
    -> io::Result<empty>;
export auto multicast_loop(os::socket::RawSocket socket, bool ipv6, bool enabled)
    -> io::Result<empty>;

export class DatagramMessage {
#if RSTD_OS_WINDOWS
    WSAMSG           message_ {};
    WSABUF           buffer_ {};
    sockaddr_storage address_ {};
    alignas(WSACMSGHDR) char control_[256] {};
#else
    ::msghdr           message_ {};
    ::iovec            buffer_ {};
    ::sockaddr_storage address_ {};
    alignas(::cmsghdr) char control_[256] {};
#endif
    net::SocketAddr local_;
    usize           capacity_;

public:
    DatagramMessage(void* data, usize capacity, net::SocketAddr local);
    DatagramMessage(const DatagramMessage&)                    = delete;
    auto operator=(const DatagramMessage&) -> DatagramMessage& = delete;
    auto prepare_send(net::DatagramTarget target) -> io::Result<empty>;
    auto result(usize transferred) -> io::Result<net::DatagramInfo>;
    auto try_receive(os::socket::RawSocket socket) -> io::Result<usize>;
    auto try_send(os::socket::RawSocket socket) -> io::Result<usize>;
    auto native_message() noexcept -> void* { return &message_; }
};

#if RSTD_OS_WINDOWS
export auto
start_datagram(os::socket::RawSocket socket, void* message, bool receiving, void* overlapped)
    -> io::Result<empty>;
export auto finish_truncated_datagram(void* message, usize transferred) noexcept -> usize;
#endif
} // namespace rstd::sys::socket
