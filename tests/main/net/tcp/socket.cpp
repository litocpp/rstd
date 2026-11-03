#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
#if RSTD_OS_UNIX
#include <fcntl.h>
#endif

import rstd;
import rstd.test;

namespace
{
using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

TEST(TcpSocket, ConfiguredOwnerBindsBeforeConnectingAndTransfersOwnership) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto server  = net::TcpSocket::make_v4().unwrap();
    EXPECT_TRUE(server.set_reuseaddr(true).is_ok());
    EXPECT_TRUE(server.bind(net::SocketAddr::ipv4_loopback(u16())).is_ok());
    auto listener = rstd::move(server).listen(i32(8)).unwrap();
    auto raw      = net::TcpSocket::make_v4().unwrap();
    auto owned    = rstd::move(raw).into_owned_socket();
    auto client   = net::TcpSocket::from_owned_socket(rstd::move(owned));
    EXPECT_TRUE(client.bind(net::SocketAddr::ipv4_loopback(u16())).is_ok());
    auto local   = client.local_addr().unwrap();
    auto connect = rstd::move(client).prepare_connect(listener.local_addr().unwrap()).unwrap();
    EXPECT_EQ(connect.local_addr().unwrap(), local);
    EXPECT_TRUE(runtime.block_on(connect.finish()).is_ok());
    auto stream = rstd::move(connect).into_stream().unwrap();
    auto peer   = runtime.block_on(listener.accept()).unwrap();
    EXPECT_EQ(peer.get<1>(), local);
    EXPECT_TRUE(
        runtime.block_on(stream.send_all(bytes::Bytes::copy_from_slice("socket"_bytes))).is_ok());
    EXPECT_EQ(runtime.block_on(peer.get<0>().recv()).unwrap().as_slice(), "socket"_bytes);
    EXPECT_TRUE(runtime.block_on(rstd::move(stream).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(peer.get<0>()).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

TEST(TcpSocket, Ipv6ListenerUsesTheSameOwnerPath) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto created = net::TcpSocket::make_v6();
    if (created.is_err()) GTEST_SKIP() << "IPv6 socket unavailable";
    auto socket = rstd::move(created).unwrap();
    ASSERT_TRUE(socket.bind(net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16())).is_ok());
    auto listener = rstd::move(socket).listen().unwrap();
    EXPECT_TRUE(listener.local_addr()->is_ipv6());
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

TEST(TcpSocket, FailedConsumingListenClosesTheDescriptor) {
    auto socket = net::TcpSocket::make_v4().unwrap();
#if RSTD_OS_UNIX
    auto descriptor = socket.as_socket().as_raw_socket();
#endif
    auto failed = rstd::move(socket).listen(i32(-1));
    ASSERT_TRUE(failed.is_err());
    EXPECT_EQ(failed.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
#if RSTD_OS_UNIX
    EXPECT_EQ(::fcntl(descriptor, F_GETFD), -1);
#endif
}
} // namespace
