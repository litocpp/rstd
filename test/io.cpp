#include <cerrno>
#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd;
using namespace rstd::io;
using rstd::io::error::Error;
using rstd::io::error::ErrorKind;
using rstd::vec::Vec;

template<rstd::size_t N>
auto native_bytes(const char (&value)[N]) -> Vec<u8> {
    return Vec<u8>::copy_from_bytes(
        slice<byte>::from_raw_parts(reinterpret_cast<byte const*>(value), usize(N - 1)));
}

template<rstd::size_t N>
auto raw_bytes(const u8 (&values)[N]) -> slice<byte> {
    return as_bytes(slice<u8>::from_raw_parts(values, usize(N)));
}

template<rstd::size_t N>
auto raw_bytes(u8 (&values)[N]) -> mut_ref<byte[]> {
    return as_bytes_mut(mut_ref<u8[]>::from_raw_parts(values, usize(N)));
}

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
    EXPECT_EQ(e.tag(), Error::Tag::Kind);
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::NotFound });
    EXPECT_TRUE(e.raw_os_error().is_none());
}

TEST(Io, ErrorFromOsError) {
    auto e = Error::from_raw_os_error(i32(2)); // ENOENT on Linux
    EXPECT_EQ(e.tag(), Error::Tag::Os);
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::NotFound });
    EXPECT_EQ(e.raw_os_error().unwrap_unchecked(), i32(2));
}

TEST(Io, LastOsError) {
    errno  = ENOENT;
    auto e = Error::last_os_error();
    EXPECT_EQ(e.raw_os_error().unwrap_unchecked(), i32(ENOENT));
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::NotFound });
}

TEST(Io, ErrorDisplay) {
    auto e = Error::from_kind(ErrorKind { ErrorKind::TimedOut });
    EXPECT_EQ(rstd::format("{}", e), "timed out");
}

TEST(Io, ErrorOsDisplay) {
    auto e = Error::from_raw_os_error(i32(2));
    auto s = rstd::format("{}", e);
    EXPECT_EQ(s, "entity not found (os error 2)");
}

TEST(Io, ErrorMessage) {
    static_assert(error::Error_READ_EXACT_EOF.tag() == Error::Tag::Message);
    auto e = error::Error_READ_EXACT_EOF;
    EXPECT_EQ(e.kind(), ErrorKind { ErrorKind::UnexpectedEof });
    EXPECT_EQ(rstd::format("{}", e), "failed to fill whole buffer");
}

TEST(Io, ErrorDebug) {
    auto e = Error::from_raw_os_error(i32(13)); // EACCES
    auto s = rstd::format("{:?}", e);
    EXPECT_EQ(s, "Os(13)");
}

// ── SeekFrom ──────────────────────────────────────────────────────────────

TEST(Io, SeekFrom) {
    auto s = SeekFrom::from_start(u64(42));
    EXPECT_EQ(s.which, SeekFrom::Which::Start);
    EXPECT_EQ(s.start, u64(42));
    EXPECT_EQ(s.offset, i64());

    auto e = SeekFrom::from_end(i64(-10));
    EXPECT_EQ(e.which, SeekFrom::Which::End);
    EXPECT_EQ(e.offset, i64(-10));

    auto c = SeekFrom::from_current(i64());
    EXPECT_EQ(c.which, SeekFrom::Which::Current);
}

// ── write_all via in-memory Write impl ────────────────────────────────────

struct BufWrite {
    u8    data[256] {};
    usize pos {};
};

template<>
struct rstd::Impl<io::Write, BufWrite> : ImplBase<BufWrite> {
    auto write(slice<byte> buf) -> io::Result<usize> {
        auto& self        = this->self();
        usize n           = rstd::min(buf.len(), usize(sizeof(self.data)) - self.pos);
        auto  destination = mut_ref<u8[]>::from_raw_parts(self.data + self.pos.to_primitive(), n);
        rstd::mem::memcpy(as_bytes_mut(destination).as_raw_ptr(), buf.as_raw_ptr(), n);
        self.pos += n;
        return Ok(n);
    }
    auto flush() -> io::Result<empty> { return Ok(empty {}); }
};

