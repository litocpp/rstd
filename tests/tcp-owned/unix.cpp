#include <rstd/test/gtest.hpp>

import rstd;
import rstd.sys.libc;
import rstd.sys.socket;
import rstd.test;

using namespace rstd;
using namespace rstd::prelude;
using namespace rstd::literals;

#if ! defined(_WIN32)
TEST(UnixStream, GenericIoHalfCloseAndPreparedFdTransfer) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    int  descriptors[2];
    ASSERT_EQ(sys::libc::socketpair(sys::libc::AF_UNIX, sys::libc::SOCK_STREAM, 0, descriptors), 0);
    auto first =
        net::UnixStream::from_owned_fd(os::fd::OwnedFd::from_raw_fd(descriptors[0])).unwrap();
    auto second =
        net::UnixStream::from_owned_fd(os::fd::OwnedFd::from_raw_fd(descriptors[1])).unwrap();
    auto pending = [&] {
        auto old = rstd::move(first);
        return rstd::move(old).into_owned_fd();
    }();
    first = net::UnixStream::from_owned_fd(runtime.block_on(rstd::move(pending)).unwrap()).unwrap();
    auto message = bytes::Bytes::copy_from_slice("unix"_bytes);
    EXPECT_TRUE(runtime.block_on(async::io::write_all(first, message)).is_ok());
    auto buffer = bytes::BytesMut::with_capacity(usize(4));
    EXPECT_TRUE(runtime.block_on(async::io::read_exact(second, buffer, usize(4))).is_ok());
    EXPECT_EQ(buffer.as_slice(), "unix"_bytes);
    EXPECT_TRUE(runtime.block_on(async::io::flush(first)).is_ok());
    EXPECT_TRUE(runtime.block_on(async::io::shutdown(first)).is_ok());
    buffer.clear();
    EXPECT_EQ(runtime.block_on(async::io::read(second, buffer)).unwrap(), usize());
    EXPECT_TRUE(runtime.block_on(rstd::move(first).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(second).close()).is_ok());
}

struct UnixTestListener {
    test::TempDir           directory { test::TempDir::make().unwrap() };
    path::PathBuf           path { path::PathBuf::from(directory.path()).join("socket"_str) };
    bytes::Bytes            name { bytes::Bytes::copy_from_slice(
        path.as_path().as_os_str().as_encoded_bytes()) };
    os::socket::OwnedSocket socket { sys::socket::unix_stream().unwrap() };

    UnixTestListener() {
        sys::libc::sockaddr_un native {};
        if (name.len() >= usize(sizeof(native.sun_path))) panic { "test socket path is too long" };
        native.sun_family = sys::libc::AF_UNIX;
#if defined(__APPLE__)
        native.sun_len = sizeof(native);
#endif
        for (usize i; i < name.len(); ++i)
            native.sun_path[i.to_primitive()] =
                static_cast<char>(name.as_slice()[i].to_primitive());
        if (sys::libc::bind(socket.as_raw_socket(),
                            reinterpret_cast<const sys::libc::sockaddr*>(&native),
                            sizeof(native)) != 0)
            panic { "test socket bind failed" };
        sys::socket::set_nonblocking(socket.as_raw_socket(), true).unwrap();
        sys::socket::listen(socket.as_raw_socket()).unwrap();
    }

    auto accept() -> io::Result<net::UnixStream> {
        auto raw = sys::libc::accept(socket.as_raw_socket(), nullptr, nullptr);
        if (raw < 0) return Err(io::Error::last_os_error());
        return net::UnixStream::from_owned_fd(os::fd::OwnedFd::from_raw_fd(raw));
    }
};

TEST(UnixConnect, MovesAndTransfersOnlyAfterFinish) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    UnixTestListener listener;
    auto             owner = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
    EXPECT_TRUE(listener.accept().is_err());
    EXPECT_TRUE(rstd::move(owner).into_stream().is_err());
    auto finishing = owner.finish();
    EXPECT_TRUE(rstd::move(owner).into_stream().is_err());
    EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
    auto moved = rstd::move(owner);
    EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
    auto assigned = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
    assigned      = rstd::move(moved);
    EXPECT_TRUE(runtime.block_on(rstd::move(moved).close()).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(finishing)).is_ok());
    auto client = rstd::move(assigned).into_stream().unwrap();
    EXPECT_TRUE(rstd::move(assigned).into_stream().is_err());
    EXPECT_TRUE(runtime.block_on(assigned.finish()).is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(assigned).close()).is_err());
    auto peer = listener.accept().unwrap();
    EXPECT_EQ(
        runtime.block_on(client.send_all(bytes::Bytes::copy_from_slice("unix"_bytes))).unwrap(),
        usize(4));
    EXPECT_EQ(runtime.block_on(peer.recv(usize(4))).unwrap().as_slice(), "unix"_bytes);
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(peer).close()).is_ok());
}

