module;
#include <rstd/macro.hpp>

module rstd;
import :net.udp;
import rstd.sys.socket;

using namespace rstd::prelude;
namespace socket = rstd::sys::socket;

namespace rstd::net
{
auto receive_datagram(io::Result<async::IoOperation> prepared)
    -> async::coro<io::Result<Datagram>> {
    if (prepared.is_err()) co_return Err(rstd::move(prepared).unwrap_err());
    auto result = co_await rstd::move(prepared).unwrap();
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    auto completion = rstd::move(result).unwrap();
    if (completion.datagram().is_none())
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidData }));
    auto info = *completion.datagram();
    co_return Ok(Datagram { rstd::move(completion).into_data(), rstd::move(info) });
}

auto receive_datagram_into(async::IoOperation operation) -> async::coro<DatagramReceive> {
    auto result = co_await future::by_ref(operation);
    auto buffer = operation.take_datagram_buffer();
    if (result.is_err())
        co_return DatagramReceive { Err(rstd::move(result).unwrap_err()), rstd::move(buffer) };
    if (result->datagram().is_none()) {
        buffer.clear();
        co_return DatagramReceive { Err(io::Error::from_kind(
                                        io::ErrorKind { io::ErrorKind::InvalidData })),
                                    rstd::move(buffer) };
    }
    co_return DatagramReceive { Ok(*result->datagram()), rstd::move(buffer) };
}

auto send_datagram(io::Result<async::IoOperation> prepared) -> async::coro<io::Result<usize>> {
    if (prepared.is_err()) co_return Err(rstd::move(prepared).unwrap_err());
    auto result = co_await rstd::move(prepared).unwrap();
    if (result.is_err()) co_return Err(rstd::move(result).unwrap_err());
    co_return Ok(result->transferred());
}

auto detach_datagram(async::CompletionSource source)
    -> async::coro<io::Result<os::socket::OwnedSocket>> {
#if RSTD_OS_WINDOWS
    if (source.is_bound())
        co_return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::Unsupported }));
#endif
    auto retained = source.clone();
    auto released = co_await rstd::move(source).release();
    if (released.is_err()) co_return Err(rstd::move(released).unwrap_err());
    co_return retained.take_owned_socket();
}

auto UdpSocket::bind(SocketAddr address, bool packet_info) -> io::Result<UdpSocket> {
    auto created = socket::udp(address);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto owned = rstd::move(created).unwrap();
    auto bound = socket::bind(owned.as_raw_socket(), address);
    if (bound.is_err()) return Err(rstd::move(bound).unwrap_err());
    return from_owned_socket(rstd::move(owned), packet_info);
}

auto UdpSocket::connect(SocketAddr peer, bool packet_info) -> io::Result<UdpSocket> {
    if (peer.port() == u16())
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
    auto created = socket::udp(peer);
    if (created.is_err()) return Err(rstd::move(created).unwrap_err());
    auto owned     = rstd::move(created).unwrap();
    auto connected = socket::connect(owned.as_raw_socket(), peer);
    if (connected.is_err()) return Err(rstd::move(connected).unwrap_err());
    return from_owned_socket(rstd::move(owned), packet_info);
}

auto UdpSocket::from_owned_socket(os::socket::OwnedSocket owned, bool packet_info)
    -> io::Result<UdpSocket> {
    auto configured = socket::configure_datagram(owned.as_raw_socket(), packet_info);
    if (configured.is_err()) return Err(rstd::move(configured).unwrap_err());
    auto local = socket::local_addr(owned.as_raw_socket());
    if (local.is_err()) return Err(rstd::move(local).unwrap_err());
    if (local->port() == u16()) {
        auto bound = socket::bind(owned.as_raw_socket(), *local);
        if (bound.is_err()) return Err(rstd::move(bound).unwrap_err());
        local = socket::local_addr(owned.as_raw_socket());
        if (local.is_err()) return Err(rstd::move(local).unwrap_err());
    }
    return Ok(UdpSocket { async::CompletionSource::socket(rstd::move(owned)), *local });
}

