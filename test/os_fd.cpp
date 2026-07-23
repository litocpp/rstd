#include <fcntl.h>
#include <unistd.h>
#include <gtest/gtest.h>
import rstd;

using namespace rstd::literals;

using rstd::os::fd::BorrowedFd;
using rstd::os::fd::AsFd;
using rstd::os::fd::AsRawFd;
using rstd::os::fd::FromRawFd;
using rstd::os::fd::INVALID_RAW_FD;
using rstd::os::fd::IntoRawFd;
using rstd::os::fd::OwnedFd;
using rstd::os::fd::RawFd;

static auto fd_open(int flags) -> int {
    return ::open("/dev/null", flags);
}

static auto fd_is_open(int fd) -> bool {
    return ::fcntl(fd, F_GETFD) != -1;
}

TEST(Fd, OwnedFdDefaultIsClosed) {
    OwnedFd fd;
    EXPECT_FALSE(fd.is_open());
    EXPECT_EQ(fd.as_raw_fd(), INVALID_RAW_FD);
}

TEST(Fd, OwnedFdClosesOnDrop) {
    int raw = fd_open(O_RDONLY);
    ASSERT_GE(raw, 0);
    ASSERT_TRUE(fd_is_open(raw));
    {
        auto fd = OwnedFd::from_raw_fd(raw);
        EXPECT_TRUE(fd.is_open());
        EXPECT_EQ(fd.as_raw_fd(), raw);
    }
    EXPECT_FALSE(fd_is_open(raw));
}

TEST(Fd, OwnedFdMoveTransfersOwnership) {
    int     raw = fd_open(O_RDONLY);
    auto    a   = OwnedFd::from_raw_fd(raw);
    OwnedFd b   = rstd::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.as_raw_fd(), raw);
    // b destroys → fd closed
}

TEST(Fd, OwnedFdMoveAssignClosesPrevious) {
    int  r1 = fd_open(O_RDONLY);
    int  r2 = fd_open(O_RDONLY);
    auto a  = OwnedFd::from_raw_fd(r1);
    auto b  = OwnedFd::from_raw_fd(r2);
    b       = rstd::move(a);
    EXPECT_FALSE(fd_is_open(r2)); // previous fd in b was closed
    EXPECT_TRUE(fd_is_open(r1));  // r1 still alive, owned by b
    EXPECT_EQ(b.as_raw_fd(), r1);
}

TEST(Fd, IntoRawFdLeaks) {
    int  raw    = fd_open(O_RDONLY);
    auto fd     = OwnedFd::from_raw_fd(raw);
    auto leaked = rstd::move(fd).into_raw_fd();
    EXPECT_EQ(leaked, raw);
    EXPECT_FALSE(fd.is_open());
    EXPECT_TRUE(fd_is_open(raw)); // ownership transferred, fd still open
    ::close(raw);                 // manual cleanup
}

TEST(Fd, TryCloneProducesDistinctFd) {
    auto a   = OwnedFd::from_raw_fd(fd_open(O_RDONLY));
    auto res = a.try_clone();
    ASSERT_TRUE(res.is_ok());
    OwnedFd b = rstd::move(res).unwrap_unchecked();
    EXPECT_TRUE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_NE(a.as_raw_fd(), b.as_raw_fd());
    EXPECT_GE(b.as_raw_fd(), 3);
    int raw_a = a.as_raw_fd();
    int raw_b = b.as_raw_fd();
    // both should be alive
    EXPECT_TRUE(fd_is_open(raw_a));
    EXPECT_TRUE(fd_is_open(raw_b));
}

TEST(Fd, BorrowedFdRoundTrip) {
    auto fd = OwnedFd::from_raw_fd(fd_open(O_RDONLY));
    auto b  = fd.as_fd();
    EXPECT_EQ(b.as_raw_fd(), fd.as_raw_fd());
}

TEST(Fd, CapabilitiesDispatchThroughPublicTraits) {
    auto fd       = OwnedFd::from_raw_fd(fd_open(O_RDONLY));
    auto borrowed = rstd::as<AsFd>(fd).as_fd();
    EXPECT_EQ(rstd::as<AsRawFd>(borrowed).as_raw_fd(), fd.as_raw_fd());

    auto raw = rstd::as<IntoRawFd>(fd).into_raw_fd();
    EXPECT_TRUE(fd_is_open(raw));

    auto adopted = FromRawFd::Api<OwnedFd>::from_raw_fd(raw);
    EXPECT_EQ(adopted.as_raw_fd(), raw);
}

TEST(Fd, InvalidRawFdPanics) {
    EXPECT_DEATH((void)OwnedFd::from_raw_fd(INVALID_RAW_FD), "invalid raw fd");
    EXPECT_DEATH((void)BorrowedFd::borrow_raw(INVALID_RAW_FD), "invalid raw fd");
}

TEST(Path, ToCStringRoundTrip) {
    rstd::ref<rstd::path::Path> p("/tmp/example"_str);
    auto                        res = p.to_cstring();
    ASSERT_TRUE(res.is_ok());
    auto cs    = rstd::move(res).unwrap_unchecked();
    auto bytes = cs.to_bytes();
    ASSERT_EQ(bytes.len(), rstd::usize(12)); // "/tmp/example" length
    auto with_nul = cs.to_bytes_with_nul();
    ASSERT_EQ(with_nul.len(), rstd::usize(13)); // includes trailing NUL
    EXPECT_EQ(with_nul[rstd::usize(12)], rstd::u8());
}

TEST(Path, ToCStringRejectsInteriorNul) {
    auto buf = rstd::vec::Vec<rstd::u8>::from("/tmp\0x"_bytes);
    auto os  = rstd::ref<rstd::ffi::OsStr>::from_encoded_bytes_unchecked(buf.as_slice());
    auto p   = rstd::ref<rstd::path::Path>(os);
    auto res = p.to_cstring();
    EXPECT_TRUE(res.is_err());
}