TEST(UnixConnect, FinishOwnsResourcesAfterOwnerDrop) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    UnixTestListener listener;
    auto             finishing = [&] {
        auto owner = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
        return owner.finish();
    }();
    EXPECT_TRUE(listener.accept().is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(finishing)).is_ok());
    auto peer = listener.accept().unwrap();
    auto eof  = runtime
                    .block_on(async::timeout(async::into_future(peer.recv()),
                                             time::Duration::from_secs(u64(1))))
                    .unwrap();
    EXPECT_TRUE(eof.is_err() || eof.unwrap().is_empty());
    EXPECT_TRUE(runtime.block_on(rstd::move(peer).close()).is_ok());
}

#if defined(__linux__)
auto unix_descriptor_count() -> usize {
    auto  path    = path::PathBuf::from("/proc/self/fd"_str);
    auto  entries = fs::read_dir(path.as_path()).unwrap();
    usize count;
    while (auto entry = entries.next()) {
        EXPECT_TRUE(entry->is_ok());
        ++count;
    }
    return count;
}
#endif

TEST(UnixConnect, CancelledUnpolledFinishIsClosableAndDoesNotConnect) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    UnixTestListener listener;
    auto             warm = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
    runtime.block_on(rstd::move(warm).close()).unwrap();
#if defined(__linux__)
    auto before = unix_descriptor_count();
#endif
    for (unsigned i = 0; i < 32; ++i) {
        auto owner = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
        {
            auto cancelled = owner.finish();
        }
        EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
        EXPECT_TRUE(rstd::move(owner).into_stream().is_err());
        EXPECT_TRUE(runtime.block_on(rstd::move(owner).close()).is_ok());
        auto repeated = runtime.block_on(rstd::move(owner).close());
        ASSERT_TRUE(repeated.is_err());
        EXPECT_EQ(repeated.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::NotConnected });
    }
    EXPECT_TRUE(listener.accept().is_err());
#if defined(__linux__)
    EXPECT_EQ(unix_descriptor_count(), before);
#endif
}

TEST(UnixConnect, FailedFinishLeavesAClosableOwner) {
    auto          runtime   = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    test::TempDir directory = test::TempDir::make().unwrap();
    auto          path      = path::PathBuf::from(directory.path()).join("missing"_str);
    auto          owner     = net::UnixStream::prepare_connect(
                                  bytes::Bytes::copy_from_slice(path.as_path().as_os_str().as_encoded_bytes()))
                                  .unwrap();
    EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
    EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
    EXPECT_TRUE(rstd::move(owner).into_stream().is_err());
    EXPECT_TRUE(runtime.block_on(rstd::move(owner).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(owner).close()).is_err());
}

TEST(UnixConnect, ClosePreventsUnpolledFinishFromConnecting) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    UnixTestListener listener;
    auto             owner     = net::UnixStream::prepare_connect(listener.name.clone()).unwrap();
    auto             finishing = owner.finish();
    auto             closing   = rstd::move(owner).close();
    auto             result    = runtime.block_on(rstd::move(finishing));
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), io::ErrorKind { io::ErrorKind::NotConnected });
    EXPECT_TRUE(runtime.block_on(rstd::move(closing)).is_ok());
    EXPECT_TRUE(listener.accept().is_err());
    EXPECT_TRUE(runtime.block_on(owner.finish()).is_err());
}

TEST(UnixConnect, ConvenienceConnectUsesPreparedConnection) {
    auto runtime = async::RuntimeBuilder::current_thread().enable_all().build().unwrap();
    UnixTestListener listener;
    auto client = runtime.block_on(net::UnixStream::connect(listener.name.clone())).unwrap();
    auto peer   = listener.accept().unwrap();
    EXPECT_TRUE(runtime.block_on(rstd::move(client).close()).is_ok());
    EXPECT_TRUE(runtime.block_on(rstd::move(peer).close()).is_ok());
}
#endif
