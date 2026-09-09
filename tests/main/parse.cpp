#include <rstd/test/gtest.hpp>

import rstd.parse.core;
import rstd.parse.alloc;

using namespace rstd::literals;
using namespace rstd::parse;
using namespace rstd::prelude;

TEST(Parse, PositionedCursorRestoresNewlineState) {
    PositionedCursor cursor(text_input("a\r\nb\rc\n"_str));
    auto             start = cursor.checkpoint();
    ASSERT_TRUE(cursor.advance(usize(2)));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(1) }));
    auto after_cr = cursor.checkpoint();
    EXPECT_FALSE(cursor.advance(cursor.len()));
    EXPECT_EQ(cursor.position(), usize(2));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(1) }));
    ASSERT_TRUE(consume_literal(cursor, "\nb\rc\n"_str).is_some());
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(4), usize(1) }));
    EXPECT_TRUE(cursor.is_eof());
    EXPECT_FALSE(cursor.advance(usize(1)));
    EXPECT_TRUE(cursor.take().is_none());
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(4), usize(1) }));
    cursor.rewind(after_cr);
    ASSERT_TRUE(cursor.take().is_some());
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(1) }));
    cursor.rewind(after_cr);
    EXPECT_TRUE(consume_literal(cursor, "\nx"_str).is_none());
    EXPECT_EQ(cursor.position(), usize(2));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(1) }));
    cursor.rewind(start);
    EXPECT_EQ(cursor.source_position(), SourcePosition {});
    EXPECT_EQ(cursor.furthest_position(), cursor.len());
    ASSERT_TRUE(cursor.advance(cursor.len()));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(4), usize(1) }));
}

TEST(Parse, SingleLineAdvancePreservesBoundariesAndCarriageReturnState) {
    PositionedCursor cursor(text_input("\rX\n"_str));
    ASSERT_TRUE(cursor.advance(usize(1)));
    auto start = cursor.checkpoint();
    EXPECT_FALSE(cursor.advance_single_line_unchecked(usize(3)));
    EXPECT_EQ(cursor.position(), usize(1));
    EXPECT_TRUE(cursor.advance_single_line_unchecked(usize()));
    EXPECT_TRUE(cursor.advance_single_line_unchecked(usize(1)));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(2) }));
    ASSERT_TRUE(cursor.advance(usize(1)));
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(3), usize(1) }));
    cursor.rewind(start);
    EXPECT_EQ(cursor.source_position(), (SourcePosition { usize(2), usize(1) }));
    EXPECT_EQ(cursor.furthest_position(), usize(3));
}

TEST(Parse, PositionedCursorBatchAndByteConsumptionAgree) {
    TextCursor legacy(text_input("a\rb"_str));
    ASSERT_TRUE(legacy.advance(usize(2)));
    EXPECT_EQ(legacy.source_position(), (SourcePosition { usize(1), usize(3) }));
    PositionedCursor empty(text_input(""_str));
    EXPECT_TRUE(empty.is_eof());
    EXPECT_TRUE(empty.advance(usize()));
    EXPECT_FALSE(empty.advance(usize(1)));
    EXPECT_EQ(empty.source_position(), SourcePosition {});
    auto             input = text_input("\r\n\r\n中\n\rX"_str);
    PositionedCursor bytes(input);
    PositionedCursor batch(input);
    for (usize offset {}; offset <= input.len(); ++offset) {
        EXPECT_EQ(bytes.source_position(), batch.source_position(offset));
        if (! bytes.is_eof()) (void)bytes.take();
    }
    ASSERT_TRUE(batch.advance(input.len()));
    EXPECT_EQ(bytes.source_position(), batch.source_position());
    auto begin = batch.checkpoint();
    EXPECT_EQ(consume_while(batch,
                            [](u8) {
                                return true;
                            })
                  .len(),
              usize());
    EXPECT_EQ(batch.span_from(begin).len(), usize());
    EXPECT_TRUE(batch.advance(usize()));

    PositionedCursor other(input);
    EXPECT_DEATH(other.rewind(begin), "checkpoint belongs to another cursor");
    EXPECT_DEATH((void)other.span_from(begin), "invalid parse checkpoint span");
}

TEST(Parse, TextViewsValidateBytesAndCodepointBoundaries) {
    TextCursor text_cursor(text_input("a中z"_str));
    auto       valid = text_cursor.text({ usize(1), usize(4) });
    ASSERT_TRUE(valid.is_ok());
    EXPECT_EQ(*valid, "中"_str);
    EXPECT_EQ(valid->as_bytes().as_raw_ptr(), text_cursor.input().as_raw_ptr() + 1);
    EXPECT_TRUE(text_cursor.text({ usize(1), usize(3) }).is_err());
    EXPECT_TRUE(text_cursor.text({ usize(2), usize(4) }).is_err());
    EXPECT_TRUE(text_cursor.text({ usize(2), usize(2) }).is_err());
    EXPECT_TRUE(text_cursor.text({ usize(4), usize(4) }).is_ok());
    ASSERT_TRUE(text_cursor.advance(usize(2)));
    EXPECT_TRUE(text_cursor.remaining_text().is_err());

    auto       raw = array<u8, 4> { u8('a'), u8(0xff), u8(0xe4), u8(0xb8) };
    TextCursor bytes(Input<u8>(raw.as_slice()));
    EXPECT_TRUE(bytes.remaining_text().is_err());
    EXPECT_EQ(bytes.text({ usize(), usize(1) }).unwrap(), "a"_str);
    EXPECT_TRUE(bytes.text({ usize(2), usize(4) }).is_err());
    auto begin = bytes.checkpoint();
    EXPECT_EQ(consume_while(bytes,
                            [](u8) {
                                return true;
                            })
                  .len(),
              raw.len());
    EXPECT_EQ(bytes.consumed(begin).len(), raw.len());
    EXPECT_TRUE(bytes.consumed_text(begin).is_err());
    EXPECT_TRUE(bytes.remaining_text().is_ok());
}

