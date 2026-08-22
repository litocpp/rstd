#include <rstd/test/gtest.hpp>

import rstd;
import rstd.parse.regex;

using namespace rstd::prelude;
using namespace rstd::literals;

namespace regex = rstd::parse::regex;

inline constexpr auto IDENTIFIER = regex::compile<R"([A-Za-z_][A-Za-z0-9_]*)">;

static_assert(IDENTIFIER.full_match("value_42"_str).is_some());
static_assert(IDENTIFIER.full_match("42value"_str).is_none());
static_assert(! IDENTIFIER.nullable());

TEST(Regex, DistinguishesSearchPrefixAndFullMatch) {
    constexpr auto expression = regex::compile<"ab+">;

    auto found = expression.find("xxabbbz"_str).unwrap();
    EXPECT_EQ(found.span(), (rstd::parse::Span { usize(2), usize(6) }));
    EXPECT_EQ(found.text(), "abbb"_str);
    EXPECT_FALSE(expression.starts_with("xxabbb"_str));
    EXPECT_TRUE(expression.starts_with("abbbx"_str));
    EXPECT_TRUE(expression.full_match("abbb"_str).is_some());
    EXPECT_TRUE(expression.full_match("abbbx"_str).is_none());
}

TEST(Regex, PreservesAlternationAndRepeatPriority) {
    constexpr auto longer_first  = regex::compile<"ab|a">;
    constexpr auto shorter_first = regex::compile<"a|ab">;
    constexpr auto greedy        = regex::compile<"a+">;
    constexpr auto lazy          = regex::compile<"a+?">;

    EXPECT_EQ(longer_first.find("ab"_str)->text(), "ab"_str);
    EXPECT_EQ(shorter_first.find("ab"_str)->text(), "a"_str);
    EXPECT_EQ(greedy.find("aaab"_str)->text(), "aaa"_str);
    EXPECT_EQ(lazy.find("aaab"_str)->text(), "a"_str);
}

TEST(Regex, SupportsClassesEscapesBoundsAndUnicode) {
    constexpr auto expression = regex::compile<R"([^\s]{2,4}\x2d\u{53f3}\d)">;

    EXPECT_TRUE(expression.full_match("ab-右7"_str).is_some());
    EXPECT_TRUE(expression.full_match("abcde-右7"_str).is_none());
    EXPECT_TRUE(expression.full_match("a -右7"_str).is_none());

    constexpr auto trailing_hyphen = regex::compile<"[a-]+">;
    EXPECT_TRUE(trailing_hyphen.full_match("a--a"_str).is_some());
}

TEST(Regex, CapturesNumberedNamedAndOptionalGroups) {
    constexpr auto date = regex::compile<R"((?<year>\d{4})-(\d{2})(?:-(\d{2}))?)">;

    auto captures = date.captures("date=2026-08-22"_str).unwrap();
    EXPECT_EQ(captures.template get<0>()->text(), "2026-08-22"_str);
    EXPECT_EQ(captures.template get<1>()->text(), "2026"_str);
    EXPECT_EQ(captures.template get<"year">()->text(), "2026"_str);
    EXPECT_EQ(captures.template get<2>()->text(), "08"_str);
    EXPECT_EQ(captures.template get<3>()->text(), "22"_str);

    auto without_day = date.captures("2026-08"_str).unwrap();
    EXPECT_TRUE(without_day.template get<3>().is_none());
}

TEST(Regex, HandlesAnchorsWordBoundariesAndOptions) {
    constexpr auto words  = regex::compile<R"(\bcat\b)">;
    constexpr auto lines  = regex::compile<"^cat$", regex::Options { .multiline = true }>;
    constexpr auto folded = regex::compile<"hello", regex::Options { .case_insensitive = true }>;

    EXPECT_TRUE(words.is_match("a cat!"_str));
    EXPECT_FALSE(words.is_match("concatenate"_str));
    EXPECT_EQ(lines.find("dog\ncat\nfox"_str)->text(), "cat"_str);
    EXPECT_TRUE(folded.full_match("HeLLo"_str).is_some());
}

TEST(Regex, FindIteratorAdvancesAfterEmptyUtf8Match) {
    constexpr auto empty = regex::compile<"a*?">;
    auto           found = empty.find_iter("右"_str);

    EXPECT_EQ(found.next()->span(), (rstd::parse::Span { usize(), usize() }));
    EXPECT_EQ(found.next()->span(), (rstd::parse::Span { usize(3), usize(3) }));
    EXPECT_TRUE(found.next().is_none());
}

TEST(Regex, CursorAdapterUsesPrefixAndAbsoluteByteOffsets) {
    constexpr auto expression = regex::compile<R"(\d+)">;
    auto           cursor     = rstd::parse::TextCursor(rstd::parse::text_input("x:123z"_str));
    ASSERT_TRUE(cursor.advance(usize(2)));

    auto span = regex::consume(cursor, expression).unwrap();
    EXPECT_EQ(span, (rstd::parse::Span { usize(2), usize(5) }));
    EXPECT_EQ(cursor.position(), usize(5));

    auto mismatch = regex::consume(cursor, expression);
    EXPECT_TRUE(mismatch.is_none());
    EXPECT_EQ(cursor.position(), usize(5));
}

TEST(Regex, CursorCapturesAreRebasedToTheOriginalInput) {
    constexpr auto expression = regex::compile<R"((\d+))">;
    auto           cursor     = rstd::parse::TextCursor(rstd::parse::text_input("x:123z"_str));
    ASSERT_TRUE(cursor.advance(usize(2)));

    auto captures = regex::consume_captures(cursor, expression).unwrap();
    EXPECT_EQ(captures.template get<0>()->span(), (rstd::parse::Span { usize(2), usize(5) }));
    EXPECT_EQ(captures.template get<1>()->text(), "123"_str);
    EXPECT_EQ(cursor.position(), usize(5));
}

TEST(Regex, AmbiguousRepeatsUseTheBoundedNfaExecutor) {
    constexpr auto expression = regex::compile<"(a|aa)*b">;
    constexpr auto input      = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
                                "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab"_str;

    EXPECT_TRUE(expression.full_match(input).is_some());
    EXPECT_TRUE(expression.full_match("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"_str).is_none());
}
