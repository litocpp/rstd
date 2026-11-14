#include <rstd/test/gtest.hpp>

import rstd.tests.enum_macro.fixture;
import rstd;
import rstd.json;
import rstd.serde.token;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace enum_macro::fixture;

static_assert(rstd::serde::Serializable<EncodeOnly> && ! rstd::serde::Deserializable<EncodeOnly>);
static_assert(! rstd::serde::Serializable<DecodeOnly> && rstd::serde::Deserializable<DecodeOnly>);
static_assert(rstd::serde::Serializable<EncodeOnly::Value_payload> &&
              ! rstd::serde::Deserializable<EncodeOnly::Value_payload>);
static_assert(! rstd::serde::Serializable<DecodeOnly::Value_payload> &&
              rstd::serde::Deserializable<DecodeOnly::Value_payload>);

TEST(EnumMacroSerde, EncodesUnitNewtypeTupleAndRecordUsingTheirOwnRepresentation) {
    EXPECT_EQ(rstd::json::encode(External::Unit()).unwrap().as_str(), R"("Unit")"_str);
    EXPECT_EQ(rstd::json::encode(External::Text("wire"_Str)).unwrap().as_str(),
              R"({"Text":"wire"})"_str);
    EXPECT_EQ(rstd::json::encode(External::Pair(i32(3), "wire"_Str)).unwrap().as_str(),
              R"({"Pair":[3,"wire"]})"_str);
    auto record       = rstd::json::encode(External::Record("wire"_Str, i32(3))).unwrap();
    auto record_value = rstd::json::from_str(record.as_str()).unwrap();
    EXPECT_TRUE(record_value ==
                rstd::json::from_str(R"({"Record":{"name":"wire","count":3}})"_str).unwrap());
    EXPECT_EQ(rstd::json::encode(External::EmptyTuple()).unwrap().as_str(),
              R"({"EmptyTuple":[]})"_str);
    EXPECT_EQ(rstd::json::encode(External::EmptyRecord()).unwrap().as_str(),
              R"({"EmptyRecord":{}})"_str);
}

TEST(EnumMacroSerde, DecodesEveryOwnedPayloadWithoutReparsingItsDeclaration) {
    auto text = rstd::json::decode<External>(R"({"Text":"wire"})"_str).unwrap();
    EXPECT_EQ(text.as_Text().field_0.as_str(), "wire"_str);
    auto pair = rstd::json::decode<External>(R"({"Pair":[3,"wire"]})"_str).unwrap();
    EXPECT_EQ(pair.as_Pair().field_0, i32(3));
    EXPECT_EQ(pair.as_Pair().field_1.as_str(), "wire"_str);
    auto record =
        rstd::json::decode<External>(R"({"Record":{"count":4,"name":"wire"}})"_str).unwrap();
    EXPECT_EQ(record.as_Record().name.as_str(), "wire"_str);
    EXPECT_EQ(record.as_Record().count, i32(4));
    EXPECT_TRUE(rstd::json::decode<External>(R"("Unit")"_str).unwrap().is_Unit());
    EXPECT_TRUE(rstd::json::decode<External>(R"({"EmptyTuple":[]})"_str).unwrap().is_EmptyTuple());
    EXPECT_TRUE(
        rstd::json::decode<External>(R"({"EmptyRecord":{}})"_str).unwrap().is_EmptyRecord());
}

TEST(EnumMacroSerde, TokenRepresentationPreservesEmptyTupleAndRecordShapes) {
    using rstd::serde::TokenKind;
    auto unit   = rstd::serde::to_tokens(External::Unit()).unwrap();
    auto tuple  = rstd::serde::to_tokens(External::EmptyTuple()).unwrap();
    auto record = rstd::serde::to_tokens(External::EmptyRecord()).unwrap();
    EXPECT_EQ(unit[usize()].kind(), TokenKind::EnumStart);
    EXPECT_EQ(unit[usize(2)].kind(), TokenKind::Unit);
    EXPECT_EQ(tuple[usize(2)].kind(), TokenKind::SequenceStart);
    EXPECT_EQ(record[usize(2)].kind(), TokenKind::MapStart);
    EXPECT_TRUE(rstd::serde::from_tokens<External>(unit.as_slice()).unwrap().is_Unit());
    EXPECT_TRUE(rstd::serde::from_tokens<External>(tuple.as_slice()).unwrap().is_EmptyTuple());
    EXPECT_TRUE(rstd::serde::from_tokens<External>(record.as_slice()).unwrap().is_EmptyRecord());
}

