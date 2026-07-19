#include <gtest/gtest.h>

import rstd.json;

using namespace rstd::prelude;
using rstd::json::Number;

TEST(JsonNumber, ClassifiesDefaultRepresentations) {
    auto zero = Number::from_u64(u64());
    EXPECT_TRUE(zero.is_i64());
    EXPECT_TRUE(zero.is_u64());
    EXPECT_FALSE(zero.is_f64());
    EXPECT_EQ(zero.as_i64(), Some(i64(0)));
    EXPECT_EQ(zero.as_u64(), Some(u64(0)));

    auto negative = Number::from_i64(i64(-1));
    EXPECT_TRUE(negative.is_i64());
    EXPECT_FALSE(negative.is_u64());
    EXPECT_EQ(negative.as_i64(), Some(i64(-1)));

    auto large = Number::from_u64(rstd::as_cast<u64>(i64::MAX) + u64(1));
    EXPECT_FALSE(large.is_i64());
    EXPECT_TRUE(large.is_u64());
    EXPECT_TRUE(large.as_i64().is_none());

    auto minimum = Number::from_i64(i64::MIN);
    auto maximum = Number::from_i64(i64::MAX);
    EXPECT_EQ(minimum.as_i64(), Some(i64::MIN));
    EXPECT_EQ(maximum.as_i64(), Some(i64::MAX));
}

TEST(JsonNumber, AcceptsOnlyFiniteFloats) {
    auto real = Number::from_f64(f64(1.0));
    ASSERT_TRUE(real.is_some());
    EXPECT_TRUE(real->is_f64());
    EXPECT_EQ(real->as_f64(), Some(f64(1.0)));

    EXPECT_TRUE(Number::from_f64(f64::NAN_).is_none());
    EXPECT_TRUE(Number::from_f64(f64::INFINITY_).is_none());
    EXPECT_TRUE(Number::from_f64(f64::NEG_INFINITY).is_none());

    auto positive_zero = Number::from_f64(f64()).unwrap();
    auto negative_zero = Number::from_f64(f64(-0.0)).unwrap();
    EXPECT_EQ(positive_zero, negative_zero);
}
