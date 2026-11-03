#include <rstd/test/gtest.hpp>
import rstd;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

namespace
{
auto completed_pair(async::Runtime& runtime) -> tuple<async::io::Stream, async::io::Stream> {
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client =
        runtime.block_on(net::TcpStream::connect(listener.local_addr().unwrap())).unwrap();
    auto server = runtime.block_on(listener.accept()).unwrap();
    runtime.block_on(rstd::move(listener).close()).unwrap();
    return { net::as_stream(rstd::move(client)).unwrap(),
             net::as_stream(rstd::move(server.get<0>())).unwrap() };
}
void close_completed(async::Runtime&                              runtime,
                     tuple<async::io::Stream, async::io::Stream>& streams) {
    auto result =
        runtime
            .block_on(async::timeout(async::join(async::into_future(streams.get<0>().close()),
                                                 async::into_future(streams.get<1>().close())),
                                     time::Duration::from_secs(u64(2))))
            .unwrap();
    EXPECT_TRUE(result.get<0>().is_ok());
    EXPECT_TRUE(result.get<1>().is_ok());
}
} // namespace

TEST(CompletionStream, OwnedReceiveReturnsStorageAndHalfCloseKeepsInputOpen) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto streams = completed_pair(runtime);
    auto buffer  = bytes::BytesMut::with_capacity(usize(32));
    auto storage = buffer.data();
    EXPECT_EQ(
        runtime.block_on(streams.get<0>().send_all(bytes::Bytes::copy_from_slice("request"_bytes)))
            .unwrap(),
        usize(7));
    auto received = runtime.block_on(streams.get<1>().recv_into(rstd::move(buffer)));
    ASSERT_TRUE(received.result.is_ok());
    EXPECT_EQ(*received.result, usize(7));
    EXPECT_EQ(received.buffer.data(), storage);
    EXPECT_EQ(received.buffer.as_slice(), "request"_bytes);
    EXPECT_TRUE(runtime.block_on(streams.get<0>().shutdown_write()).is_ok());
    auto eof = runtime.block_on(streams.get<1>().recv_into(rstd::move(received.buffer)));
    ASSERT_TRUE(eof.result.is_ok());
    EXPECT_EQ(*eof.result, usize());
    EXPECT_EQ(eof.buffer.data(), storage);
    EXPECT_EQ(
        runtime.block_on(streams.get<1>().send_all(bytes::Bytes::copy_from_slice("reply"_bytes)))
            .unwrap(),
        usize(5));
    EXPECT_EQ(runtime.block_on(streams.get<0>().recv()).unwrap().as_slice(), "reply"_bytes);
    EXPECT_TRUE(runtime.block_on(streams.get<1>().flush()).is_ok());
    close_completed(runtime, streams);
    EXPECT_TRUE(runtime.block_on(streams.get<0>().close()).is_ok());
}

TEST(CompletionStream, CloseStopsPendingReceiveAndRepeatedWaitsAreValid) {
    auto runtime   = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto streams   = completed_pair(runtime);
    auto buffer    = bytes::BytesMut::with_capacity(usize(64));
    auto storage   = buffer.data();
    auto receiving = runtime.spawn(streams.get<0>().recv_into(rstd::move(buffer)));
    runtime.block_on(async::sleep(time::Duration::from_millis(u64(2))));
    EXPECT_TRUE(runtime
                    .block_on(async::timeout(async::into_future(streams.get<0>().close()),
                                             time::Duration::from_secs(u64(2))))
                    .unwrap()
                    .is_ok());
    auto received = runtime.block_on(rstd::move(receiving)).unwrap();
    EXPECT_EQ(received.buffer.data(), storage);
    EXPECT_TRUE(received.result.is_err() || *received.result == usize());
    EXPECT_TRUE(runtime.block_on(streams.get<0>().close()).is_ok());
    EXPECT_TRUE(runtime.block_on(streams.get<1>().close()).is_ok());
}

TEST(CompletionStream, UnpolledOperationRetainsBufferAfterOwnerMovesAndDrops) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto streams = completed_pair(runtime);
    auto buffer  = bytes::BytesMut::with_capacity(usize(17));
    auto storage = buffer.data();
    auto receive = [&] {
        auto owned = rstd::move(streams.get<0>());
        auto moved = rstd::move(owned);
        return moved.recv_into(rstd::move(buffer));
    }();
    auto received = runtime.block_on(rstd::move(receive));
    EXPECT_TRUE(received.result.is_err());
    EXPECT_EQ(received.buffer.data(), storage);
    auto peer = runtime.block_on(streams.get<1>().recv());
    EXPECT_TRUE(peer.is_err() || peer->is_empty());
    EXPECT_TRUE(runtime.block_on(streams.get<1>().close()).is_ok());
}
