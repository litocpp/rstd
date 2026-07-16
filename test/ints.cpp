#include <gtest/gtest.h>
import rstd;

using namespace rstd;

static_assert(Impled<u16, str_::FromStr>);
static_assert(Impled<i128, str_::FromStr>);
static_assert(! Impled<bool, str_::FromStr>);
static_assert(! Impled<char, str_::FromStr>);

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

TEST(IntFromStr, ParsesPrimitiveIntegerBoundaries) {
    EXPECT_EQ(from_str<i8>("-128").unwrap(), i8_::MIN);
    EXPECT_EQ(from_str<i8>("+127").unwrap(), i8_::MAX);
    EXPECT_EQ(from_str<u8>("255").unwrap(), u8_::MAX);
    EXPECT_EQ(from_str<i32>("-0").unwrap(), 0);
    EXPECT_EQ(from_str<i64>("-9223372036854775808").unwrap(), i64_::MIN);
    EXPECT_EQ(from_str<u64>("18446744073709551615").unwrap(), u64_::MAX);
    EXPECT_EQ(from_str<i128>("-170141183460469231731687303715884105728").unwrap(), i128_::MIN);
    EXPECT_EQ(from_str<u128>("340282366920938463463374607431768211455").unwrap(), u128_::MAX);
}

TEST(IntFromStr, ReportsRustCompatibleErrorKinds) {
    auto empty = from_str<i32>("").unwrap_err();
    EXPECT_TRUE(empty.kind()->is_Empty());
    EXPECT_EQ(rstd::format("{}", empty), "cannot parse integer from empty string");
    EXPECT_EQ(rstd::format("{:?}", empty), "ParseIntError { kind: Empty }");

    auto invalid = from_str<i32>(" 1").unwrap_err();
    EXPECT_TRUE(invalid.kind()->is_InvalidDigit());
    EXPECT_EQ(rstd::format("{}", invalid), "invalid digit found in string");

    EXPECT_TRUE(from_str<u32>("-1").unwrap_err().kind()->is_InvalidDigit());
    EXPECT_TRUE(from_str<i32>("+").unwrap_err().kind()->is_InvalidDigit());
    EXPECT_TRUE(from_str<i8>("128").unwrap_err().kind()->is_PosOverflow());
    EXPECT_TRUE(from_str<i8>("-129").unwrap_err().kind()->is_NegOverflow());
    EXPECT_TRUE(from_str<u128>("340282366920938463463374607431768211456")
                    .unwrap_err()
                    .kind()
                    ->is_PosOverflow());
}
