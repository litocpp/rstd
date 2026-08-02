#include <cmath>
#include <rstd/test/gtest.hpp>
import rstd;

using namespace rstd::literals;

TEST(F32Consts, Pi) {
    EXPECT_NEAR(rstd::f32::consts::PI.to_primitive(), 3.14159265f, 1e-6f);
    EXPECT_FLOAT_EQ(rstd::f32::consts::TAU.to_primitive(),
                    (2.0_f32 * rstd::f32::consts::PI).to_primitive());
    EXPECT_FLOAT_EQ(rstd::f32::consts::FRAC_PI_2.to_primitive(),
                    (rstd::f32::consts::PI / 2.0_f32).to_primitive());
}

TEST(F32Consts, SqrtAndLog) {
    EXPECT_NEAR(
        (rstd::f32::consts::SQRT_2 * rstd::f32::consts::SQRT_2).to_primitive(), 2.0f, 1e-6f);
    EXPECT_NEAR(rstd::f32::consts::E.to_primitive(), 2.71828183f, 1e-6f);
    EXPECT_NEAR((rstd::f32::consts::LN_2 * rstd::f32::consts::LOG2_E).to_primitive(), 1.0f, 1e-6f);
}

TEST(F32Limits, MatchPrimitiveConstants) {
    EXPECT_EQ(rstd::f32::MAX.to_primitive(), __FLT_MAX__);
    EXPECT_EQ(rstd::f32::MIN.to_primitive(), -__FLT_MAX__);
    EXPECT_EQ(rstd::f32::MIN_POSITIVE.to_primitive(), __FLT_MIN__);
    EXPECT_EQ(rstd::f32::EPSILON.to_primitive(), __FLT_EPSILON__);
    EXPECT_EQ(rstd::f32::MANTISSA_DIGITS, 24_u32);
    EXPECT_EQ(rstd::f32::RADIX, 2_u32);
}

TEST(F32Limits, InfAndNan) {
    EXPECT_TRUE(rstd::f32::INFINITY_.is_infinite());
    EXPECT_GT(rstd::f32::INFINITY_, 0.0_f32);
    EXPECT_TRUE(rstd::f32::NEG_INFINITY.is_infinite());
    EXPECT_LT(rstd::f32::NEG_INFINITY, 0.0_f32);
    EXPECT_TRUE(rstd::f32::NAN_.is_nan());
    EXPECT_NE(rstd::f32::NAN_, rstd::f32::NAN_);
}

TEST(F64Consts, Pi) {
    EXPECT_NEAR(rstd::f64::consts::PI.to_primitive(), 3.141592653589793, 1e-15);
    EXPECT_DOUBLE_EQ(rstd::f64::consts::TAU.to_primitive(),
                     (2.0_f64 * rstd::f64::consts::PI).to_primitive());
}

TEST(F64Limits, MatchPrimitiveConstants) {
    EXPECT_EQ(rstd::f64::MAX.to_primitive(), __DBL_MAX__);
    EXPECT_EQ(rstd::f64::MIN.to_primitive(), -__DBL_MAX__);
    EXPECT_EQ(rstd::f64::EPSILON.to_primitive(), __DBL_EPSILON__);
    EXPECT_EQ(rstd::f64::MANTISSA_DIGITS, 53_u32);
    EXPECT_EQ(rstd::f64::MIN_EXP, rstd::i32(-1021));
    EXPECT_EQ(rstd::f64::MAX_EXP, rstd::i32(1024));
}

TEST(F64Limits, InfAndNan) {
    EXPECT_TRUE(rstd::f64::INFINITY_.is_infinite());
    EXPECT_GT(rstd::f64::INFINITY_, 0.0_f64);
    EXPECT_TRUE(rstd::f64::NAN_.is_nan());
    EXPECT_NE(rstd::f64::NAN_, rstd::f64::NAN_);
}

TEST(FloatMethods, ClassificationBitsAndOrdering) {
    EXPECT_EQ(rstd::f32().classify(), rstd::num::FpCategory::Zero);
    EXPECT_EQ(rstd::f32::MIN_POSITIVE.classify(), rstd::num::FpCategory::Normal);
    EXPECT_TRUE((-0.0_f32).is_sign_negative());
    EXPECT_TRUE(rstd::f32::from_bits(1_u32).is_subnormal());
    EXPECT_EQ(rstd::f32::from_bits(0x3f800000_u32), 1.0_f32);
    EXPECT_EQ(rstd::f64::from_bits(rstd::f64::consts::PI.to_bits()), rstd::f64::consts::PI);
    EXPECT_EQ((-0.0_f64).total_cmp(0.0_f64), std::strong_ordering::less);

    auto bytes = (1.0_f32).to_be_bytes();
    EXPECT_EQ(rstd::f32::from_be_bytes(bytes), 1.0_f32);
}

TEST(FloatMethods, RoundingMathAndClamp) {
    EXPECT_EQ((-2.5_f64).abs(), 2.5_f64);
    EXPECT_EQ((2.75_f64).floor(), 2.0_f64);
    EXPECT_EQ((2.25_f64).ceil(), 3.0_f64);
    EXPECT_EQ((2.75_f64).trunc(), 2.0_f64);
    EXPECT_EQ((2.75_f64).fract(), 0.75_f64);
    EXPECT_EQ((9.0_f64).sqrt(), 3.0_f64);
    EXPECT_EQ((2.0_f64).powi(rstd::i32(-3)), 0.125_f64);
    EXPECT_EQ((3.0_f64).clamp(1.0_f64, 2.0_f64), 2.0_f64);
}

