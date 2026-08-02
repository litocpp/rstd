#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::bytes::Bytes;
using rstd::bytes::BytesMut;

TEST(BytesMut, PutSliceAndFreeze) {
    auto buf = BytesMut::with_capacity(usize(8));
    buf.put_slice("hello"_bytes);

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

    byte raw[] { byte { 4 }, byte { 5 } };
    buf.put_slice(rstd::as_u8_slice(slice<byte>::from_raw_parts(raw, usize(2))));

    auto frozen = buf.freeze();
    ASSERT_EQ(frozen.len(), usize(5));
    for (rstd::size_t index = 0; index < 5; ++index) {
        EXPECT_EQ(frozen[usize(index)], u8(index + 1));
    }
}

TEST(BytesMut, ResizeFillsLogicalGrowth) {
    auto buf = BytesMut::with_capacity(usize(8));
    buf.put_slice("ab"_bytes);

    buf.resize(usize(6), u8(0x7f));
    ASSERT_EQ(buf.len(), usize(6));
    EXPECT_EQ(buf[usize()], u8('a'));
    EXPECT_EQ(buf[usize(1)], u8('b'));
    for (rstd::size_t index = 2; index < 6; ++index) {
        EXPECT_EQ(buf[usize(index)], u8(0x7f));
    }

    buf.truncate(usize(3));
    buf.resize(usize(5), u8(0x55));
    ASSERT_EQ(buf.len(), usize(5));
    EXPECT_EQ(buf[usize(2)], u8(0x7f));
    EXPECT_EQ(buf[usize(3)], u8(0x55));
    EXPECT_EQ(buf[usize(4)], u8(0x55));
}

TEST(BytesMut, AdvanceAndSplitTo) {
    rstd::byte data[] { byte { 1 }, byte { 2 }, byte { 3 }, byte { 4 }, byte { 5 } };
    auto       buf = BytesMut::make();
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
    rstd::byte data[] { byte { 9 }, byte { 8 }, byte { 7 } };
    auto bytes = Bytes::copy_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, usize(3)));

    EXPECT_EQ(bytes.remaining(), usize(3));
    bytes.advance(usize(1));
    EXPECT_EQ(bytes.len(), usize(2));
    EXPECT_EQ(bytes[usize()], u8(8));
}

TEST(Bytes, CopiesLogicalValuesFromPhysicalByteStorage) {
    rstd::byte data[] { byte { 0x61 }, byte {}, byte { 0xff } };
    auto       raw   = slice<rstd::byte>::from_raw_parts(data, usize(3));
    auto       bytes = Bytes::copy_from_slice(rstd::as_u8_slice(raw));

    ASSERT_EQ(bytes.len(), usize(3));
    EXPECT_EQ(bytes[usize()], u8('a'));
    EXPECT_EQ(bytes[usize(1)], u8());
    EXPECT_EQ(bytes[usize(2)], u8(0xff));
}

TEST(ByteSlice, ProjectsPhysicalStorageAsLogicalU8WithoutCopying) {
    byte data[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto values = slice<u8>::from_raw_parts(data, usize(4));
    auto raw    = rstd::as_bytes(values);

    EXPECT_EQ(raw.as_raw_ptr(), data);
    ASSERT_EQ(raw.len(), usize(4));
    EXPECT_EQ(values[usize()], u8());
    EXPECT_EQ(values[usize(1)], u8(127));
    EXPECT_EQ(values[usize(2)], u8(128));
    EXPECT_EQ(values[usize(3)], u8(255));
}

TEST(ByteSlice, MutatesPhysicalStorageThroughLogicalAndRawViews) {
    byte data[] { byte {}, byte {} };
    auto values = mut_ref<u8[]>::from_raw_parts(data, usize(2));
    auto raw    = rstd::as_bytes_mut(values);

    values[usize()] = u8(128);
    raw[usize(1)]   = rstd::byte { 255 };

    EXPECT_EQ(data[0], rstd::byte { 128 });
    EXPECT_EQ(data[1], rstd::byte { 255 });
    EXPECT_EQ(values[usize()], u8(128));
    EXPECT_EQ(values[usize(1)], u8(255));
}

TEST(ByteSlice, LogicalProjectionConvertsElementsToU8) {
    rstd::byte data[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto       raw    = slice<rstd::byte>::from_raw_parts(data, usize(4));
    auto       values = rstd::as_u8_slice(raw);

    ASSERT_EQ(values.len(), usize(4));
    EXPECT_EQ(values[usize()], u8());
    EXPECT_EQ(values[usize(3)], u8(255));
}

TEST(ByteSlice, EmptyLogicalProjectionIsStable) {
    auto values = rstd::as_u8_slice(slice<rstd::byte> {});
    EXPECT_TRUE(values.is_empty());
    EXPECT_EQ(values.as_raw_ptr(), nullptr);
}

TEST(Hash, RawAndOwnedByteViewsMatch) {
    rstd::byte raw[] { byte {}, byte { 127 }, byte { 128 }, byte { 255 } };
    auto       raw_values = rstd::as_u8_slice(slice<rstd::byte>::from_raw_parts(raw, usize(4)));
    auto       owned      = Vec<u8>::from(raw_values);

    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(raw_values);

    rstd::hash::DefaultHasher owned_hasher;
    owned_hasher.write(owned.as_slice());

    EXPECT_EQ(raw_hasher.finish(), owned_hasher.finish());
}

TEST(Hash, ValueUsesItsRawObjectRepresentation) {
    u32 value(0x12345678U);

    rstd::hash::DefaultHasher value_hasher;
    value_hasher.write_value(value);

    auto raw = slice<rstd::byte>::from_raw_parts(
        reinterpret_cast<rstd::byte const*>(rstd::addressof(value)), usize(sizeof(value)));
    rstd::hash::DefaultHasher raw_hasher;
    raw_hasher.write(rstd::as_u8_slice(raw));

    EXPECT_EQ(value_hasher.finish(), raw_hasher.finish());
}
