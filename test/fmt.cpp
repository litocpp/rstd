#include <gtest/gtest.h>
import rstd;
import rstd.core;

using namespace rstd;

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
