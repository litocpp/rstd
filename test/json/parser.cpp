#include <gtest/gtest.h>

import rstd.json;

using namespace rstd::prelude;
using rstd::json::Category;
using rstd::json::Value;

namespace
{

auto parse(const char* input) {
    return rstd::json::from_str(rstd::ref<rstd::str>(input));
}

void expect_syntax_error(const char* input) {
    auto result = parse(input);
    ASSERT_TRUE(result.is_err()) << input;
    auto error = result.unwrap_err();
    EXPECT_EQ(error.classify(), Category::Syntax) << input;
}

void expect_eof_error(const char* input) {
    auto result = parse(input);
    ASSERT_TRUE(result.is_err()) << input;
    auto error = result.unwrap_err();
    EXPECT_EQ(error.classify(), Category::Eof) << input;
}

} // namespace

TEST(JsonParser, ParsesScalarsAndNumberKinds) {
    EXPECT_TRUE(parse("null").unwrap().is_null());
    EXPECT_EQ(parse(" true ").unwrap().as_bool(), Some(true));
    EXPECT_EQ(parse("false").unwrap().as_bool(), Some(false));
    EXPECT_EQ(parse("-7").unwrap().as_i64(), Some(i64(-7)));
    EXPECT_EQ(parse("18446744073709551615").unwrap().as_u64(), Some(rstd::u64_::MAX));
    EXPECT_TRUE(parse("18446744073709551616").unwrap().is_f64());
    EXPECT_TRUE(parse("-0").unwrap().is_f64());
    EXPECT_TRUE(parse("1.0").unwrap().is_f64());
    EXPECT_TRUE(parse("1e0").unwrap().is_f64());
    EXPECT_EQ(parse("1e-400").unwrap().as_f64(), Some(0.0));
}

TEST(JsonParser, ParsesNestedContainersAndReplacesDuplicateKeys) {
    auto result = parse(R"({"b":null,"a":[true,{"x":1}],"a":2})");
    ASSERT_TRUE(result.is_ok());
    auto value = result.unwrap();

    ASSERT_TRUE(value.is_object());
    EXPECT_EQ((**value.get("a")).as_u64(), Some(u64(2)));
    EXPECT_TRUE((**value.get("b")).is_null());

    auto keys  = (**value.as_object()).keys();
    auto first = keys.next();
    ASSERT_TRUE(first.is_some());
    EXPECT_EQ(**first, "a");
}

TEST(JsonParser, DecodesStringEscapesAndUnicode) {
    auto value = parse(R"("quote:\" slash:\/ line:\n bmp:\u00e9 pair:\ud83d\ude00")").unwrap();
    ASSERT_TRUE(value.as_str().is_some());
    EXPECT_EQ(*value.as_str(), rstd::ref<rstd::str>("quote:\" slash:/ line:\n bmp:é pair:😀"));
}

TEST(JsonParser, AcceptsJsonWhitespaceRawUtf8AndAllSimpleEscapes) {
    auto empty = parse("\t\r\n [ ] \r").unwrap();
    EXPECT_TRUE(empty.is_array());

    auto           escaped    = parse(R"("\"\\\/\b\f\n\r\t")").unwrap();
    const rstd::u8 expected[] = { '"', '\\', '/', 0x08, 0x0c, '\n', '\r', '\t' };
    EXPECT_EQ(*escaped.as_str(), rstd::ref<rstd::str>(expected, sizeof(expected)));

    auto raw = parse("\"你好，JSON\"").unwrap();
    EXPECT_EQ(*raw.as_str(), rstd::ref<rstd::str>("你好，JSON"));

    auto empty_string = parse("\"\"").unwrap();
    ASSERT_TRUE(empty_string.as_str().is_some());
    EXPECT_EQ((*empty_string.as_str()).size(), 0u);
}

TEST(JsonParser, RejectsInvalidGrammar) {
    expect_syntax_error("+1");
    expect_syntax_error("01");
    expect_eof_error("1.");
    expect_syntax_error("1.x");
    expect_eof_error("1e");
    expect_eof_error("1e+");
    expect_syntax_error("[1,]");
    expect_syntax_error("{\"a\":1,}");
    expect_syntax_error("{a:1}");
    expect_syntax_error("null true");
    expect_syntax_error("/* comment */ null");
    expect_syntax_error(R"("\q")");
    expect_syntax_error(R"("\udc00")");
    expect_syntax_error(R"("\ud800x")");
    expect_syntax_error(R"("\ud800\ud800")");
    expect_syntax_error(R"("\uZZZZ")");
    expect_syntax_error("1e400");

    const rstd::u8 control[] = { '"', 0x1f, '"' };
    auto control_result = rstd::json::from_slice(rstd::slice<rstd::u8>::from_raw_parts(control, 3));
    EXPECT_TRUE(control_result.is_err());
    EXPECT_TRUE(control_result.unwrap_err().is_syntax());
}