TEST(Io, WriteAll) {
    BufWrite w;
    auto     msg = native_bytes("hello world");
    auto     res = io::write_all(w, as_bytes(msg.as_slice()));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(w.pos, usize(11));
    EXPECT_EQ(w.data[0], u8('h'));
    EXPECT_EQ(w.data[10], u8('d'));
}

// ── read_exact via in-memory Read impl ────────────────────────────────────

struct MemRead {
    const u8* data;
    usize     len;
    usize     pos {};
};

struct MemReadAt {
    const u8* data;
    usize     len;

    auto read_at(mut_ref<byte[]> buf, u64 offset) const -> io::Result<usize> {
        auto position = try_from<usize>(offset);
        if (position.is_err()) return Ok(usize());
        auto position_value = rstd::move(position).unwrap_unchecked();
        if (position_value >= len) return Ok(usize());
        auto count  = rstd::min(buf.len(), len - position_value);
        auto source = slice<u8>::from_raw_parts(data + position_value.to_primitive(), count);
        rstd::mem::memcpy(buf.as_raw_ptr(), as_bytes(source).as_raw_ptr(), count);
        return Ok(count);
    }
};

template<>
struct rstd::Impl<io::ReadAt, MemReadAt> : rstd::ImplBase<MemReadAt> {
    auto read_at(mut_ref<byte[]> buf, u64 offset) const -> io::Result<usize> {
        return this->self().read_at(buf, offset);
    }
};

template<>
struct rstd::Impl<io::Read, MemRead> : ImplBase<MemRead> {
    auto read(mut_ref<byte[]> buf) -> io::Result<usize> {
        auto& self   = this->self();
        usize n      = rstd::min(buf.len(), self.len - self.pos);
        auto  source = slice<u8>::from_raw_parts(self.data + self.pos.to_primitive(), n);
        rstd::mem::memcpy(buf.as_raw_ptr(), as_bytes(source).as_raw_ptr(), n);
        self.pos += n;
        return Ok(n);
    }
};

TEST(Io, ReadExact) {
    auto    src = native_bytes("abcdef");
    MemRead r { src.data(), src.len(), usize() };
    u8      dst[4] {};
    auto    res = io::read_exact(r, raw_bytes(dst));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(dst[0], u8('a'));
    EXPECT_EQ(dst[3], u8('d'));
    EXPECT_EQ(r.pos, usize(4));
}

TEST(Io, ReadExactUnexpectedEof) {
    auto    src = native_bytes("ab");
    MemRead r { src.data(), src.len(), usize() };
    u8      dst[4] {};
    auto    res = io::read_exact(r, raw_bytes(dst));
    EXPECT_TRUE(res.is_err());
    EXPECT_EQ(res.unwrap_err_unchecked().kind(), ErrorKind { ErrorKind::UnexpectedEof });
}

TEST(Io, ReadExactAtDoesNotChangeSourceState) {
    auto      src = native_bytes("abcdefgh");
    MemReadAt source { src.data(), src.len() };
    u8        dst[4] {};

    auto result = io::read_exact_at(source, raw_bytes(dst), u64(2));

    EXPECT_TRUE(result.is_ok());
    EXPECT_FALSE(rstd::mem::memcmp(dst, "cdef", usize(4)));
}

