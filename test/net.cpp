#include <gtest/gtest.h>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

struct WakerCounts {
    std::atomic<int> clones { 0 };
    std::atomic<int> wakes { 0 };
    std::atomic<int> wake_refs { 0 };
    std::atomic<int> drops { 0 };
};

auto count_clone(voidp data) -> voidp {
    auto* counts = static_cast<WakerCounts*>(data);
    ++counts->clones;
    return data;
}

void count_wake(voidp data) {
    ++static_cast<WakerCounts*>(data)->wakes;
}

void count_wake_by_ref(voidp data) {
    ++static_cast<WakerCounts*>(data)->wake_refs;
}

void count_drop(voidp data) {
    ++static_cast<WakerCounts*>(data)->drops;
}

const task::RawWakerVTable COUNT_WAKER_VTABLE {
    &count_clone,
    &count_wake,
    &count_wake_by_ref,
    &count_drop,
};

auto fd_is_open(int fd) -> bool {
    return sys::libc::fcntl(fd, sys::libc::F_GETFD) != -1;
}

auto would_block(io::error::Error const& error) -> bool {
    return error.kind() == io::error::ErrorKind { io::error::ErrorKind::WouldBlock };
}

async::coro<io::Result<usize>> write_some(net::TcpStream& stream, bytes::Bytes const& bytes) {
    while (true) {
        auto written = stream.try_write(bytes);
        if (written.is_ok()) co_return written;

        auto error = rstd::move(written).unwrap_err_unchecked();
        if (! would_block(error)) co_return Err(rstd::move(error));

        auto ready = co_await stream.writable();
        if (ready.is_err()) co_return Err(rstd::move(ready).unwrap_err_unchecked());
    }
}

async::coro<io::Result<usize>> read_some(net::TcpStream& stream, bytes::BytesMut& buf) {
    while (true) {
        auto read = stream.try_read(buf);
        if (read.is_ok()) co_return read;

        auto error = rstd::move(read).unwrap_err_unchecked();
        if (! would_block(error)) co_return Err(rstd::move(error));

        auto ready = co_await stream.readable();
        if (ready.is_err()) co_return Err(rstd::move(ready).unwrap_err_unchecked());
    }
}

async::coro<io::Result<bytes::BytesMut>> tcp_roundtrip(net::TcpListener& listener,
                                                       net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) co_return Err(rstd::move(client).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto accepted_pair = rstd::move(accepted).unwrap_unchecked();
    auto server_stream = rstd::move(accepted_pair.template get<0>());

    const u8 payload[] = { 'p', 'i', 'n', 'g' };
    auto     bytes = bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(payload, 4));
    auto     written = co_await write_some(client_stream, bytes);
    if (written.is_err()) co_return Err(rstd::move(written).unwrap_err_unchecked());

    auto received = bytes::BytesMut::with_capacity(4);
    auto read = co_await read_some(server_stream, received);
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());

    co_return Ok(rstd::move(received));
}

async::coro<io::Result<net::TcpStream>> connected_client(net::TcpListener& listener,
                                                         net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) co_return Err(rstd::move(client).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    co_return Ok(rstd::move(client).unwrap_unchecked());
}

async::coro<io::Result<usize>> shutdown_write_reads_eof(net::TcpListener& listener,
                                                        net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) co_return Err(rstd::move(client).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto accepted_pair = rstd::move(accepted).unwrap_unchecked();
    auto server_stream = rstd::move(accepted_pair.template get<0>());

    auto shutdown = client_stream.shutdown();
    if (shutdown.is_err()) co_return Err(rstd::move(shutdown).unwrap_err_unchecked());

    auto received = bytes::BytesMut::with_capacity(1);
    auto read = co_await read_some(server_stream, received);
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());
    co_return Ok(rstd::move(read).unwrap_unchecked());
}

