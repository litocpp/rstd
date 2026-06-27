#include <gtest/gtest.h>
import rstd;

using namespace rstd::prelude;
using rstd::bytes::Bytes;
using rstd::bytes::BytesMut;

TEST(BytesMut, PutSliceAndFreeze) {
    auto buf = BytesMut::with_capacity(8);
    rstd::u8 hello[] { 'h', 'e', 'l', 'l', 'o' };

    buf.put_slice(rstd::slice<rstd::u8>::from_raw_parts(hello, 5));

    ASSERT_EQ(buf.len(), 5);
    EXPECT_EQ(buf[0], 'h');
    EXPECT_GE(buf.capacity(), 8);

    auto frozen = buf.freeze();
    EXPECT_EQ(frozen.len(), 5);
    EXPECT_EQ(frozen[4], 'o');
}

TEST(BytesMut, ChunkMutAndAdvanceMut) {
    auto buf = BytesMut::with_capacity(4);

    auto chunk = buf.chunk_mut();
    ASSERT_EQ(chunk.len(), 4);
    chunk[0] = 1;
    chunk[1] = 2;
    chunk[2] = 3;
    buf.advance_mut(3);

    EXPECT_EQ(buf.len(), 3);
    EXPECT_EQ(buf[0], 1);
    EXPECT_EQ(buf[2], 3);
}

TEST(BytesMut, AdvanceAndSplitTo) {
    rstd::u8 data[] { 1, 2, 3, 4, 5 };
    auto     buf = BytesMut::make();
    buf.extend_from_slice(data, 5);

    buf.advance(2);
    ASSERT_EQ(buf.len(), 3);
    EXPECT_EQ(buf[0], 3);

    auto prefix = buf.split_to(2);
    EXPECT_EQ(prefix.len(), 2);
    EXPECT_EQ(prefix[0], 3);
    EXPECT_EQ(prefix[1], 4);

    EXPECT_EQ(buf.len(), 1);
    EXPECT_EQ(buf[0], 5);
}

TEST(Bytes, CopyFromSliceAndAdvance) {
    rstd::u8 data[] { 9, 8, 7 };
    auto     bytes = Bytes::copy_from_slice(rstd::slice<rstd::u8>::from_raw_parts(data, 3));

    EXPECT_EQ(bytes.remaining(), 3);
    bytes.advance(1);
    EXPECT_EQ(bytes.len(), 2);
    EXPECT_EQ(bytes[0], 8);
}