static_assert([] {
    PositionedCursor cursor(text_input("x\r\n"_str));
    (void)cursor.advance(usize(2));
    auto checkpoint = cursor.checkpoint();
    (void)cursor.take();
    cursor.rewind(checkpoint);
    (void)cursor.take();
    return cursor.source_position() == SourcePosition { usize(2), usize(1) };
}());

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

    template<typename T, typename Adapter, typename ObserverType>
    auto match(Driver<T, Adapter, ObserverType>& driver)
        -> Match<Span, typename Adapter::error_type> {
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
    auto digit  = atomic(RuleId("digit"_str), ascii::digit);
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
    EXPECT_EQ(cursor.consumed_text(begin).unwrap(), "ab"_str);
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
    auto           item  = text(RuleId("item"_str), "a"_str);
    auto           comma = text(RuleId("comma"_str), ","_str);
    auto           rule  = separated_one(RuleId("items"_str), rstd::move(item), rstd::move(comma));
    NoopObserver   observer;
    RuntimeAdapter adapter(SourceId("fixture"_str));
    Driver         driver(text_input("a,"_str), adapter, observer);

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

TEST(Parse, AsciiPredicatesComposeWithoutRules) {
    constexpr auto bare_key = any_of(ascii::alnum, one_of(rstd::u8('_'), rstd::u8('-')));

    static_assert(ascii::digit(rstd::u8('0')));
    static_assert(ascii::hex_digit(rstd::u8('F')));
    static_assert(! ascii::digit(rstd::u8('a')));
    static_assert(bare_key(rstd::u8('_')));
    static_assert(bare_key(rstd::u8('z')));
    static_assert(! bare_key(rstd::u8('.')));

    EXPECT_EQ(*rstd::ascii::digit_value(rstd::u8('f'), rstd::u8(16)), rstd::u8(15));
    EXPECT_TRUE(rstd::ascii::digit_value(rstd::u8('2'), rstd::u8(2)).is_none());

    for (rstd::uint16_t raw = 0; raw <= 0xff; ++raw) {
        auto const value = rstd::u8(static_cast<rstd::uint8_t>(raw));
        EXPECT_EQ(rstd::ascii::is_digit(value), raw >= '0' && raw <= '9');
        EXPECT_EQ(rstd::ascii::is_alpha(value),
                  (raw >= 'a' && raw <= 'z') || (raw >= 'A' && raw <= 'Z'));
        EXPECT_EQ(rstd::ascii::is_space(value),
                  raw == ' ' || raw == '\t' || raw == '\n' || raw == '\r' || raw == '\v' ||
                      raw == '\f');
    }
}

TEST(Parse, ConsumeFragmentsOwnRollbackAndSpans) {
    TextCursor cursor(text_input("12ab"_str));

    auto digits = consume_while_one(cursor, ascii::digit);
    ASSERT_TRUE(digits.is_some());
    EXPECT_EQ(*digits, (Span { .begin = rstd::usize(), .end = rstd::usize(2) }));

    auto before = cursor.position();
    auto failed = consume_n(cursor, rstd::usize(3), ascii::alpha);
    EXPECT_TRUE(failed.is_none());
    EXPECT_EQ(cursor.position(), before);
    EXPECT_EQ(cursor.furthest_position(), rstd::usize(4));

    auto literal = consume_literal(cursor, "ab"_str);
    ASSERT_TRUE(literal.is_some());
    EXPECT_EQ(cursor.text(*literal).unwrap(), "ab"_str);
    EXPECT_TRUE(cursor.is_eof());
}

TEST(Parse, FixedCollectionReportsCapacityThroughAdapter) {
    auto digit  = atomic(RuleId("digit"_str), ascii::digit);
    auto digits = repeat_one_fixed<2>(RuleId("digits"_str), rstd::move(digit));

    auto result = parse(text_input("123"_str), digits);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind, ErrorKind::Capacity);
}

TEST(Parse, FixedAndVecCollectionsPreserveTheSameGrammarResult) {
    auto fixed_digit  = atomic(RuleId("digit"_str), ascii::digit);
    auto fixed_digits = repeat_one_fixed<3>(RuleId("digits"_str), rstd::move(fixed_digit));
    auto fixed        = parse(text_input("123x"_str), fixed_digits).unwrap().unwrap();

    auto vec_digit  = atomic(RuleId("digit"_str), ascii::digit);
    auto vec_digits = repeat_one(RuleId("digits"_str), rstd::move(vec_digit));
    auto dynamic =
        parse(text_input("123x"_str), SourceId("fixture"_str), vec_digits).unwrap().unwrap();

    ASSERT_EQ(fixed.len(), dynamic.len());
    for (rstd::usize index {}; index < fixed.len(); ++index) {
        EXPECT_EQ(fixed[index], dynamic[index]);
    }
}
