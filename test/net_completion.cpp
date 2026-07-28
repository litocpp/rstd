#include <gtest/gtest.h>

import rstd;

using namespace rstd;
using namespace rstd::literals;
using namespace rstd::prelude;

namespace
{

auto round_trip(net::TcpListener& listener, net::SocketAddr address)
    -> async::coro<io::Result<bytes::BytesMut>> {
    auto connected = co_await net::TcpStream::connect(address);
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client  = rstd::move(connected).unwrap_unchecked();
    auto pair    = rstd::move(accepted).unwrap_unchecked();
    auto server  = rstd::move(pair.template get<0>());
    auto payload = bytes::Bytes::copy_from_slice("iocp"_bytes);
    auto written = co_await async::io::write_all(client, payload);
    if (written.is_err()) co_return Err(rstd::move(written).unwrap_err_unchecked());

    auto received = bytes::BytesMut::with_capacity(usize(4));
    auto read     = co_await async::io::read_exact(server, received, usize(4));
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());

    auto shutdown = client.shutdown();
    if (shutdown.is_err()) co_return Err(rstd::move(shutdown).unwrap_err_unchecked());
    auto eof_buffer = bytes::BytesMut::with_capacity(usize(1));
    auto eof        = co_await async::io::read(server, eof_buffer);
    if (eof.is_err()) co_return Err(rstd::move(eof).unwrap_err_unchecked());
    if (rstd::move(eof).unwrap_unchecked() != usize()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
    }
    co_return Ok(rstd::move(received));
}

#if defined(_WIN32)
auto detach_connected_socket(net::SocketAddr address)
    -> async::coro<io::Result<os::socket::OwnedSocket>> {
    auto connected = co_await net::TcpStream::connect(address);
    if (connected.is_err()) co_return Err(rstd::move(connected).unwrap_err_unchecked());
    co_return co_await rstd::move(connected).unwrap_unchecked().into_owned_socket();
}
#endif

} // namespace

TEST(NetCompletion, LoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());
    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto address      = tcp_listener.local_addr();
    ASSERT_TRUE(address.is_ok());

    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result =
        runtime.block_on(round_trip(tcp_listener, rstd::move(address).unwrap_unchecked()));

    ASSERT_TRUE(result.is_ok());
    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), usize(4));
    EXPECT_EQ(received[usize()], u8('i'));
    EXPECT_EQ(received[usize(3)], u8('p'));
}

TEST(NetCompletion, Ipv6LoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16()));
    ASSERT_TRUE(listener.is_ok());
    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto address      = tcp_listener.local_addr();
    ASSERT_TRUE(address.is_ok());

    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result =
        runtime.block_on(round_trip(tcp_listener, rstd::move(address).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().len(), usize(4));
}

TEST(NetCompletion, MultiThreadLoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());
    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto address      = tcp_listener.local_addr();
    ASSERT_TRUE(address.is_ok());

    auto runtime = async::RuntimeBuilder::multi_thread()
                       .worker_threads(usize(2))
                       .enable_all()
                       .build()
                       .unwrap();
    auto result =
        runtime.block_on(round_trip(tcp_listener, rstd::move(address).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().len(), usize(4));
}

TEST(NetCompletion, CancelPendingAcceptLeavesListenerUsable) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());
    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto address      = tcp_listener.local_addr();
    ASSERT_TRUE(address.is_ok());
    auto target = rstd::move(address).unwrap_unchecked();

    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto accept  = async::AbortOnDropHandle { runtime.spawn(tcp_listener.accept()) };
    auto timed =
        runtime.block_on(async::timeout(rstd::move(accept), time::Duration::from_millis(u64(2))));
    EXPECT_TRUE(timed.is_err());

    auto result = runtime.block_on(round_trip(tcp_listener, target));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked().len(), usize(4));
}

TEST(NetCompletion, ConnectRefusedPreservesErrorKind) {
    auto address = net::SocketAddr::ipv4_loopback(u16());
    {
        auto listener = net::TcpListener::bind(address);
        ASSERT_TRUE(listener.is_ok());
        auto tcp_listener = rstd::move(listener).unwrap_unchecked();
        auto local        = tcp_listener.local_addr();
        ASSERT_TRUE(local.is_ok());
        address = rstd::move(local).unwrap_unchecked();
    }

    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result  = runtime.block_on(net::TcpStream::connect(address));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::ConnectionRefused });
}

#if defined(_WIN32)
TEST(NetCompletion, AssociatedSocketCannotDetachFromIocp) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());
    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto address      = tcp_listener.local_addr();
    ASSERT_TRUE(address.is_ok());

    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto result = runtime.block_on(detach_connected_socket(rstd::move(address).unwrap_unchecked()));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::Unsupported });
}
#endif
