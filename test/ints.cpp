#include <gtest/gtest.h>
#include <type_traits>
import rstd;

using namespace rstd;
using namespace rstd::literals;

static_assert(Impled<u16, str_::FromStr>);
static_assert(Impled<i128, str_::FromStr>);
static_assert(! Impled<bool, str_::FromStr>);
static_assert(! Impled<char, str_::FromStr>);

using WideningResult = decltype(try_from<u16>(u8 {}));
static_assert(mtp::same_as<WideningResult, Result<u16, convert::Infallible>>);

using NarrowingResult = decltype(try_from<u8>(u16 {}));
static_assert(mtp::same_as<NarrowingResult, Result<u8, num::TryFromIntError>>);

template<typename Wrapper, typename Primitive>
inline constexpr bool has_wrapper_layout =
    sizeof(Wrapper) == sizeof(Primitive) && alignof(Wrapper) == alignof(Primitive) &&
    std::is_standard_layout_v<Wrapper> && std::is_trivially_copyable_v<Wrapper> &&
    std::is_trivially_destructible_v<Wrapper>;

static_assert(has_wrapper_layout<u8, rstd::uint8_t>);
static_assert(has_wrapper_layout<u16, rstd::uint16_t>);
static_assert(has_wrapper_layout<u32, rstd::uint32_t>);
static_assert(has_wrapper_layout<u64, rstd::uint64_t>);
static_assert(has_wrapper_layout<u128, unsigned __int128>);
static_assert(has_wrapper_layout<usize, rstd::size_t>);
static_assert(has_wrapper_layout<i8, rstd::int8_t>);
static_assert(has_wrapper_layout<i16, rstd::int16_t>);
static_assert(has_wrapper_layout<i32, rstd::int32_t>);
static_assert(has_wrapper_layout<i64, rstd::int64_t>);
static_assert(has_wrapper_layout<i128, __int128>);
static_assert(has_wrapper_layout<isize, rstd::ptrdiff_t>);
static_assert(has_wrapper_layout<f32, float>);
static_assert(has_wrapper_layout<f64, double>);
static_assert(mtp::is_int<u32>);
static_assert(mtp::is_int<const u32&>);
static_assert(! mtp::is_int<rstd::uint32_t>);
static_assert(! mtp::is_int<bool>);
static_assert(rstd::is_raw_int<rstd::uint32_t>);
static_assert(rstd::is_raw_int<rstd::uint64_t>);
static_assert(rstd::is_raw_int<bool>);
static_assert(! rstd::is_raw_int<u32>);
static_assert(! rstd::is_raw_int<double>);
static_assert(mtp::is_arithmetic<u32>);
static_assert(mtp::is_arithmetic<rstd::uint32_t>);
static_assert(num::Integer<u32>);
static_assert(! num::Integer<rstd::uint32_t>);
static_assert(mtp::is_float<double>);
static_assert(! mtp::is_float<f64>);
static_assert(num::Float<f64>);
static_assert(! num::Float<double>);

template<typename... Types>
struct NumericTypes {};

template<typename From, typename... To>
consteval auto supports_fallible_and_lossy_row(NumericTypes<To...>) -> bool {
    return ((Impled<To, convert::TryFrom<From>> && Impled<From, convert::TryInto<To>> &&
             requires(From value) { AsCast<To, From>::cast(value); }) &&
            ...);
}

template<typename... Types>
consteval auto supports_fallible_and_lossy_matrix(NumericTypes<Types...> types) -> bool {
    return (supports_fallible_and_lossy_row<Types>(types) && ...);
}

using NumericWrappers =
    NumericTypes<u8, u16, u32, u64, u128, usize, i8, i16, i32, i64, i128, isize, f32, f64>;

static_assert(supports_fallible_and_lossy_matrix(NumericWrappers {}));

enum class NumericCode : rstd::uint8_t
{
    Maximum = 255
};

static_assert(Impled<u8, convert::From<NumericCode>>);
static_assert(Impled<u8, convert::TryFrom<rstd::int16_t>>);
static_assert(Impled<rstd::uint16_t, convert::TryFrom<u8>>);
static_assert(Impled<NumericCode, convert::TryFrom<u16>>);