TEST(FloatMethods, TrigonometryRemainderAndAdjacentValues) {
    EXPECT_NEAR(rstd::f32::consts::FRAC_PI_2.sin().to_primitive(), 1.0f, 1e-6f);
    EXPECT_NEAR(rstd::f64::consts::PI.cos().to_primitive(), -1.0, 1e-15);
    EXPECT_NEAR((1.0_f64).atan2(1.0_f64).to_primitive(),
                rstd::f64::consts::FRAC_PI_4.to_primitive(),
                1e-15);
    EXPECT_EQ(5.5_f32 % 2.0_f32, 1.5_f32);
    EXPECT_EQ((-1.5_f32).rem_euclid(1.0_f32), 0.5_f32);
    EXPECT_EQ((0.0_f32).next_up().to_bits(), 1_u32);
    EXPECT_EQ((0.0_f32).next_down().to_bits(), 0x80000001_u32);
    EXPECT_EQ(rstd::f32::MAX.next_up(), rstd::f32::INFINITY_);
    EXPECT_EQ(rstd::f32::INFINITY_.next_up(), rstd::f32::INFINITY_);
    EXPECT_TRUE(rstd::f32::NAN_.next_down().is_nan());
}

TEST(FloatFromStr, ParsesDecimalSpecialAndBoundaryValues) {
    EXPECT_EQ(rstd::from_str<rstd::f32>("1.25"_str).unwrap(), 1.25_f32);
    EXPECT_EQ(rstd::from_str<rstd::f64>("-.5e2"_str).unwrap(), -50.0_f64);
    EXPECT_TRUE(rstd::from_str<rstd::f32>("inf"_str).unwrap().is_infinite());
    EXPECT_TRUE(rstd::from_str<rstd::f64>("-infinity"_str).unwrap().is_sign_negative());
    EXPECT_TRUE(rstd::from_str<rstd::f32>("NaN"_str).unwrap().is_nan());
    EXPECT_EQ(rstd::from_str<rstd::f64>("5e-324"_str).unwrap().to_bits(), 1_u64);
}

TEST(FloatFromStr, ReportsEmptyInvalidAndOverflow) {
    auto empty = rstd::from_str<rstd::f32>(""_str);
    ASSERT_TRUE(empty.is_err());
    EXPECT_EQ(empty.unwrap_err().kind(), rstd::num::FloatErrorKind::Empty);

    auto invalid = rstd::from_str<rstd::f64>("1.2x"_str);
    ASSERT_TRUE(invalid.is_err());
    EXPECT_EQ(invalid.unwrap_err().kind(), rstd::num::FloatErrorKind::Invalid);

    auto positive = rstd::from_str<rstd::f32>("1e100"_str);
    ASSERT_TRUE(positive.is_err());
    EXPECT_EQ(positive.unwrap_err().kind(), rstd::num::FloatErrorKind::PosOverflow);

    auto negative = rstd::from_str<rstd::f64>("-1e1000"_str);
    ASSERT_TRUE(negative.is_err());
    EXPECT_EQ(negative.unwrap_err().kind(), rstd::num::FloatErrorKind::NegOverflow);
}

TEST(FloatConversion, CheckedAndLossyBoundaries) {
    EXPECT_EQ(rstd::try_from<rstd::u8>(42.0_f64).unwrap(), 42_u8);
    EXPECT_TRUE(rstd::try_from<rstd::u8>(42.5_f64).is_err());
    EXPECT_TRUE(rstd::try_from<rstd::u8>(-1.0_f64).is_err());
    EXPECT_TRUE(rstd::try_from<rstd::u8>(rstd::f64::NAN_).is_err());
    EXPECT_TRUE(rstd::try_from<rstd::u8>(rstd::f64::INFINITY_).is_err());

    EXPECT_EQ(rstd::as_cast<rstd::u8>(42.75_f64), 42_u8);
    EXPECT_EQ(rstd::as_cast<rstd::u8>(-1.0_f64), rstd::u8());
    EXPECT_EQ(rstd::as_cast<rstd::u8>(rstd::f64::NAN_), rstd::u8());
    EXPECT_EQ(rstd::as_cast<rstd::u8>(rstd::f64::INFINITY_), rstd::u8::MAX);

    EXPECT_EQ(rstd::try_from<rstd::f32>(rstd::u32(16'777'216)).unwrap(), 16'777'216.0_f32);
    EXPECT_TRUE(rstd::try_from<rstd::f32>(rstd::u32(16'777'217)).is_err());
    EXPECT_EQ(rstd::try_from<rstd::f32>(1.5_f64).unwrap(), 1.5_f32);
    EXPECT_TRUE(rstd::try_from<rstd::f32>(0.1_f64).is_err());
}

TEST(FloatMethodsDeathTest, InvalidClampPanics) {
    EXPECT_DEATH((void)(1.0_f32).clamp(2.0_f32, 1.0_f32), "min > max");
    EXPECT_DEATH((void)(1.0_f32).clamp(rstd::f32::NAN_, 1.0_f32), "either was NaN");
}

static_assert(rstd::f32::consts::PI > 3.14_f32 && rstd::f32::consts::PI < 3.15_f32);
static_assert(rstd::f64::consts::PI > 3.14_f64 && rstd::f64::consts::PI < 3.15_f64);
static_assert(rstd::f32::MANTISSA_DIGITS == 24_u32);
static_assert(rstd::f64::MANTISSA_DIGITS == 53_u32);
