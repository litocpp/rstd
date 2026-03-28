#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd;
using namespace rstd::io;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;

// ── ErrorKind ─────────────────────────────────────────────────────────────

TEST(Io, ErrorKindDisplay) {
    auto e = ErrorKind { ErrorKind::NotFound };
    auto s = rstd::format("{}", e);
    EXPECT_EQ(s, "entity not found");
}

TEST(Io, ErrorKindDebug) {
    auto e = ErrorKind { ErrorKind::PermissionDenied };
    auto s = rstd::format("{:?}", e);
    EXPECT_EQ(s, "permission denied");
}

// ── Error ─────────────────────────────────────────────────────────────────

TEST(Io, ErrorFromKind) {
    auto e = Error::from_kind(ErrorKind { ErrorKind::NotFound });
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::NotFound });
    EXPECT_TRUE(e.raw_os_error().is_none());
}

TEST(Io, ErrorFromOsError) {
    auto e = Error::from_raw_os_error(2); // ENOENT on Linux
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::NotFound });
    EXPECT_EQ(e.raw_os_error().unwrap_unchecked(), 2);
}

TEST(Io, ErrorDisplay) {
    auto e = Error::from_kind(ErrorKind { ErrorKind::TimedOut });
    EXPECT_EQ(rstd::format("{}", e), "timed out");
}

TEST(Io, ErrorOsDisplay) {
    auto e = Error::from_raw_os_error(2);
    auto s = rstd::format("{}", e);
    EXPECT_EQ(s, "entity not found (os error 2)");
}

TEST(Io, ErrorMessage) {
    auto e = error::Error_READ_EXACT_EOF;
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::UnexpectedEof });
    EXPECT_EQ(rstd::format("{}", e), "failed to fill whole buffer");
}

TEST(Io, ErrorDebug) {
    auto e = Error::from_raw_os_error(13); // EACCES
    auto s = rstd::format("{:?}", e);
    EXPECT_EQ(s, "Os(13)");
}

// ── SeekFrom ──────────────────────────────────────────────────────────────

TEST(Io, SeekFrom) {
    auto s = SeekFrom::from_start(42);
    EXPECT_EQ(s.which, SeekFrom::Which::Start);
    EXPECT_EQ(s.offset, i64(42));

    auto e = SeekFrom::from_end(-10);
    EXPECT_EQ(e.which, SeekFrom::Which::End);
    EXPECT_EQ(e.offset, i64(-10));

    auto c = SeekFrom::from_current(0);
    EXPECT_EQ(c.which, SeekFrom::Which::Current);
}

// ── write_all via in-memory Write impl ────────────────────────────────────

struct BufWrite {
    u8    data[256] {};
    usize pos = 0;
};

template<>
struct rstd::Impl<io::Write, BufWrite> : ImplBase<BufWrite> {
    auto write(const u8* buf, usize len) -> io::Result<usize> {
        auto& self = this->self();
        usize n = cppstd::min(len, usize(sizeof(self.data)) - self.pos);
        cppstd::memcpy(self.data + self.pos, buf, n);
        self.pos += n;
        return Ok(n);
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

TEST(Io, WriteAll) {
    BufWrite w;
    const u8 msg[] = "hello world";
    auto res = io::write_all(w, msg, sizeof(msg) - 1);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(w.pos, usize(11));
    EXPECT_EQ(w.data[0], u8('h'));
    EXPECT_EQ(w.data[10], u8('d'));
}

// ── read_exact via in-memory Read impl ────────────────────────────────────

struct MemRead {
    const u8* data;
    usize     len;
    usize     pos = 0;
};

template<>
struct rstd::Impl<io::Read, MemRead> : ImplBase<MemRead> {
    auto read(u8* buf, usize len) -> io::Result<usize> {
        auto& self = this->self();
        usize n = cppstd::min(len, self.len - self.pos);
        cppstd::memcpy(buf, self.data + self.pos, n);
        self.pos += n;
        return Ok(n);
    }
};

TEST(Io, ReadExact) {
    const u8 src[] = "abcdef";
    MemRead r { src, 6, 0 };
    u8 dst[4] {};
    auto res = io::read_exact(r, dst, 4);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(dst[0], u8('a'));
    EXPECT_EQ(dst[3], u8('d'));
    EXPECT_EQ(r.pos, usize(4));
}

TEST(Io, ReadExactUnexpectedEof) {
    const u8 src[] = "ab";
    MemRead r { src, 2, 0 };
    u8 dst[4] {};
    auto res = io::read_exact(r, dst, 4);
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.unwrap_err_unchecked().kind(),
              ErrorKind { ErrorKind::UnexpectedEof });
}

// ── Stdio smoke tests ─────────────────────────────────────────────────────

TEST(Io, EprintSmoke) {
    // Must compile and not crash.
    io::eprint_fmt({ (const u8*)"[io test]\n", 10, nullptr, 0 });
}

TEST(Io, PrintlnSmoke) {
    io::println<> {};
    io::println { "io println: {}", 42 };
}
