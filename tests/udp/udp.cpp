#include <rstd/test/gtest.hpp>
#if ! defined(_WIN32)
#include <fcntl.h>
#endif

import rstd;
import rstd.test;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd;

auto wait_try_receive(const net::UdpSocket& socket, usize size)
    -> async::coro<io::Result<net::Datagram>> {
    while (true) {
        auto received = socket.try_recv(size);
        if (received.is_ok() ||
            received.unwrap_err().kind() != io::ErrorKind { io::ErrorKind::WouldBlock })
            co_return received;
        co_await async::sleep(time::Duration::from_millis(u64(1)));
    }
}

auto receive_with_timeout(async::Runtime&       runtime,
                          const net::UdpSocket& socket,
                          usize                 length = usize(65535)) {
    return runtime.block_on(
        async::timeout(async::into_future(socket.recv(length)), time::Duration::from_secs(u64(1))));
}

void verify_datagram(net::SocketAddr bind_address) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(bind_address, true).unwrap();
    auto sender   = net::UdpSocket::bind(bind_address).unwrap();
    auto sent     = runtime.block_on(
        sender.send_to(bytes::Bytes::copy_from_slice("datagram"_bytes), receiver.local_addr()));
    ASSERT_TRUE(sent.is_ok());
    EXPECT_EQ(*sent, usize(8));
    auto received = receive_with_timeout(runtime, receiver);
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    const auto& packet = received->unwrap();
    EXPECT_EQ(packet.data.len(), usize(8));
    EXPECT_EQ(packet.data[usize()], u8('d'));
    EXPECT_EQ(packet.info.peer, sender.local_addr());
    ASSERT_TRUE(packet.info.packet.is_some());
    EXPECT_EQ(packet.info.packet->local, receiver.local_addr());
    EXPECT_TRUE(packet.info.packet->interface_index > u32());
    EXPECT_FALSE(packet.info.truncated);
    EXPECT_FALSE(packet.info.control_truncated);
}

TEST(Udp, Ipv4DataAndMetadata) {
    verify_datagram(net::SocketAddr::ipv4_loopback(u16()));
}
TEST(Udp, Ipv6DataAndMetadata) {
    verify_datagram(net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16()));
}

void verify_connected_datagram(net::SocketAddr address) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto server  = net::UdpSocket::bind(address).unwrap();
    auto client  = net::UdpSocket::connect(server.local_addr(), true).unwrap();
    EXPECT_EQ(client.peer_addr().unwrap(), server.local_addr());
    EXPECT_EQ(client.local_addr(),
              server.local_addr().is_ipv4()
                  ? net::SocketAddr::ipv4_loopback(client.local_addr().port())
                  : net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), client.local_addr().port()));
    ASSERT_TRUE(runtime
                    .block_on(client.send_to(bytes::Bytes::copy_from_slice("query"_bytes),
                                             server.local_addr()))
                    .is_ok());
    auto incoming = receive_with_timeout(runtime, server).unwrap().unwrap();
    EXPECT_EQ(incoming.info.peer, client.local_addr());
    auto alien = net::UdpSocket::bind(address).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(alien.send_to(bytes::Bytes::copy_from_slice("alien"_bytes),
                                            client.local_addr()))
                    .is_ok());
    ASSERT_TRUE(runtime
                    .block_on(server.send_to(bytes::Bytes::copy_from_slice("reply"_bytes),
                                             client.local_addr()))
                    .is_ok());
    auto response = receive_with_timeout(runtime, client).unwrap().unwrap();
    EXPECT_EQ(response.data.as_slice(), "reply"_bytes);
    EXPECT_EQ(response.info.peer, server.local_addr());
    ASSERT_TRUE(response.info.packet.is_some());
    EXPECT_EQ(response.info.packet->local, client.local_addr());
#if ! defined(_WIN32)
    auto released = runtime.block_on(rstd::move(client).into_owned_socket()).unwrap();
    auto imported = net::UdpSocket::from_owned_socket(rstd::move(released)).unwrap();
    EXPECT_EQ(imported.peer_addr().unwrap(), server.local_addr());
#endif
}