template<typename T>
concept has_native_add = requires(T value) { value + 1; };

template<typename T>
concept has_increment = requires(T value) { ++value; };

template<typename T>
concept has_decrement = requires(T value) { --value; };

template<typename T>
concept can_increment_temporary = requires { ++T(); };

template<typename T>
concept can_decrement_temporary = requires { --T(); };

static_assert(mtp::same_as<decltype(u8() + u8()), u8>);
static_assert(mtp::same_as<decltype(i16() * i16()), i16>);
static_assert(! has_native_add<u8>);
static_assert(has_increment<u8>);
static_assert(has_decrement<usize>);
static_assert(! has_increment<i8>);
static_assert(! has_decrement<isize>);
static_assert(! can_increment_temporary<u8>);
static_assert(! can_decrement_temporary<usize>);
static_assert(mtp::same_as<decltype(++*static_cast<u8*>(nullptr)), u8&>);
static_assert(mtp::same_as<decltype((*static_cast<u8*>(nullptr))++), u8>);
static_assert(mtp::same_as<decltype(--*static_cast<usize*>(nullptr)), usize&>);
static_assert(mtp::same_as<decltype((*static_cast<usize*>(nullptr))--), usize>);
static_assert(! std::is_convertible_v<int, usize>);

consteval auto accepts_usize(usize value) -> usize {
    return value;
}

static_assert(accepts_usize(usize(4)) == 4_usize);
static_assert(u8('x') == 120_u8);
static_assert(u64(4) == 4_u64);
static_assert([] {
    usize value {};
    value = usize(4);
    return value == 4_usize;
}());
static_assert([] {
    auto value = 1_u8;
    if (++value != 2_u8) return false;
    if (value++ != 2_u8 || value != 3_u8) return false;
    if (--value != 2_u8) return false;
    return value-- == 2_u8 && value == 1_u8;
}());

TEST(IntConstruction, ExplicitPrimitiveWidths) {
    rstd::ptrdiff_t value = 7;
    EXPECT_EQ(usize(value), 7_usize);
}

TEST(IntIncrement, PrefixAndPostfixPreserveCppValueCategories) {
    auto value = 1_u8;

    auto& incremented = ++value;
    EXPECT_EQ(&incremented, &value);
    EXPECT_EQ(value, 2_u8);
    EXPECT_EQ(value++, 2_u8);
    EXPECT_EQ(value, 3_u8);

    auto& decremented = --value;
    EXPECT_EQ(&decremented, &value);
    EXPECT_EQ(value, 2_u8);
    EXPECT_EQ(value--, 2_u8);
    EXPECT_EQ(value, 1_u8);
}

static_assert(Impled<u16, convert::From<u8>>);
static_assert(! Impled<u8, convert::From<u16>>);
static_assert(Impled<u8, convert::TryFrom<u16>>);
static_assert(Impled<i16, convert::From<u8>>);
static_assert(Impled<i16, convert::From<i8>>);
static_assert(! Impled<i8, convert::From<u8>>);
static_assert(! Impled<u8, convert::From<i8>>);
static_assert(Impled<f64, convert::From<u32>>);
static_assert(! Impled<f64, convert::From<u64>>);
static_assert(Impled<f64, convert::From<f32>>);
static_assert(Impled<f32, convert::TryFrom<f64>>);

TEST(U8, MinMax) {
    EXPECT_EQ(u8::MIN, 0_u8);
    EXPECT_EQ(u8::MAX, 255_u8);
    EXPECT_EQ(u8::BITS, 8_u32);
}

TEST(U16, MinMax) {
    EXPECT_EQ(u16::MIN, 0_u16);
    EXPECT_EQ(u16::MAX, 65535_u16);
    EXPECT_EQ(u16::BITS, 16_u32);
}

TEST(U32, MinMax) {
    EXPECT_EQ(u32::MIN, 0_u32);
    EXPECT_EQ(u32::MAX, 4294967295_u32);
    EXPECT_EQ(u32::BITS, 32_u32);
}

