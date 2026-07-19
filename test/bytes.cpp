#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::bytes::Bytes;
using rstd::bytes::BytesMut;

TEST(BytesMut, PutSliceAndFreeze) {
    auto     buf = BytesMut::with_capacity(usize(8));
    rstd::u8 hello[] { u8('h'), u8('e'), u8('l'), u8('l'), u8('o') };

    buf.put_slice(rstd::slice<rstd::u8>::from_raw_parts(hello, usize(5)));

    ASSERT_EQ(buf.len(), usize(5));
    EXPECT_EQ(buf[usize()], u8('h'));
    EXPECT_GE(buf.capacity(), usize(8));

    auto frozen = buf.freeze();
    EXPECT_EQ(frozen.len(), usize(5));
    EXPECT_EQ(frozen[usize(4)], u8('o'));
}

TEST(BytesMut, ChunkMutAndAdvanceMut) {
    auto buf = BytesMut::with_capacity(usize(4));

    auto chunk = buf.chunk_mut();
    ASSERT_EQ(chunk.len(), usize(4));
    chunk[usize()]  = u8(1);
    chunk[usize(1)] = u8(2);
    chunk[usize(2)] = u8(3);
    buf.advance_mut(usize(3));

    EXPECT_EQ(buf.len(), usize(3));
    EXPECT_EQ(buf[usize()], u8(1));
    EXPECT_EQ(buf[usize(2)], u8(3));
}

TEST(BytesMut, AppendsAfterWritableChunkAtTheLogicalEnd) {
    auto buf        = BytesMut::with_capacity(usize(4));
    auto chunk      = buf.chunk_mut();
    chunk[usize()]  = u8(1);
    chunk[usize(1)] = u8(2);
    chunk[usize(2)] = u8(3);
    buf.advance_mut(usize(3));

    byte raw[] { 4, 5 };
    buf.put_bytes(slice<byte>::from_raw_parts(raw, usize(2)));

    auto frozen = buf.freeze();
    ASSERT_EQ(frozen.len(), usize(5));
    for (rstd::size_t index = 0; index < 5; ++index) {
        EXPECT_EQ(frozen[usize(index)], u8(index + 1));
    }
}

TEST(BytesMut, AdvanceAndSplitTo) {
    rstd::u8 data[] { u8(1), u8(2), u8(3), u8(4), u8(5) };
    auto     buf = BytesMut::make();
    buf.extend_from_slice(slice<u8>::from_raw_parts(data, usize(5)));

    buf.advance(usize(2));
    ASSERT_EQ(buf.len(), usize(3));
    EXPECT_EQ(buf[usize()], u8(3));

    auto prefix = buf.split_to(usize(2));
    EXPECT_EQ(prefix.len(), usize(2));
    EXPECT_EQ(prefix[usize()], u8(3));
    EXPECT_EQ(prefix[usize(1)], u8(4));

    EXPECT_EQ(buf.len(), usize(1));
    EXPECT_EQ(buf[usize()], u8(5));
}

TEST(Bytes, CopyFromSliceAndAdvance) {
    rstd::u8 data[] { u8(9), u8(8), u8(7) };
    auto     bytes = Bytes::copy_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, usize(3)));

    EXPECT_EQ(bytes.remaining(), usize(3));
    bytes.advance(usize(1));
    EXPECT_EQ(bytes.len(), usize(2));
    EXPECT_EQ(bytes[usize()], u8(8));
}

TEST(Bytes, CopyFromRawBytesCreatesU8Objects) {
    const rstd::uint8_t data[] { 'a', 0, 0xff };
    auto bytes = Bytes::copy_from_bytes(slice<rstd::byte>::from_raw_parts(data, usize(3)));

    ASSERT_EQ(bytes.len(), usize(3));
    EXPECT_EQ(bytes[usize()], u8('a'));
    EXPECT_EQ(bytes[usize(1)], u8());
    EXPECT_EQ(bytes[usize(2)], u8(0xff));
}

TEST(ByteSlice, ProjectsOwnedU8StorageWithoutCopying) {
    u8   data[] { u8(), u8(127), u8(128), u8(255) };
    auto values = slice<u8>::from_raw_parts(data, usize(4));
    auto raw    = rstd::as_bytes(values);

    EXPECT_EQ(raw.as_raw_ptr(), reinterpret_cast<rstd::byte const*>(data));
    ASSERT_EQ(raw.len(), usize(4));
    EXPECT_EQ(raw[usize()], rstd::byte(0));
    EXPECT_EQ(raw[usize(1)], rstd::byte(127));
    EXPECT_EQ(raw[usize(2)], rstd::byte(128));
    EXPECT_EQ(raw[usize(3)], rstd::byte(255));
}

TEST(ByteSlice, MutatesOwnedU8StorageThroughRawView) {
    u8   data[] { u8(), u8() };
    auto values = mut_ref<u8[]>::from_raw_parts(data, usize(2));
    auto raw    = rstd::as_bytes_mut(values);

    raw[usize()]  = rstd::byte(128);
    raw[usize(1)] = rstd::byte(255);

    EXPECT_EQ(data[0], u8(128));
    EXPECT_EQ(data[1], u8(255));
}

TEST(ByteSlice, LazilyYieldsU8ValuesFromExternalStorage) {
    rstd::byte data[] { 0, 127, 128, 255 };
    auto       raw    = slice<rstd::byte>::from_raw_parts(data, usize(4));
    auto       values = rstd::u8_values(raw);

    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], u8());
    EXPECT_EQ(values[usize(3)], u8(255));

    rstd::size_t index = 0;
    for (u8 value : values) {
        EXPECT_EQ(value, u8(data[index]));
        ++index;
    }
    EXPECT_EQ(index, rstd::size_t(4));
}

TEST(ByteSlice, EmptyLazyRangeIsStable) {
    auto values = rstd::u8_values(slice<rstd::byte> {});
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.begin(), values.end());
}

TEST(Hash, RawAndOwnedByteViewsMatch) {
    rstd::byte raw[] { 0, 127, 128, 255 };
    u8         owned[] { u8(), u8(127), u8(128), u8(255) };

    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(slice<rstd::byte>::from_raw_parts(raw, usize(4)));

    rstd::hash::DefaultHasher owned_hasher;
    owned_hasher.write(rstd::as_bytes(slice<u8>::from_raw_parts(owned, usize(4))));

    EXPECT_EQ(raw_hasher.finish(), owned_hasher.finish());
}

TEST(Hash, ValueUsesItsRawObjectRepresentation) {
    u32 value(0x12345678U);

    rstd::hash::DefaultHasher value_hasher;
    value_hasher.write_value(value);

    auto raw = slice<rstd::byte>::from_raw_parts(
        reinterpret_cast<rstd::byte const*>(rstd::addressof(value)), usize(sizeof(value)));
    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(raw);

    EXPECT_EQ(value_hasher.finish(), raw_hasher.finish());
}