TEST(EnumMacroSerde, RejectsWrongTupleArityAndMismatchedPayloadShapes) {
    const ref<str> invalid[] = {
        R"({"Pair":[1]})"_str,
        R"({"Pair":[1,"wire",2]})"_str,
        R"({"Pair":{"field_0":1,"field_1":"wire"}})"_str,
        R"({"Text":{"field_0":"wire"}})"_str,
        R"({"Text":null})"_str,
        R"({"EmptyTuple":{}})"_str,
        R"({"EmptyRecord":[]})"_str,
        R"({"Record":{"name":"wire"}})"_str,
        R"({"Record":{"name":"wire","count":1,"extra":2}})"_str,
        R"({"Unknown":null})"_str,
    };
    for (auto input : invalid) EXPECT_TRUE(rstd::json::decode<External>(input).is_err());
}

TEST(EnumMacroSerde, InternallyTaggedRecordsRespectTagOrderAndStrictness) {
    auto original = Internal::Record("wire"_Str);
    auto encoded  = rstd::json::encode(original).unwrap();
    EXPECT_EQ(rstd::json::decode<Internal>(encoded.as_str()).unwrap().as_Record().name.as_str(),
              "wire"_str);
    EXPECT_EQ(rstd::json::decode<Internal>(R"({"name":"wire","kind":"Record"})"_str)
                  .unwrap()
                  .as_Record()
                  .name.as_str(),
              "wire"_str);
    EXPECT_TRUE(rstd::json::decode<Internal>(R"({"kind":"Unit"})"_str).unwrap().is_Unit());
    const ref<str> invalid[] = {
        R"({"kind":"Record"})"_str,
        R"({"kind":"Record","name":null})"_str,
        R"({"kind":"Record","name":"wire","extra":true})"_str,
        R"({"kind":"Unit","extra":null})"_str,
        R"({"kind":"Unknown"})"_str,
        R"({"name":"wire"})"_str,
    };
    for (auto input : invalid) EXPECT_TRUE(rstd::json::decode<Internal>(input).is_err());
}

TEST(EnumMacroSerde, AdjacentlyTaggedValuesKeepNullAndMissingContentDistinct) {
    auto original  = Adjacent::Text("wire"_Str);
    auto encoded   = rstd::json::encode(original).unwrap();
    auto roundtrip = rstd::json::from_str(encoded.as_str()).unwrap();
    EXPECT_TRUE(roundtrip == rstd::json::from_str(R"({"kind":"Text","data":"wire"})"_str).unwrap());
    EXPECT_EQ(rstd::json::decode<Adjacent>(R"({"data":"wire","kind":"Text"})"_str)
                  .unwrap()
                  .as_Text()
                  .field_0.as_str(),
              "wire"_str);
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Unit"})"_str).unwrap().is_Unit());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Optional","data":null})"_str)
                    .unwrap()
                    .as_Optional()
                    .field_0.is_none());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Optional"})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Text","data":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Unit","extra":null})"_str).is_err());
}

TEST(EnumMacroSerde, ForwardsFieldHelpersAndRootDirectionToOwnedPayloads) {
    auto record =
        rstd::json::decode<FieldOptions>(R"({"Record":{"wire_name":"renamed"}})"_str).unwrap();
    EXPECT_EQ(record.as_Record().name.as_str(), "renamed"_str);
    EXPECT_TRUE(record.as_Record().note.is_none());
    auto encoded   = rstd::json::encode(record).unwrap();
    auto roundtrip = rstd::json::from_str(encoded.as_str()).unwrap();
    EXPECT_TRUE(
        roundtrip ==
        rstd::json::from_str(R"({"Record":{"wire_name":"renamed","note":null}})"_str).unwrap());
    auto decode_only = rstd::json::decode<DecodeOnly>(R"({"Value":"direction"})"_str).unwrap();
    EXPECT_EQ(decode_only.as_Value().field_0.as_str(), "direction"_str);
    EXPECT_EQ(rstd::json::encode(EncodeOnly::Value("direction"_Str)).unwrap().as_str(),
              R"({"Value":"direction"})"_str);
}

TEST(EnumMacroSerde, ReportsDuplicateRecordFieldsThroughStructuredTokenInput) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    Vec<Token> tokens;
    tokens.push(Token::marker(TokenKind::EnumStart));
    tokens.push(Token::string("Record"_str));
    tokens.push(Token::container(TokenKind::MapStart, usize(3)));
    tokens.push(Token::string("name"_str));
    tokens.push(Token::string("first"_str));
    tokens.push(Token::string("name"_str));
    tokens.push(Token::string("second"_str));
    tokens.push(Token::string("count"_str));
    tokens.push(Token::signed_integer(i64(1)));
    tokens.push(Token::marker(TokenKind::MapEnd));
    tokens.push(Token::marker(TokenKind::EnumEnd));
    auto result = rstd::serde::from_tokens<External>(tokens.as_slice());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::DuplicateField);
}
