#include <fcntl.h>
#include <rstd/test/gtest.hpp>
#include <atomic>
import rstd;

using namespace rstd;
using namespace rstd::prelude;

namespace
{

auto copied_bytes(const void* data, rstd::size_t len) -> bytes::Bytes {
    auto raw = slice<byte>::from_raw_parts(static_cast<byte const*>(data), usize(len));
    return bytes::Bytes::copy_from_slice(rstd::as_u8_slice(raw));
}

struct WakerCounts {
    std::atomic<int> clones { 0 };
    std::atomic<int> wakes { 0 };
    std::atomic<int> wake_refs { 0 };
    std::atomic<int> drops { 0 };
};

extern const task::RawWakerVTable COUNT_WAKER_VTABLE;

auto count_clone(voidp data) -> task::RawWaker {
    auto* counts = static_cast<WakerCounts*>(data);
    ++counts->clones;
    return task::RawWaker::from_raw_parts(data, rstd::addressof(COUNT_WAKER_VTABLE));
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
    return ::fcntl(fd, F_GETFD) != -1;
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

    const char payload[] = { 'p', 'i', 'n', 'g' };
    auto       bytes     = copied_bytes(payload, sizeof(payload));
    auto       written   = co_await write_some(client_stream, bytes);
    if (written.is_err()) co_return Err(rstd::move(written).unwrap_err_unchecked());

    auto received = bytes::BytesMut::with_capacity(usize(4));
    auto read     = co_await read_some(server_stream, received);
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());

    co_return Ok(rstd::move(received));
}

async::coro<io::Result<bytes::BytesMut>> spawned_tcp_roundtrip(net::TcpListener& listener,
                                                               net::SocketAddr   addr) {
    auto handle = async::spawn(tcp_roundtrip(listener, addr));
    auto joined = co_await rstd::move(handle);
    if (joined.is_err()) {
        co_return Err(
            io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Other }));
    }
    co_return rstd::move(joined).unwrap();
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

    auto received = bytes::BytesMut::with_capacity(usize(1));
    auto read     = co_await read_some(server_stream, received);
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

    const char first_payload[] = { 'a' };
    auto       first_bytes     = copied_bytes(first_payload, sizeof(first_payload));
    auto       first_written   = co_await write_some(client_stream, first_bytes);
    if (first_written.is_err()) co_return Err(rstd::move(first_written).unwrap_err_unchecked());

    auto first_read_buf = bytes::BytesMut::with_capacity(usize(1));
    auto first_read     = co_await read_some(server_stream, first_read_buf);
    if (first_read.is_err()) co_return Err(rstd::move(first_read).unwrap_err_unchecked());

    auto empty_read_buf = bytes::BytesMut::with_capacity(usize(1));
    auto empty_read     = server_stream.try_read(empty_read_buf);
    if (empty_read.is_ok()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }
    auto empty_error = rstd::move(empty_read).unwrap_err_unchecked();
    if (! would_block(empty_error)) co_return Err(rstd::move(empty_error));

    const char second_payload[] = { 'b' };
    auto       second_bytes     = copied_bytes(second_payload, sizeof(second_payload));
    auto       second_written   = co_await write_some(client_stream, second_bytes);
    if (second_written.is_err()) {
        co_return Err(rstd::move(second_written).unwrap_err_unchecked());
    }

    auto received    = bytes::BytesMut::with_capacity(usize(1));
    auto second_read = co_await read_some(server_stream, received);
    if (second_read.is_err()) co_return Err(rstd::move(second_read).unwrap_err_unchecked());

    co_return Ok(rstd::move(received));
}

async::coro<io::Result<async::ReadyEvent>> wait_for_readiness(async::ReadinessFuture readiness) {
    co_return co_await rstd::move(readiness);
}

