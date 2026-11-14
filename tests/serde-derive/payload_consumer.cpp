#include <rstd/test/gtest.hpp>
import rstd.tests.serde_payload;
import rstd.json;
import rstd.serde.token;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace serde_payload;
using rstd::tuple;

struct NotDebug {};
static_assert(rstd::Impled<tuple<>, rstd::fmt::Debug>);
static_assert(rstd::Impled<tuple<i32, const i32&>, rstd::fmt::Debug>);
static_assert(! rstd::Impled<tuple<NotDebug>, rstd::fmt::Debug>);

TEST(CoreDebug, TupleUsesOrdinaryTupleDelimitersAndPrettyFormatting) {
    EXPECT_EQ(rstd::format("{:?}", tuple<> {}), "()"_str);
    EXPECT_EQ(rstd::format("{:#?}", tuple<> {}), "()"_str);
    EXPECT_EQ(rstd::format("{:?}", tuple<i32> { i32(7) }), "(7,)"_str);
    EXPECT_EQ(rstd::format("{:?}", tuple<i32, String> { i32(7), "tuple"_Str }),
              "(7, \"tuple\")"_str);
    const auto nested = tuple<tuple<i32>, tuple<>> { tuple<i32> { i32(7) }, tuple<> {} };
    EXPECT_EQ(rstd::format("{:?}", nested), "((7,), ())"_str);
    EXPECT_EQ(rstd::format("{:#?}", nested), "(\n    (\n        7,\n    ),\n    (),\n)"_str);
    const auto number = i32(3);
    EXPECT_EQ(rstd::format("{:?}", tuple<const i32&> { number }), "(3,)"_str);
}

static_assert(! rstd::serde::Serializable<Directed> && rstd::serde::Deserializable<Directed>);
static_assert(rstd::mtp::same_as<rstd::serde::Definition<FlexibleExternal>,
                                 rstd::serde::VariantDefinition<FlexibleExternal,
                                                                "serde_payload::FlexibleExternal",
                                                                rstd::serde::Direction::Both>>);

TEST(SerdePayload, CoreAdaptersConsumeMetadataAcrossModuleImports) {
    EXPECT_EQ(rstd::format("{:?}", Unit {}), "Unit"_str);
    EXPECT_EQ(rstd::format("{:?}", EmptyTuple {}), "Tuple()"_str);
    EXPECT_EQ(rstd::format("{:?}", EmptyRecord {}), "Record {}"_str);
    EXPECT_EQ(rstd::format("{:?}", Generic<i32> { i32(7) }), "Generic(7)"_str);
    EXPECT_EQ(rstd::format("{:?}", Pair { u64(7), "pair"_Str }), "Pair(7, \"pair\")"_str);
    const auto value = Pair { u64(7), "pair"_Str };
    auto       copy  = rstd::as<rstd::clone::Clone>(value).clone();
    copy.second      = "copy"_Str;
    EXPECT_EQ(value.second.as_str(), "pair"_str);
    const auto generic = Generic<String> { "generic"_Str };
    EXPECT_EQ(rstd::as<rstd::clone::Clone>(generic).clone().label.as_str(), "generic"_str);
}

TEST(CoreDebug, VariantNameOverridesReachUnitOwnedAndExternalPayloads) {
    EXPECT_EQ(rstd::format("{:?}", DebugNames::Idle()), "idle-label"_str);
    EXPECT_EQ(rstd::format("{:?}", DebugNames::Owned(u64(7), "text"_Str)),
              "OwnedLabel(7, \"text\")"_str);
    EXPECT_EQ(rstd::format("{:?}", DebugNames::External(i32(7))),
              "ExternalLabel { number: 7 }"_str);
    EXPECT_EQ(rstd::format("{:#?}", DebugNames::External(i32(7))),
              "ExternalLabel {\n    number: 7,\n}"_str);
}

struct PayloadDescription {
    int  arity      = -1;
    bool is_unit    = false;
    bool is_integer = false;
    template<typename... Ts>
    void tuple() {
        arity = sizeof...(Ts);
    }
    void unit() { is_unit = true; }
    void unsigned_integer(rstd::size_t) { is_integer = true; }
};

TEST(SerdePayload, DescriptionsKeepTupleArityDistinctFromUnitAndNewtype) {
    auto tuple = PayloadDescription {};
    rstd::serde::describe<Pair>(tuple);
    EXPECT_EQ(tuple.arity, 2);
    auto empty_tuple = PayloadDescription {};
    rstd::serde::describe<EmptyTuple>(empty_tuple);
    EXPECT_EQ(empty_tuple.arity, 0);
    EXPECT_FALSE(empty_tuple.is_unit);
    auto unit = PayloadDescription {};
    rstd::serde::describe<Unit>(unit);
    EXPECT_TRUE(unit.is_unit);
    auto scalar = PayloadDescription {};
    rstd::serde::describe<Scalar>(scalar);
    EXPECT_TRUE(scalar.is_integer);
    EXPECT_EQ(scalar.arity, -1);
}

