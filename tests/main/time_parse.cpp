#include <rstd/test/gtest.hpp>
import rstd;
using namespace rstd::prelude;
using namespace rstd::literals;
using namespace rstd::time;

TEST(DateTimeParse, ValidatesCalendarAndOffsetAndKeepsOwnedResults) {
    auto parsed = [] {
        auto text = "1970-01-01T01:30:00.123456789+01:30"_Str;
        return parse_rfc3339(text.as_str()).unwrap();
    }();
    EXPECT_EQ(parsed.base_time.seconds, i64());
    EXPECT_EQ(parsed.base_time.nanoseconds, u32(123456789));
    EXPECT_FALSE(parsed.leap_second);
    EXPECT_EQ(parsed.unix_milliseconds().unwrap(), i64(123));
    EXPECT_EQ(format_rfc3339(parsed).unwrap(), "1970-01-01T00:00:00.123456789+00:00"_str);
    EXPECT_EQ(parse_rfc3339("2000-02-29t00:00:00z"_str)->unix_milliseconds().unwrap(),
              i64(951782400000LL));
    EXPECT_EQ(parse_rfc3339("1970-01-01 00:00:00\xe2\x88\x92"
                            "01:00"_str)
                  ->base_time.seconds,
              i64(3600));
    EXPECT_EQ(parse_rfc3339("1970-01-01T00:00:00.123456789999Z"_str)->base_time.nanoseconds,
              u32(123456789));
    for (auto text : { "1900-02-29T00:00:00Z"_str,
                       "2000-04-31T00:00:00Z"_str,
                       "1970-00-01T00:00:00Z"_str,
                       "1970-01-00T00:00:00Z"_str,
                       "1970-01-01T24:00:00Z"_str,
                       "1970-01-01T00:60:00Z"_str,
                       "1970-01-01T00:00:61Z"_str,
                       "1970-01-01T00:00:00+24:00"_str,
                       "1970-01-01T00:00:00+00:60"_str })
        EXPECT_TRUE(parse_rfc3339(text).is_err());
}

TEST(DateTimeParse, ReportsSyntaxOffsetsAndHonorsExplicitGrammarOptions) {
    auto error = parse_rfc3339("1970/01-01T00:00:00Z"_str).unwrap_err();
    EXPECT_EQ(error.kind, DateTimeParseErrorKind::Syntax);
    EXPECT_EQ(error.offset, usize(5));
    for (auto text : { ""_str,
                       "1970-01-01"_str,
                       "1970-01-01T00:00Z"_str,
                       "1970-01-01T00:00:00.Z"_str,
                       "1970-01-01T00:00:00+0100"_str,
                       "1970-01-01T00:00:00Z "_str,
                       " 1970-01-01T00:00:00Z"_str,
                       "+10000-01-01T00:00:00Z"_str,
                       "1970-1-01T00:00:00Z"_str })
        EXPECT_TRUE(parse_rfc3339(text).is_err());
    Rfc3339Options options { .allow_space_separator = false,
                             .allow_unicode_minus   = false,
                             .max_fractional_digits = usize(9) };
    EXPECT_TRUE(parse_rfc3339("1970-01-01 00:00:00Z"_str, options).is_err());
    EXPECT_TRUE(parse_rfc3339("1970-01-01T00:00:00\xe2\x88\x92"
                              "01:00"_str,
                              options)
                    .is_err());
    EXPECT_TRUE(parse_rfc3339("1970-01-01T00:00:00.1234567890Z"_str, options).is_err());
    EXPECT_TRUE(parse_rfc3339("1970-01-01t00:00:00.123456789z"_str, options).is_ok());
}

TEST(DateTimeParse, KeepsLeapSecondsSeparateFromNormalizedTime) {
    auto parsed = parse_rfc3339("2016-12-31T23:59:60.123456789Z"_str).unwrap();
    EXPECT_TRUE(parsed.leap_second);
    EXPECT_EQ(parsed.base_time.seconds, i64(1483228799));
    EXPECT_EQ(parsed.base_time.nanoseconds, u32(123456789));
    EXPECT_EQ(parsed.unix_milliseconds().unwrap(), i64(1483228800123LL));
    EXPECT_EQ(format_rfc3339(parsed, true).unwrap(), "2016-12-31T23:59:60.123456789Z"_str);
    auto negative = parse_rfc3339("1969-12-31T23:59:60.123Z"_str).unwrap();
    EXPECT_EQ(negative.unix_milliseconds().unwrap(), i64(123));
    EXPECT_EQ(format_rfc3339(negative, true).unwrap(), "1969-12-31T23:59:60.123Z"_str);
    EXPECT_TRUE(Time::from_hms_nano(u8(23), u8(59), u8(60), u32()).is_err());
    EXPECT_TRUE(SystemTime::from_unix_time(i64(), u32(1000000000)).is_none());
    EXPECT_TRUE(format_rfc3339({ { i64(), u32() }, true }).is_err());
}

TEST(DateTimeParse, FormatsWideYearsAndAutomaticFractionPrecision) {
    for (auto year : { i32::MIN,
                       i32(-262143),
                       i32(-10000),
                       i32(-1),
                       i32(),
                       i32(9999),
                       i32(10000),
                       i32(262142),
                       i32::MAX }) {
        auto parsed =
            make_parsed_datetime({ year, Month::January, u8(1) }, u8(), u8(), u8(), u32()).unwrap();
        auto text          = format_rfc3339(parsed, true).unwrap();
        auto absolute      = year < i32() ? -i64(year.to_primitive()) : i64(year.to_primitive());
        auto prefix        = year < i32() ? "-"_str : year > i32(9999) ? "+"_str : ""_str;
        auto expected_year = rstd::format("{}{:04}", prefix, absolute);
        EXPECT_TRUE(text.as_str().starts_with(expected_year.as_str()));
        EXPECT_TRUE(text.as_str().ends_with("-01-01T00:00:00Z"_str));
    }
    for (auto nanos : { u32(1), u32(1000), u32(1000000), u32(123456789) }) {
        auto text   = format_rfc3339({ { i64(-1), nanos }, false }, true).unwrap();
        auto parsed = parse_rfc3339(text.as_str()).unwrap();
        EXPECT_EQ(parsed.base_time.seconds, i64(-1));
        EXPECT_EQ(parsed.base_time.nanoseconds, nanos);
    }
    EXPECT_TRUE(
        make_parsed_datetime({ i32(2001), Month::February, u8(29) }, u8(), u8(), u8(), u32())
            .is_err());
    EXPECT_TRUE(format_rfc3339({ { i64::MAX, u32() }, false }).is_err());
    EXPECT_TRUE(format_rfc3339({ { i64(), u32(1000000000) }, false }).is_err());
}

TEST(DateTimeParse, ChecksMillisecondOverflowWithoutLosingNegativeBoundaryValues) {
    ParsedDateTime minimum { { i64(-9223372036854776LL), u32(192000000) }, false };
    ParsedDateTime maximum { { i64(9223372036854775LL), u32(807000000) }, false };
    EXPECT_EQ(minimum.unix_milliseconds().unwrap(), i64::MIN);
    EXPECT_EQ(maximum.unix_milliseconds().unwrap(), i64::MAX);
    minimum.base_time.nanoseconds = u32(191999999);
    maximum.base_time.nanoseconds = u32(808000000);
    EXPECT_TRUE(minimum.unix_milliseconds().is_err());
    EXPECT_TRUE(maximum.unix_milliseconds().is_err());
}
