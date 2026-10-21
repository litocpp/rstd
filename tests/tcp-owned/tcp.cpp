#include <rstd/test/gtest.hpp>

import rstd;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

auto pair(async::Runtime& runtime) -> tuple<net::TcpStream, net::TcpStream> {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client =
        runtime.block_on(net::TcpStream::connect(listener.local_addr().unwrap())).unwrap();
    auto accepted = runtime.block_on(listener.accept()).unwrap();
    auto server   = rstd::move(accepted.get<0>());
    runtime.block_on(rstd::move(listener).close()).unwrap();
    return { rstd::move(client), rstd::move(server) };
}

TEST(TcpOwned, ReceiveAndSendOwnSocketsBeforeFirstPollAndAfterCallerDrop) {
    auto runtime   = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets   = pair(runtime);
    auto receiving = sockets.get<0>().recv(usize(4));
    auto sending   = sockets.get<1>().send_all(bytes::Bytes::copy_from_slice("kept"_bytes));
    auto pending   = [&] {
        auto old = rstd::move(sockets);
        return tuple { rstd::move(receiving), rstd::move(sending) };
    }();
    auto result = runtime.block_on(async::join(async::into_future(rstd::move(pending.get<0>())),
                                               async::into_future(rstd::move(pending.get<1>()))));
    EXPECT_EQ(result.get<0>().unwrap().as_slice(), "kept"_bytes);
    EXPECT_EQ(result.get<1>().unwrap(), usize(4));
}

TEST(TcpOwned, PreparedAcceptSurvivesListenerDrop) {
    auto            runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    net::SocketAddr address;
    auto            prepared = [&] {
        auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        address       = listener.local_addr().unwrap();
        return listener.accept();
    }();
    auto client   = runtime.block_on(net::TcpStream::connect(address)).unwrap();
    auto accepted = runtime.block_on(rstd::move(prepared)).unwrap();
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(accepted.get<0>()).close()).is_ok());
}

auto echo(const net::TcpStream& stream, usize remaining) -> async::coro<empty> {
    while (remaining != usize()) {
        auto chunk = (co_await stream.recv(usize(4096))).unwrap();
        EXPECT_FALSE(chunk.is_empty());
        if (chunk.is_empty()) break;
        remaining -= chunk.len();
        (co_await stream.send_all(rstd::move(chunk))).unwrap();
    }
    co_return empty {};
}

auto consume(const net::TcpStream& stream, usize count) -> async::coro<usize> {
    usize received;
    while (received < count) {
        auto chunk = (co_await stream.recv(usize(8192))).unwrap();
        if (chunk.is_empty()) break;
        for (auto byte : chunk.as_slice()) EXPECT_EQ(byte, u8(7));
        received += chunk.len();
    }
    co_return received;
}