TEST(U64, MinMax) {
    EXPECT_EQ(u64::MIN, 0_u64);
    EXPECT_EQ(u64::MAX.to_primitive(), ~rstd::uint64_t(0));
    EXPECT_EQ(u64::BITS, 64_u32);
}

TEST(U128, MinMax) {
    EXPECT_EQ(u128::MIN, 0_u128);
    EXPECT_EQ(u128::MAX, ~u128());
    EXPECT_EQ(u128::BITS, 128_u32);
}

TEST(Usize, MinMax) {
    EXPECT_EQ(usize::MIN, 0_usize);
    EXPECT_EQ(usize::MAX.to_primitive(), ~rstd::size_t(0));
    EXPECT_EQ(usize::BITS.to_primitive(), 8u * sizeof(usize));
}

TEST(I8, MinMax) {
    EXPECT_EQ(i8::MIN, i8(-128));
    EXPECT_EQ(i8::MAX, 127_i8);
    EXPECT_EQ(i8::BITS, 8_u32);
}

TEST(I16, MinMax) {
    EXPECT_EQ(i16::MIN, i16(-32768));
    EXPECT_EQ(i16::MAX, 32767_i16);
    EXPECT_EQ(i16::BITS, 16_u32);
}

TEST(I32, MinMax) {
    EXPECT_EQ(i32::MIN, -2147483647_i32 - 1_i32);
    EXPECT_EQ(i32::MAX, 2147483647_i32);
    EXPECT_EQ(i32::BITS, 32_u32);
}

TEST(I64, MinMax) {
    EXPECT_EQ(i64::MIN, -9223372036854775807_i64 - 1_i64);
    EXPECT_EQ(i64::MAX, 9223372036854775807_i64);
    EXPECT_EQ(i64::BITS, 64_u32);
}

TEST(I128, MinMax) {
    EXPECT_EQ(i128::MIN + 1_i128, -i128::MAX);
    EXPECT_EQ(i128::MAX, as_cast<i128>(~u128() >> 1_u64));
    EXPECT_EQ(i128::BITS, 128_u32);
}

TEST(Isize, MinMax) {
    EXPECT_EQ(isize::MIN, -isize::MAX - 1_isize);
    EXPECT_EQ(isize::MAX.to_primitive(), static_cast<rstd::ptrdiff_t>(~rstd::uintptr_t(0) >> 1));
    EXPECT_EQ(isize::BITS.to_primitive(), 8u * sizeof(isize));
}

static_assert(u8::MAX == 255_u8);
static_assert(u16::MAX == 65535_u16);
static_assert(u32::MAX == 4294967295_u32);
static_assert(i32::MIN == -2147483647_i32 - 1_i32);
static_assert(i32::MAX == 2147483647_i32);
static_assert(u128::BITS == 128_u32);
static_assert(i128::MIN + 1_i128 == -i128::MAX);

