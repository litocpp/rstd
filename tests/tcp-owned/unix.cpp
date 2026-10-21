#include <rstd/test/gtest.hpp>

import rstd;
import rstd.sys.libc;

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
#endif
