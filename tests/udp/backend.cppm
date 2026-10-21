module;
#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>

export module rstd:udp_backend_tests;
import :async;
import :async.poll;
import :async.runtime;
import :net.udp;
import :alloc.shared_tracking;
import rstd.alloc;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

template<typename F>
struct CancelDatagramOnPoll {
    using Output = empty;
    Option<F>             receive;
    const net::UdpSocket* sender;
    net::SocketAddr       target;
    auto poll(mut_ref<CancelDatagramOnPoll> self, task::Context& cx) -> task::Poll<empty> {
        auto result = future::poll(*self->receive, cx);
        EXPECT_TRUE(result.is_pending());
        EXPECT_TRUE(
            self->sender
                ->try_send(bytes::Bytes::copy_from_slice("race"_bytes), { self->target, None() })
                .is_ok());
        self->receive = None();
        return task::Poll<empty>::Ready(empty {});
    }
};

void verify_udp_backend(async::IoBackendPreference preference) {
    auto available = async::Poll::init(preference);
    if (available.is_err() && preference == async::IoBackendPreference::NativeCompletionRequired) {
        GTEST_SKIP() << "native completion backend is unavailable";
    }
    ASSERT_TRUE(available.is_ok());
    auto probe   = rstd::move(available).unwrap();
    auto cleanup = async::Poll::shutdown(probe.state);
    while (! cleanup.is_empty()) cleanup.pop_front().unwrap().dispatch();
    auto builder = async::RuntimeBuilder::current_thread();
    builder.enable_all();
    async::RuntimeBuilderConfigAccess::set_io_backend(builder, preference);
    auto built = builder.build();
    ASSERT_TRUE(built.is_ok());
    auto runtime = rstd::move(built).unwrap();
    auto server  = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16()), true).unwrap();
    auto client  = net::UdpSocket::connect(server.local_addr(), true).unwrap();
    EXPECT_EQ(client.peer_addr().unwrap(), server.local_addr());
    auto sent = runtime.block_on(
        client.send_to(bytes::Bytes::copy_from_slice("backend"_bytes), server.local_addr()));
    ASSERT_TRUE(sent.is_ok());
    auto received = runtime.block_on(async::timeout(async::into_future(server.recv(usize(3))),
                                                    time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(received.is_ok());
    ASSERT_TRUE(received->is_ok());
    EXPECT_EQ(received->unwrap().data.len(), usize(3));
    EXPECT_TRUE(received->unwrap().info.truncated);
    ASSERT_TRUE(received->unwrap().info.packet.is_some());
    EXPECT_EQ(received->unwrap().info.packet->local, server.local_addr());
    ASSERT_TRUE(runtime
                    .block_on(server.send_to(bytes::Bytes::copy_from_slice("reply"_bytes),
                                             client.local_addr()))
                    .is_ok());
    auto response = runtime.block_on(
        async::timeout(async::into_future(client.recv()), time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(response.is_ok());
    ASSERT_TRUE(response->is_ok());
    EXPECT_EQ(response->unwrap().data.as_slice(), "reply"_bytes);
    EXPECT_EQ(response->unwrap().info.peer, server.local_addr());
    ASSERT_TRUE(response->unwrap().info.packet.is_some());
    EXPECT_EQ(response->unwrap().info.packet->local, client.local_addr());
    ASSERT_TRUE(runtime.block_on(client.send_to(bytes::Bytes {}, server.local_addr())).is_ok());
    auto empty = runtime.block_on(async::timeout(async::into_future(server.recv(usize())),
                                                 time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(empty.is_ok());
    ASSERT_TRUE(empty->is_ok());
    EXPECT_TRUE(empty->unwrap().data.is_empty());
    EXPECT_FALSE(empty->unwrap().info.truncated);
#if ! RSTD_OS_WINDOWS
    auto pending = runtime.spawn(server.recv());
    auto timed   = runtime.block_on(
        async::timeout(future::by_ref(pending), time::Duration::from_millis(u64(2))));
    ASSERT_TRUE(timed.is_err());
    pending.abort();
    EXPECT_TRUE(runtime.block_on(rstd::move(pending)).is_err());
    auto detached = runtime.block_on(rstd::move(server).into_owned_socket());
    ASSERT_TRUE(detached.is_ok());
    server = net::UdpSocket::from_owned_socket(rstd::move(detached).unwrap(), true).unwrap();
    ASSERT_TRUE(runtime
                    .block_on(client.send_to(bytes::Bytes::copy_from_slice("after-cancel"_bytes),
                                             server.local_addr()))
                    .is_ok());
    auto next = runtime.block_on(
        async::timeout(async::into_future(server.recv()), time::Duration::from_secs(u64(1))));
    ASSERT_TRUE(next.is_ok());
    ASSERT_TRUE(next->is_ok());
    EXPECT_EQ(next->unwrap().data.len(), usize(12));
    for (unsigned i = 0; i < 4; ++i) {
        auto cancelled = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        auto tracker   = rstd::alloc::SharedTrackingAllocator<>::make();
        auto receive   = async::into_future(
            cancelled.recv_into(bytes::BytesMut::with_capacity_in(usize(16), tracker.clone())));
        runtime.block_on(CancelDatagramOnPoll<decltype(receive)> {
            Some(rstd::move(receive)), &client, cancelled.local_addr() });
        EXPECT_TRUE(runtime.block_on(rstd::move(cancelled).close()).is_ok());
        EXPECT_EQ(tracker.snapshot().live_bytes, u64());
        EXPECT_EQ(tracker.snapshot().allocations, tracker.snapshot().deallocations);
    }
    {
        auto closing = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        auto owned   = runtime.block_on(rstd::move(closing).into_owned_socket()).unwrap();
        auto source  = async::CompletionSource::socket(rstd::move(owned));
        auto read    = async::IoOperation::recv_datagram_into(
            source, bytes::BytesMut::with_capacity(usize(8)), server.local_addr());
        auto retained = source.clone();
        auto outputs =
            runtime.block_on(async::join(rstd::move(source).release(), rstd::move(read)));
        EXPECT_TRUE(outputs.get<0>().is_ok());
        ASSERT_TRUE(outputs.get<1>().is_err());
        EXPECT_EQ(outputs.get<1>().unwrap_err().kind(),
                  io::ErrorKind { io::ErrorKind::NotConnected });
        EXPECT_TRUE(retained.take_owned_socket().is_ok());
    }

#endif
}

#if RSTD_OS_LINUX
TEST(UdpBackend, ReadinessEmulation) {
    verify_udp_backend(async::IoBackendPreference::ReadinessEmulationRequired);
}
TEST(UdpBackend, NativeCompletion) {
    verify_udp_backend(async::IoBackendPreference::NativeCompletionRequired);
}
#endif
