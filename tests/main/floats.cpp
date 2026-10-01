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

TEST(FloatMethods, TangentAndInverseTrigonometry) {
    EXPECT_NEAR((0.5_f32).tan().to_primitive(), std::tan(0.5f), 1e-7f);
    EXPECT_NEAR((0.5_f64).tan().to_primitive(), std::tan(0.5), 1e-15);
    EXPECT_NEAR((0.5_f32).asin().to_primitive(), std::asin(0.5f), 1e-7f);
    EXPECT_NEAR((0.5_f64).asin().to_primitive(), std::asin(0.5), 1e-15);
    EXPECT_NEAR((0.5_f32).atan().to_primitive(), std::atan(0.5f), 1e-7f);
    EXPECT_NEAR((0.5_f64).atan().to_primitive(), std::atan(0.5), 1e-15);
    EXPECT_TRUE((-0.0_f32).tan().is_sign_negative());
    EXPECT_TRUE((-0.0_f64).asin().is_sign_negative());
    EXPECT_TRUE((-0.0_f64).atan().is_sign_negative());
    EXPECT_TRUE((2.0_f32).asin().is_nan());
    EXPECT_TRUE((2.0_f64).asin().is_nan());
    EXPECT_TRUE(rstd::f32::NAN_.atan().is_nan());
    EXPECT_TRUE(rstd::f64::INFINITY_.tan().is_nan());
    EXPECT_NEAR(rstd::f64::INFINITY_.atan().to_primitive(),
                rstd::f64::consts::FRAC_PI_2.to_primitive(),
                1e-15);
}

static_assert((0.0_f32).copysign(-0.0_f32).to_bits() == 0x80000000_u32);
static_assert((-0.0_f64).copysign(0.0_f64).to_bits() == 0_u64);
static_assert(rstd::f32::from_bits(0x7fc01234_u32).copysign(-0.0_f32).to_bits() == 0xffc01234_u32);
static_assert(rstd::f64::from_bits(0x7ff8000000001234_u64).copysign(-0.0_f64).to_bits() ==
              0xfff8000000001234_u64);

template<typename T>
void check_float_copysign() {
    using Bits                  = typename T::Bits;
    using RawBits               = typename Bits::primitive_type;
    constexpr auto sign_mask    = RawBits(1) << (sizeof(RawBits) * 8 - 1);
    constexpr auto nan_bits     = sizeof(RawBits) == 4 ? 0x7fc01234ull : 0x7ff8000000001234ull;
    auto           nan          = T::from_bits(Bits(static_cast<RawBits>(nan_bits)));
    auto           negative_nan = T::from_bits(Bits(static_cast<RawBits>(nan_bits) | sign_mask));

    EXPECT_EQ(T(3.5).copysign(T(-1)), T(-3.5));
    EXPECT_EQ(T(-3.5).copysign(T(1)), T(3.5));
    EXPECT_EQ(T(0).copysign(T(-0.0)).to_bits(), Bits(sign_mask));
    EXPECT_EQ(T(-0.0).copysign(T(0)).to_bits(), Bits(0));
    EXPECT_EQ(T::INFINITY_.copysign(T(-0.0)), T::NEG_INFINITY);
    EXPECT_EQ(T::NEG_INFINITY.copysign(T(0)), T::INFINITY_);
    EXPECT_EQ(nan.copysign(T(-1)).to_bits(), negative_nan.to_bits());
    EXPECT_EQ(negative_nan.copysign(T(1)).to_bits(), nan.to_bits());
    EXPECT_EQ(T(3.5).copysign(negative_nan), T(-3.5));
    EXPECT_EQ(T(-3.5).copysign(nan), T(3.5));
}

TEST(FloatMethods, CopySignPreservesMagnitudeAndPayload) {
    check_float_copysign<rstd::f32>();
    check_float_copysign<rstd::f64>();
}