async::coro<io::Result<empty>> detach_with_pending_readiness(net::TcpListener& listener,
                                                             net::SocketAddr   addr) {
    auto client = co_await net::TcpStream::connect(addr);
    if (client.is_err()) co_return Err(rstd::move(client).unwrap_err_unchecked());

    auto accepted = co_await listener.accept();
    if (accepted.is_err()) co_return Err(rstd::move(accepted).unwrap_err_unchecked());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto accepted_pair = rstd::move(accepted).unwrap_unchecked();
    auto server_stream = rstd::move(accepted_pair.template get<0>());

    auto waiter = async::spawn(wait_for_readiness(client_stream.readable()));
    co_await async::yield_now();

    auto detached = co_await rstd::move(client_stream).into_owned_fd();
    if (detached.is_err()) co_return Err(rstd::move(detached).unwrap_err_unchecked());

    auto joined = co_await rstd::move(waiter);
    if (joined.is_err()) {
        co_return Err(
            io::error::Error::from_kind(io::error::ErrorKind { io::error::ErrorKind::Other }));
    }
    auto readiness = rstd::move(joined).unwrap_unchecked();
    if (readiness.is_ok()) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidInput }));
    }

    auto restored = net::TcpStream::from_owned_fd(rstd::move(detached).unwrap_unchecked());
    if (restored.is_err()) co_return Err(rstd::move(restored).unwrap_err_unchecked());
    auto restored_stream = rstd::move(restored).unwrap_unchecked();

    const char payload[] = { 'r' };
    auto       bytes     = copied_bytes(payload, sizeof(payload));
    auto       written   = co_await write_some(restored_stream, bytes);
    if (written.is_err()) co_return Err(rstd::move(written).unwrap_err_unchecked());

    auto received = bytes::BytesMut::with_capacity(usize(1));
    auto read     = co_await read_some(server_stream, received);
    if (read.is_err()) co_return Err(rstd::move(read).unwrap_err_unchecked());
    if (rstd::move(read).unwrap_unchecked() != usize(1) || received[usize()] != u8('r')) {
        co_return Err(io::error::Error::from_kind(
            io::error::ErrorKind { io::error::ErrorKind::InvalidData }));
    }

    co_return Ok(empty {});
}

} // namespace

TEST(NetSocketAddr, Ipv4StateIsPortable) {
    auto addr = net::SocketAddr::ipv4(net::Ipv4Addr::make(u8(192), u8(), u8(2), u8(1)), u16(8080));
    EXPECT_TRUE(addr.is_ipv4());
    EXPECT_FALSE(addr.is_ipv6());
    EXPECT_EQ(addr.port(), u16(8080));
    EXPECT_EQ(addr.octet(usize()), u8(192));
    EXPECT_EQ(addr.octet(usize(3)), u8(1));
}

TEST(NetSocketAddr, Ipv6StateIsPortable) {
    auto addr = net::SocketAddr::ipv6(
        net::Ipv6Addr::make(u16(0x2001), u16(0x0db8), u16(), u16(), u16(), u16(), u16(), u16(1)),
        u16(443),
        u32(7),
        u32(9));
    EXPECT_TRUE(addr.is_ipv6());
    EXPECT_FALSE(addr.is_ipv4());
    EXPECT_EQ(addr.port(), u16(443));
    EXPECT_EQ(addr.flowinfo(), u32(7));
    EXPECT_EQ(addr.scope_id(), u32(9));
    EXPECT_EQ(addr.octet(usize()), u8(0x20));
    EXPECT_EQ(addr.octet(usize(1)), u8(0x01));
    EXPECT_EQ(addr.octet(usize(15)), u8(0x01));
}

TEST(NetTcp, LoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());
    EXPECT_NE(rstd::move(addr).unwrap_unchecked().port(), u16());

    auto local_addr = tcp_listener.local_addr();
    ASSERT_TRUE(local_addr.is_ok());

    auto result =
        async::block_on(tcp_roundtrip(tcp_listener, rstd::move(local_addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());

    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), usize(4));
    EXPECT_EQ(received[usize()], u8('p'));
    EXPECT_EQ(received[usize(1)], u8('i'));
    EXPECT_EQ(received[usize(2)], u8('n'));
    EXPECT_EQ(received[usize(3)], u8('g'));
}

TEST(NetTcp, MultiThreadRuntimeLoopbackRoundTrip) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto runtime_result =
        async::RuntimeBuilder::multi_thread().worker_threads(usize(2)).enable_io().build();
    ASSERT_TRUE(runtime_result.is_ok());
    auto runtime = rstd::move(runtime_result).unwrap_unchecked();

    auto result =
        runtime.block_on(spawned_tcp_roundtrip(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());

    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), usize(4));
    EXPECT_EQ(received[usize()], u8('p'));
    EXPECT_EQ(received[usize(1)], u8('i'));
    EXPECT_EQ(received[usize(2)], u8('n'));
    EXPECT_EQ(received[usize(3)], u8('g'));
}