TEST(TcpOwned, ConcurrentReadAndWriteKeepLargeBuffersAlive) {
    auto runtime = async::RuntimeBuilder::multi_thread()
                       .worker_threads(usize(2))
                       .enable_all()
                       .build()
                       .unwrap();
    auto sockets = pair(runtime);
    auto data    = bytes::BytesMut::with_capacity(usize(2 * 1024 * 1024));
    data.resize(usize(2 * 1024 * 1024), u8(7));
    auto server   = runtime.spawn(echo(sockets.get<1>(), data.len()));
    auto receiver = runtime.spawn(consume(sockets.get<0>(), data.len()));
    EXPECT_EQ(runtime.block_on(sockets.get<0>().send_all(data.freeze())).unwrap(),
              usize(2 * 1024 * 1024));
    EXPECT_EQ(runtime.block_on(rstd::move(receiver)).unwrap(), usize(2 * 1024 * 1024));
    EXPECT_TRUE(runtime.block_on(rstd::move(server)).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

TEST(TcpOwned, CancelThenCloseDrainsAndPeerObservesEof) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto receive = runtime.spawn(sockets.get<0>().recv());
    EXPECT_TRUE(
        runtime
            .block_on(async::timeout(future::by_ref(receive), time::Duration::from_millis(u64(2))))
            .is_err());
    receive.abort();
    EXPECT_TRUE(runtime.block_on(rstd::move(receive)).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(sockets.get<1>().recv()).unwrap().is_empty());
    EXPECT_TRUE(runtime.block_on(sockets.get<1>().recv(usize())).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

TEST(TcpOwned, PreparedCloseAndDetachOwnTheirResources) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto address  = listener.local_addr().unwrap();
    auto closing  = [&] {
        auto old = rstd::move(listener);
        return rstd::move(old).close();
    }();
    EXPECT_TRUE(runtime.block_on(rstd::move(closing)).is_ok());
    auto rebound  = net::TcpListener::bind(address).unwrap();
    auto detached = [&] {
        auto old = rstd::move(rebound);
        return rstd::move(old).into_owned_socket();
    }();
    auto imported =
        net::TcpListener::from_owned_socket(runtime.block_on(rstd::move(detached)).unwrap())
            .unwrap();
    EXPECT_EQ(imported.local_addr().unwrap(), address);
    EXPECT_TRUE(runtime.block_on(rstd::move(imported).close()).is_ok());
    auto udp         = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto udp_address = udp.local_addr();
    EXPECT_TRUE(runtime.block_on(rstd::move(udp).close()).is_ok());
    auto udp_rebound = net::UdpSocket::bind(udp_address).unwrap();
    EXPECT_TRUE(runtime.block_on(rstd::move(udp_rebound).close()).is_ok());
}

TEST(TcpOwned, CancelledLargeSendKeepsItsStorageUntilCloseCompletes) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto data    = bytes::BytesMut::with_capacity(usize(16 * 1024 * 1024));
    data.resize(usize(16 * 1024 * 1024), u8(9));
    auto sending = runtime.spawn(sockets.get<0>().send_all(data.freeze()));
    auto blocked = runtime.block_on(
        async::timeout(future::by_ref(sending), time::Duration::from_millis(u64(2))));
    EXPECT_TRUE(blocked.is_err());
    sending.abort();
    EXPECT_TRUE(runtime.block_on(rstd::move(sending)).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

TEST(TcpOwned, PreparedConnectMovesAndTransfersOnlyAfterHandshake) {
    auto runtime    = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto listener   = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto connecting = net::TcpStream::prepare_connect(listener.local_addr().unwrap()).unwrap();
    EXPECT_TRUE(rstd::move(connecting).into_stream().is_err());
    auto finishing = connecting.finish();
    EXPECT_TRUE(runtime.block_on(connecting.finish()).is_err());
    auto moved = rstd::move(connecting);
    EXPECT_TRUE(runtime.block_on(rstd::move(finishing)).is_ok());
    auto client = rstd::move(moved).into_stream().unwrap();
    EXPECT_TRUE(rstd::move(moved).into_stream().is_err());
    EXPECT_TRUE(runtime.block_on(moved.finish()).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(moved).close()).is_err());
    auto accepted = runtime.block_on(listener.accept()).unwrap();
    auto peer     = rstd::move(accepted.get<0>());
    EXPECT_EQ(
        runtime.block_on(client.send_all(bytes::Bytes::copy_from_slice("made"_bytes))).unwrap(),
        usize(4));
    EXPECT_EQ(runtime.block_on(peer.recv(usize(4))).unwrap().as_slice(), "made"_bytes);
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(peer.recv()).unwrap().is_empty());
    EXPECT_TRUE(runtime.block_on(rstd::move(peer).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

TEST(TcpOwned, PreparedFinishOwnsTheSocketAfterItsConnectorIsDropped) {
    auto runtime   = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto listener  = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto finishing = [&] {
        auto owner = net::TcpStream::prepare_connect(listener.local_addr().unwrap()).unwrap();
        return owner.finish();
    }();
    EXPECT_TRUE(runtime.block_on(rstd::move(finishing)).is_ok());
    auto accepted = runtime.block_on(listener.accept()).unwrap();
    auto eof      = runtime
                        .block_on(async::timeout(async::into_future(accepted.get<0>().recv()),
                                                 time::Duration::from_secs(u64(1))))
                        .unwrap();
    EXPECT_TRUE(eof.is_err() || eof.unwrap().is_empty());
    EXPECT_TRUE(runtime.block_on(rstd::move(accepted.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

#if defined(__linux__)
auto tcp_descriptor_count() -> usize {
    auto  path    = path::PathBuf::from("/proc/self/fd"_str);
    auto  entries = fs::read_dir(path.as_path()).unwrap();
    usize count;
    while (auto entry = entries.next()) {
        EXPECT_TRUE(entry->is_ok());
        ++count;
    }
    return count;
}
#endif

TEST(TcpOwned, CancellingUnpolledFinishKeepsTheOwnerClosableWithoutDescriptorGrowth) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto warm    = pair(runtime);
    EXPECT_TRUE(runtime.block_on(rstd::move(warm.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(warm.get<1>()).close()).is_ok());
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
#if defined(__linux__)
    auto before = tcp_descriptor_count();
#endif
    for (unsigned i = 0; i < 32; ++i) {
        auto connecting = net::TcpStream::prepare_connect(listener.local_addr().unwrap()).unwrap();
        {
            auto cancelled = connecting.finish();
        }
        EXPECT_TRUE(runtime.block_on(connecting.finish()).is_err());
        EXPECT_TRUE(rstd::move(connecting).into_stream().is_err());
        auto closing = rstd::move(connecting).close();
        EXPECT_TRUE(runtime.block_on(rstd::move(closing)).is_ok());
        EXPECT_TRUE(connecting.local_addr().is_err());
    }
    EXPECT_TRUE(listener.try_accept().is_err());
#if defined(__linux__)
    EXPECT_EQ(tcp_descriptor_count(), before);
#endif
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

TEST(TcpOwned, FailedHandshakeLeavesAnExplicitlyClosableOwner) {
    auto runtime  = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto address  = listener.local_addr().unwrap();
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
    auto connecting = net::TcpStream::prepare_connect(address).unwrap();
    EXPECT_TRUE(runtime.block_on(connecting.finish()).is_err());
    EXPECT_TRUE(rstd::move(connecting).into_stream().is_err());
    EXPECT_TRUE(runtime.block_on(connecting.finish()).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(connecting).close()).is_ok());
}

TEST(TcpOwned, InterruptedHandshakeAndCloseKeepAnOwnerUntilReleaseCompletes) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto warm    = pair(runtime);
    EXPECT_TRUE(runtime.block_on(rstd::move(warm.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(warm.get<1>()).close()).is_ok());
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
#if defined(__linux__)
    auto before = tcp_descriptor_count();
#endif
    usize interrupted_connect;
    usize interrupted_close;
    for (unsigned i = 0; i < 8; ++i) {
        auto connecting = net::TcpStream::prepare_connect(listener.local_addr().unwrap()).unwrap();
        auto result     = runtime.block_on(async::timeout(async::into_future(connecting.finish()),
                                                          time::Duration::from_millis(u64())));
        if (result.is_err())
            ++interrupted_connect;
        else
            EXPECT_TRUE(result.unwrap().is_ok());
        auto closing = async::into_future(rstd::move(connecting).close());
        auto early   = runtime.block_on(
            async::timeout(future::by_ref(closing), time::Duration::from_millis(u64())));
        if (early.is_err()) {
            ++interrupted_close;
            EXPECT_TRUE(runtime.block_on(rstd::move(closing)).is_ok());
        } else
            EXPECT_TRUE(early.unwrap().is_ok());
#if defined(__linux__)
        EXPECT_EQ(tcp_descriptor_count(), before);
#endif
        auto accepted = listener.try_accept();
        if (accepted.is_ok()) {
            auto peer = rstd::move(accepted).unwrap();
            auto eof  = runtime
                            .block_on(async::timeout(async::into_future(peer.get<0>().recv()),
                                                     time::Duration::from_secs(u64(1))))
                            .unwrap();
            EXPECT_TRUE(eof.is_err() || eof.unwrap().is_empty());
            EXPECT_TRUE(runtime.block_on(rstd::move(peer.get<0>()).close()).is_ok());
        }
    }
#if defined(__linux__)
    EXPECT_EQ(tcp_descriptor_count(), before);
#endif
    io::eprintln(
        "interrupted TCP connect/close: {}/8, {}/8", interrupted_connect, interrupted_close);
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

#if ! defined(_WIN32)
TEST(TcpOwned, PreparedFdDetachSurvivesStreamAndListenerDestruction) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto pending = [&] {
        auto old = rstd::move(sockets.get<0>());
        return rstd::move(old).into_owned_fd();
    }();
    auto restored =
        net::TcpStream::from_owned_fd(runtime.block_on(rstd::move(pending)).unwrap()).unwrap();
    EXPECT_TRUE(runtime.block_on(restored.send(bytes::Bytes::copy_from_slice("fd"_bytes))).is_ok());
    EXPECT_EQ(runtime.block_on(sockets.get<1>().recv()).unwrap().as_slice(), "fd"_bytes);
    auto listening = [] {
        auto old = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        return rstd::move(old).into_owned_fd();
    }();
    auto listener =
        net::TcpListener::from_owned_fd(runtime.block_on(rstd::move(listening)).unwrap()).unwrap();
    EXPECT_TRUE(listener.local_addr().is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(restored).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

struct ReplacePendingWrite {
    using Output = io::Result<usize>;
    net::TcpStream* stream;
    bytes::Bytes    abandoned   = bytes::Bytes::copy_from_slice("abandoned-long"_bytes);
    bytes::Bytes    replacement = bytes::Bytes::copy_from_slice("new"_bytes);
    bool            started     = false;
    auto poll(mut_ref<ReplacePendingWrite> self, task::Context& cx) -> task::Poll<Output> {
        if (! self->started) {
            auto old = async::io::write(*self->stream, self->abandoned);
            EXPECT_TRUE(future::poll(old, cx).is_pending());
            self->started = true;
        }
        return async::io::poll_write(*self->stream, cx, self->replacement);
    }
};

struct ReplacePendingRead {
    using Output = io::Result<usize>;
    net::TcpStream*  stream;
    net::TcpStream*  sender;
    bytes::BytesMut* replacement;
    bytes::BytesMut  abandoned = bytes::BytesMut::with_capacity(usize(32));
    bool             started   = false;
    auto poll(mut_ref<ReplacePendingRead> self, task::Context& cx) -> task::Poll<Output> {
        if (! self->started) {
            auto old = async::io::read(*self->stream, self->abandoned);
            EXPECT_TRUE(future::poll(old, cx).is_pending());
            EXPECT_TRUE(
                self->sender->try_write(bytes::Bytes::copy_from_slice("new"_bytes)).is_ok());
            self->started = true;
        }
        EXPECT_TRUE(self->abandoned.is_empty());
        return async::io::poll_read(*self->stream, cx, *self->replacement);
    }
};

TEST(TcpOwned, PendingWriteDoesNotConsumeAbandonedInput) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    EXPECT_EQ(runtime.block_on(ReplacePendingWrite { &sockets.get<0>() }).unwrap(), usize(3));
    EXPECT_EQ(runtime.block_on(sockets.get<1>().recv()).unwrap().as_slice(), "new"_bytes);
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

TEST(TcpOwned, PendingReadUsesReplacementBuffer) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto buffer  = bytes::BytesMut::make();
    EXPECT_EQ(runtime.block_on(ReplacePendingRead { &sockets.get<0>(), &sockets.get<1>(), &buffer })
                  .unwrap(),
              usize(3));
    EXPECT_EQ(buffer.as_slice(), "new"_bytes);
    EXPECT_TRUE(runtime.block_on(async::io::shutdown(sockets.get<1>())).is_ok());
    auto eof = bytes::BytesMut::make();
    EXPECT_EQ(runtime.block_on(async::io::read(sockets.get<0>(), eof)).unwrap(), usize());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}
#endif

TEST(TcpOwned, UnpolledSendAllAfterCloseReturnsNotConnected) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto sending = sockets.get<0>().send_all(bytes::Bytes::copy_from_slice("late"_bytes));
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    auto result = runtime.block_on(rstd::move(sending));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::NotConnected });
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}

#if defined(_WIN32)
TEST(TcpOwned, IocpBorrowedPollReportsUnsupported) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto sockets = pair(runtime);
    auto buffer  = bytes::BytesMut::with_capacity(usize(4));
    auto read    = runtime.block_on(async::io::read(sockets.get<0>(), buffer));
    ASSERT_TRUE(read.is_err());
    EXPECT_EQ(read.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    auto data  = bytes::Bytes::copy_from_slice("test"_bytes);
    auto write = runtime.block_on(async::io::write(sockets.get<0>(), data));
    ASSERT_TRUE(write.is_err());
    EXPECT_EQ(write.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sockets.get<1>()).close()).is_ok());
}
#endif
