#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;

using namespace rstd::literals;

namespace derive_tests::records
{

struct NotDebug {
    int value;
};

struct [[pmacro::derive("rstd::Debug")]] [[pmacro::helper("rstd::Debug", rename("RecordView"))]]
PublicFields {
    const rstd::i32  count;
    rstd::i32&       current;
    const rstd::i32& limit;
    [[pmacro::helper("rstd::Debug", rename("label"))]]
    rstd::ref<rstd::str> name;
    [[pmacro::helper("rstd::Debug", skip)]]
    NotDebug secret;
};

struct [[pmacro::derive("rstd::Debug")]] Empty {};

struct [[pmacro::derive("rstd::Debug")]] AllSkipped {
    [[pmacro::helper("rstd::Debug", skip)]]
    NotDebug secret;
};

struct [[pmacro::derive("rstd::Debug")]] Leaf {
    rstd::i32            count;
    rstd::ref<rstd::str> name;
};

struct [[pmacro::derive("rstd::Debug")]] Branch {
    Leaf  child;
    Empty empty;
};

struct [[pmacro::derive("rstd::Debug",
                        "rstd::Display")]] [[pmacro::helper("rstd::Display",
                                                            format("{name}: {count}"))]] Summary {
    rstd::i32            count;
    rstd::ref<rstd::str> name;
};

struct [[pmacro::derive("rstd::Display")]] [[pmacro::helper(
    "rstd::Display",
    format("{{{name}}}: {count:03} / {name}"))]] Formatted {
    rstd::i32            count;
    rstd::ref<rstd::str> name;
};

struct [[pmacro::derive("rstd::Display")]] [[pmacro::helper("rstd::Display", format("{value}"))]]
SelectedField {
    NotDebug  state;
    rstd::i32 value;
};

struct [[pmacro::derive("rstd::Debug", "rstd::Display", "rstd::FromStr")]] [[pmacro::helper(
    "rstd::Display",
    transparent)]] [[pmacro::helper("rstd::FromStr", transparent)]] Number {
    rstd::i32 value;
};

struct [[pmacro::derive("rstd::Display")]] [[pmacro::helper("rstd::Display", transparent)]] Text {
    rstd::ref<rstd::str> value;
};

struct [[pmacro::derive("rstd::FromStr")]] [[pmacro::helper("rstd::FromStr", transparent)]]
SmallNumber {
    rstd::i8 value;
};

static_assert(! rstd::Impled<NotDebug, rstd::fmt::Debug>);
static_assert(rstd::Impled<PublicFields, rstd::fmt::Debug>);
static_assert(rstd::Impled<Number, rstd::fmt::Debug, rstd::fmt::Display, rstd::str_::FromStr>);
static_assert(rstd::mtp::same_as<typename rstd::Impl<rstd::str_::FromStr, Number>::Err,
                                 typename rstd::Impl<rstd::str_::FromStr, rstd::i32>::Err>);
static_assert(rstd::mtp::same_as<decltype(rstd::from_str<Number>(""_str)),
                                 rstd::Result<Number, rstd::num::ParseIntError>>);
static_assert(rstd::mtp::same_as<decltype(rstd::from_str<SmallNumber>(""_str)),
                                 rstd::Result<SmallNumber, rstd::num::ParseIntError>>);

TEST(DeriveDebug, PublicConstAndReferenceFieldsUseDebugAndTraitHelpers) {
    auto       current = rstd::i32(4);
    const auto limit   = rstd::i32(9);
    const auto value =
        PublicFields { rstd::i32(3), current, limit, "line\n\"quoted\""_str, NotDebug { 99 } };
    EXPECT_EQ(
        rstd::format("{:?}", value),
        "RecordView { count: 3, current: 4, limit: 9, label: \"line\\n\\\"quoted\\\"\" }"_str);
    current = rstd::i32(8);
    EXPECT_EQ(
        rstd::format("{:?}", value),
        "RecordView { count: 3, current: 8, limit: 9, label: \"line\\n\\\"quoted\\\"\" }"_str);
}

TEST(DeriveDebug, EmptyAndSkippedRecordsStayEmptyInBothModes) {
    const auto empty   = Empty {};
    const auto skipped = AllSkipped { NotDebug { 42 } };
    EXPECT_EQ(rstd::format("{:?}", empty), "Empty {}"_str);
    EXPECT_EQ(rstd::format("{:#?}", empty), "Empty {}"_str);
    EXPECT_EQ(rstd::format("{:?}", skipped), "AllSkipped {}"_str);
    EXPECT_EQ(rstd::format("{:#?}", skipped), "AllSkipped {}"_str);
}

TEST(DeriveDebug, NestedCompactAndPrettyKeepIndentationAndEscaping) {
    const auto value = Branch { Leaf { rstd::i32(5), "a\nb"_str }, Empty {} };
    EXPECT_EQ(rstd::format("{:?}", value),
              "Branch { child: Leaf { count: 5, name: \"a\\nb\" }, empty: Empty {} }"_str);
    EXPECT_EQ(rstd::format("{:#?}", value),
              "Branch {\n"
              "    child: Leaf {\n"
              "        count: 5,\n"
              "        name: \"a\\nb\",\n"
              "    },\n"
              "    empty: Empty {},\n"
              "}"_str);
}

TEST(DeriveDebug, WriterFailurePropagatesInsideNestedFields) {
    const auto   value       = Branch { Leaf { rstd::i32(5), "name"_str }, Empty {} };
    rstd::size_t remaining   = 25;
    auto reject_after_prefix = [](void* state, const rstd::uint8_t*, rstd::size_t size) -> bool {
        auto& budget = *static_cast<rstd::size_t*>(state);
        if (size > budget) return false;
        budget -= size;
        return true;
    };
    rstd::fmt::Formatter formatter(&remaining, reject_after_prefix);
    EXPECT_FALSE(rstd::as<rstd::fmt::Debug>(value).fmt(formatter));
    EXPECT_LT(remaining, rstd::size_t(25));
}

TEST(DeriveDisplay, NamedFormatUsesFieldNamesRatherThanDeclarationOrder) {
    const auto value = Summary { rstd::i32(12), "jobs"_str };
    EXPECT_EQ(rstd::format("{}", value), "jobs: 12"_str);
    EXPECT_EQ(rstd::format("{:?}", value), "Summary { count: 12, name: \"jobs\" }"_str);
}

TEST(DeriveDisplay, NamedFormatSupportsEscapesRepeatedFieldsAndSpecs) {
    const auto value = Formatted { rstd::i32(7), "jobs"_str };
    EXPECT_EQ(rstd::format("{}", value), "{jobs}: 007 / jobs"_str);
}

TEST(DeriveDisplay, UnusedFieldsDoNotRequireFormattingTraits) {
    const auto value = SelectedField { NotDebug { 1 }, rstd::i32(7) };
    EXPECT_EQ(rstd::format("{}", value), "7"_str);
}

TEST(DeriveDisplay, TransparentNumberPreservesFormatterOptions) {
    const auto value = Number { rstd::i32(42) };
    EXPECT_EQ(rstd::format("{}", value), "42"_str);
    EXPECT_EQ(rstd::format("{:+06}", value), rstd::format("{:+06}", value.value));
    EXPECT_EQ(rstd::format("{:_^8}", value), rstd::format("{:_^8}", value.value));
    EXPECT_EQ(rstd::format("{:?}", value), "Number { value: 42 }"_str);
}

TEST(DeriveDisplay, TransparentTextPreservesWidthFillAndPrecision) {
    const auto value = Text { "hello"_str };
    EXPECT_EQ(rstd::format("{}", value), "hello"_str);
    EXPECT_EQ(rstd::format("{:_>7.3}", value), "____hel"_str);
    EXPECT_EQ(rstd::format("{:_>7.3}", value), rstd::format("{:_>7.3}", value.value));
}

TEST(DeriveDisplay, WriterFailurePropagatesForNamedAndTransparentRecords) {
    auto reject = [](void*, const rstd::uint8_t*, rstd::size_t) -> bool {
        return false;
    };
    rstd::fmt::Formatter formatter(nullptr, reject);
    const auto           summary = Summary { rstd::i32(12), "jobs"_str };
    const auto           number  = Number { rstd::i32(42) };
    EXPECT_FALSE(rstd::as<rstd::fmt::Display>(summary).fmt(formatter));
    EXPECT_FALSE(rstd::as<rstd::fmt::Display>(number).fmt(formatter));
}

TEST(DeriveFromStr, TransparentRecordParsesItsOnlyField) {
    EXPECT_EQ(rstd::from_str<Number>("42"_str).unwrap().value, rstd::i32(42));
    EXPECT_EQ(rstd::from_str<Number>("-17"_str).unwrap().value, rstd::i32(-17));
    EXPECT_EQ(rstd::from_str<Number>("+17"_str).unwrap().value, rstd::i32(17));
    EXPECT_EQ(rstd::from_str<SmallNumber>("-128"_str).unwrap().value, rstd::i8::MIN);
    EXPECT_EQ(rstd::from_str<SmallNumber>("127"_str).unwrap().value, rstd::i8::MAX);

    auto parsed = rstd::from_str<Number>("-17"_str).unwrap();
    auto text   = rstd::format("{}", parsed);
    EXPECT_EQ(rstd::from_str<Number>(text.as_str()).unwrap().value, parsed.value);
}

TEST(DeriveFromStr, TransparentRecordPreservesErrorTypeAndKinds) {
    EXPECT_TRUE(rstd::from_str<Number>(""_str).unwrap_err().kind()->is_Empty());
    EXPECT_TRUE(rstd::from_str<Number>("not-a-number"_str).unwrap_err().kind()->is_InvalidDigit());
    EXPECT_TRUE(rstd::from_str<Number>(" 42"_str).unwrap_err().kind()->is_InvalidDigit());
    EXPECT_TRUE(rstd::from_str<Number>("42 "_str).unwrap_err().kind()->is_InvalidDigit());
    EXPECT_TRUE(rstd::from_str<SmallNumber>("128"_str).unwrap_err().kind()->is_PosOverflow());
    EXPECT_TRUE(rstd::from_str<SmallNumber>("-129"_str).unwrap_err().kind()->is_NegOverflow());
}

} // namespace derive_tests::records
