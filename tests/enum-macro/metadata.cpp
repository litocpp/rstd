#include <rstd/test/gtest.hpp>
#include <type_traits>

import rstd.tests.enum_macro.fixture;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;

using TupleDescription       = rstd::PayloadDefinition<Message::Tuple_payload>;
using RecordDescription      = rstd::PayloadDefinition<Message::Named_payload>;
using EmptyTupleDescription  = rstd::PayloadDefinition<Message::EmptyTuple_payload>;
using EmptyRecordDescription = rstd::PayloadDefinition<Message::EmptyRecord_payload>;

static_assert(Message::Unit_variant::kind == rstd::VariantKind::Unit);
static_assert(Message::Tuple_variant::kind == rstd::VariantKind::Tuple);
static_assert(Message::Named_variant::kind == rstd::VariantKind::Record);
static_assert(DuplicatePayload::First_variant::kind == rstd::VariantKind::External);
static_assert(std::is_void_v<Message::Unit_variant::payload_type>);
static_assert(std::is_same_v<DuplicatePayload::First_variant::payload_type, SharedPayload>);
static_assert(TupleDescription::kind == rstd::VariantKind::Tuple);
static_assert(RecordDescription::kind == rstd::VariantKind::Record);
static_assert(EmptyTupleDescription::kind == rstd::VariantKind::Tuple);
static_assert(EmptyRecordDescription::kind == rstd::VariantKind::Record);
static_assert(TupleDescription::field_count == 2);
static_assert(RecordDescription::field_count == 2);
static_assert(EmptyTupleDescription::field_count == 0);
static_assert(EmptyRecordDescription::field_count == 0);
static_assert(std::is_same_v<TupleDescription::field_at<0>::type, i32>);
static_assert(std::is_same_v<RecordDescription::field_at<1>::type, String>);

TEST(EnumMacroMetadata, DescribesFieldsInDeclarationOrderWithActualNames) {
    EXPECT_EQ(TupleDescription::field_at<0>::name(), "0"_str);
    EXPECT_EQ(TupleDescription::field_at<1>::name(), "1"_str);
    EXPECT_EQ(RecordDescription::field_at<0>::name(), "count"_str);
    EXPECT_EQ(RecordDescription::field_at<1>::name(), "text"_str);
    auto value = RecordDescription::make(i32(3), "metadata"_Str);
    EXPECT_EQ(RecordDescription::field_at<0>::get(value), i32(3));
    RecordDescription::field_at<0>::get(value) = i32(4);
    EXPECT_EQ(value.count, i32(4));
    int visited = 0;
    RecordDescription::fields([&]<typename Field>() {
        if (visited == 0) EXPECT_EQ(Field::name(), "count"_str);
        if (visited == 1) EXPECT_EQ(Field::name(), "text"_str);
        ++visited;
    });
    EXPECT_EQ(visited, 2);
}

TEST(EnumMacroMetadata, PublicVariantDescriptionSelectsTheActiveEntry) {
    const auto value         = Message::Named(i32(5), "entry"_Str);
    int        visited       = 0;
    int        matched_count = 0;
    rstd::Impl<rstd::VariantDescription, Message>::variants([&]<typename Entry>() {
        ++visited;
        if (Entry::matches(value)) {
            ++matched_count;
            if constexpr (! std::is_void_v<typename Entry::payload_type>) {
                EXPECT_EQ(rstd::format("{:?}", Entry::get(value)),
                          rstd::format("{:?}", value.as_Named()));
            }
        }
    });
    EXPECT_EQ(visited, 6);
    EXPECT_EQ(matched_count, 1);
}
