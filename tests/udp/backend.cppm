module;
#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
#if RSTD_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#endif

export module rstd:udp_backend_tests;
import :async;
import :async.poll;
import :async.runtime;
import :net.udp;
import :alloc.shared_tracking;
import rstd.alloc;
import rstd.test;
import rstd.sys.socket;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

#if RSTD_OS_LINUX
TEST(UdpMetadata, OriginalDestinationChecksNativeStructureAndConflicts) {
    byte                         payload[8] {};
    auto                         local = net::SocketAddr::ipv4_loopback(u16(8000));
    sys::socket::DatagramMessage message(payload, usize(8), local);
    auto                         native = static_cast<msghdr*>(message.native_message());
    sockaddr_in                  address {};
    address.sin_family      = AF_INET;
    address.sin_port        = htons(9000);
    address.sin_addr.s_addr = htonl(0x7f000001);
    __builtin_memcpy(native->msg_name, &address, sizeof(address));
    native->msg_namelen = sizeof(address);
    auto first          = CMSG_FIRSTHDR(native);
    first->cmsg_level   = IPPROTO_IP;
    first->cmsg_type    = IP_ORIGDSTADDR;
    first->cmsg_len     = CMSG_LEN(sizeof(address));
    __builtin_memcpy(CMSG_DATA(first), &address, sizeof(address));
    native->msg_controllen = CMSG_SPACE(sizeof(address));
    auto result            = message.result(usize());
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->original_destination.is_some());
    EXPECT_EQ(*result->original_destination, net::SocketAddr::ipv4_loopback(u16(9000)));
    native->msg_flags = MSG_CTRUNC | MSG_TRUNC;
    auto truncated    = message.result(usize());
    ASSERT_TRUE(truncated.is_ok());
    EXPECT_TRUE(truncated->control_truncated);
    EXPECT_TRUE(truncated->truncated);
    native->msg_flags = 0;
    first->cmsg_len   = CMSG_LEN(sizeof(address)) - 1;
    EXPECT_TRUE(message.result(usize()).is_err());
    first->cmsg_len    = CMSG_LEN(sizeof(address));
    address.sin_family = AF_INET6;
    __builtin_memcpy(CMSG_DATA(first), &address, sizeof(address));
    EXPECT_TRUE(message.result(usize()).is_err());
    address.sin_family = AF_INET;
    __builtin_memcpy(CMSG_DATA(first), &address, sizeof(address));
    native->msg_controllen = 2 * CMSG_SPACE(sizeof(address));
    auto second            = CMSG_NXTHDR(native, first);
    ASSERT_NE(second, nullptr);
    second->cmsg_level = IPPROTO_IP;
    second->cmsg_type  = IP_ORIGDSTADDR;
    second->cmsg_len   = CMSG_LEN(sizeof(address));
    __builtin_memcpy(CMSG_DATA(second), &address, sizeof(address));
    EXPECT_TRUE(message.result(usize()).is_ok());
    address.sin_port = htons(9001);
    __builtin_memcpy(CMSG_DATA(second), &address, sizeof(address));
    EXPECT_TRUE(message.result(usize()).is_err());
    second->cmsg_len = 1024;
    EXPECT_TRUE(message.result(usize()).is_err());
}

TEST(UdpMetadata, OriginalDestinationRetainsIpv6ScopeAndPort) {
    byte                         payload[8] {};
    sys::socket::DatagramMessage message(
        payload, usize(8), net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16(8000)));
    auto         native = static_cast<msghdr*>(message.native_message());
    sockaddr_in6 address {};
    address.sin6_family           = AF_INET6;
    address.sin6_port             = htons(9000);
    address.sin6_addr.s6_addr[0]  = 0xfe;
    address.sin6_addr.s6_addr[1]  = 0x80;
    address.sin6_addr.s6_addr[15] = 1;
    address.sin6_scope_id         = 7;
    __builtin_memcpy(native->msg_name, &address, sizeof(address));
    native->msg_namelen = sizeof(address);
    auto first          = CMSG_FIRSTHDR(native);
    first->cmsg_level   = IPPROTO_IPV6;
    first->cmsg_type    = IPV6_ORIGDSTADDR;
    first->cmsg_len     = CMSG_LEN(sizeof(address));
    __builtin_memcpy(CMSG_DATA(first), &address, sizeof(address));
    native->msg_controllen = CMSG_SPACE(sizeof(address));
    auto result            = message.result(usize());
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->original_destination.is_some());
    EXPECT_EQ(result->original_destination->port(), u16(9000));
    EXPECT_EQ(result->original_destination->scope_id(), u32(7));
}
#endif

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