TEST(NetTcp, ConnectRefusedReturnsError) {
    auto local_addr = net::SocketAddr::ipv4_loopback(u16());
    {
        auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
        ASSERT_TRUE(listener.is_ok());

        auto tcp_listener = rstd::move(listener).unwrap_unchecked();
        auto addr         = tcp_listener.local_addr();
        ASSERT_TRUE(addr.is_ok());
        local_addr = rstd::move(addr).unwrap_unchecked();
    }

    auto result = async::block_on(net::TcpStream::connect(local_addr));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(rstd::move(result).unwrap_err_unchecked().kind(),
              io::error::ErrorKind { io::error::ErrorKind::ConnectionRefused });
}

TEST(NetTcp, ShutdownWriteReadsEof) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto result = async::block_on(
        shutdown_write_reads_eof(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());
    EXPECT_EQ(rstd::move(result).unwrap_unchecked(), usize());
}

TEST(NetTcp, FromOwnedFdKeepsOwnership) {
    auto runtime  = async::Runtime {};
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto client =
        runtime.block_on(connected_client(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(client.is_ok());

    auto client_stream = rstd::move(client).unwrap_unchecked();
    auto detached      = runtime.block_on(rstd::move(client_stream).into_owned_fd());
    ASSERT_TRUE(detached.is_ok());
    auto fd  = rstd::move(detached).unwrap_unchecked();
    auto raw = fd.as_raw_fd();
    ASSERT_TRUE(fd_is_open(raw));

    {
        auto restored = net::TcpStream::from_owned_fd(rstd::move(fd));
        ASSERT_TRUE(restored.is_ok());
        EXPECT_TRUE(fd_is_open(raw));
    }
    EXPECT_FALSE(fd_is_open(raw));
}

TEST(NetTcp, ListenerFromOwnedFdKeepsListeningSocket) {
    auto runtime  = async::Runtime {};
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto before       = tcp_listener.local_addr();
    ASSERT_TRUE(before.is_ok());

    auto detached = runtime.block_on(rstd::move(tcp_listener).into_owned_fd());
    ASSERT_TRUE(detached.is_ok());

    auto restored = net::TcpListener::from_owned_fd(rstd::move(detached).unwrap_unchecked());
    ASSERT_TRUE(restored.is_ok());
    auto after = restored.unwrap_unchecked().local_addr();
    ASSERT_TRUE(after.is_ok());
    EXPECT_EQ(after.unwrap_unchecked(), before.unwrap_unchecked());
}

TEST(NetTcp, DetachCancelsOldReadinessBeforeFdReuse) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto result = async::block_on(
        detach_with_pending_readiness(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());
}

TEST(NetTcp, ReadinessFutureDropCancelsWaiter) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto counts = WakerCounts {};
    auto waker  = task::Waker::from_raw(
        task::RawWaker::from_raw_parts(&counts, rstd::addressof(COUNT_WAKER_VTABLE)));
    auto cx = task::Context { waker };

    {
        auto readable = tcp_listener.readable();
        auto first    = future::poll(readable, cx);
        EXPECT_TRUE(first.is_pending());
    }

    EXPECT_EQ(counts.clones.load(), 1);
    EXPECT_EQ(counts.drops.load(), 1);

    auto client = async::block_on(net::TcpStream::connect(rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(client.is_ok());

    auto accepted = async::block_on(tcp_listener.accept());
    ASSERT_TRUE(accepted.is_ok());
    EXPECT_EQ(counts.wakes.load(), 0);
    EXPECT_EQ(counts.wake_refs.load(), 0);
}

TEST(NetTcp, RepeatedWouldBlockWaitsForNextReadiness) {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(listener.is_ok());

    auto tcp_listener = rstd::move(listener).unwrap_unchecked();
    auto addr         = tcp_listener.local_addr();
    ASSERT_TRUE(addr.is_ok());

    auto result =
        async::block_on(repeated_readiness(tcp_listener, rstd::move(addr).unwrap_unchecked()));
    ASSERT_TRUE(result.is_ok());

    auto received = rstd::move(result).unwrap_unchecked();
    ASSERT_EQ(received.len(), usize(1));
    EXPECT_EQ(received[usize()], u8('b'));
}
