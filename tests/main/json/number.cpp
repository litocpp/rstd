#include <rstd/test/gtest.hpp>

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

TEST(JsonNumber, NumericComparisonPreservesLargeIntegers) {
    auto rounded = Number::from_f64(f64(0x1p53)).unwrap();
    auto above   = Number::from_u64(u64(9007199254740993ULL));
    EXPECT_TRUE(above.numeric_cmp(rounded) > 0);
    EXPECT_TRUE(rounded.numeric_cmp(above) < 0);
    auto signed_max   = Number::from_i64(i64::MAX);
    auto signed_limit = Number::from_f64(f64(0x1p63)).unwrap();
    EXPECT_TRUE(signed_max.numeric_cmp(signed_limit) < 0);
    auto unsigned_max   = Number::from_u64(u64::MAX);
    auto unsigned_limit = Number::from_f64(f64(0x1p64)).unwrap();
    EXPECT_TRUE(unsigned_max.numeric_cmp(unsigned_limit) < 0);
    auto signed_min = Number::from_i64(i64::MIN);
    EXPECT_TRUE(signed_min.numeric_cmp(Number::from_f64(f64(-0x1p63)).unwrap()) == 0);
    EXPECT_TRUE(
        Number::from_i64(i64::MIN + i64(1)).numeric_cmp(Number::from_f64(f64(-0x1p63)).unwrap()) >
        0);
    EXPECT_TRUE(signed_min.numeric_cmp(unsigned_max) < 0);
}

TEST(JsonNumber, NumericComparisonHandlesFractionsAndZero) {
    auto zero     = Number::from_u64(u64());
    auto positive = Number::from_f64(f64(0.5)).unwrap();
    auto negative = Number::from_f64(f64(-0.5)).unwrap();
    EXPECT_TRUE(zero.numeric_cmp(positive) < 0);
    EXPECT_TRUE(zero.numeric_cmp(negative) > 0);
    EXPECT_TRUE(Number::from_i64(i64(-1)).numeric_cmp(Number::from_f64(f64(-1.5)).unwrap()) > 0);
    EXPECT_TRUE(Number::from_u64(u64(1)).numeric_cmp(Number::from_f64(f64(1.5)).unwrap()) < 0);
    auto float_zero = Number::from_f64(f64(-0.0)).unwrap();
    EXPECT_TRUE(zero.numeric_cmp(float_zero) == 0);
    EXPECT_TRUE(! (zero == float_zero));
    EXPECT_TRUE(zero.numeric_cmp(Number::from_f64(f64(0x1p-1074)).unwrap()) < 0);
    EXPECT_TRUE(zero.numeric_cmp(Number::from_f64(f64(-0x1p-1074)).unwrap()) > 0);
    EXPECT_TRUE(Number::from_i64(i64::MIN).numeric_cmp(Number::from_f64(f64(-0x1p100)).unwrap()) >
                0);
    EXPECT_TRUE(Number::from_u64(u64::MAX).numeric_cmp(Number::from_f64(f64(0x1p100)).unwrap()) <
                0);
}

TEST(JsonNumber, NumericComparisonOrdersMixedRepresentations) {
    rstd::array<Number, 11> ordered { Number::from_f64(f64(-0x1p100)).unwrap(),
                                      Number::from_i64(i64::MIN),
                                      Number::from_i64(i64(-2)),
                                      Number::from_f64(f64(-1.5)).unwrap(),
                                      Number::from_i64(i64(-1)),
                                      Number::from_u64(u64()),
                                      Number::from_f64(f64(0.5)).unwrap(),
                                      Number::from_u64(u64(1)),
                                      Number::from_u64(u64(9007199254740993ULL)),
                                      Number::from_u64(u64::MAX),
                                      Number::from_f64(f64(0x1p100)).unwrap() };
    for (usize i {}; i < usize(11); ++i)
        for (usize j {}; j < usize(11); ++j) {
            auto compared = ordered[i].numeric_cmp(ordered[j]);
            EXPECT_TRUE((compared < 0) == (i < j));
            EXPECT_TRUE((compared == 0) == (i == j));
            EXPECT_TRUE((compared > 0) == (i > j));
        }
}
