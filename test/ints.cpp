#include <gtest/gtest.h>
import rstd;

TEST(U8, MinMax) {
    EXPECT_EQ(rstd::u8_::MIN, 0u);
    EXPECT_EQ(rstd::u8_::MAX, 255u);
    EXPECT_EQ(rstd::u8_::BITS, 8u);
}

TEST(U16, MinMax) {
    EXPECT_EQ(rstd::u16_::MIN, 0u);
    EXPECT_EQ(rstd::u16_::MAX, 65535u);
    EXPECT_EQ(rstd::u16_::BITS, 16u);
}

TEST(U32, MinMax) {
    EXPECT_EQ(rstd::u32_::MIN, 0u);
    EXPECT_EQ(rstd::u32_::MAX, 4294967295u);
    EXPECT_EQ(rstd::u32_::BITS, 32u);
}

TEST(U64, MinMax) {
    EXPECT_EQ(rstd::u64_::MIN, 0u);
    EXPECT_EQ(rstd::u64_::MAX, rstd::numeric_limits<rstd::u64>::max());
    EXPECT_EQ(rstd::u64_::BITS, 64u);
}

TEST(U128, MinMax) {
    EXPECT_EQ(rstd::u128_::MIN, rstd::u128(0));
    EXPECT_EQ(rstd::u128_::MAX, ~rstd::u128(0));
    EXPECT_EQ(rstd::u128_::BITS, 128u);
}

TEST(Usize, MinMax) {
    EXPECT_EQ(rstd::usize_::MIN, 0u);
    EXPECT_EQ(rstd::usize_::MAX, rstd::numeric_limits<rstd::usize>::max());
    EXPECT_EQ(rstd::usize_::BITS, 8u * sizeof(rstd::usize));
}

TEST(I8, MinMax) {
    EXPECT_EQ(rstd::i8_::MIN, -128);
    EXPECT_EQ(rstd::i8_::MAX, 127);
    EXPECT_EQ(rstd::i8_::BITS, 8u);
}

TEST(I16, MinMax) {
    EXPECT_EQ(rstd::i16_::MIN, -32768);
    EXPECT_EQ(rstd::i16_::MAX, 32767);
    EXPECT_EQ(rstd::i16_::BITS, 16u);
}

TEST(I32, MinMax) {
    EXPECT_EQ(rstd::i32_::MIN, -2147483647 - 1);
    EXPECT_EQ(rstd::i32_::MAX, 2147483647);
    EXPECT_EQ(rstd::i32_::BITS, 32u);
}

TEST(I64, MinMax) {
    EXPECT_EQ(rstd::i64_::MIN, rstd::numeric_limits<rstd::i64>::min());
    EXPECT_EQ(rstd::i64_::MAX, rstd::numeric_limits<rstd::i64>::max());
    EXPECT_EQ(rstd::i64_::BITS, 64u);
}

TEST(I128, MinMax) {
    EXPECT_EQ(rstd::i128_::MIN + 1, -rstd::i128_::MAX);
    EXPECT_EQ(rstd::i128_::MAX, static_cast<rstd::i128>(~rstd::u128(0) >> 1));
    EXPECT_EQ(rstd::i128_::BITS, 128u);
}

TEST(Isize, MinMax) {
    EXPECT_EQ(rstd::isize_::MIN, rstd::numeric_limits<rstd::isize>::min());
    EXPECT_EQ(rstd::isize_::MAX, rstd::numeric_limits<rstd::isize>::max());
    EXPECT_EQ(rstd::isize_::BITS, 8u * sizeof(rstd::isize));
}

static_assert(rstd::u8_::MAX == 255);
static_assert(rstd::u16_::MAX == 65535);
static_assert(rstd::u32_::MAX == 4294967295u);
static_assert(rstd::i32_::MIN == -2147483647 - 1);
static_assert(rstd::i32_::MAX == 2147483647);
static_assert(rstd::u128_::BITS == 128);
static_assert(rstd::i128_::MIN + 1 == -rstd::i128_::MAX);
