#include <fcntl.h>
#include <unistd.h>

#include <gtest/gtest.h>
import rstd;

using rstd::sys::fd::BorrowedFd;
using rstd::sys::fd::INVALID_RAW_FD;
using rstd::sys::fd::OwnedFd;
using rstd::sys::fd::RawFd;

static auto fd_open(int flags) -> int { return ::open("/dev/null", flags); }

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
        OwnedFd fd { raw };
        EXPECT_TRUE(fd.is_open());
        EXPECT_EQ(fd.as_raw_fd(), raw);
    }
    EXPECT_FALSE(fd_is_open(raw));
}

TEST(Fd, OwnedFdMoveTransfersOwnership) {
    int     raw = fd_open(O_RDONLY);
    OwnedFd a { raw };
    OwnedFd b = rstd::move(a);
    EXPECT_FALSE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_EQ(b.as_raw_fd(), raw);
    // b destroys → fd closed
}

TEST(Fd, OwnedFdMoveAssignClosesPrevious) {
    int     r1 = fd_open(O_RDONLY);
    int     r2 = fd_open(O_RDONLY);
    OwnedFd a { r1 };
    OwnedFd b { r2 };
    b = rstd::move(a);
    EXPECT_FALSE(fd_is_open(r2)); // previous fd in b was closed
    EXPECT_TRUE(fd_is_open(r1));  // r1 still alive, owned by b
    EXPECT_EQ(b.as_raw_fd(), r1);
}

TEST(Fd, IntoRawFdLeaks) {
    int     raw = fd_open(O_RDONLY);
    OwnedFd fd { raw };
    auto    leaked = fd.into_raw_fd();
    EXPECT_EQ(leaked, raw);
    EXPECT_FALSE(fd.is_open());
    EXPECT_TRUE(fd_is_open(raw)); // ownership transferred, fd still open
    ::close(raw);                 // manual cleanup
}

TEST(Fd, TryCloneProducesDistinctFd) {
    OwnedFd a { fd_open(O_RDONLY) };
    auto    res = a.try_clone();
    ASSERT_TRUE(res.is_ok());
    OwnedFd b = rstd::move(res).unwrap_unchecked();
    EXPECT_TRUE(a.is_open());
    EXPECT_TRUE(b.is_open());
    EXPECT_NE(a.as_raw_fd(), b.as_raw_fd());
    int raw_a = a.as_raw_fd();
    int raw_b = b.as_raw_fd();
    // both should be alive
    EXPECT_TRUE(fd_is_open(raw_a));
    EXPECT_TRUE(fd_is_open(raw_b));
}

TEST(Fd, BorrowedFdRoundTrip) {
    OwnedFd fd { fd_open(O_RDONLY) };
    auto    b = fd.as_fd();
    EXPECT_EQ(b.as_raw_fd(), fd.as_raw_fd());
}

TEST(Path, ToCStringRoundTrip) {
    rstd::ref<rstd::path::Path> p("/tmp/example");
    auto                        res = p.to_cstring();
    ASSERT_TRUE(res.is_ok());
    auto cs    = rstd::move(res).unwrap_unchecked();
    auto bytes = cs.to_bytes();
    ASSERT_EQ(bytes.len(), 12u); // "/tmp/example" length
    auto with_nul = cs.to_bytes_with_nul();
    ASSERT_EQ(with_nul.len(), 13u); // includes trailing NUL
    EXPECT_EQ(with_nul.p[12], 0);
}

TEST(Path, ToCStringRejectsInteriorNul) {
    rstd::u8                    buf[] = { '/', 't', 'm', 'p', 0, 'x' };
    rstd::ref<rstd::path::Path> p     = rstd::ref<rstd::path::Path>::from_raw_parts(buf, 6);
    auto                        res   = p.to_cstring();
    EXPECT_TRUE(res.is_err());
}
