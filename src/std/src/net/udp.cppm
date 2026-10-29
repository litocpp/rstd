export module rstd:net.udp;
export import rstd.net.datagram;
export import rstd.io;
export import :async;
export import :bytes;
import rstd.os.socket;

using namespace rstd::prelude;

namespace rstd::net
{
export struct Datagram {
    bytes::Bytes data;
    DatagramInfo info;
};

export struct DatagramReceive {
    io::Result<DatagramInfo> result;
    bytes::BytesMut          buffer;
};

export struct UdpSocketOptions {
    bool reuse_address {};
    bool packet_info {};
};

export class UdpSocket;
export class UdpSocketBuilder {
    os::socket::OwnedSocket socket_;
    explicit UdpSocketBuilder(os::socket::OwnedSocket socket): socket_(rstd::move(socket)) {}

public:
    UdpSocketBuilder(UdpSocketBuilder&&) noexcept                           = default;
    auto        operator=(UdpSocketBuilder&&) noexcept -> UdpSocketBuilder& = default;
    static auto make_v4() -> io::Result<UdpSocketBuilder>;
    static auto make_v6() -> io::Result<UdpSocketBuilder>;
    static auto from_owned_socket(os::socket::OwnedSocket socket) -> UdpSocketBuilder {
        return UdpSocketBuilder(rstd::move(socket));
    }
    auto as_socket() const noexcept [[clang::lifetimebound]] -> os::socket::BorrowedSocket {
        return socket_.as_socket();
    }
    auto into_owned_socket() && -> os::socket::OwnedSocket { return rstd::move(socket_); }
    auto bind(SocketAddr address, UdpSocketOptions options = {}) && -> io::Result<UdpSocket>;
    auto connect(SocketAddr peer, UdpSocketOptions options = {}) && -> io::Result<UdpSocket>;
};

export class UdpSocket {
    async::CompletionSource source_;
    SocketAddr              local_;

    UdpSocket(async::CompletionSource source, SocketAddr local)
        : source_(rstd::move(source)), local_(local) {}

public:
    UdpSocket(const UdpSocket&)                        = delete;
    auto operator=(const UdpSocket&) -> UdpSocket&     = delete;
    UdpSocket(UdpSocket&&) noexcept                    = default;
    auto operator=(UdpSocket&&) noexcept -> UdpSocket& = default;

    /// New IPv6 sockets are IPv6-only. Packet metadata is opt-in.
    static auto bind(SocketAddr address, bool packet_info = false, bool reuse_address = false)
        -> io::Result<UdpSocket>;
    /// Selects one receive peer and the route's local address without a handshake.
    static auto connect(SocketAddr peer, bool packet_info = false) -> io::Result<UdpSocket>;
    static auto bind_with(SocketAddr address, UdpSocketOptions options) -> io::Result<UdpSocket>;
    static auto connect_with(SocketAddr peer, UdpSocketOptions options) -> io::Result<UdpSocket>;
    static auto from_owned_socket(os::socket::OwnedSocket socket, bool packet_info = false)
        -> io::Result<UdpSocket>;
    /// Finish or drop outstanding operations before awaiting detach or close.
    auto into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
    auto close() && -> async::coro<io::Result<empty>>;
    auto local_addr() const noexcept -> SocketAddr { return local_; }
    auto peer_addr() const -> io::Result<SocketAddr>;
    /// Returns the kernel's current IP path MTU in bytes, including IP headers.
    /// Requires a connected peer and does not probe or change discovery policy.
    auto path_mtu() const -> io::Result<u32>;
    auto join_multicast(SocketAddr group, u32 interface_index) const -> io::Result<empty>;
    auto leave_multicast(SocketAddr group, u32 interface_index) const -> io::Result<empty>;
    auto set_multicast_ttl(u32 ttl) const -> io::Result<empty>;
    auto set_multicast_loop(bool enabled) const -> io::Result<empty>;
    auto set_ttl(u32 ttl) const -> io::Result<empty>;
    auto capabilities() const noexcept -> DatagramCapabilities;
    /// Cancellation retains resources until safe, but may consume a datagram.
    /// Overwrites the buffer; errors return it empty. Completion always returns its allocator.
    auto recv_into(bytes::BytesMut buffer) const -> async::coro<DatagramReceive>;
    /// Does not allocate or clear spare capacity. WouldBlock leaves an empty buffer.
    auto try_recv_into(bytes::BytesMut& buffer) const -> io::Result<DatagramInfo>;
    auto recv(usize maximum_size = usize(65535)) const -> async::coro<io::Result<Datagram>>;
    auto send(bytes::Bytes data, DatagramTarget target) const -> async::coro<io::Result<usize>>;
    auto send_to(bytes::Bytes data, SocketAddr target) const -> async::coro<io::Result<usize>>;
    auto try_recv(usize maximum_size = usize(65535)) const -> io::Result<Datagram>;
    auto try_send(bytes::Bytes const& data, DatagramTarget target) const -> io::Result<usize>;
};
} // namespace rstd::net