TEST(Io, RangeReaderOwnsAnIndependentCursor) {
    auto src    = native_bytes("0123456789");
    auto source = io::SharedReadAt::make(MemReadAt { src.data(), src.len() });
    auto first  = io::RangeReader::make(source.clone(), u64(2), u64(5)).unwrap_unchecked();
    auto second = io::RangeReader::make(rstd::move(source), u64(4), u64(3)).unwrap_unchecked();

    u8 first_buf[3] {};
    u8 second_buf[3] {};
    EXPECT_EQ(as<io::Read>(first).read(raw_bytes(first_buf)).unwrap_unchecked(), usize(3));
    EXPECT_EQ(as<io::Read>(second).read(raw_bytes(second_buf)).unwrap_unchecked(), usize(3));
    EXPECT_FALSE(rstd::mem::memcmp(first_buf, "234", usize(3)));
    EXPECT_FALSE(rstd::mem::memcmp(second_buf, "456", usize(3)));
    EXPECT_EQ(first.position(), u64(3));
    EXPECT_EQ(second.position(), u64(3));

    EXPECT_EQ(as<io::Seek>(first).seek(SeekFrom::from_end(i64(-2))).unwrap_unchecked(), u64(3));
    u8 tail[2] {};
    EXPECT_EQ(as<io::Read>(first).read(raw_bytes(tail)).unwrap_unchecked(), usize(2));
    EXPECT_FALSE(rstd::mem::memcmp(tail, "56", usize(2)));
}

TEST(Io, ReadRangeCreatesReusableReadersAndSubranges) {
    auto src    = native_bytes("0123456789");
    auto source = io::SharedReadAt::make(MemReadAt { src.data(), src.len() });
    auto range  = io::ReadRange::make(rstd::move(source), u64(2), u64(6)).unwrap_unchecked();
    auto first  = range.reader();
    auto second = range.reader();
    auto tail   = range.subrange(u64(3), u64(2)).unwrap_unchecked().into_reader();

    u8 first_buf[2] {};
    u8 second_buf[2] {};
    u8 tail_buf[2] {};
    EXPECT_EQ(as<io::Read>(first).read(raw_bytes(first_buf)).unwrap_unchecked(), usize(2));
    EXPECT_EQ(as<io::Read>(second).read(raw_bytes(second_buf)).unwrap_unchecked(), usize(2));
    EXPECT_EQ(as<io::Read>(tail).read(raw_bytes(tail_buf)).unwrap_unchecked(), usize(2));
    EXPECT_FALSE(rstd::mem::memcmp(first_buf, "23", usize(2)));
    EXPECT_FALSE(rstd::mem::memcmp(second_buf, "23", usize(2)));
    EXPECT_FALSE(rstd::mem::memcmp(tail_buf, "56", usize(2)));
    EXPECT_TRUE(range.subrange(u64(5), u64(2)).is_err());
}

TEST(Io, ReadSeekHandleDispatchesBothCapabilities) {
    auto src    = native_bytes("abcdef");
    auto source = io::SharedReadAt::make(MemReadAt { src.data(), src.len() });
    auto range  = io::RangeReader::make(rstd::move(source), u64(1), u64(4)).unwrap_unchecked();
    auto handle = io::ReadSeekHandle::make(rstd::move(range));

    u8 out[2] {};
    EXPECT_EQ(handle->read(raw_bytes(out)).unwrap_unchecked(), usize(2));
    EXPECT_FALSE(rstd::mem::memcmp(out, "bc", usize(2)));
    EXPECT_EQ(handle->seek(SeekFrom::from_start(u64())).unwrap_unchecked(), u64());
}

// ── Stdio smoke tests ─────────────────────────────────────────────────────

TEST(Io, EprintSmoke) {
    // Must compile and not crash.
    const rstd::uint8_t message[] { '[', 'i', 'o', ' ', 't', 'e', 's', 't', ']', '\n' };
    io::eprint_fmt({ message, 10, nullptr, 0 });
}

TEST(Io, PrintlnSmoke) {
    io::println<> {};
    io::println { "io println: {}", 42 };
}

// ── Cursor<Vec<u8>> ───────────────────────────────────────────────────────

TEST(Io, CursorVecRead) {
    Vec<u8>  v      = Vec<u8>::with_capacity(usize(5));
    const u8 init[] = { u8(1), u8(2), u8(3), u8(4), u8(5) };
    for (auto b : init) v.push(rstd::move(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    u8   buf[3] {};
    auto res = as<io::Read>(cur).read(raw_bytes(buf));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
    EXPECT_EQ(buf[0], u8(1));
    EXPECT_EQ(buf[2], u8(3));
    EXPECT_EQ(cur.position(), u64(3));
}

TEST(Io, CursorVecWrite) {
    auto     cur   = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(usize()));
    const u8 msg[] = { u8('h'), u8('i') };
    auto     res   = as<io::Write>(cur).write(raw_bytes(msg));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(2));
    EXPECT_EQ(cur.position(), u64(2));
    EXPECT_EQ(cur.get_ref().len(), usize(2));
    EXPECT_EQ(cur.get_ref()[usize()], u8('h'));
}

