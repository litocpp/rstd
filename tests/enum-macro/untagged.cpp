#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;
import rstd.json;
import rstd.serde.token;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;

static_assert(! __is_constructible(ItemRef));
static_assert(! __is_constructible(RequiredReference));

template<typename T>
concept TemporaryReplay = requires(T&& captured) { rstd::move(captured).replay(); };
static_assert(! TemporaryReplay<rstd::serde::CapturedTokens>);
static_assert(! TemporaryReplay<rstd::json::CapturedValue>);

TEST(EnumMacroUntagged, BarePayloadsReplayInDeclarationOrder) {
    EXPECT_EQ(rstd::json::encode(ItemRef::Id(i64(42))).unwrap().as_str(), "42"_str);
    auto id = rstd::json::decode<ItemRef>("42"_str).unwrap();
    EXPECT_EQ(id.as_Id().field_0, i64(42));
    auto object = rstd::json::decode<ItemRef>(R"({"id":3,"name":"three"})"_str).unwrap();
    EXPECT_EQ(object.as_Object().id, i64(3));
    EXPECT_EQ(object.as_Object().name.as_str(), "three"_str);
    EXPECT_TRUE(rstd::json::decode<Number>("42"_str).unwrap().is_Integer());
    EXPECT_EQ(rstd::json::decode<Number>("4.5"_str).unwrap().as_Floating().field_0, f64(4.5));
    EXPECT_TRUE(rstd::json::decode_direct<Number>("4.5"_str).unwrap().is_Floating());
    auto sequence = rstd::json::decode_direct<Vec<Number>>("[4.5,2]"_str).unwrap();
    EXPECT_TRUE(sequence[usize()].is_Floating());
    EXPECT_TRUE(sequence[usize(1)].is_Integer());
    auto value  = rstd::json::from_str(R"({"shared":4,"second":"ok"})"_str).unwrap();
    auto second = rstd::json::decode_value<ObjectChoice>(value).unwrap();
    EXPECT_EQ(second.as_Second().second.as_str(), "ok"_str);
    EXPECT_TRUE(
        rstd::json::decode<ObjectChoice>(R"({"shared":4,"second":"ok"})"_str).unwrap().is_Second());
    EXPECT_TRUE(rstd::json::decode_direct<ObjectChoice>(R"({"shared":4,"second":"ok"})"_str)
                    .unwrap()
                    .is_Second());
}

TEST(EnumMacroUntagged, TokenCaptureAdvancesOneValueAndReplaysIndependentCursors) {
    auto tokens = rstd::serde::to_tokens(ObjectChoice::Second(i64(4), "ok"_Str)).unwrap();
    auto length = tokens.len();
    tokens.push(rstd::serde::Token::signed_integer(i64(9)));
    auto source = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto result = rstd::serde::deserialize<ObjectChoice>(source).unwrap();
    EXPECT_TRUE(result.is_Second());
    EXPECT_EQ(source.position(), length);
    EXPECT_EQ(rstd::serde::deserialize<i64>(source).unwrap(), i64(9));
    auto number = rstd::serde::to_tokens(f64(4.5)).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<Number>(number.as_slice()).unwrap().is_Floating());
}

