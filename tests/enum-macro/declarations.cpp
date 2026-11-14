#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;

static_assert(! rstd::mtp::init<NoDefault>);
static_assert(rstd::mtp::init<DefaultNumber>);
static_assert(rstd::mtp::init<Maybe<i32>>);
static_assert(
    rstd::Impled<Message, rstd::VariantDescription, rstd::fmt::Debug, rstd::clone::Clone>);
static_assert(! rstd::mtp::copy<Message>);

TEST(EnumMacro, ConstructsAllPayloadShapesWithoutLosingTheirTags) {
    auto unit = Message::Unit();
    EXPECT_TRUE(unit.is_Unit());
    EXPECT_EQ(unit.tag(), Message::Tag::Unit);
    EXPECT_EQ(unit.index(), 0u);
    auto tuple = Message::Tuple(i32(3), "tuple"_Str);
    EXPECT_EQ(tuple.as_Tuple().field_0, i32(3));
    EXPECT_EQ(tuple.as_Tuple().field_1.as_str(), "tuple"_str);
    EXPECT_EQ(tuple.as_Tuple().template get<0>(), i32(3));
    EXPECT_EQ(tuple.as_Tuple().template get<1>().as_str(), "tuple"_str);
    auto named = Message::Named(i32(4), "named"_Str);
    EXPECT_EQ(named.as_Named().count, i32(4));
    EXPECT_EQ(named.as_Named().text.as_str(), "named"_str);
    EXPECT_TRUE(Message::EmptyTuple().is_EmptyTuple());
    EXPECT_TRUE(Message::EmptyRecord().is_EmptyRecord());
    auto wrapped = Message::Wrapped(rstd::tuple<i32, i32> { i32(5), i32(6) });
    EXPECT_EQ(wrapped.as_Wrapped().field_0.template get<0>(), i32(5));
    EXPECT_EQ(wrapped.as_Wrapped().field_0.template get<1>(), i32(6));
}

TEST(EnumMacro, KeepsTupleAndRecordDebugDistinctIncludingEmptyPayloads) {
    EXPECT_EQ(rstd::format("{:?}", Message::Unit()), "Unit"_str);
    EXPECT_EQ(rstd::format("{:?}", Message::EmptyTuple()), "EmptyTuple()"_str);
    EXPECT_EQ(rstd::format("{:?}", Message::EmptyRecord()), "EmptyRecord {}"_str);
    EXPECT_EQ(rstd::format("{:?}", Message::Tuple(i32(3), "tuple"_Str)), "Tuple(3, \"tuple\")"_str);
    EXPECT_EQ(rstd::format("{:?}", Message::Named(i32(4), "named"_Str)),
              "Named { count: 4, text: \"named\" }"_str);
}

TEST(EnumMacro, CallsOutOfLineCustomMembers) {
    auto value = Message::custom();
    ASSERT_TRUE(value.is_Named());
    EXPECT_EQ(value.score(), i32(7));
    EXPECT_EQ(value.as_Named().text.as_str(), "custom"_str);
    EXPECT_EQ(Message::Unit().score(), i32());
}

TEST(EnumMacro, DefaultsOnlyToTheExplicitlySelectedVariant) {
    DefaultNumber value;
    ASSERT_TRUE(value.is_Number());
    EXPECT_EQ(value.as_Number().value, i32());
    Maybe<i32> maybe;
    EXPECT_TRUE(maybe.is_None());
    EXPECT_EQ(maybe.index(), 0u);
}

TEST(EnumMacro, ClonesOwnedPayloadsWithoutCopyingTheEnum) {
    auto original = Message::Named(i32(7), "original"_Str);
    auto copy     = rstd::as<rstd::clone::Clone>(original).clone();
    ASSERT_TRUE(copy.is_Named());
    copy.as_Named().text = "changed"_Str;
    EXPECT_EQ(original.as_Named().text.as_str(), "original"_str);
    rstd::as<rstd::clone::Clone>(copy).clone_from(original);
    EXPECT_EQ(copy.as_Named().text.as_str(), "original"_str);
    rstd::as<rstd::clone::Clone>(copy).clone_from(Message::EmptyTuple());
    EXPECT_TRUE(copy.is_EmptyTuple());
    rstd::as<rstd::clone::Clone>(copy).clone_from(copy);
    EXPECT_TRUE(copy.is_EmptyTuple());
}

TEST(EnumMacro, KeepsIdenticalExternalPayloadTypesInDifferentVariants) {
    auto first  = DuplicatePayload::First(SharedPayload { i32(3) });
    auto second = DuplicatePayload::Second(SharedPayload { i32(8) });
    EXPECT_NE(first.tag(), second.tag());
    EXPECT_EQ(first.as_First().value, i32(3));
    EXPECT_EQ(second.as_Second().value, i32(8));
    first = second;
    EXPECT_TRUE(first.is_Second());
    EXPECT_EQ(first.as_Second().value, i32(8));
}

TEST(EnumMacro, ReplacesPayloadsThroughThePublicInterface) {
    auto value = Message::Unit();
    value.replace_Tuple(i32(5), "tuple"_Str);
    ASSERT_TRUE(value.is_Tuple());
    EXPECT_EQ(value.as_Tuple().field_0, i32(5));
    value.replace_Named(i32(6), "record"_Str);
    ASSERT_TRUE(value.is_Named());
    EXPECT_EQ(value.as_Named().count, i32(6));
    value.replace_EmptyRecord();
    EXPECT_TRUE(value.is_EmptyRecord());
    value.replace_Unit();
    EXPECT_TRUE(value.is_Unit());
}
