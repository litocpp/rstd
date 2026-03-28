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

// ── Cursor<Vec<u8>> ───────────────────────────────────────────────────────

TEST(Io, CursorVecRead) {
    using rstd::vec::Vec;
    Vec<u8> v = Vec<u8>::with_capacity(5);
    const u8 init[] = { 1, 2, 3, 4, 5 };
    for (auto b : init) v.push(u8(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    u8 buf[3] {};
    auto res = as<io::Read>(cur).read(buf, 3);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
    EXPECT_EQ(buf[0], u8(1));
    EXPECT_EQ(buf[2], u8(3));
    EXPECT_EQ(cur.position(), u64(3));
}

TEST(Io, CursorVecWrite) {
    using rstd::vec::Vec;
    auto cur = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(0));
    const u8 msg[] = { 'h', 'i' };
    auto res = as<io::Write>(cur).write(msg, 2);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(2));
    EXPECT_EQ(cur.position(), u64(2));
    EXPECT_EQ(cur.get_ref().len(), usize(2));
    EXPECT_EQ(cur.get_ref()[0], u8('h'));
}

TEST(Io, CursorVecSeek) {
    using rstd::vec::Vec;
    Vec<u8> v = Vec<u8>::with_capacity(4);
    for (u8 b : { u8(10), u8(20), u8(30), u8(40) }) v.push(u8(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    // Seek to end
    auto res = as<io::Seek>(cur).seek(io::SeekFrom::from_end(0));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), u64(4));
    // Seek back 2
    auto res2 = as<io::Seek>(cur).seek(io::SeekFrom::from_current(-2));
    EXPECT_TRUE(res2.is_ok());
    EXPECT_EQ(res2.unwrap_unchecked(), u64(2));
    u8 buf[1] {};
    as<io::Read>(cur).read(buf, 1);
    EXPECT_EQ(buf[0], u8(30));
}

TEST(Io, CursorVecBufRead) {
    using rstd::vec::Vec;
    Vec<u8> v = Vec<u8>::with_capacity(3);
    for (u8 b : { u8(7), u8(8), u8(9) }) v.push(u8(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    auto res = as<io::BufRead>(cur).fill_buf();
    EXPECT_TRUE(res.is_ok());
    auto sl = res.unwrap_unchecked();
    EXPECT_EQ(sl.len(), usize(3));
    EXPECT_EQ(sl[0], u8(7));
    as<io::BufRead>(cur).consume(2);
    EXPECT_EQ(cur.position(), u64(2));
}

// ── Cursor<slice<u8>> ─────────────────────────────────────────────────────

TEST(Io, CursorSliceRead) {
    const u8 data[] = { 10, 20, 30 };
    auto sl  = slice<u8>::from_raw_parts(data, 3);
    auto cur = io::Cursor<slice<u8>>(sl);
    u8 buf[2] {};
    auto res = as<io::Read>(cur).read(buf, 2);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(buf[0], u8(10));
    EXPECT_EQ(buf[1], u8(20));
    EXPECT_EQ(cur.position(), u64(2));
}

// ── BufReader ─────────────────────────────────────────────────────────────

TEST(Io, BufReaderBasic) {
    // Use Cursor<Vec<u8>> as the inner reader.
    using rstd::vec::Vec;
    Vec<u8> v = Vec<u8>::with_capacity(6);
    const u8 init[] = { 1, 2, 3, 4, 5, 6 };
    for (auto b : init) v.push(u8(b));
    auto inner = io::Cursor<Vec<u8>>(rstd::move(v));
    auto br    = io::BufReader<io::Cursor<Vec<u8>>>(rstd::move(inner), 4);
    u8 buf[6] {};
    // First read — fills internal 4-byte buffer, returns 4
    auto r1 = as<io::Read>(br).read(buf, 6);
    EXPECT_TRUE(r1.is_ok());
    usize n1 = r1.unwrap_unchecked();
    EXPECT_GT(n1, usize(0));
    EXPECT_LE(n1, usize(6));
    EXPECT_EQ(buf[0], u8(1));
}

TEST(Io, BufReaderFillBuf) {
    using rstd::vec::Vec;
    Vec<u8> v = Vec<u8>::with_capacity(3);
    for (u8 b : { u8(11), u8(22), u8(33) }) v.push(u8(b));
    auto inner = io::Cursor<Vec<u8>>(rstd::move(v));
    auto br    = io::BufReader<io::Cursor<Vec<u8>>>(rstd::move(inner), 8);
    auto res = as<io::BufRead>(br).fill_buf();
    EXPECT_TRUE(res.is_ok());
    auto sl = res.unwrap_unchecked();
    EXPECT_GE(sl.len(), usize(3));
    EXPECT_EQ(sl[0], u8(11));
    as<io::BufRead>(br).consume(1);
}

// ── BufWriter ─────────────────────────────────────────────────────────────

TEST(Io, BufWriterBasic) {
    using rstd::vec::Vec;
    // Use Cursor<Vec<u8>> as sink.
    auto sink_cur = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(0));
    auto bw       = io::BufWriter<io::Cursor<Vec<u8>>>(rstd::move(sink_cur), 8);
    const u8 msg[] = { 'a', 'b', 'c' };
    auto res = as<io::Write>(bw).write(msg, 3);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
    // Flush to push data to inner.
    auto fres = as<io::Write>(bw).flush();
    EXPECT_TRUE(fres.is_ok());
}

// ── Empty / Repeat / Sink ─────────────────────────────────────────────────

TEST(Io, EmptyRead) {
    auto e = io::empty_io();
    u8 buf[4] {};
    auto res = as<io::Read>(e).read(buf, 4);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(0));
}

TEST(Io, EmptyWrite) {
    auto e = io::empty_io();
    const u8 msg[] = "x";
    auto res = as<io::Write>(e).write(msg, 1);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(1));
}

TEST(Io, RepeatRead) {
    auto r = io::repeat(u8(0xAB));
    u8 buf[4] {};
    auto res = as<io::Read>(r).read(buf, 4);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(4));
    for (int i = 0; i < 4; ++i)
        EXPECT_EQ(buf[i], u8(0xAB));
}

TEST(Io, SinkWrite) {
    auto s = io::sink();
    const u8 msg[] = { 1, 2, 3 };
    auto res = as<io::Write>(s).write(msg, 3);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
}

// ── copy() ────────────────────────────────────────────────────────────────

TEST(Io, CopyAll) {
    using rstd::vec::Vec;
    // Source: Cursor over 5 bytes
    Vec<u8> src_v = Vec<u8>::with_capacity(5);
    for (u8 b : { u8(1), u8(2), u8(3), u8(4), u8(5) }) src_v.push(u8(b));
    auto src = io::Cursor<Vec<u8>>(rstd::move(src_v));
    // Dest: Cursor<Vec<u8>> starting empty
    auto dst = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(0));
    auto res = io::copy(src, dst);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), u64(5));
    EXPECT_EQ(dst.get_ref().len(), usize(5));
    EXPECT_EQ(dst.get_ref()[0], u8(1));
    EXPECT_EQ(dst.get_ref()[4], u8(5));
}
