#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd;

static_assert(Impled<f32, fmt::Display>);
static_assert(Impled<f32, fmt::Debug>);
static_assert(Impled<f64, fmt::LowerExp>);
static_assert(Impled<f64, fmt::UpperExp>);

TEST(Fmt, Basic) {
    auto s = rstd::format("Hello, {}!", "world");
    EXPECT_EQ(s, "Hello, world!");
}

TEST(Fmt, MultipleArgs) {
    auto s = rstd::format("{} + {} = {}", 1, 2, 3);
    EXPECT_EQ(s, "1 + 2 = 3");
}

TEST(Fmt, Escaping) {
    auto s = rstd::format("{{ Hello {} }}", "world");
    EXPECT_EQ(s, "{ Hello world }");
}

TEST(Fmt, IntegerTypes) {
    auto s = rstd::format("Values: {}, {}, {}, {}", (i32)-42, (u64)123456789, (int)0, (i8)-1);
    EXPECT_EQ(s, "Values: -42, 123456789, 0, -1");
}

TEST(Fmt, FloatDisplayAndDebug) {
    EXPECT_EQ(rstd::format("{} {} {}", 1.0, 1e20, 1e-7), "1 100000000000000000000 0.0000001");
    EXPECT_EQ(rstd::format("{:?} {:?} {:?} {:?}", 1.0, 1e16, 1e-4, 1e-5), "1.0 1e16 0.0001 1e-5");
    EXPECT_EQ(rstd::format("{}", 1.2345678f), "1.2345678");
}

TEST(Fmt, FloatPrecisionAndExponent) {
    EXPECT_EQ(rstd::format("{:.3}", 1.25), "1.250");
    EXPECT_EQ(rstd::format("{:.3?}", 1.25), "1.250");
    EXPECT_EQ(rstd::format("{:e}", 42.0), "4.2e1");
    EXPECT_EQ(rstd::format("{:.3e}", 42.0), "4.200e1");
    EXPECT_EQ(rstd::format("{:E}", 0.0042), "4.2E-3");
    EXPECT_EQ(rstd::format("{:#}", 1.0), "1");
}

TEST(Fmt, FloatSpecialValuesAndSigns) {
    EXPECT_EQ(rstd::format("{} {:+} {}", f64_::NAN_, f64_::NAN_, -f64_::NAN_), "NaN NaN NaN");
    EXPECT_EQ(rstd::format("{} {:+} {}", f64_::INFINITY_, f64_::INFINITY_, f64_::NEG_INFINITY),
              "inf +inf -inf");
    EXPECT_EQ(rstd::format("{} {:?} {:e}", -0.0, -0.0, -0.0), "-0 -0.0 -0e0");
}

TEST(Fmt, FloatPadding) {
    EXPECT_EQ(rstd::format("{:8.2}", -1.5), "   -1.50");
    EXPECT_EQ(rstd::format("{:08.2}", -1.5), "-0001.50");
    EXPECT_EQ(rstd::format("{:+08.2}", 1.5), "+0001.50");
    EXPECT_EQ(rstd::format("{:0>8.2}", -1.5), "000-1.50");
    EXPECT_EQ(rstd::format("{:<08.2}", -1.5), "-0001.50");
    EXPECT_EQ(rstd::format("{:_^9.2}", 1.5), "__1.50___");
}

TEST(Fmt, FloatLargePrecision) {
    auto formatted = rstd::format("{:.1200}", 1.0);
    ASSERT_EQ(formatted.size(), 1202u);
    EXPECT_EQ(formatted.as_raw_ptr()[0], '1');
    EXPECT_EQ(formatted.as_raw_ptr()[1], '.');
    EXPECT_EQ(formatted.as_raw_ptr()[formatted.size() - 1], '0');

    auto scientific = rstd::format("{:.1200e}", 1.0);
    ASSERT_EQ(scientific.size(), 1204u);
    EXPECT_EQ(scientific.as_raw_ptr()[scientific.size() - 2], 'e');
    EXPECT_EQ(scientific.as_raw_ptr()[scientific.size() - 1], '0');
}

TEST(Fmt, FloatBoundariesAndRounding) {
    const auto min_subnormal = rstd::bit_cast<f64>(u64(1));
    auto       fixed         = rstd::format("{}", min_subnormal);
    ASSERT_EQ(fixed.size(), 326u);
    EXPECT_EQ(fixed.as_raw_ptr()[0], '0');
    EXPECT_EQ(fixed.as_raw_ptr()[1], '.');
    EXPECT_EQ(fixed.as_raw_ptr()[fixed.size() - 1], '5');

    EXPECT_EQ(rstd::format("{:?}", min_subnormal), "5e-324");
    EXPECT_EQ(rstd::format("{:?}", f64_::MIN_POSITIVE), "2.2250738585072014e-308");
    EXPECT_EQ(rstd::format("{:?}", f64_::MAX), "1.7976931348623157e308");
    EXPECT_EQ(rstd::format("{:?}", rstd::bit_cast<f32>(u32(1))), "1e-45");
    EXPECT_EQ(rstd::format("{:?}", f32_::MAX), "3.4028235e38");

    EXPECT_EQ(rstd::format("{:.0} {:.0} {:.0} {:.0}", 0.5, 1.5, 2.5, 3.5), "0 2 2 4");
    EXPECT_EQ(rstd::format("{:.0} {:.0}", 9.5, 99.5), "10 100");
    EXPECT_EQ(rstd::format("{:.3e}", 0.0), "0.000e0");
    EXPECT_EQ(rstd::format("{:.900}", min_subnormal).size(), 902u);
    EXPECT_EQ(rstd::format("{:.900}", f64_::MIN_POSITIVE).size(), 902u);
    EXPECT_EQ(rstd::format("{:.900e}", min_subnormal).size(), 907u);
}

TEST(Fmt, Duration) {
    auto d = time::Duration::from_millis(1500);
    auto s = rstd::format("Time: {:?}", d);
    EXPECT_EQ(s, "Time: 1.5s");
}

TEST(Fmt, DurationMs) {
    auto d = time::Duration::from_millis(500);
    auto s = rstd::format("{:?}", d);
    EXPECT_EQ(s, "500ms");
}

TEST(Fmt, DurationNs) {
    auto d = time::Duration::from_nanos(789);
    auto s = rstd::format("{:?}", d);
    EXPECT_EQ(s, "789ns");
}
