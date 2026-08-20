#include <rstd/test/gtest.hpp>

import rstd.json;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::json::Category;
using rstd::json::Value;

namespace
{

auto parse(ref<str> input) {
    return rstd::json::from_str(input);
}

void expect_syntax_error(ref<str> input) {
    auto result = parse(input);
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_EQ(error.classify(), Category::Syntax);
}

void expect_eof_error(ref<str> input) {
    auto result = parse(input);
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_EQ(error.classify(), Category::Eof);
}

} // namespace

TEST(JsonParser, ParsesScalarsAndNumberKinds) {
    EXPECT_TRUE(parse("null"_str).unwrap().is_null());
    EXPECT_EQ(parse(" true "_str).unwrap().as_bool(), Some(true));
    EXPECT_EQ(parse("false"_str).unwrap().as_bool(), Some(false));
    EXPECT_EQ(parse("-7"_str).unwrap().as_i64(), Some(i64(-7)));
    EXPECT_EQ(parse("18446744073709551615"_str).unwrap().as_u64(), Some(u64::MAX));
    EXPECT_TRUE(parse("18446744073709551616"_str).unwrap().is_f64());
    EXPECT_TRUE(parse("-0"_str).unwrap().is_f64());
    EXPECT_TRUE(parse("1.0"_str).unwrap().is_f64());
    EXPECT_TRUE(parse("1e0"_str).unwrap().is_f64());
    EXPECT_EQ(parse("1e-400"_str).unwrap().as_f64(), Some(f64()));
}

TEST(JsonParser, RoundsDecimalFloatsLikeSerdeJson) {
    EXPECT_EQ(parse("2.638344616030823e-256"_str).unwrap().as_f64(),
              Some(f64(2.638344616030823e-256)));
    EXPECT_EQ(parse("1009e-31"_str).unwrap().as_f64(), Some(f64(1.009e-28)));
    EXPECT_EQ(parse("1.7976931348623157e308"_str).unwrap().as_f64(), Some(f64::MAX));
    EXPECT_TRUE(parse("1.7976931348623159e308"_str).is_err());

    EXPECT_EQ(parse("2.4703282292062327e-324"_str).unwrap().as_f64(), Some(f64()));
    EXPECT_EQ(parse("2.4703282292062328e-324"_str).unwrap().as_f64(),
              Some(rstd::bit_cast<f64>(u64(1))));

    auto negative_zero = parse("-1e-400000000000000000000"_str).unwrap().as_f64().unwrap();
    EXPECT_EQ(rstd::bit_cast<u64>(negative_zero), u64(1) << u64(63));
}

TEST(JsonParser, ParsesNestedContainersAndReplacesDuplicateKeys) {
    auto result = parse(R"({"b":null,"a":[true,{"x":1}],"a":2})"_str);
    ASSERT_TRUE(result.is_ok());
    auto value = result.unwrap();

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ((**value.get("a"_str)).as_u64(), Some(u64(2)));
    EXPECT_TRUE((**value.get("b"_str)).is_null());

    auto keys  = (**value.as_object()).keys();
    auto first = keys.next();
    ASSERT_TRUE(first.is_some());
    EXPECT_EQ(**first, "a"_str);
}

TEST(JsonParser, RejectsDuplicateKeysWhenRequested) {
    auto result = rstd::json::from_str(R"({"a":1,"a":2})"_str,
                                       rstd::json::ParseOptions { .reject_duplicate_keys = true });
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().classify(), Category::Syntax);
}

TEST(JsonParser, DecodesStringEscapesAndUnicode) {
    auto value = parse(R"("quote:\" slash:\/ line:\n bmp:\u00e9 pair:\ud83d\ude00")"_str).unwrap();
    ASSERT_TRUE(value.as_str().is_some());
    EXPECT_EQ(*value.as_str(), "quote:\" slash:/ line:\n bmp:é pair:😀"_str);
}

TEST(JsonParser, AcceptsJsonWhitespaceRawUtf8AndAllSimpleEscapes) {
    auto empty = parse("\t\r\n [ ] \r"_str).unwrap();
    EXPECT_TRUE(empty.is_array());

    auto escaped = parse(R"("\"\\\/\b\f\n\r\t")"_str).unwrap();
    EXPECT_EQ(*escaped.as_str(), "\"\\/\b\f\n\r\t"_str);

    auto raw = parse("\"你好，JSON\""_str).unwrap();
    EXPECT_EQ(*raw.as_str(), "你好，JSON"_str);

    auto empty_string = parse("\"\""_str).unwrap();
    ASSERT_TRUE(empty_string.as_str().is_some());
    EXPECT_EQ((*empty_string.as_str()).size(), usize());
}