auto UdpSocket::into_owned_socket() && -> async::coro<io::Result<os::socket::OwnedSocket>> {
    return detach_datagram(rstd::move(source_));
}

auto close_datagram(async::CompletionSource source) -> async::coro<io::Result<empty>> {
    auto retained = source.clone();
    auto released = co_await rstd::move(source).release();
    auto owned    = retained.take_owned_socket();
    if (owned.is_err()) {
        if (released.is_err()) co_return Err(rstd::move(released).unwrap_err());
        co_return Err(rstd::move(owned).unwrap_err());
    }
    {
        auto closing = rstd::move(owned).unwrap();
    }
    co_return released;
}

auto UdpSocket::close() && -> async::coro<io::Result<empty>> {
    return close_datagram(rstd::move(source_));
}

auto UdpSocket::capabilities() const noexcept -> DatagramCapabilities {
    return socket::datagram_capabilities(local_);
}

auto UdpSocket::peer_addr() const -> io::Result<SocketAddr> {
    return socket::peer_addr(source_.as_raw_fd());
}

auto UdpSocket::recv_into(bytes::BytesMut buffer) const -> async::coro<DatagramReceive> {
    return receive_datagram_into(
        async::IoOperation::recv_datagram_into(source_, rstd::move(buffer), local_));
}

auto UdpSocket::recv(usize maximum_size) const -> async::coro<io::Result<Datagram>> {
    return receive_datagram(async::IoOperation::recv_datagram(source_, maximum_size, local_));
}

auto UdpSocket::send(bytes::Bytes data, DatagramTarget target) const
    -> async::coro<io::Result<usize>> {
    return send_datagram(
        async::IoOperation::send_datagram(source_, rstd::move(data), rstd::move(target), local_));
}

auto UdpSocket::send_to(bytes::Bytes data, SocketAddr target) const
    -> async::coro<io::Result<usize>> {
    return send(rstd::move(data), DatagramTarget { target, None() });
}

auto UdpSocket::try_recv(usize maximum_size) const -> io::Result<Datagram> {
    if (maximum_size > usize(65535))
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
    auto buffer = bytes::BytesMut::with_capacity(maximum_size);
    auto info   = try_recv_into(buffer);
    if (info.is_err()) return Err(rstd::move(info).unwrap_err());
    return Ok(Datagram { buffer.freeze(), rstd::move(info).unwrap() });
}

auto UdpSocket::try_recv_into(bytes::BytesMut& buffer) const -> io::Result<DatagramInfo> {
    buffer.clear();
    auto                    writable = buffer.spare_capacity_mut();
    auto                    length   = rstd::min(writable.len(), usize(65535));
    socket::DatagramMessage message(writable.as_byte_ptr(), length, local_);
    auto                    received = message.try_receive(source_.as_raw_fd());
    if (received.is_err()) return Err(rstd::move(received).unwrap_err());
    auto info = message.result(*received);
    if (info.is_err()) return Err(rstd::move(info).unwrap_err());
    buffer.advance_mut_unchecked(*received);
    return info;
}

auto UdpSocket::try_send(bytes::Bytes const& data, DatagramTarget target) const
    -> io::Result<usize> {
    if (data.len() > usize(65535))
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidInput }));
    socket::DatagramMessage message(const_cast<byte*>(data.data()), data.len(), local_);
    auto                    prepared = message.prepare_send(rstd::move(target));
    if (prepared.is_err()) return Err(rstd::move(prepared).unwrap_err());
    auto sent = message.try_send(source_.as_raw_fd());
    if (sent.is_ok() && *sent != data.len())
        return Err(io::Error::from_kind(io::ErrorKind { io::ErrorKind::InvalidData }));
    return sent;
}
} // namespace rstd::net