TEST(Io, CursorAcceptsExternalRawBuffers) {
    Vec<u8> source = Vec<u8>::make();
    source.push(u8(0));
    source.push(u8(128));
    source.push(u8(255));
    auto reader = io::Cursor<Vec<u8>>(rstd::move(source));

    byte destination[3] {};
    auto writable = mut_ref<byte[]>::from_raw_parts(destination, usize(3));
    EXPECT_EQ(as<io::Read>(reader).read(writable).unwrap_unchecked(), usize(3));
    EXPECT_EQ(destination[0], byte(0));
    EXPECT_EQ(destination[1], byte(128));
    EXPECT_EQ(destination[2], byte(255));

    const byte input[] { 'a', 0, 0xff };
    auto       writer   = io::Cursor<Vec<u8>>(Vec<u8>::make());
    auto       readable = slice<byte>::from_raw_parts(input, usize(3));
    EXPECT_EQ(as<io::Write>(writer).write(readable).unwrap_unchecked(), usize(3));
    EXPECT_EQ(writer.get_ref()[usize()], u8('a'));
    EXPECT_EQ(writer.get_ref()[usize(1)], u8());
    EXPECT_EQ(writer.get_ref()[usize(2)], u8(255));
}

TEST(Io, CursorVecSeek) {
    Vec<u8> v = Vec<u8>::with_capacity(usize(4));
    for (u8 b : { u8(10), u8(20), u8(30), u8(40) }) v.push(rstd::move(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    // Seek to end
    auto res = as<io::Seek>(cur).seek(io::SeekFrom::from_end(i64()));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), u64(4));
    // Seek back 2
    auto res2 = as<io::Seek>(cur).seek(io::SeekFrom::from_current(i64(-2)));
    EXPECT_TRUE(res2.is_ok());
    EXPECT_EQ(res2.unwrap_unchecked(), u64(2));
    u8 buf[1] {};
    as<io::Read>(cur).read(raw_bytes(buf));
    EXPECT_EQ(buf[0], u8(30));
}

TEST(Io, CursorVecBufRead) {
    Vec<u8> v = Vec<u8>::with_capacity(usize(3));
    for (u8 b : { u8(7), u8(8), u8(9) }) v.push(rstd::move(b));
    auto cur = io::Cursor<Vec<u8>>(rstd::move(v));
    auto res = as<io::BufRead>(cur).fill_buf();
    EXPECT_TRUE(res.is_ok());
    auto sl = res.unwrap_unchecked();
    EXPECT_EQ(sl.len(), usize(3));
    EXPECT_EQ(sl[usize()], u8(7));
    as<io::BufRead>(cur).consume(usize(2));
    EXPECT_EQ(cur.position(), u64(2));
}

// ── Cursor<slice<u8>> ─────────────────────────────────────────────────────

TEST(Io, CursorSliceRead) {
    const u8 data[] = { u8(10), u8(20), u8(30) };
    auto     sl     = slice<u8>::from_raw_parts(data, usize(3));
    auto     cur    = io::Cursor<slice<u8>>(sl);
    u8       buf[2] {};
    auto     res = as<io::Read>(cur).read(raw_bytes(buf));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(buf[0], u8(10));
    EXPECT_EQ(buf[1], u8(20));
    EXPECT_EQ(cur.position(), u64(2));
}

// ── BufReader ─────────────────────────────────────────────────────────────