TEST(Udp, ConnectedIpv4SelectsSourceAndFiltersReceivePeer) {
    verify_connected_datagram(net::SocketAddr::ipv4_loopback(u16()));
    EXPECT_TRUE(net::UdpSocket::connect(net::SocketAddr::ipv4_loopback(u16())).is_err());
}

TEST(Udp, ConnectedIpv6SelectsSourceAndFiltersReceivePeer) {
    verify_connected_datagram(net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16()));
}

#if defined(__linux__)
TEST(Udp, ConnectedSocketRejectsADifferentLocalDestination) {
    auto runtime     = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto server      = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client      = net::UdpSocket::connect(server.local_addr()).unwrap();
    auto wrong_local = net::SocketAddr::ipv4(net::Ipv4Addr::make(u8(127), u8(), u8(), u8(2)),
                                             client.local_addr().port());
    ASSERT_TRUE(
        runtime.block_on(server.send_to(bytes::Bytes::copy_from_slice("wrong"_bytes), wrong_local))
            .is_ok());
    ASSERT_TRUE(runtime
                    .block_on(server.send_to(bytes::Bytes::copy_from_slice("right"_bytes),
                                             client.local_addr()))
                    .is_ok());
    auto received = receive_with_timeout(runtime, client).unwrap().unwrap();
    EXPECT_EQ(received.data.as_slice(), "right"_bytes);
}
#endif

TEST(Udp, ZeroLengthAndTruncatedDatagramsAreDistinctFromEof) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime.block_on(sender.send_to(bytes::Bytes {}, receiver.local_addr())).is_ok());
    auto empty = receive_with_timeout(runtime, receiver, usize());
    ASSERT_TRUE(empty.is_ok());
    ASSERT_TRUE(empty->is_ok());
    EXPECT_TRUE(empty->unwrap().data.is_empty());
    EXPECT_FALSE(empty->unwrap().info.truncated);
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("0123456789"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto short_packet = receive_with_timeout(runtime, receiver, usize(4));
    ASSERT_TRUE(short_packet.is_ok());
    ASSERT_TRUE(short_packet->is_ok());
    EXPECT_EQ(short_packet->unwrap().data.len(), usize(4));
    EXPECT_TRUE(short_packet->unwrap().info.truncated);
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("next"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto next = receive_with_timeout(runtime, receiver);
    ASSERT_TRUE(next.is_ok());
    ASSERT_TRUE(next->is_ok());
    EXPECT_EQ(next->unwrap().data.len(), usize(4));
    EXPECT_FALSE(next->unwrap().info.truncated);
}

TEST(Udp, ZeroCapacityConsumesOneDatagram) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("x"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto result = receive_with_timeout(runtime, receiver, usize());
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_ok());
    EXPECT_TRUE(result->unwrap().data.is_empty());
    EXPECT_TRUE(result->unwrap().info.truncated);
    auto none = receiver.try_recv();
    ASSERT_TRUE(none.is_err());
    EXPECT_EQ(none.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::WouldBlock });
}