template<typename T>
void check_float_math_values() {
    constexpr double tolerance = sizeof(typename T::primitive_type) == 4 ? 1e-6 : 1e-14;
    EXPECT_NEAR(T(0.5).acos().to_primitive(), 1.0471975511965977, tolerance);
    EXPECT_NEAR(T(-8).cbrt().to_primitive(), -2.0, tolerance);
    EXPECT_NEAR(T(1).sinh().to_primitive(), 1.1752011936438014, tolerance);
    EXPECT_NEAR(T(1).cosh().to_primitive(), 1.5430806348152437, tolerance);
    EXPECT_NEAR(T(1).tanh().to_primitive(), 0.7615941559557649, tolerance);
    EXPECT_NEAR(T(2).asinh().to_primitive(), 1.4436354751788103, tolerance);
    EXPECT_NEAR(T(2).acosh().to_primitive(), 1.3169578969248167, tolerance);
    EXPECT_NEAR(T(0.5).atanh().to_primitive(), 0.5493061443340548, tolerance);
    EXPECT_NEAR(T(1).exp_m1().to_primitive(), 1.7182818284590452, tolerance);
    EXPECT_NEAR(T(1).ln_1p().to_primitive(), 0.6931471805599453, tolerance);

    auto tiny = T(sizeof(typename T::primitive_type) == 4 ? 1e-8 : 1e-20);
    EXPECT_NEAR((tiny.exp_m1() / tiny).to_primitive(), 1.0, tolerance);
    EXPECT_NEAR((tiny.ln_1p() / tiny).to_primitive(), 1.0, tolerance);
    EXPECT_NEAR(((-tiny).exp_m1() / -tiny).to_primitive(), 1.0, tolerance);
    EXPECT_NEAR(((-tiny).ln_1p() / -tiny).to_primitive(), 1.0, tolerance);
    EXPECT_NEAR((tiny.asinh() / tiny).to_primitive(), 1.0, tolerance);
    EXPECT_NEAR(T(60).sinh().asinh().to_primitive(), 60.0, tolerance * 60);
    EXPECT_NEAR(T(60).cosh().acosh().to_primitive(), 60.0, tolerance * 60);
}

TEST(FloatMethods, RootsHyperbolicAndSmallArguments) {
    check_float_math_values<rstd::f32>();
    check_float_math_values<rstd::f64>();
}

template<typename T>
void check_float_math_boundaries() {
    auto zero              = T(0);
    auto negative_zero     = T(-0.0);
    auto infinity          = T::INFINITY_;
    auto negative_infinity = T::NEG_INFINITY;
    auto nan               = T::NAN_;

    constexpr T (T::* zero_preserving_functions[])() const noexcept = {
        &T::cbrt, &T::sinh, &T::tanh, &T::asinh, &T::atanh, &T::exp_m1, &T::ln_1p,
    };
    for (auto function : zero_preserving_functions) {
        EXPECT_EQ((zero.*function)().to_bits(), zero.to_bits());
        EXPECT_EQ((negative_zero.*function)().to_bits(), negative_zero.to_bits());
    }
    EXPECT_EQ(T(1).acos(), zero);
    EXPECT_EQ(T(1).acosh(), zero);
    EXPECT_EQ(zero.cosh(), T(1));
    EXPECT_EQ(infinity.cbrt(), infinity);
    EXPECT_EQ(negative_infinity.cbrt(), negative_infinity);
    EXPECT_EQ(infinity.sinh(), infinity);
    EXPECT_EQ(negative_infinity.sinh(), negative_infinity);
    EXPECT_EQ(infinity.cosh(), infinity);
    EXPECT_EQ(negative_infinity.cosh(), infinity);
    EXPECT_EQ(infinity.tanh(), T(1));
    EXPECT_EQ(negative_infinity.tanh(), T(-1));
    EXPECT_EQ(infinity.asinh(), infinity);
    EXPECT_EQ(negative_infinity.asinh(), negative_infinity);
    EXPECT_EQ(infinity.acosh(), infinity);
    EXPECT_EQ(T(1).atanh(), infinity);
    EXPECT_EQ(T(-1).atanh(), negative_infinity);
    EXPECT_EQ(infinity.exp_m1(), infinity);
    EXPECT_EQ(negative_infinity.exp_m1(), T(-1));
    EXPECT_EQ(infinity.ln_1p(), infinity);
    EXPECT_EQ(T(-1).ln_1p(), negative_infinity);
    EXPECT_TRUE(T(2).acos().is_nan());
    EXPECT_TRUE(T(-2).acos().is_nan());
    EXPECT_TRUE(T(0.5).acosh().is_nan());
    EXPECT_TRUE(negative_infinity.acosh().is_nan());
    EXPECT_TRUE(T(2).atanh().is_nan());
    EXPECT_TRUE(T(-2).atanh().is_nan());
    EXPECT_TRUE(infinity.atanh().is_nan());
    EXPECT_TRUE(T(-2).ln_1p().is_nan());

    constexpr T (T::* functions[])() const noexcept = {
        &T::acos,  &T::cbrt,  &T::sinh,  &T::cosh,   &T::tanh,
        &T::asinh, &T::acosh, &T::atanh, &T::exp_m1, &T::ln_1p,
    };
    for (auto function : functions) {
        EXPECT_TRUE((nan.*function)().is_nan());
    }
}

TEST(FloatMethods, MathDomainsAndSpecialValues) {
    check_float_math_boundaries<rstd::f32>();
    check_float_math_boundaries<rstd::f64>();
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