TEST(Io, BufReaderBasic) {
    // Use Cursor<Vec<u8>> as the inner reader.
    Vec<u8>  v      = Vec<u8>::with_capacity(usize(6));
    const u8 init[] = { u8(1), u8(2), u8(3), u8(4), u8(5), u8(6) };
    for (auto b : init) v.push(rstd::move(b));
    auto inner = io::Cursor<Vec<u8>>(rstd::move(v));
    auto br    = io::BufReader<io::Cursor<Vec<u8>>>(rstd::move(inner), usize(4));
    u8   buf[6] {};
    // First read — fills internal 4-byte buffer, returns 4
    auto r1 = as<io::Read>(br).read(raw_bytes(buf));
    EXPECT_TRUE(r1.is_ok());
    usize n1 = r1.unwrap_unchecked();
    EXPECT_GT(n1, usize(0));
    EXPECT_LE(n1, usize(6));
    EXPECT_EQ(buf[0], u8(1));
}

TEST(Io, BufReaderFillBuf) {
    Vec<u8> v = Vec<u8>::with_capacity(usize(3));
    for (u8 b : { u8(11), u8(22), u8(33) }) v.push(rstd::move(b));
    auto inner = io::Cursor<Vec<u8>>(rstd::move(v));
    auto br    = io::BufReader<io::Cursor<Vec<u8>>>(rstd::move(inner), usize(8));
    auto res   = as<io::BufRead>(br).fill_buf();
    EXPECT_TRUE(res.is_ok());
    auto sl = res.unwrap_unchecked();
    EXPECT_GE(sl.len(), usize(3));
    EXPECT_EQ(sl[usize()], u8(11));
    as<io::BufRead>(br).consume(usize(1));
}

// ── BufWriter ─────────────────────────────────────────────────────────────

TEST(Io, BufWriterBasic) {
    // Use Cursor<Vec<u8>> as sink.
    auto     sink_cur = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(usize()));
    auto     bw       = io::BufWriter<io::Cursor<Vec<u8>>>(rstd::move(sink_cur), usize(8));
    const u8 msg[]    = { u8('a'), u8('b'), u8('c') };
    auto     res      = as<io::Write>(bw).write(raw_bytes(msg));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
    // Flush to push data to inner.
    auto fres = as<io::Write>(bw).flush();
    EXPECT_TRUE(fres.is_ok());
}

// ── Empty / Repeat / Sink ─────────────────────────────────────────────────

TEST(Io, EmptyRead) {
    auto e = io::empty_io();
    u8   buf[4] {};
    auto res = as<io::Read>(e).read(raw_bytes(buf));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(0));
}

TEST(Io, EmptyWrite) {
    auto     e     = io::empty_io();
    const u8 msg[] = { u8('x') };
    auto     res   = as<io::Write>(e).write(raw_bytes(msg));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(1));
}

TEST(Io, RepeatRead) {
    auto r = io::repeat(u8(0xAB));
    u8   buf[4] {};
    auto res = as<io::Read>(r).read(raw_bytes(buf));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(4));
    for (rstd::size_t i = 0; i < 4; ++i) EXPECT_EQ(buf[i], u8(0xAB));
}

TEST(Io, SinkWrite) {
    auto     s     = io::sink();
    const u8 msg[] = { u8(1), u8(2), u8(3) };
    auto     res   = as<io::Write>(s).write(raw_bytes(msg));
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), usize(3));
}

// ── copy() ────────────────────────────────────────────────────────────────

TEST(Io, CopyAll) {
    // Source: Cursor over 5 bytes
    Vec<u8> src_v = Vec<u8>::with_capacity(usize(5));
    for (u8 b : { u8(1), u8(2), u8(3), u8(4), u8(5) }) src_v.push(rstd::move(b));
    auto src = io::Cursor<Vec<u8>>(rstd::move(src_v));
    // Dest: Cursor<Vec<u8>> starting empty
    auto dst = io::Cursor<Vec<u8>>(Vec<u8>::with_capacity(usize()));
    auto res = io::copy(src, dst);
    EXPECT_TRUE(res.is_ok());
    EXPECT_EQ(res.unwrap_unchecked(), u64(5));
    EXPECT_EQ(dst.get_ref().len(), usize(5));
    EXPECT_EQ(dst.get_ref()[usize()], u8(1));
    EXPECT_EQ(dst.get_ref()[usize(4)], u8(5));
}
