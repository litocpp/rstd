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
    static auto bind(SocketAddr address, bool packet_info = false) -> io::Result<UdpSocket>;
    /// Selects one receive peer and the route's local address without a handshake.
    static auto connect(SocketAddr peer, bool packet_info = false) -> io::Result<UdpSocket>;
    static auto from_owned_socket(os::socket::OwnedSocket socket, bool packet_info = false)
        -> io::Result<UdpSocket>;
    /// Finish or drop outstanding operations before awaiting detach or close.
    auto into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>>;
    auto close() && -> async::coro<io::Result<empty>>;
    auto local_addr() const noexcept -> SocketAddr { return local_; }
    auto peer_addr() const -> io::Result<SocketAddr>;
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