TEST(Udp, ReceiveOwnsSocketBeforeFirstPollAndAfterCallerDrop) {
    auto            runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    net::SocketAddr address;
    auto            pending = [&address] {
        auto socket = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        address     = socket.local_addr();
        return socket.recv(usize(4));
    }();
    auto sender = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(
        runtime.block_on(sender.send_to(bytes::Bytes::copy_from_slice("kept"_bytes), address))
            .is_ok());
    auto received = runtime.block_on(
        async::timeout(async::into_future(rstd::move(pending)), time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    EXPECT_EQ(received->unwrap().data.len(), usize(4));
}

TEST(Udp, MovingSocketKeepsPreparedOperationsValid) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto socket  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto pending = socket.recv(usize(4));
    auto moved   = rstd::move(socket);
    auto sender  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("move"_bytes),
                                             moved.local_addr()))
                    .is_ok());
    auto received = runtime.block_on(
        async::timeout(async::into_future(rstd::move(pending)), time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    EXPECT_EQ(received->unwrap().data.len(), usize(4));
}

TEST(Udp, SyncTryAndInvalidArguments) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto data     = bytes::Bytes::copy_from_slice("sync"_bytes);
    ASSERT_TRUE(sender.try_send(data, { receiver.local_addr(), None() }).is_ok());
    auto received =
        runtime.block_on(async::timeout(async::into_future(wait_try_receive(receiver, usize(4))),
                                        time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    EXPECT_EQ(received->unwrap().data.len(), usize(4));
    ASSERT_TRUE(sender.try_send(data, { receiver.local_addr(), None() }).is_ok());
    auto truncated =
        runtime.block_on(async::timeout(async::into_future(wait_try_receive(receiver, usize(2))),
                                        time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(truncated.is_ok());
    ASSERT_TRUE(truncated->is_ok());
    EXPECT_EQ(truncated->unwrap().data.len(), usize(2));
    EXPECT_TRUE(truncated->unwrap().info.truncated);
    auto none = receiver.try_recv();
    ASSERT_TRUE(none.is_err());
    EXPECT_EQ(none.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::WouldBlock });
    EXPECT_TRUE(receiver.try_recv(usize(65536)).is_err());
    EXPECT_TRUE(
        sender.try_send(data, { net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16(53)), None() })
            .is_err());
    auto wrong_source = net::PacketInfo {
        net::SocketAddr::ipv4_loopback(u16(sender.local_addr().port().to_primitive() == 1 ? 2 : 1)),
        u32()
    };
    EXPECT_TRUE(sender.try_send(data, { receiver.local_addr(), Some(wrong_source) }).is_err());
}

TEST(Udp, UnpolledReceiveCanBeDroppedBeforeSocketExtraction) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto socket  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto address = socket.local_addr();
    {
        auto dropped = socket.recv();
        (void)dropped;
    }
    auto detached = runtime.block_on(rstd::move(socket).into_owned_socket());
    ASSERT_TRUE(detached.is_ok());
    auto restored = net::UdpSocket::from_owned_socket(rstd::move(detached).unwrap()).unwrap();
    EXPECT_EQ(restored.local_addr(), address);
}

auto dual_transport(net::UdpSocket& server, net::UdpSocket& client, net::TcpListener& listener)
    -> async::coro<bool> {
    auto sent =
        co_await client.send_to(bytes::Bytes::copy_from_slice("both"_bytes), server.local_addr());
    if (sent.is_err()) co_return false;
    auto packet = co_await server.recv();
    if (packet.is_err()) co_return false;
    auto connected = co_await net::TcpStream::connect(server.local_addr());
    if (connected.is_err()) co_return false;
    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return false;
    auto stream  = rstd::move(connected).unwrap();
    auto tuple   = rstd::move(accepted).unwrap();
    auto peer    = rstd::move(tuple.template get<0>());
    auto written = co_await stream.send_all(rstd::move(packet->data));
    if (written.is_err()) co_return false;
    auto buffer = bytes::BytesMut::with_capacity(usize(4));
    while (buffer.len() != usize(4)) {
        auto read = co_await peer.recv(usize(4) - buffer.len());
        if (read.is_err() || read->is_empty()) co_return false;
        buffer.extend_from_slice(read->as_slice());
    }
    co_return buffer.as_slice() == "both"_bytes;
}

TEST(Udp, UdpAndTcpSharePortAndCompletionRuntime) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto server   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto listener = net::TcpListener::bind(server.local_addr()).unwrap();
    auto result   = runtime.block_on(
        async::timeout(async::into_future(dual_transport(server, client, listener)),
                       time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(*result);
}

TEST(Udp, MultiThreadRuntimeSharesCompletionSource) {
    auto runtime  = async::RuntimeBuilder::multi_thread()
                        .worker_threads(usize(2))
                        .enable_all()
                        .build()
                        .unwrap();
    auto server   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto listener = net::TcpListener::bind(server.local_addr()).unwrap();
    auto result   = runtime.block_on(
        async::timeout(async::into_future(dual_transport(server, client, listener)),
                       time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(result.is_ok());
    EXPECT_TRUE(*result);
}

#if ! defined(_WIN32)
TEST(Udp, OwnedFileCompletionReleaseStillClosesDescriptor) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto file    = fs::File::open("/dev/zero"_str).unwrap();
    auto raw     = file.as_raw_fd();
    auto source =
        async::CompletionSource::file(os::fd::OwnedFd::from_raw_fd(rstd::move(file).into_raw_fd()));
    auto clone = source.clone();
    auto read  = runtime.block_on(async::IoOperation::read(source, usize(4)));
    ASSERT_TRUE(read.is_ok());
    EXPECT_EQ(read->transferred(), usize(4));
    auto closed = runtime.block_on(rstd::move(source).release());
    ASSERT_TRUE(closed.is_ok());
    EXPECT_LT(::fcntl(raw, F_GETFD), 0);
}

TEST(Udp, CancelDrainDetachAndReimport) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto socket  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto handle  = runtime.spawn(socket.recv());
    auto timed   = runtime.block_on(
        async::timeout(future::by_ref(handle), time::Duration::from_millis(u64(2))));
    ASSERT_TRUE(timed.is_err());
    handle.abort();
    EXPECT_TRUE(runtime.block_on(rstd::move(handle)).is_err());
    auto detached = runtime.block_on(rstd::move(socket).into_owned_socket());
    ASSERT_TRUE(detached.is_ok());
    auto restored = net::UdpSocket::from_owned_socket(rstd::move(detached).unwrap()).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("after"_bytes),
                                             restored.local_addr()))
                    .is_ok());
    auto received = receive_with_timeout(runtime, restored);
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    EXPECT_EQ(received->unwrap().data.len(), usize(5));
}
#endif