async::coro<io::Result<bytes::BytesMut>> repeated_readiness(net::TcpListener& listener,
                                                            net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) co_return Err(rstd::move(client).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto accepted_pair = rstd::move(accepted).unwrap_unchecked();
    auto server_stream = rstd::move(accepted_pair.template get<0>());

    const u8 first_payload[] = { 'a' };
    auto     first_bytes =
        bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(first_payload, 1));
    auto first_written = co_await write_some(client_stream, first_bytes);
    if (first_written.is_err()) co_return Err(rstd::move(first_written).unwrap_err_unchecked());

    auto first_read_buf = bytes::BytesMut::with_capacity(1);
    auto first_read = co_await read_some(server_stream, first_read_buf);
    if (first_read.is_err()) co_return Err(rstd::move(first_read).unwrap_err_unchecked());

    auto empty_read_buf = bytes::BytesMut::with_capacity(1);
    auto empty_read = server_stream.try_read(empty_read_buf);
    if (empty_read.is_ok()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    auto empty_error = rstd::move(empty_read).unwrap_err_unchecked();
    if (! would_block(empty_error)) co_return Err(rstd::move(empty_error));

    const u8 second_payload[] = { 'b' };
    auto     second_bytes =
        bytes::Bytes::copy_from_slice(slice<u8>::from_raw_parts(second_payload, 1));
    auto second_written = co_await write_some(client_stream, second_bytes);
    if (second_written.is_err()) {
        co_return Err(rstd::move(second_written).unwrap_err_unchecked());
    }

    auto received = bytes::BytesMut::with_capacity(1);
    auto second_read = co_await read_some(server_stream, received);
    if (second_read.is_err()) co_return Err(rstd::move(second_read).unwrap_err_unchecked());

    co_return Ok(rstd::move(received));
}

} // namespace

TEST(NetTcp, LoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());
    EXPECT_NE(rstd::move(addr).unwrap_unchecked().port(), 0);

    auto local_addr = tcp_listener.local_addr();
    ASSERT_TRUE(local_addr.is_ok());

    auto result = async::block_on(
        tcp_roundtrip(tcp_listener, rstd::move(local_addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());

    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), 4u);
    EXPECT_EQ(received[0], u8('p'));
    EXPECT_EQ(received[1], u8('i'));
    EXPECT_EQ(received[2], u8('n'));
    EXPECT_EQ(received[3], u8('g'));
}

TEST(NetTcp, ConnectRefusedReturnsError) {
    auto local_addr = net::SocketAddr::ipv4_loopback(0);
    {
        auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
        ASSERT_TRUE(listener.is_ok());

        auto tcp_listener = rstd::move(listener).unwrap_unchecked();
        auto addr = tcp_listener.local_addr();
        ASSERT_TRUE(addr.is_ok());
        local_addr = rstd::move(addr).unwrap_unchecked();
    }

    auto result = async::block_on(net::TcpStream::connect(local_addr));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::ConnectionRefused });
}

TEST(NetTcp, ShutdownWriteReadsEof) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto result = async::block_on(
        shutdown_write_reads_eof(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked(), 0u);
}

TEST(NetTcp, FromOwnedFdKeepsOwnership) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto client = async::block_on(
        connected_client(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(client.is_ok());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto fd = client_stream.into_owned_fd();
    auto raw = fd.as_raw_fd();
    ASSERT_TRUE(fd_is_open(raw));

    {
        auto restored = net::TcpStream::from_owned_fd(rstd::move(fd));
        ASSERT_TRUE(restored.is_ok());
        EXPECT_TRUE(fd_is_open(raw));
    }
    EXPECT_FALSE(fd_is_open(raw));
}

TEST(NetTcp, ReadinessFutureDropCancelsWaiter) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(task::RawWaker { &counts, &COUNT_WAKER_VTABLE });
    auto cx     = task::Context { waker };

    {
        auto readable = tcp_listener.readable();
        auto first = future::poll(readable, cx);
        EXPECT_TRUE(first.is_pending());
    }

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.drops.load(), 1);

    auto client = async::block_on(
        net::TcpStream::connect(rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(client.is_ok());

    auto accepted = async::block_on(tcp_listener.accept());
    ASSERT_TRUE(accepted.is_ok());
    EXPECT_EQ(counts.wakes.load(), 0);
    EXPECT_EQ(counts.wake_refs.load(), 0);
}

TEST(NetTcp, RepeatedWouldBlockWaitsForNextReadiness) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(0));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto result = async::block_on(
        repeated_readiness(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());

    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), 1u);
    EXPECT_EQ(received[0], u8('b'));
}
