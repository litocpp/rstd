#include <cmath>
#include <gtest/gtest.h>
import rstd;

TEST(F32Consts, Pi) {
    EXPECT_NEAR(rstd::f32_::consts::PI, 3.14159265f, 1e-6f);
    EXPECT_FLOAT_EQ(rstd::f32_::consts::TAU, 2.0f * rstd::f32_::consts::PI);
    EXPECT_FLOAT_EQ(rstd::f32_::consts::FRAC_PI_2, rstd::f32_::consts::PI / 2.0f);
}

TEST(F32Consts, SqrtAndLog) {
    EXPECT_NEAR(rstd::f32_::consts::SQRT_2 * rstd::f32_::consts::SQRT_2, 2.0f, 1e-6f);
    EXPECT_NEAR(rstd::f32_::consts::E, 2.71828183f, 1e-6f);
    EXPECT_NEAR(rstd::f32_::consts::LN_2 * rstd::f32_::consts::LOG2_E, 1.0f, 1e-6f);
}

TEST(F32Limits, MatchNumericLimits) {
    EXPECT_EQ(rstd::f32_::MAX, rstd::numeric_limits<rstd::f32>::max());
    EXPECT_EQ(rstd::f32_::MIN, rstd::numeric_limits<rstd::f32>::lowest());
    EXPECT_EQ(rstd::f32_::MIN_POSITIVE, rstd::numeric_limits<rstd::f32>::min());
    EXPECT_EQ(rstd::f32_::EPSILON, rstd::numeric_limits<rstd::f32>::epsilon());
    EXPECT_EQ(rstd::f32_::MANTISSA_DIGITS, 24u);
    EXPECT_EQ(rstd::f32_::RADIX, 2u);
}

TEST(F32Limits, InfAndNan) {
    EXPECT_TRUE(std::isinf(rstd::f32_::INFINITY_));
    EXPECT_GT(rstd::f32_::INFINITY_, 0.0f);
    EXPECT_TRUE(std::isinf(rstd::f32_::NEG_INFINITY));
    EXPECT_LT(rstd::f32_::NEG_INFINITY, 0.0f);
    EXPECT_TRUE(std::isnan(rstd::f32_::NAN_));
    EXPECT_NE(rstd::f32_::NAN_, rstd::f32_::NAN_);
}

TEST(F64Consts, Pi) {
    EXPECT_NEAR(rstd::f64_::consts::PI, 3.141592653589793, 1e-15);
    EXPECT_DOUBLE_EQ(rstd::f64_::consts::TAU, 2.0 * rstd::f64_::consts::PI);
}

TEST(F64Limits, MatchNumericLimits) {
    EXPECT_EQ(rstd::f64_::MAX, rstd::numeric_limits<rstd::f64>::max());
    EXPECT_EQ(rstd::f64_::MIN, rstd::numeric_limits<rstd::f64>::lowest());
    EXPECT_EQ(rstd::f64_::EPSILON, rstd::numeric_limits<rstd::f64>::epsilon());
    EXPECT_EQ(rstd::f64_::MANTISSA_DIGITS, 53u);
    EXPECT_EQ(rstd::f64_::MIN_EXP, -1021);
    EXPECT_EQ(rstd::f64_::MAX_EXP, 1024);
}

TEST(F64Limits, InfAndNan) {
    EXPECT_TRUE(std::isinf(rstd::f64_::INFINITY_));
    EXPECT_GT(rstd::f64_::INFINITY_, 0.0);
    EXPECT_TRUE(std::isnan(rstd::f64_::NAN_));
    EXPECT_NE(rstd::f64_::NAN_, rstd::f64_::NAN_);
}

static_assert(rstd::f32_::consts::PI > 3.14f && rstd::f32_::consts::PI < 3.15f);
static_assert(rstd::f64_::consts::PI > 3.14 && rstd::f64_::consts::PI < 3.15);
static_assert(rstd::f32_::MANTISSA_DIGITS == 24);
static_assert(rstd::f64_::MANTISSA_DIGITS == 53);