#if defined(__linux__)
TEST(Udp, WildcardReplyUsesActualDestinationAddress) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto server  = net::UdpSocket::bind(net::SocketAddr::ipv4_any(u16()), true).unwrap();
    ASSERT_TRUE(server.capabilities().source_selection);
    auto client      = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto destination = net::SocketAddr::ipv4(net::Ipv4Addr::make(u8(127), u8(), u8(), u8(2)),
                                             server.local_addr().port());
    ASSERT_TRUE(
        runtime.block_on(client.send_to(bytes::Bytes::copy_from_slice("route"_bytes), destination))
            .is_ok());
    auto request = receive_with_timeout(runtime, server);
    ASSERT_TRUE(request.is_ok());
    ASSERT_TRUE(request->is_ok());
    const auto& packet = request->unwrap();
    ASSERT_TRUE(packet.info.packet.is_some());
    EXPECT_EQ(packet.info.packet->local, destination);
    auto replied = runtime.block_on(
        server.send(packet.data.clone(), { packet.info.peer, packet.info.packet }));
    ASSERT_TRUE(replied.is_ok());
    auto response = receive_with_timeout(runtime, client);
    ASSERT_TRUE(response.is_ok());
    ASSERT_TRUE(response->is_ok());
    EXPECT_EQ(response->unwrap().info.peer, destination);
}
#endif

TEST(Udp, OwnedReceiveReusesAllocationAndPreservesAllocator) {
    auto runtime    = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16()), true).unwrap();
    auto sender     = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto allocator  = rstd::alloc::SharedTrackingAllocator<>::make();
    auto allocation = Vec<u8, decltype(allocator)>::with_capacity_aligned_in(
        usize(32), usize(64), allocator.clone());
    auto buffer   = bytes::BytesMut::from_vec(rstd::move(allocation));
    auto before   = allocator.snapshot();
    auto pointer  = buffer.data();
    auto capacity = buffer.capacity();
    for (unsigned i = 0; i < 8; ++i) {
        auto message = i % 2 ? "x"_bytes : "longer"_bytes;
        ASSERT_TRUE(runtime
                        .block_on(sender.send_to(bytes::Bytes::copy_from_slice(message),
                                                 receiver.local_addr()))
                        .is_ok());
        auto received = runtime.block_on(receiver.recv_into(rstd::move(buffer)));
        ASSERT_TRUE(received.result.is_ok());
        EXPECT_EQ(received.buffer.as_slice(), message);
        EXPECT_EQ(received.buffer.data(), pointer);
        EXPECT_EQ(received.buffer.capacity(), capacity);
        EXPECT_EQ(received.buffer.alignment(), usize(64));
        EXPECT_TRUE(received.result->packet.is_some());
        buffer = rstd::move(received.buffer);
    }
    EXPECT_EQ(allocator.snapshot().allocations, before.allocations);
    EXPECT_EQ(allocator.snapshot().grows, before.grows);
    EXPECT_EQ(allocator.snapshot().deallocations, before.deallocations);
    auto none = receiver.try_recv_into(buffer);
    ASSERT_TRUE(none.is_err());
    EXPECT_EQ(none.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::WouldBlock });
    EXPECT_TRUE(buffer.is_empty());
    EXPECT_EQ(buffer.capacity(), capacity);
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("sync"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    ASSERT_TRUE(receiver.try_recv_into(buffer).is_ok());
    EXPECT_EQ(buffer.as_slice(), "sync"_bytes);
    EXPECT_EQ(allocator.snapshot().allocations, before.allocations);
    EXPECT_EQ(allocator.snapshot().grows, before.grows);
    buffer.reserve(capacity + usize(1));
    EXPECT_TRUE(allocator.snapshot().grows > before.grows);
}