TEST(JsonParser, RejectsInvalidGrammar) {
    expect_syntax_error("+1"_str);
    expect_syntax_error("01"_str);
    expect_eof_error("1."_str);
    expect_syntax_error("1.x"_str);
    expect_eof_error("1e"_str);
    expect_eof_error("1e+"_str);
    expect_syntax_error("[1,]"_str);
    expect_syntax_error("{\"a\":1,}"_str);
    expect_syntax_error("{a:1}"_str);
    expect_syntax_error("null true"_str);
    expect_syntax_error("/* comment */ null"_str);
    expect_syntax_error(R"("\q")"_str);
    expect_syntax_error(R"("\udc00")"_str);
    expect_syntax_error(R"("\ud800x")"_str);
    expect_syntax_error(R"("\ud800\ud800")"_str);
    expect_syntax_error(R"("\uZZZZ")"_str);
    expect_syntax_error("1e400"_str);

    auto control_result = rstd::json::from_slice("\"\x1f\""_bytes);
    EXPECT_TRUE(control_result.is_err());
    EXPECT_TRUE(control_result.unwrap_err().is_syntax());
}

TEST(JsonParser, CommentsRequireExplicitOption) {
    const auto comments = rstd::json::ParseOptions { .allow_comments = true };

    EXPECT_TRUE(parse("/* comment */ null"_str).is_err());
    EXPECT_TRUE(rstd::json::from_str("/* comment */ null"_str, comments).unwrap().is_null());
    EXPECT_EQ((*rstd::json::from_str("[1, // line\n 2, /* block */ 3]"_str, comments)
                    .unwrap()
                    .as_array()
                    .unwrap())
                  .len(),
              usize(3));
    EXPECT_TRUE(rstd::json::from_str("1 // trailing"_str, comments).is_ok());

    auto unterminated = rstd::json::from_str("/* unterminated"_str, comments);
    ASSERT_TRUE(unterminated.is_err());
    EXPECT_TRUE(unterminated.unwrap_err().is_eof());
}

TEST(JsonParser, ClassifiesTruncatedInputAsEof) {
    expect_eof_error(""_str);
    expect_eof_error("["_str);
    expect_eof_error("[0"_str);
    expect_eof_error("{\"k\""_str);
    expect_eof_error("{\"k\":"_str);
    expect_eof_error("\""_str);
    expect_eof_error(R"("\u00)"_str);
}

TEST(JsonParser, EnforcesRecursionLimitAtContainerStart) {
    auto accepted = ::alloc::string::String::make();
    for (usize i {}; i < usize(127); ++i) accepted.push_ascii(u8('['));
    for (usize i {}; i < usize(127); ++i) accepted.push_ascii(u8(']'));
    EXPECT_TRUE(rstd::json::from_str(accepted.as_str()).is_ok());

    auto rejected = ::alloc::string::String::make();
    for (usize i {}; i < usize(129); ++i) rejected.push_ascii(u8('['));
    auto result = rstd::json::from_str(rejected.as_str());
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_TRUE(error.is_syntax());
    EXPECT_EQ(error.line(), usize(1));
    EXPECT_EQ(error.column(), usize(128));
}

TEST(JsonParser, SliceAndFromStrTraitReuseParser) {
    auto from_slice = rstd::json::from_slice("[1,2]"_bytes);
    auto from_trait = rstd::from_str<Value>("[1,2]"_str);

    ASSERT_TRUE(from_slice.is_ok());
    ASSERT_TRUE(from_trait.is_ok());
    EXPECT_EQ(*from_slice, *from_trait);

    rstd::byte invalid[] = { rstd::byte { 0xff } };
    auto       invalid_result =
        rstd::json::from_slice(rstd::slice<rstd::u8>::from_raw_parts(invalid, usize(1)));
    EXPECT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(invalid_result.unwrap_err().is_syntax());

    auto empty = rstd::json::from_slice(rstd::slice<rstd::u8>());
    ASSERT_TRUE(empty.is_err());
    EXPECT_TRUE(empty.unwrap_err().is_eof());
}

TEST(JsonParser, TracksOneBasedLineAndColumn) {
    auto result = parse("{\n  \"key\": [1,]\n}"_str);
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_TRUE(error.is_syntax());
    EXPECT_EQ(error.line(), usize(2));
    EXPECT_EQ(error.column(), usize(13));

    auto message = rstd::format("{}", error);
    EXPECT_TRUE(message.as_str().contains("line 2 column 13"_str));
}

TEST(JsonParser, MatchesReferenceEofAndControlPositions) {
    auto list = parse("["_str).unwrap_err();
    EXPECT_EQ(list.line(), usize(1));
    EXPECT_EQ(list.column(), usize(1));

    auto number = parse("1."_str).unwrap_err();
    EXPECT_EQ(number.line(), usize(1));
    EXPECT_EQ(number.column(), usize(2));

    auto string = parse("\""_str).unwrap_err();
    EXPECT_EQ(string.line(), usize(1));
    EXPECT_EQ(string.column(), usize(1));

    auto newline = parse("\"\n\""_str).unwrap_err();
    EXPECT_EQ(newline.line(), usize(2));
    EXPECT_EQ(newline.column(), usize());

    auto surrogate = parse(R"("\uD83C\uFFFF")"_str).unwrap_err();
    EXPECT_EQ(surrogate.line(), usize(1));
    EXPECT_EQ(surrogate.column(), usize(13));

    auto truncated_surrogate = parse(R"("\uD800)"_str).unwrap_err();
    EXPECT_TRUE(truncated_surrogate.is_eof());
    EXPECT_EQ(truncated_surrogate.column(), usize(7));

    auto overflow = parse("1e400"_str).unwrap_err();
    EXPECT_EQ(overflow.column(), usize(5));
}
