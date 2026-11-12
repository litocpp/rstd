#include <rstd/test/gtest.hpp>
import rstd.core;
import rstd.alloc;
using namespace rstd::prelude;
using namespace rstd::literals;

TEST(FmtArguments, AutomaticArgumentsKeepEscapesAndSpecs) {
    EXPECT_EQ(rstd::format("{{}} {} {:03} {}", "jobs"_str, i32(7), i32(9)), "{} jobs 007 9"_str);
}

TEST(FmtArguments, IndexedArgumentsCanRepeatAndReorder) {
    EXPECT_EQ(rstd::format("{{{1}}}: {0:03} / {1}", i32(7), "jobs"_str), "{jobs}: 007 / jobs"_str);
    EXPECT_EQ(rstd::format("{1:03} {0:>4}", i32(8), i32(9)), "009    8"_str);
}

TEST(FmtArguments, ExplicitIndicesDoNotAdvanceAutomaticArguments) {
    EXPECT_EQ(rstd::format("{} {1} {} {0}", i32(10), i32(20)), "10 20 20 10"_str);
}

TEST(FmtArguments, DynamicArgumentsRejectInvalidIndices) {
    i32  value(4);
    auto source = rstd::fmt::Arguments::make("{}", value);
    auto check  = [&]<rstd::size_t N>(const char (&format)[N]) {
        alloc::string::String output;
        rstd::fmt::Formatter  formatter(output);
        auto                  arguments = static_cast<rstd::fmt::Arguments>(source);
        arguments.fmt_ptr               = format;
        arguments.fmt_len               = N - 1;
        EXPECT_FALSE(formatter.write_fmt(arguments));
    };
    check("{1}");
    check("{999999999999999999999999999999999999999}");
    check("{name}");
    check("{} {}");
}

TEST(FmtArguments, StringDebugEscapesControlsAndKeepsUtf8) {
    EXPECT_EQ(rstd::format("{:?}", rstd::ref<rstd::str> {}), "\"\""_str);
    auto text = "a\n\r\t\0\"\\\x01\x7f\xc3\xa9"_str;
    EXPECT_EQ(rstd::format("{:?}", text), "\"a\\n\\r\\t\\0\\\"\\\\\\u{1}\\u{7f}\xc3\xa9\""_str);
    auto owned = alloc::string::String::make(text);
    EXPECT_EQ(rstd::format("{:?}", owned), rstd::format("{:?}", text));
    EXPECT_EQ(rstd::format("{}", text), text);
}