TEST(Udp, OwnedReceiveErrorReturnsOriginalStorage) {
    auto runtime   = async::RuntimeBuilder::current_thread().build().unwrap();
    auto receiver  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto allocator = rstd::alloc::SharedTrackingAllocator<>::make();
    auto buffer    = bytes::BytesMut::with_capacity_in(usize(32), allocator.clone());
    buffer.extend_from_slice("old"_bytes);
    auto pointer  = buffer.data();
    auto received = runtime.block_on(receiver.recv_into(rstd::move(buffer)));
    ASSERT_TRUE(received.result.is_err());
    EXPECT_EQ(received.result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    EXPECT_TRUE(received.buffer.is_empty());
    EXPECT_EQ(received.buffer.data(), pointer);
    EXPECT_EQ(received.buffer.capacity(), usize(32));
    auto before = allocator.snapshot();
    received.buffer.reserve(usize(64));
    EXPECT_TRUE(allocator.snapshot().grows > before.grows);
}

TEST(Udp, ReusableBufferTruncationAndZeroCapacity) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("long"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto short_read =
        runtime.block_on(receiver.recv_into(bytes::BytesMut::with_capacity(usize(2))));
    ASSERT_TRUE(short_read.result.is_ok());
    EXPECT_TRUE(short_read.result->truncated);
    EXPECT_EQ(short_read.buffer.as_slice(), "lo"_bytes);
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("x"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto empty = runtime.block_on(receiver.recv_into(bytes::BytesMut {}));
    ASSERT_TRUE(empty.result.is_ok());
    EXPECT_TRUE(empty.result->truncated);
    EXPECT_TRUE(empty.buffer.is_empty());
    ASSERT_TRUE(runtime.block_on(sender.send_to(bytes::Bytes {}, receiver.local_addr())).is_ok());
    auto zero = runtime.block_on(receiver.recv_into(rstd::move(short_read.buffer)));
    ASSERT_TRUE(zero.result.is_ok());
    EXPECT_FALSE(zero.result->truncated);
    EXPECT_TRUE(zero.buffer.is_empty());
}

TEST(Udp, SpareCapacityDoesNotClearOrExposeHistoricalBytes) {
    auto buffer = bytes::BytesMut::with_capacity(usize(16));
    buffer.resize(usize(16), u8(0xa5));
    buffer.clear();
    auto spare = buffer.spare_capacity_mut();
    EXPECT_EQ(spare.len(), usize(16));
    EXPECT_TRUE(buffer.is_empty());
    // The allocation was initialized above; observing its sentinel detects unwanted clearing.
    EXPECT_EQ(static_cast<unsigned char>(spare.as_byte_ptr()[15]), 0xa5);
    spare.as_byte_ptr()[0] = byte('x');
    buffer.advance_mut_unchecked(usize(1));
    EXPECT_EQ(buffer.as_slice(), "x"_bytes);
}

TEST(Udp, OversizedReusableBufferKeepsCapacity) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto receiver = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto sender   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("big"_bytes),
                                             receiver.local_addr()))
                    .is_ok());
    auto result =
        runtime.block_on(receiver.recv_into(bytes::BytesMut::with_capacity(usize(65536))));
    ASSERT_TRUE(result.result.is_ok());
    EXPECT_EQ(result.buffer.as_slice(), "big"_bytes);
    EXPECT_EQ(result.buffer.capacity(), usize(65536));
}