TEST(SerdePayload, ShapeComesFromTypedMetadata) {
    EXPECT_EQ(rstd::json::encode(Scalar { u64(7) }).unwrap().as_str(), "7"_str);
    EXPECT_EQ(rstd::json::encode(Pair { u64(7), "pair"_Str }).unwrap().as_str(),
              R"([7,"pair"])"_str);
    EXPECT_EQ(rstd::json::decode<Pair>(R"([7,"pair"])"_str).unwrap().second.as_str(), "pair"_str);
    EXPECT_TRUE(rstd::json::decode<Pair>("[7]"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Pair>(R"([7,"pair",8])"_str).is_err());
    EXPECT_EQ(rstd::json::encode(EmptyTuple {}).unwrap().as_str(), "[]"_str);
    EXPECT_EQ(rstd::json::encode(EmptyRecord {}).unwrap().as_str(), "{}"_str);
    EXPECT_EQ(rstd::json::encode(Unit {}).unwrap().as_str(), "null"_str);
    EXPECT_TRUE(rstd::json::decode<EmptyTuple>("[]"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<EmptyRecord>("{}"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<Unit>("null"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<EmptyTuple>("{}"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<EmptyRecord>("[]"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<EmptyTuple>("[1]"_str).is_err());
}

TEST(SerdePayload, RecordOptionsBindMetadataFieldsWithoutChangingOrder) {
    auto value = Fields { u64(7), None(), u64(99) };
    EXPECT_EQ(rstd::json::encode(value).unwrap().as_str(), R"({"note":null,"wire":7})"_str);
    auto tokens = rstd::serde::to_tokens(value).unwrap();
    EXPECT_EQ(tokens[usize(1)].as_str(), "note"_str);
    EXPECT_EQ(tokens[usize(3)].as_str(), "wire"_str);
    EXPECT_EQ(rstd::json::decode<Fields>(R"({"legacy":7})"_str).unwrap().number, u64(7));
    EXPECT_TRUE(rstd::json::decode<Fields>(R"({"wire":7,"extra":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Fields>(R"({"wire":7,"legacy":8})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Fields>(R"({"note":null})"_str).is_err());
    EXPECT_EQ(rstd::json::decode<Inherited>(R"({"value":7,"extra":null})"_str).unwrap().value,
              u64(7));
    EXPECT_EQ(rstd::json::decode<Directed>("7"_str).unwrap().value, u64(7));
}

TEST(SerdePayload, EnumOwnerAppliesWireNamesForEachRepresentation) {
    const auto external = External::Number(u64(7));
    EXPECT_EQ(rstd::json::encode(external).unwrap().as_str(), R"({"number":7})"_str);
    EXPECT_EQ(rstd::json::decode<External>(R"({"number":7})"_str).unwrap().as_Number().label,
              u64(7));
    EXPECT_TRUE(rstd::json::decode<External>(R"({"Number":7})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<External>(R"("idle")"_str).unwrap().is_Idle());
    EXPECT_EQ(rstd::json::encode(External::Tuple()).unwrap().as_str(), R"({"Tuple":[]})"_str);
    EXPECT_EQ(rstd::json::encode(External::Record()).unwrap().as_str(), R"({"Record":{}})"_str);
    EXPECT_TRUE(rstd::json::decode<Internal>(R"({"kind":"fields","wire":7})"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<Internal>(R"({"kind":"Fields","wire":7})"_str).is_err());
    EXPECT_TRUE(
        rstd::json::decode<FlexibleInternal>(R"({"kind":"Fields","wire":7,"extra":null})"_str)
            .is_err());
    EXPECT_TRUE(
        rstd::json::decode<FlexibleInternal>(R"({"kind":"Idle","extra":null})"_str).is_ok());
    EXPECT_EQ(rstd::json::encode(Adjacent::Number(u64(7))).unwrap().as_str(),
              R"({"data":7,"kind":"number"})"_str);
    EXPECT_EQ(rstd::json::decode<Adjacent>(R"({"kind":"number","data":7})"_str)
                  .unwrap()
                  .as_Number()
                  .label,
              u64(7));
    auto tokens = rstd::serde::to_tokens(external).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<External>(tokens.as_slice()).unwrap().is_Number());
    EXPECT_EQ(rstd::json::decode<FlexibleExternal>(R"({"Inherited":{"value":7,"extra":null}})"_str)
                  .unwrap()
                  .as_Inherited()
                  .value,
              u64(7));
}
