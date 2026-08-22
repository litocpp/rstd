#include <rstd/test/gtest.hpp>

import rstd.parse;

using namespace rstd::literals;
using namespace rstd::parse;

namespace
{

struct Observer {
    int starts {};
    int successes {};
    int mismatches {};
    int errors {};

    void start(RuleId, rstd::usize) noexcept { ++starts; }
    void success(RuleId, Span) noexcept { ++successes; }
    void mismatch(RuleId, rstd::usize) noexcept { ++mismatches; }
    void error(RuleId, const ParseError&) noexcept { ++errors; }
};

struct StalledRule {
    using value_type               = Span;
    static constexpr bool nullable = false;

    constexpr auto id() const noexcept -> RuleId { return RuleId("stalled"_str); }

    template<typename T, typename ObserverType>
    auto match(Driver<T, ObserverType>& driver) -> Match<Span> {
        auto position = driver.cursor().position();
        return rstd::Ok(rstd::Some(Span { .begin = position, .end = position }));
    }
};

} // namespace

TEST(Parse, OrderedChoiceRewindsConsumedInput) {
    auto     first  = text(RuleId("first"_str), "abx"_str);
    auto     second = text(RuleId("second"_str), "abc"_str);
    auto     rule   = choice(RuleId("choice"_str), rstd::move(first), rstd::move(second));
    Observer observer;

    auto result = parse(text_input("abc"_str), SourceId("fixture"_str), rule, observer);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(result->unwrap().begin, rstd::usize());
    EXPECT_EQ(result->unwrap().end, rstd::usize(3));
    EXPECT_GT(observer.mismatches, 0);
    EXPECT_EQ(observer.starts, observer.successes + observer.mismatches + observer.errors);
}

TEST(Parse, CommitStopsOrderedChoice) {
    auto     prefix    = text(RuleId("prefix"_str), "abx"_str);
    auto     committed = commit(RuleId("committed"_str), rstd::move(prefix));
    auto     fallback  = text(RuleId("fallback"_str), "abc"_str);
    auto     rule      = choice(RuleId("choice"_str), rstd::move(committed), rstd::move(fallback));
    Observer observer;

    auto result = parse(text_input("abc"_str), SourceId("fixture"_str), rule, observer);

    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err();
    EXPECT_EQ(error.diagnostic().kind(), ErrorKind::Expected);
    EXPECT_EQ(error.diagnostic().span(), (Span { .begin = rstd::usize(3), .end = rstd::usize(3) }));
    ASSERT_TRUE(error.diagnostic().expected().is_some());
    EXPECT_EQ(*error.diagnostic().expected(), "prefix"_str);
    EXPECT_EQ(observer.starts, observer.successes + observer.mismatches + observer.errors);
}

