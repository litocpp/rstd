#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>
#if RSTD_OS_LINUX
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#endif

import rstd;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

void verify_original_destination(bool ipv6) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto created = ipv6 ? net::UdpSocketBuilder::make_v6() : net::UdpSocketBuilder::make_v4();
    if (ipv6 && created.is_err()) GTEST_SKIP() << "IPv6 socket unavailable";
    auto builder    = rstd::move(created).unwrap();
    auto configured = os::linux::net::set_recv_original_destination(builder.as_socket(), true);
#if ! RSTD_OS_LINUX
    ASSERT_TRUE(configured.is_err());
    EXPECT_EQ(configured.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
    return;
#else
    ASSERT_TRUE(configured.is_ok());
    auto owned    = rstd::move(builder).into_owned_socket();
    builder       = net::UdpSocketBuilder::from_owned_socket(rstd::move(owned));
    auto local    = ipv6 ? net::SocketAddr::ipv6(net::Ipv6Addr::loopback(), u16())
                         : net::SocketAddr::ipv4_loopback(u16());
    auto receiver = rstd::move(builder).bind(local, { true, true }).unwrap();
    EXPECT_TRUE(receiver.capabilities().original_destination);
    auto sender = net::UdpSocket::connect_with(receiver.local_addr(), { false, true }).unwrap();
    for (unsigned i = 0; i < 3; ++i) {
        auto message = i == 0 ? ""_bytes : "destination"_bytes;
        EXPECT_TRUE(runtime
                        .block_on(sender.send_to(bytes::Bytes::copy_from_slice(message),
                                                 receiver.local_addr()))
                        .is_ok());
        auto received = runtime
                            .block_on(async::timeout(async::into_future(receiver.recv()),
                                                     time::Duration::from_secs(u64(1))))
                            .unwrap()
                            .unwrap();
        EXPECT_EQ(received.data.as_slice(), message);
        ASSERT_TRUE(received.info.original_destination.is_some());
        EXPECT_EQ(*received.info.original_destination, receiver.local_addr());
        ASSERT_TRUE(received.info.packet.is_some());
        EXPECT_EQ(received.info.packet->local, receiver.local_addr());
        EXPECT_EQ(received.info.peer, sender.local_addr());
        EXPECT_FALSE(received.info.control_truncated);
    }
    auto ordinary = net::UdpSocket::bind_with(local, {}).unwrap();
    EXPECT_TRUE(runtime
                    .block_on(sender.send_to(bytes::Bytes::copy_from_slice("plain"_bytes),
                                             ordinary.local_addr()))
                    .is_ok());
    auto received = runtime.block_on(ordinary.recv()).unwrap();
    EXPECT_TRUE(received.info.original_destination.is_none());
    EXPECT_TRUE(runtime.block_on(rstd::move(ordinary).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(sender).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(receiver).close()).is_ok());
#endif
}

TEST(UdpSocketBuilder, OriginalDestinationV4) {
    verify_original_destination(false);
}
TEST(UdpSocketBuilder, OriginalDestinationV6) {
    verify_original_destination(true);
}

TEST(UdpSocketBuilder, FailedConnectConsumesTheOwner) {
    auto builder = net::UdpSocketBuilder::make_v4().unwrap();
#if RSTD_OS_LINUX
    auto raw = builder.as_socket().as_raw_socket();
#endif
    auto connected = rstd::move(builder).connect(net::SocketAddr::ipv4_loopback(u16()));
    ASSERT_TRUE(connected.is_err());
    EXPECT_EQ(connected.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::InvalidInput });
#if RSTD_OS_LINUX
    EXPECT_EQ(::fcntl(raw, F_GETFD), -1);
#endif
}

TEST(LinuxSocket, MarkUsesTheBorrowedOwnerBeforeBind) {
    auto socket = net::TcpSocket::make_v4().unwrap();
    auto marked = os::linux::net::set_mark(socket.as_socket(), u32(0x200));
#if RSTD_OS_LINUX
    if (marked.is_err() &&
        marked.unwrap_err().kind() == io::ErrorKind { io::ErrorKind::PermissionDenied })
        GTEST_SKIP() << "SO_MARK requires network capabilities";
    ASSERT_TRUE(marked.is_ok());
    unsigned  mark {};
    socklen_t length = sizeof(mark);
    EXPECT_EQ(::getsockopt(socket.as_socket().as_raw_socket(), SOL_SOCKET, SO_MARK, &mark, &length),
              0);
    EXPECT_EQ(mark, 0x200u);
    EXPECT_TRUE(socket.bind(net::SocketAddr::ipv4_loopback(u16())).is_ok());
#else
    ASSERT_TRUE(marked.is_err());
    EXPECT_EQ(marked.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
#endif
}

void verify_transparent(bool ipv6) {
    auto socket      = (ipv6 ? net::TcpSocket::make_v6() : net::TcpSocket::make_v4()).unwrap();
    auto transparent = os::linux::net::set_transparent(socket.as_socket(), true);
#if RSTD_OS_LINUX
    if (transparent.is_err() &&
        transparent.unwrap_err().kind() == io::ErrorKind { io::ErrorKind::PermissionDenied })
        GTEST_SKIP() << "transparent binding requires network capabilities";
    ASSERT_TRUE(transparent.is_ok());
    auto local = net::SocketAddr::parse(ipv6 ? "[2001:db8::1]:0"_str : "198.18.0.1:0"_str).unwrap();
    EXPECT_TRUE(socket.bind(local).is_ok());
    EXPECT_EQ(socket.local_addr()->is_ipv6(), ipv6);
    EXPECT_NE(socket.local_addr()->port(), u16());
    EXPECT_TRUE(os::linux::net::set_transparent(socket.as_socket(), false).is_ok());
#else
    ASSERT_TRUE(transparent.is_err());
    EXPECT_EQ(transparent.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::Unsupported });
#endif
}
TEST(LinuxSocket, TransparentV4) {
    verify_transparent(false);
}
TEST(LinuxSocket, TransparentV6) {
    verify_transparent(true);
}
