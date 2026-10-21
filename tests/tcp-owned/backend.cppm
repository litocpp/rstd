module;
#include <rstd/macro.hpp>
#include <rstd/test/gtest.hpp>

export module rstd:tcp_owned_backend_tests;
import :async;
import :async.poll;
import :async.runtime;
import :net.tcp;
import :net.udp;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

auto stream_close_source(async::Registration registration, async::CompletionSource source)
    -> async::coro<io::Result<empty>>;
namespace rstd::net
{
auto close_datagram(async::CompletionSource source) -> async::coro<io::Result<empty>>;
}

void verify_owned_tcp_backend(async::IoBackendPreference preference) {
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
    auto runtime  = builder.build().unwrap();
    auto listener = net::TcpListener::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
    auto client =
        runtime.block_on(net::TcpStream::connect(listener.local_addr().unwrap())).unwrap();
    auto accepted  = runtime.block_on(listener.accept()).unwrap();
    auto server    = rstd::move(accepted.get<0>());
    auto receiving = server.recv(usize(4));
    EXPECT_EQ(
        runtime.block_on(client.send_all(bytes::Bytes::copy_from_slice("kept"_bytes))).unwrap(),
        usize(4));
    EXPECT_EQ(runtime.block_on(rstd::move(receiving)).unwrap().as_slice(), "kept"_bytes);
    auto waiting = runtime.spawn(server.recv());
    EXPECT_TRUE(
        runtime
            .block_on(async::timeout(future::by_ref(waiting), time::Duration::from_millis(u64(2))))
            .is_err());
    waiting.abort();
    EXPECT_TRUE(runtime.block_on(rstd::move(waiting)).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(server).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(client.recv()).unwrap().is_empty());
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    {
        auto connection =
            runtime.block_on(net::TcpStream::connect(listener.local_addr().unwrap())).unwrap();
        auto peer         = runtime.block_on(listener.accept()).unwrap();
        auto owned        = runtime.block_on(rstd::move(connection).into_owned_socket()).unwrap();
        auto registration = async::Registration::register_fd(owned.as_raw_socket()).unwrap();
        auto source       = async::CompletionSource::socket(rstd::move(owned));
        auto metadata     = source.clone();
        EXPECT_TRUE(
            runtime.block_on(stream_close_source(rstd::move(registration), rstd::move(source)))
                .is_ok());
        {
            auto retained = metadata.take_owned_socket();
            EXPECT_TRUE(retained.is_err());
        }
        auto eof = runtime
                       .block_on(async::timeout(async::into_future(peer.get<0>().recv()),
                                                time::Duration::from_secs(u64(1))))
                       .unwrap();
        EXPECT_TRUE(eof.is_err() || eof.unwrap().is_empty());
        EXPECT_TRUE(runtime.block_on(rstd::move(peer.get<0>()).close()).is_ok());
    }
    {
        auto socket   = net::UdpSocket::bind(net::SocketAddr::ipv4_loopback(u16())).unwrap();
        auto address  = socket.local_addr();
        auto owned    = runtime.block_on(rstd::move(socket).into_owned_socket()).unwrap();
        auto source   = async::CompletionSource::socket(rstd::move(owned));
        auto metadata = source.clone();
        EXPECT_TRUE(runtime.block_on(net::close_datagram(rstd::move(source))).is_ok());
        auto rebound = net::UdpSocket::bind(address);
        EXPECT_TRUE(rebound.is_ok());
        {
            auto retained = metadata.take_owned_socket();
            EXPECT_TRUE(retained.is_err());
        }
        if (rebound.is_ok())
            EXPECT_TRUE(runtime.block_on(rstd::move(rebound).unwrap().close()).is_ok());
    }
    usize interrupted_connect;
    usize interrupted_close;
    for (unsigned i = 0; i < 4; ++i) {
        auto connecting = net::TcpStream::prepare_connect(listener.local_addr().unwrap()).unwrap();
        auto finished   = runtime.block_on(async::timeout(async::into_future(connecting.finish()),
                                                          time::Duration::from_millis(u64())));
        if (finished.is_err())
            ++interrupted_connect;
        else
            EXPECT_TRUE(finished.unwrap().is_ok());
        auto closing = async::into_future(rstd::move(connecting).close());
        auto early   = runtime.block_on(
            async::timeout(future::by_ref(closing), time::Duration::from_millis(u64())));
        if (early.is_err()) {
            ++interrupted_close;
            EXPECT_TRUE(runtime.block_on(rstd::move(closing)).is_ok());
        } else
            EXPECT_TRUE(early.unwrap().is_ok());
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
    io::eprintln("backend interrupted TCP connect/close: {}/4, {}/4",
                 interrupted_connect,
                 interrupted_close);
    EXPECT_TRUE(runtime.block_on(rstd::move(listener).close()).is_ok());
}

#if RSTD_OS_LINUX
TEST(TcpOwnedBackend, ReadinessEmulation) {
    verify_owned_tcp_backend(async::IoBackendPreference::ReadinessEmulationRequired);
}
TEST(TcpOwnedBackend, NativeCompletion) {
    verify_owned_tcp_backend(async::IoBackendPreference::NativeCompletionRequired);
}
#endif