TEST(Parse, RepeatAndOptionalOwnTheirValues) {
    auto digit  = atomic(RuleId("digit"_str), [](rstd::u8 value) {
        return value >= rstd::u8('0') && value <= rstd::u8('9');
    });
    auto digits = repeat_one(RuleId("digits"_str), rstd::move(digit));
    auto sign   = optional(RuleId("sign"_str), text(RuleId("minus"_str), "-"_str));
    auto rule   = seq(RuleId("number"_str), rstd::move(sign), rstd::move(digits));

    auto result = parse(text_input("-42x"_str), SourceId("fixture"_str), rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    auto value = result->unwrap();
    EXPECT_TRUE(rstd::get<0>(value).is_some());
    EXPECT_EQ(rstd::get<1>(value).len(), rstd::usize(2));
}

TEST(Parse, TokenInputUsesTheSameDriver) {
    int  tokens[] { 1, 2, 3 };
    auto token = atomic(RuleId("two"_str), [](int value) {
        return value == 2;
    });
    auto any   = atomic(RuleId("any"_str), [](int) {
        return true;
    });
    auto rule  = seq(RuleId("tokens"_str), rstd::move(any), rstd::move(token));

    auto input  = Input<int>(rstd::slice<int>::from_raw_parts(tokens, rstd::usize(3)));
    auto result = parse(input, SourceId("tokens"_str), rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(rstd::get<1>(result->unwrap()).begin, rstd::usize(1));
    EXPECT_EQ(rstd::get<1>(result->unwrap()).end, rstd::usize(2));
}

TEST(Parse, SourcePositionUsesUtf8ByteOffsets) {
    auto       input = text_input("a\n中"_str);
    TextCursor cursor(input);
    (void)cursor.take();
    (void)cursor.take();
    (void)cursor.take();

    EXPECT_EQ(cursor.position(), rstd::usize(3));
    EXPECT_EQ(cursor.source_position(cursor.position()),
              (SourcePosition { .line = rstd::usize(2), .column = rstd::usize(2) }));
}

TEST(Parse, CursorOwnsLookaheadSpansAndConsumedViews) {
    TextCursor cursor(text_input("ab\nc"_str));
    auto       begin = cursor.checkpoint();

    ASSERT_TRUE(cursor.peek(rstd::usize(1)).is_some());
    EXPECT_EQ(cursor.peek(rstd::usize(1))->get(), rstd::u8('b'));
    EXPECT_EQ(cursor.len(), rstd::usize(4));
    EXPECT_EQ(cursor.remaining(), rstd::usize(4));

    (void)cursor.take();
    (void)cursor.take();
    EXPECT_EQ(cursor.consumed_text(begin), "ab"_str);
    EXPECT_EQ(cursor.view(cursor.span_from(begin)).len(), rstd::usize(2));
    EXPECT_EQ(cursor.remaining(), rstd::usize(2));
    EXPECT_EQ(cursor.source_position(),
              (SourcePosition { .line = rstd::usize(1), .column = rstd::usize(3) }));

    (void)cursor.take();
    EXPECT_EQ(cursor.source_position(),
              (SourcePosition { .line = rstd::usize(2), .column = rstd::usize(1) }));
    cursor.rewind(begin);
    EXPECT_EQ(cursor.position(), rstd::usize());
    EXPECT_EQ(cursor.furthest_position(), rstd::usize(3));
}

TEST(Parse, DelimitedAndSeparatedKeepOnlyValues) {
    auto item  = atomic(RuleId("item"_str), [](rstd::u8 value) {
        return value >= rstd::u8('a') && value <= rstd::u8('z');
    });
    auto comma = text(RuleId("comma"_str), ","_str);
    auto items = separated_one(RuleId("items"_str), rstd::move(item), rstd::move(comma));
    auto rule  = delimited(RuleId("list"_str),
                           text(RuleId("open"_str), "["_str),
                           rstd::move(items),
                           text(RuleId("close"_str), "]"_str));

    auto result = parse(text_input("[a,b,c]"_str), SourceId("fixture"_str), rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(result->unwrap().len(), rstd::usize(3));
}

TEST(Parse, SeparatedDoesNotConsumeTrailingSeparator) {
    auto         item  = text(RuleId("item"_str), "a"_str);
    auto         comma = text(RuleId("comma"_str), ","_str);
    auto         rule  = separated_one(RuleId("items"_str), rstd::move(item), rstd::move(comma));
    NoopObserver observer;
    Driver       driver(text_input("a,"_str), SourceId("fixture"_str), observer);

    auto result = driver.apply(rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(result->unwrap().len(), rstd::usize(1));
    EXPECT_EQ(driver.cursor().position(), rstd::usize(1));
}

TEST(Parse, LookaheadRulesNeverConsumeInput) {
    auto positive = lookahead(RuleId("positive"_str), text(RuleId("prefix"_str), "ab"_str));
    auto negative = not_at(RuleId("negative"_str), text(RuleId("excluded"_str), "x"_str));
    auto prefix   = seq(RuleId("predicates"_str), rstd::move(positive), rstd::move(negative));
    auto rule = seq(RuleId("input"_str), rstd::move(prefix), text(RuleId("value"_str), "ab"_str));

    auto result = parse(text_input("ab"_str), SourceId("fixture"_str), rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(rstd::get<1>(result->unwrap()).begin, rstd::usize());
    EXPECT_EQ(rstd::get<1>(result->unwrap()).end, rstd::usize(2));
}

TEST(Parse, FailedSemanticBranchDoesNotPublishAValue) {
    int  calls  = 0;
    auto first  = map(RuleId("first-value"_str), text(RuleId("first"_str), "abx"_str), [&](Span) {
        ++calls;
        return 1;
    });
    auto second = map(RuleId("second-value"_str), text(RuleId("second"_str), "abc"_str), [&](Span) {
        ++calls;
        return 2;
    });
    auto rule   = choice(RuleId("choice"_str), rstd::move(first), rstd::move(second));

    auto result = parse(text_input("abc"_str), SourceId("fixture"_str), rule);

    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_some());
    EXPECT_EQ(result->unwrap(), 2);
    EXPECT_EQ(calls, 1);
}

TEST(Parse, RepeatRejectsAFalseProgressDeclarationAtRuntime) {
    auto rule = repeat(RuleId("repeat"_str), StalledRule {});

    auto result = parse(text_input("input"_str), SourceId("fixture"_str), rule);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().diagnostic().kind(), ErrorKind::Stalled);
}