TEST(IntFromStr, ParsesPrimitiveIntegerBoundaries) {
    EXPECT_EQ(from_str<i8>("-128").unwrap(), i8::MIN);
    EXPECT_EQ(from_str<i8>("+127").unwrap(), i8::MAX);
    EXPECT_EQ(from_str<u8>("255").unwrap(), u8::MAX);
    EXPECT_EQ(from_str<i32>("-0").unwrap(), i32());
    EXPECT_EQ(from_str<i64>("-9223372036854775808").unwrap(), i64::MIN);
    EXPECT_EQ(from_str<u64>("18446744073709551615").unwrap(), u64::MAX);
    EXPECT_EQ(from_str<i128>("-170141183460469231731687303715884105728").unwrap(), i128::MIN);
    EXPECT_EQ(from_str<u128>("340282366920938463463374607431768211455").unwrap(), u128::MAX);
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

TEST(IntTryFrom, ChecksPrimitiveIntegerRanges) {
    EXPECT_EQ(try_from<u16>(u8::MAX).unwrap(), 255_u16);
    EXPECT_EQ(try_into<i16>(u8::MAX).unwrap(), 255_i16);
    EXPECT_EQ(try_from<u8>(u16(255)).unwrap(), 255_u8);

    EXPECT_TRUE(try_from<u8>(i16(-1)).is_err());
    EXPECT_TRUE(try_from<u8>(u16(256)).is_err());
    EXPECT_TRUE(try_from<i8>(i16(-129)).is_err());
    EXPECT_TRUE(try_from<i8>(i16(128)).is_err());
    EXPECT_TRUE(try_from<i64>(u64::MAX).is_err());
    EXPECT_TRUE(try_from<i128>(u128::MAX).is_err());
    EXPECT_EQ(try_from<u8>(NumericCode::Maximum).unwrap(), u8::MAX);
    EXPECT_EQ(try_from<rstd::uint16_t>(u8::MAX).unwrap(), rstd::uint16_t(255));
    EXPECT_EQ(try_from<NumericCode>(u16(255)).unwrap(), NumericCode::Maximum);
    EXPECT_TRUE(try_from<NumericCode>(u16(256)).is_err());
    EXPECT_TRUE(try_from<u8>(rstd::int16_t(-1)).is_err());
    EXPECT_EQ(as_cast<u8>(rstd::int16_t(-1)), u8::MAX);

    auto error = try_from<u8>(u16(256)).unwrap_err();
    EXPECT_EQ(format("{}", error), "out of range integral type conversion attempted");
}

TEST(IntArithmetic, CheckedOverflowingSaturatingAndWrapping) {
    EXPECT_TRUE(u8::MAX.checked_add(1_u8).is_none());
    EXPECT_EQ((10_u8).checked_sub(3_u8), Some(7_u8));
    EXPECT_TRUE(i8::MIN.checked_neg().is_none());
    EXPECT_TRUE(i8::MIN.checked_div(-1_i8).is_none());

    auto [wrapped, overflow] = u8::MAX.overflowing_add(1_u8);
    EXPECT_EQ(wrapped, u8());
    EXPECT_TRUE(overflow);
    EXPECT_EQ(u8::MAX.wrapping_add(1_u8), u8());
    EXPECT_EQ(u8::MAX.saturating_add(1_u8), u8::MAX);
    EXPECT_EQ(i8::MIN.saturating_neg(), i8::MAX);

    EXPECT_EQ((3_u16).checked_pow(4_u32), Some(81_u16));
    EXPECT_TRUE(u8(16).checked_pow(2_u32).is_none());
    EXPECT_EQ(u8(16).wrapping_pow(2_u32), u8());
    EXPECT_EQ(u8(16).saturating_pow(2_u32), u8::MAX);
    EXPECT_EQ((3_u16).pow(4_u32), 81_u16);
}

TEST(IntArithmetic, SignedUnsignedMethodsCoverBoundaries) {
    EXPECT_TRUE(i8::MAX.checked_add_unsigned(1_u8).is_none());
    EXPECT_EQ(i8(-1).checked_add_unsigned(1_u8), Some(i8()));
    EXPECT_EQ(i8::MIN.checked_add_unsigned(u8::MAX), Some(i8::MAX));
    EXPECT_EQ(i8::MIN.saturating_sub_unsigned(1_u8), i8::MIN);

    EXPECT_TRUE(u8().checked_add_signed(-1_i8).is_none());
    EXPECT_EQ((1_u8).checked_add_signed(-1_i8), Some(u8()));
    EXPECT_EQ(u8::MAX.checked_add_signed(i8::MIN), Some(u8(127)));
    EXPECT_EQ(u8::MAX.saturating_add_signed(1_i8), u8::MAX);
    EXPECT_EQ(u8().wrapping_add_signed(-1_i8), u8::MAX);
}

TEST(IntBits, CountsRotatesPowersAndByteOrder) {
    auto value = u16(0x1234);
    EXPECT_EQ(value.count_ones(), u32(5));
    EXPECT_EQ(value.count_zeros(), u32(11));
    EXPECT_EQ(u8(0b11110000).leading_ones(), u32(4));
    EXPECT_EQ(u8(0b00001111).trailing_ones(), u32(4));
    EXPECT_EQ(u8(0b10000001).rotate_left(1_u64), u8(0b00000011));
    EXPECT_EQ(u8(0b10000001).rotate_right(1_u64), u8(0b11000000));
    EXPECT_EQ(u8(0b00010110).reverse_bits(), u8(0b01101000));
    EXPECT_EQ(value.swap_bytes(), u16(0x3412));
    EXPECT_TRUE(u16(1024).is_power_of_two());
    EXPECT_EQ(u16(1025).next_power_of_two(), u16(2048));
    EXPECT_TRUE(u8(129).checked_next_power_of_two().is_none());

    auto bytes = value.to_be_bytes();
    EXPECT_EQ(bytes[usize()], u8(0x12));
    EXPECT_EQ(bytes[usize(1)], u8(0x34));
    EXPECT_EQ(u16::from_be_bytes(bytes), value);
    EXPECT_EQ(u128::from_le_bytes(u128::MAX.to_le_bytes()), u128::MAX);
    EXPECT_EQ(i128::from_ne_bytes(i128::MIN.to_ne_bytes()), i128::MIN);
}

TEST(IntAsCast, UsesRustModuloSemantics) {
    EXPECT_EQ(as_cast<i8>(u8::MAX), i8(-1));
    EXPECT_EQ(as_cast<u8>(i16(-1)), u8::MAX);
    EXPECT_EQ(as_cast<u16>(u32(0x1'0001)), u16(1));
}

TEST(IntArithmeticDeathTest, InvalidDivisionAndRemainderAlwaysPanic) {
    EXPECT_DEATH((void)(i8::MIN / -1_i8), "integer arithmetic with overflow");
    EXPECT_DEATH((void)(i8::MIN % -1_i8), "integer arithmetic with overflow");
    EXPECT_DEATH((void)(1_u8 / u8()), "divide by zero");
}

#if RSTD_TEST_CHECK_INTEGER_OVERFLOW
TEST(IntArithmeticDeathTest, CheckedProfilePanicsOnOrdinaryOverflow) {
    EXPECT_DEATH((void)(u8::MAX + 1_u8), "integer arithmetic with overflow");
    EXPECT_DEATH((void)(u8() - 1_u8), "integer arithmetic with overflow");
    EXPECT_DEATH((void)(i8::MAX * 2_i8), "integer arithmetic with overflow");
    EXPECT_DEATH((void)-i8::MIN, "integer arithmetic with overflow");
    EXPECT_DEATH((void)(1_u8 << 8_u64), "shift with overflow");
    EXPECT_DEATH((void)u8(16).pow(2_u32), "integer arithmetic with overflow");
    EXPECT_DEATH((void)i8::MIN.abs(), "integer arithmetic with overflow");
    EXPECT_DEATH((void)u8(129).next_power_of_two(), "integer arithmetic with overflow");
    EXPECT_DEATH(
        {
            auto value = u8::MAX;
            ++value;
        },
        "integer arithmetic with overflow");
    EXPECT_DEATH(
        {
            auto value = u8();
            --value;
        },
        "integer arithmetic with overflow");
}
#else
TEST(IntArithmetic, UncheckedProfileWrapsOrdinaryOverflow) {
    EXPECT_EQ(u8::MAX + 1_u8, u8());
    EXPECT_EQ(u8() - 1_u8, u8::MAX);
    EXPECT_EQ(i8::MAX * 2_i8, -2_i8);
    EXPECT_EQ(-i8::MIN, i8::MIN);
    EXPECT_EQ(1_u8 << 8_u64, 1_u8);
    EXPECT_EQ(u8(16).pow(2_u32), u8());
    EXPECT_EQ(i8::MIN.abs(), i8::MIN);
    EXPECT_EQ(u8(129).next_power_of_two(), u8());

    auto maximum = u8::MAX;
    EXPECT_EQ(++maximum, u8());
    auto zero = u8();
    EXPECT_EQ(--zero, u8::MAX);
}
#endif