TEST(JsonParser, CommentsRequireExplicitOption) {
    const auto comments = rstd::json::ParseOptions { .allow_comments = true };

    EXPECT_TRUE(parse("/* comment */ null").is_err());
    EXPECT_TRUE(rstd::json::from_str("/* comment */ null", comments).unwrap().is_null());
    EXPECT_EQ((*rstd::json::from_str("[1, // line\n 2, /* block */ 3]", comments)
                    .unwrap()
                    .as_array()
                    .unwrap())
                  .len(),
              3u);
    EXPECT_TRUE(rstd::json::from_str("1 // trailing", comments).is_ok());

    auto unterminated = rstd::json::from_str("/* unterminated", comments);
    ASSERT_TRUE(unterminated.is_err());
    EXPECT_TRUE(unterminated.unwrap_err().is_eof());
}

TEST(JsonParser, ClassifiesTruncatedInputAsEof) {
    expect_eof_error("");
    expect_eof_error("[");
    expect_eof_error("[0");
    expect_eof_error("{\"k\"");
    expect_eof_error("{\"k\":");
    expect_eof_error("\"");
    expect_eof_error(R"("\u00)");
}

TEST(JsonParser, EnforcesRecursionLimitAtContainerStart) {
    auto accepted = ::alloc::string::String::make();
    for (usize i = 0; i < 127; ++i) accepted.push_back('[');
    for (usize i = 0; i < 127; ++i) accepted.push_back(']');
    EXPECT_TRUE(rstd::json::from_str(accepted.as_str()).is_ok());

    auto rejected = ::alloc::string::String::make();
    for (usize i = 0; i < 129; ++i) rejected.push_back('[');
    auto result = rstd::json::from_str(rejected.as_str());
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_TRUE(error.is_syntax());
    EXPECT_EQ(error.line(), 1u);
    EXPECT_EQ(error.column(), 128u);
}

TEST(JsonParser, SliceAndFromStrTraitReuseParser) {
    const rstd::u8 input[] = { '[', '1', ',', '2', ']' };
    auto from_slice = rstd::json::from_slice(rstd::slice<rstd::u8>::from_raw_parts(input, 5));
    auto from_trait = rstd::from_str<Value>("[1,2]");

    ASSERT_TRUE(from_slice.is_ok());
    ASSERT_TRUE(from_trait.is_ok());
    EXPECT_EQ(*from_slice, *from_trait);

    const rstd::u8 invalid[] = { 0xff };
    auto invalid_result = rstd::json::from_slice(rstd::slice<rstd::u8>::from_raw_parts(invalid, 1));
    EXPECT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(invalid_result.unwrap_err().is_syntax());

    auto empty = rstd::json::from_slice(rstd::slice<rstd::u8>());
    ASSERT_TRUE(empty.is_err());
    EXPECT_TRUE(empty.unwrap_err().is_eof());
}

TEST(JsonParser, TracksOneBasedLineAndColumn) {
    auto result = parse("{\n  \"key\": [1,]\n}");
    ASSERT_TRUE(result.is_err());
    auto error = result.unwrap_err();
    EXPECT_TRUE(error.is_syntax());
    EXPECT_EQ(error.line(), 2u);
    EXPECT_EQ(error.column(), 13u);

    auto message = rstd::format("{}", error);
    EXPECT_TRUE(rstd::str_::contains(message.as_str(), "line 2 column 13"));
}

TEST(JsonParser, MatchesReferenceEofAndControlPositions) {
    auto list = parse("[").unwrap_err();
    EXPECT_EQ(list.line(), 1u);
    EXPECT_EQ(list.column(), 1u);

    auto number = parse("1.").unwrap_err();
    EXPECT_EQ(number.line(), 1u);
    EXPECT_EQ(number.column(), 2u);

    auto string = parse("\"").unwrap_err();
    EXPECT_EQ(string.line(), 1u);
    EXPECT_EQ(string.column(), 1u);

    auto newline = parse("\"\n\"").unwrap_err();
    EXPECT_EQ(newline.line(), 2u);
    EXPECT_EQ(newline.column(), 0u);

    auto surrogate = parse(R"("\uD83C\uFFFF")").unwrap_err();
    EXPECT_EQ(surrogate.line(), 1u);
    EXPECT_EQ(surrogate.column(), 13u);

    auto truncated_surrogate = parse(R"("\uD800)").unwrap_err();
    EXPECT_TRUE(truncated_surrogate.is_eof());
    EXPECT_EQ(truncated_surrogate.column(), 7u);

    auto overflow = parse("1e400").unwrap_err();
    EXPECT_EQ(overflow.column(), 5u);
}