TEST(EnumMacroUntagged, RequiredNondefaultFieldsPreserveInitializersNullAndValidation) {
    auto value = rstd::json::decode<RequiredReference>(R"({"item":4})"_str).unwrap();
    EXPECT_TRUE(value.item.is_Id());
    EXPECT_TRUE(value.enabled);
    EXPECT_EQ(value.note->as_str(), "fallback"_str);
    EXPECT_EQ(value.count, u64(7));
    EXPECT_EQ(value.fallback.as_Id().field_0, i64(13));
    EXPECT_EQ(value.local, i64(12));
    auto present =
        rstd::json::decode<RequiredReference>(
            R"({"legacy":{"id":2,"name":"two"},"note":null,"enabled":false,"count":0})"_str)
            .unwrap();
    EXPECT_TRUE(present.item.is_Object());
    EXPECT_TRUE(present.note.is_none());
    EXPECT_FALSE(present.enabled);
    EXPECT_EQ(present.count, u64());
    EXPECT_EQ(present.local, i64(24));
    auto tokens = rstd::serde::to_tokens(present).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<RequiredReference>(tokens.as_slice()).is_ok());
    EXPECT_TRUE(rstd::json::decode<NestedReference>(R"({"Object":{"item":4}})"_str)
                    .unwrap()
                    .as_Object()
                    .item.is_Id());
    EXPECT_TRUE(rstd::json::decode<RequiredReference>(R"({"item":4,"count":99})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<RequiredReference>(R"({"item":4,"legacy":5})"_str).is_err());
    auto missing = rstd::json::decode<RequiredOrder>("{}"_str).unwrap_err();
    EXPECT_EQ(missing.data_error()->get().kind(), rstd::serde::ErrorKind::MissingField);
    EXPECT_EQ(missing.data_error()->get().message().unwrap(), "first"_str);
}

TEST(EnumMacroUntagged, RejectsDuplicateUnknownAndMalformedInputs) {
    EXPECT_TRUE(rstd::json::decode<ItemRef>(R"({"id":1,"id":2,"name":"x"})"_str,
                                            { .reject_duplicate_keys = true })
                    .is_err());
    EXPECT_TRUE(rstd::json::decode_direct<ItemRef>(R"({"id":1,"id":2,"name":"x"})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<ItemRef>(R"({"id":1,"name":"x","extra":0})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<ItemRef>(R"({"id":1,"name":)"_str).is_err());
    auto failure = rstd::json::decode<RequiredReference>(R"({"item":false})"_str).unwrap_err();
    EXPECT_EQ(failure.data_error()->get().kind(), rstd::serde::ErrorKind::InvalidValue);
    EXPECT_TRUE(failure.data_error()->get().source().is_some());
    auto path = failure.data_error()->get().path().segments();
    ASSERT_EQ(path.len(), usize(1));
    EXPECT_EQ(path[usize()].name().unwrap(), "item"_str);
    auto tokens = rstd::serde::to_tokens(ItemRef::Object(i64(1), "one"_Str)).unwrap();
    tokens.pop();
    EXPECT_TRUE(rstd::serde::from_tokens<ItemRef>(tokens.as_slice()).is_err());
}

TEST(EnumMacroUntagged, FormatsWithoutCaptureReportUnsupportedWithoutConsuming) {
    struct NoCapture : rstd::serde::TokenDeserializer {
        using TokenDeserializer::TokenDeserializer;
        auto capture_value() = delete;
    };
    static_assert(rstd::serde::Deserializer<NoCapture>);
    static_assert(! rstd::serde::CapturingDeserializer<NoCapture>);
    auto tokens  = rstd::serde::to_tokens(i64(1)).unwrap();
    auto source  = NoCapture(tokens.as_slice());
    auto failure = rstd::serde::deserialize<ItemRef>(source).unwrap_err();
    EXPECT_EQ(failure.kind(), rstd::serde::ErrorKind::Unsupported);
    EXPECT_EQ(source.position(), usize());
}

TEST(EnumMacroUntagged, ValidatesFinalEnumExactlyOnceAfterBranchSelection) {
    static_assert(! rstd::serde::Serializable<ValidatedRef>);
    ReferenceValidation::calls = 0;
    EXPECT_TRUE(rstd::json::decode<ValidatedRef>("3"_str).unwrap().is_Id());
    EXPECT_TRUE(rstd::json::decode<ValidatedRef>(R"({"type":1})"_str).unwrap().is_Key());
    EXPECT_EQ(ReferenceValidation::calls, 2u);
    auto rejected = rstd::json::decode_direct<ValidatedRef>(R"({"type":2})"_str).unwrap_err();
    EXPECT_EQ(rejected.kind(), rstd::serde::ErrorKind::InvalidValue);
    EXPECT_EQ(rejected.message().unwrap(), "invalid key type"_str);
    EXPECT_EQ(ReferenceValidation::calls, 3u);
    EXPECT_TRUE(rstd::json::decode<ValidatedRef>("false"_str).is_err());
    EXPECT_EQ(ReferenceValidation::calls, 3u);
    auto tokens = rstd::serde::to_tokens(i64(3)).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<ValidatedRef>(tokens.as_slice()).is_ok());
    EXPECT_EQ(ReferenceValidation::calls, 4u);
}

TEST(EnumMacroUntagged, PreservesUnitTupleAndEmptyContainerShapes) {
    EXPECT_TRUE(rstd::json::decode_direct<Shapes>("null"_str).unwrap().is_Unit());
    EXPECT_TRUE(rstd::json::decode_direct<Shapes>("[]"_str).unwrap().is_Array());
    EXPECT_TRUE(rstd::json::decode_direct<Shapes>("{}"_str).unwrap().is_Record());
    EXPECT_TRUE(rstd::json::decode_direct<Shapes>(R"([1,"one"])"_str).unwrap().is_Tuple());
    auto tokens = rstd::serde::to_tokens(Shapes::Tuple(i64(1), "one"_Str)).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<Shapes>(tokens.as_slice()).unwrap().is_Tuple());
    EXPECT_EQ(rstd::json::encode(Shapes::Unit()).unwrap().as_str(), "null"_str);
    EXPECT_EQ(rstd::json::encode(Shapes::Array(Vec<i64> {})).unwrap().as_str(), "[]"_str);
}
