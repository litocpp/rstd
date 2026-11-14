#include <rstd/enum.hpp>
#include <rstd/test/gtest.hpp>

import rstd.core;
import rstd.alloc;

using namespace rstd::prelude;
using namespace rstd::literals;
using alloc::string::String;

namespace derive_tests::payloads
{
template<typename T>
struct InternalTagAdlProbe {
    static_assert(! rstd::mtp::same_as<T, T>,
                  "internal default tags must not instantiate their associated payloads");
};
template<typename Tag>
constexpr auto rejects_internal_tag() -> bool {
    return ! rstd::derive::DerivedClone<Tag> && ! rstd::derive::DerivedDebug<Tag> &&
           ! rstd::derive::DerivedError<Tag> && ! rstd::derive::DerivedDisplay<Tag> &&
           ! rstd::derive::DerivedFromStr<Tag>;
}
static_assert(rejects_internal_tag<rstd::default_tag<InternalTagAdlProbe<int>>>());
static_assert(rejects_internal_tag<rstd::in_class_default_tag<InternalTagAdlProbe<int>>>());

struct Unit {};
struct EmptyTuple {};
struct EmptyRecord {};
template<typename T>
struct Tuple {
    T label;
};
struct Record {
    String text;
    i32    number;
};
constexpr auto rstd_payload_definition(Unit*) {
    return rstd::VariantPayload<Unit, rstd::VariantKind::Unit, "Unit"> {};
}
constexpr auto rstd_payload_definition(EmptyTuple*) {
    return rstd::VariantPayload<EmptyTuple, rstd::VariantKind::Tuple, "EmptyTuple"> {};
}
constexpr auto rstd_payload_definition(EmptyRecord*) {
    return rstd::VariantPayload<EmptyRecord, rstd::VariantKind::Record, "EmptyRecord"> {};
}
template<typename T>
constexpr auto rstd_payload_definition(Tuple<T>*) {
    return rstd::VariantPayload<Tuple<T>,
                                rstd::VariantKind::Tuple,
                                "Tuple",
                                rstd::PayloadField<"label", &Tuple<T>::label>> {};
}
constexpr auto rstd_payload_definition(Record*) {
    return rstd::VariantPayload<Record,
                                rstd::VariantKind::Record,
                                "Record",
                                rstd::PayloadField<"text", &Record::text>,
                                rstd::PayloadField<"number", &Record::number>> {};
}
constexpr auto rstd_debug_definition(Unit*) {
    return rstd::derive::DebugPayload<Unit> {};
}
constexpr auto rstd_debug_definition(EmptyTuple*) {
    return rstd::derive::DebugPayload<EmptyTuple> {};
}
constexpr auto rstd_debug_definition(EmptyRecord*) {
    return rstd::derive::DebugPayload<EmptyRecord> {};
}
template<typename T>
constexpr auto rstd_debug_definition(Tuple<T>*) {
    return rstd::derive::DebugPayload<Tuple<T>> {};
}
constexpr auto rstd_debug_definition(Record*) {
    return rstd::derive::DebugPayload<Record> {};
}
template<typename T>
constexpr auto rstd_clone_definition(Tuple<T>*) {
    return rstd::derive::ClonePayload<Tuple<T>> {};
}
constexpr auto rstd_clone_definition(Record*) {
    return rstd::derive::ClonePayload<Record> {};
}

class [[pmacro::derive("rstd::Debug", "rstd::Clone"),
        pmacro::helper("rstd::Debug", variants),
        pmacro::helper("rstd::Clone", variants)]] Event {
    RSTD_ENUM_TYPES(Event, (Idle), (NamedTuple, (Tuple<i32>)), (NamedRecord, (Record)))
};

static_assert(rstd::DescribedPayload<Tuple<String>>);
static_assert(rstd::PayloadDefinition<Record>::field_count == 2);
static_assert(rstd::mtp::same_as<rstd::PayloadDefinition<Record>::field_at<1>::type, i32>);
static_assert(rstd::mtp::same_as<decltype(rstd::PayloadDefinition<Record>::field_at<0>::get(
                                     rstd::declval<const Record&>())),
                                 const String&>);

TEST(PayloadMetadata, OrderedFieldsAndTypedAccess) {
    auto   value = rstd::PayloadDefinition<Record>::make("hello"_Str, i32(3));
    String names;
    rstd::PayloadDefinition<Record>::fields([&]<typename Field>() {
        names.push_str(Field::name());
    });
    EXPECT_EQ(names.as_str(), "textnumber"_str);
    rstd::PayloadDefinition<Record>::field_at<1>::get(value) = i32(7);
    EXPECT_EQ(value.number, i32(7));
}

TEST(PayloadMetadata, DebugDistinguishesUnitTupleAndRecord) {
    EXPECT_EQ(rstd::format("{:?}", Unit {}), "Unit"_str);
    EXPECT_EQ(rstd::format("{:?}", EmptyTuple {}), "EmptyTuple()"_str);
    EXPECT_EQ(rstd::format("{:?}", EmptyRecord {}), "EmptyRecord {}"_str);
    EXPECT_EQ(rstd::format("{:?}", Tuple<i32> { i32(4) }), "Tuple(4)"_str);
    EXPECT_EQ(rstd::format("{:?}", Record { "hello"_Str, i32(4) }),
              "Record { text: \"hello\", number: 4 }"_str);
    EXPECT_EQ(rstd::format("{:#?}", Tuple<i32> { i32(4) }), "Tuple(\n    4,\n)"_str);
}

TEST(PayloadMetadata, VariantDebugUsesOuterNameAndCloneOwnsFields) {
    auto value = Event::NamedRecord("hello"_Str, i32(4));
    EXPECT_EQ(rstd::format("{:?}", value), "NamedRecord { text: \"hello\", number: 4 }"_str);
    EXPECT_EQ(rstd::format("{:?}", Event::NamedTuple(i32(2))), "NamedTuple(2)"_str);
    auto copy                  = rstd::as<rstd::clone::Clone>(value).clone();
    copy.as_NamedRecord().text = "copy"_Str;
    EXPECT_EQ(value.as_NamedRecord().text.as_str(), "hello"_str);
    EXPECT_EQ(copy.as_NamedRecord().text.as_str(), "copy"_str);
    const auto tuple = Tuple<String> { "tuple"_Str };
    EXPECT_EQ(rstd::as<rstd::clone::Clone>(tuple).clone().label.as_str(), "tuple"_str);
}
} // namespace derive_tests::payloads
