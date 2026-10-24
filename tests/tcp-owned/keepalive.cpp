#include <rstd/test/gtest.hpp>
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
TEST(TcpKeepalive, AppliesAndReadsSocketSettings) {
    auto runtime = rstd::async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    auto listener = rstd::net::TcpListener::bind(rstd::net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client = runtime.block_on(rstd::net::TcpStream::connect(listener.local_addr().unwrap())).unwrap();
    auto accepted = runtime.block_on(listener.accept()).unwrap();
    auto server = rstd::move(accepted.get<0>());
    EXPECT_FALSE(client.keepalive().unwrap());
    EXPECT_TRUE(client.set_keepalive({true, {}, {}}).is_ok());
    EXPECT_TRUE(client.keepalive().unwrap());
    rstd::net::TcpKeepalive timed {true, Some(rstd::time::Duration::from_secs(u64(20))), Some(rstd::time::Duration::from_secs(u64(5)))};
    auto configured = client.set_keepalive(timed);
    if (client.keepalive_timing_supported()) EXPECT_TRUE(configured.is_ok());
    else EXPECT_TRUE(configured.unwrap_err().kind() == rstd::io::ErrorKind {rstd::io::ErrorKind::Unsupported});
    EXPECT_TRUE(client.set_keepalive({false, {}, {}}).is_ok());
    EXPECT_FALSE(client.keepalive().unwrap());
    EXPECT_TRUE(client.set_keepalive({true, Some(rstd::time::Duration::from_millis(u64(1))), {}}).is_err());
    EXPECT_FALSE(client.keepalive().unwrap());
    EXPECT_TRUE(client.set_nodelay(false).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(server).close()).is_ok());
}
