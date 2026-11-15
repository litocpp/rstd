#include <rstd/test/gtest.hpp>
import rstd.tests.serde_derive;
import rstd.json;
import rstd.serde.token;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;
using namespace serde_derive::nested;

namespace serde_derive::nested
{
auto decode_in_peer(ref<str> input) -> Result<Adjacent, rstd::json::DecodeError>;
auto decode_factory_in_peer(ref<str> input) -> Result<FactoryRecord, rstd::json::DecodeError>;
} // namespace serde_derive::nested

static_assert(rstd::serde::Serializable<EncodeOnly> && ! rstd::serde::Deserializable<EncodeOnly>);
static_assert(! rstd::serde::Serializable<DecodeOnly> && rstd::serde::Deserializable<DecodeOnly>);
static_assert(rstd::serde::Serializable<EncodeVariants> &&
              ! rstd::serde::Deserializable<EncodeVariants>);
static_assert(! rstd::serde::Serializable<DecodeVariants> &&
              rstd::serde::Deserializable<DecodeVariants>);

TEST(SerdeDerive, NewtypeIsScalarAndPreservesNull) {
    EXPECT_EQ(rstd::json::encode(Identifier { u64(42) }).unwrap().as_str(), "42"_str);
    EXPECT_EQ(rstd::json::decode<Identifier>("42"_str).unwrap().value, u64(42));
    EXPECT_TRUE(rstd::json::decode<Identifier>(R"({"value":42})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<Identifier>("null"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<OptionalText>("null"_str).unwrap().value.is_none());
    auto tokens = rstd::serde::to_tokens(Identifier { u64(7) }).unwrap();
    EXPECT_EQ(rstd::serde::from_tokens<Identifier>(tokens.as_slice()).unwrap().value, u64(7));
}

TEST(SerdeDerive, UntaggedLegacyPayloadRetainsValidationErrorAsSource) {
    FactoryState::validations = 0;
    auto value = rstd::json::decode_direct<Untagged>(R"({"name":"ok","wire":1})"_str).unwrap();
    EXPECT_TRUE(value.is_Record());
    EXPECT_EQ(FactoryState::validations, 1u);
    auto failure =
        rstd::json::decode_direct<Untagged>(R"({"name":"bad","wire":99})"_str).unwrap_err();
    EXPECT_EQ(failure.kind(), rstd::serde::ErrorKind::InvalidValue);
    ASSERT_TRUE(failure.source().is_some());
    auto cause = rstd::error::downcast_ref<rstd::serde::Error>(*failure.source());
    ASSERT_TRUE(cause.is_some());
    EXPECT_EQ(cause->get().kind(), rstd::serde::ErrorKind::InvalidValue);
    EXPECT_EQ(cause->get().message().unwrap(), "count must not be 99"_str);
    EXPECT_EQ(FactoryState::validations, 2u);
}

TEST(SerdeDerive, UntaggedCaptureRetainsFilteredTagsAndStrictRecordPolicy) {
    auto valid = rstd::json::decode<StrictUntagged>(R"({"kind":"Value","name":"ok"})"_str).unwrap();
    EXPECT_TRUE(valid.is_Value());
    EXPECT_TRUE(rstd::json::decode<StrictUntagged>(R"({"kind":"Value","name":"ok","extra":0})"_str)
                    .is_err());
    EXPECT_TRUE(
        rstd::json::decode<FlexibleUntagged>(R"({"kind":"Value","name":"ok","extra":0})"_str)
            .is_ok());
    auto tokens = rstd::serde::to_tokens(valid).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<StrictUntagged>(tokens.as_slice()).is_ok());
    auto extra = rstd::json::from_str(R"({"kind":"Value","name":"ok","extra":0})"_str).unwrap();
    auto extra_tokens = rstd::serde::to_tokens(extra).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<StrictUntagged>(extra_tokens.as_slice()).is_err());
    EXPECT_TRUE(rstd::serde::from_tokens<FlexibleUntagged>(extra_tokens.as_slice()).is_ok());
}

TEST(SerdeDerive, StringEnumRemainsDistinctFromUnitEnum) {
    auto string      = rstd::serde::to_tokens(StringState::Ready).unwrap();
    auto enumeration = rstd::serde::to_tokens(EnumState::Ready).unwrap();
    EXPECT_EQ(string[usize()].kind(), rstd::serde::TokenKind::String);
    EXPECT_EQ(enumeration[usize()].kind(), rstd::serde::TokenKind::EnumStart);
    EXPECT_TRUE(rstd::serde::from_tokens<StringState>(enumeration.as_slice()).is_err());
    EXPECT_TRUE(rstd::serde::from_tokens<EnumState>(string.as_slice()).is_err());
    EXPECT_EQ(rstd::json::encode(StringState::Ready).unwrap().as_str(), R"("ready")"_str);
    EXPECT_TRUE(rstd::json::decode<StringState>(R"("Ready")"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<StringState>(R"({"ready":null})"_str).is_err());
    EXPECT_EQ(rstd::json::decode<EnumState>(R"("ready")"_str).unwrap(), EnumState::Ready);
}

TEST(SerdeDerive, ExternalVariantsReusePayloadDefinitions) {
    auto original = External::Text(String::make("wire"_str));
    auto tokens   = rstd::serde::to_tokens(original).unwrap();
    EXPECT_EQ(
        rstd::serde::from_tokens<External>(tokens.as_slice()).unwrap().as_Text().value.as_str(),
        "wire"_str);
    auto encoded = rstd::json::encode(original).unwrap();
    EXPECT_EQ(encoded.as_str(), R"({"Text":"wire"})"_str);
    EXPECT_EQ(rstd::json::decode<External>(encoded.as_str()).unwrap().as_Text().value.as_str(),
              "wire"_str);
    EXPECT_TRUE(rstd::json::decode<External>(R"("Idle")"_str).unwrap().is_Idle());
    EXPECT_TRUE(rstd::json::decode<External>(R"({"Text":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<External>(R"({"text":"wire"})"_str).is_err());
}

TEST(SerdeDerive, InternalVariantsKeepUnknownAndPayloadRules) {
    auto original = Internal::Record(String::make("wire"_str), None());
    auto encoded  = rstd::json::encode(original).unwrap();
    EXPECT_EQ(rstd::json::decode<Internal>(encoded.as_str()).unwrap().as_Record().name.as_str(),
              "wire"_str);
    EXPECT_TRUE(rstd::json::decode<Internal>(R"({"name":"wire","kind":"Record"})"_str).is_ok());
    const ref<str> invalid[] = {
        R"({"kind":"Record"})"_str,
        R"({"kind":"Record","name":null})"_str,
        R"({"kind":"Record","name":"wire","other":null})"_str,
        R"({"kind":"Unknown","name":"wire"})"_str,
        R"({"kind":null})"_str,
        R"({"kind":"Idle","other":null})"_str,
    };
    for (auto input : invalid) EXPECT_TRUE(rstd::json::decode<Internal>(input).is_err());
    EXPECT_TRUE(
        rstd::json::decode<FlexibleInternal>(R"({"kind":"Record","name":"wire","other":null})"_str)
            .is_ok());
    auto tokens = rstd::serde::to_tokens(original).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<Internal>(tokens.as_slice()).unwrap().is_Record());
}

TEST(SerdeDerive, AdjacentVariantsKeepTagAndContentRulesAcrossImporters) {
    auto original = Adjacent::Text(String::make("wire"_str));
    auto encoded  = rstd::json::encode(original).unwrap();
    EXPECT_EQ(decode_in_peer(encoded.as_str()).unwrap().as_Text().value.as_str(), "wire"_str);
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"data":"wire","kind":"Text"})"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Idle"})"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Idle","data":null})"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<Adjacent>(R"({"kind":"Optional","data":null})"_str)
                    .unwrap()
                    .as_Optional()
                    .value.is_none());
    const ref<str> invalid[] = {
        R"({"kind":"Text"})"_str,           R"({"kind":"Text","data":null})"_str,
        R"({"kind":"Idle","data":{}})"_str, R"({"kind":"Idle","extra":null})"_str,
        R"({"data":"wire"})"_str,
    };
    for (auto input : invalid) EXPECT_TRUE(rstd::json::decode<Adjacent>(input).is_err());
    EXPECT_TRUE(rstd::json::decode<FlexibleAdjacent>(
                    R"({"kind":"Record","data":{"name":"wire"},"extra":null})"_str)
                    .is_ok());
    auto tokens = rstd::serde::to_tokens(original).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<Adjacent>(tokens.as_slice()).unwrap().is_Text());
}

TEST(SerdeDerive, RecordPathStillUsesExistingDerive) {
    auto value = rstd::json::decode<Record>(R"({"name":"wire","ignored":true})"_str).unwrap();
    EXPECT_TRUE(value.note.is_none());
    auto roundtrip = rstd::json::encode(value).unwrap();
    EXPECT_EQ(rstd::json::decode<Record>(roundtrip.as_str()).unwrap().name.as_str(), "wire"_str);
}

TEST(SerdeDerive, DefaultFactoriesRunOnlyForMissingFields) {
    FactoryState::calls       = 0;
    FactoryState::validations = 0;
    auto first = rstd::json::decode<FactoryRecord>(R"({"name":"first"})"_str).unwrap();
    EXPECT_EQ(first.count, u64(1));
    ASSERT_TRUE(first.note.is_some());
    EXPECT_TRUE(first.note->as_str() == "fallback"_str);
    EXPECT_TRUE(first.enabled);
    auto second = rstd::json::decode<FactoryRecord>(R"({"name":"second"})"_str).unwrap();
    EXPECT_EQ(second.count, u64(2));
    auto present = rstd::json::decode<FactoryRecord>(
                       R"({"name":"present","legacy":0,"note":null,"enabled":false})"_str)
                       .unwrap();
    EXPECT_EQ(present.count, u64());
    EXPECT_TRUE(present.note.is_none());
    EXPECT_FALSE(present.enabled);
    EXPECT_EQ(FactoryState::calls, 2u);
    EXPECT_EQ(FactoryState::validations, 3u);

    EXPECT_TRUE(rstd::json::decode<FactoryRecord>(R"({"name":"bad","wire":null})"_str).is_err());
    EXPECT_EQ(FactoryState::calls, 2u);
    EXPECT_EQ(FactoryState::validations, 3u);
    EXPECT_TRUE(
        rstd::json::decode<FactoryRecord>(R"({"name":"bad","wire":1,"legacy":2})"_str).is_err());
    EXPECT_EQ(FactoryState::calls, 2u);
}

TEST(SerdeDerive, ValidationRunsAfterDecodedValuesAndDefaultsAcrossFormats) {
    FactoryState::calls       = 0;
    FactoryState::validations = 0;
    auto rejected = rstd::json::decode<FactoryRecord>(R"({"name":"bad","wire":99})"_str);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(FactoryState::calls, 0u);
    EXPECT_EQ(FactoryState::validations, 1u);
    EXPECT_TRUE(rstd::json::decode<FactoryRecord>(R"({"wire":1})"_str).is_err());
    EXPECT_EQ(FactoryState::validations, 1u);

    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    Vec<Token> tokens;
    tokens.push(Token::container(TokenKind::MapStart, usize(1)));
    tokens.push(Token::string("name"_str));
    tokens.push(Token::string("token"_str));
    tokens.push(Token::marker(TokenKind::MapEnd));
    auto value = rstd::serde::from_tokens<FactoryRecord>(tokens.as_slice()).unwrap();
    EXPECT_EQ(value.count, u64(1));
    EXPECT_EQ(FactoryState::validations, 2u);

    value.count  = u64(99);
    auto encoded = rstd::serde::to_tokens(value).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<FactoryRecord>(encoded.as_slice()).is_err());
    EXPECT_EQ(FactoryState::validations, 3u);

    EXPECT_TRUE(
        rstd::json::decode<ValidatedInternal>(R"({"kind":"Record","name":"bad","wire":99})"_str)
            .is_err());
    EXPECT_EQ(FactoryState::validations, 4u);
    EXPECT_TRUE(decode_factory_in_peer(R"({"name":"peer","wire":99})"_str).is_err());
    EXPECT_EQ(FactoryState::validations, 5u);
}

TEST(SerdeDerive, FactoryResolutionFollowsDeclarationOrder) {
    FactoryState::calls = 0;
    auto first          = rstd::json::decode<FactoryOrder>("{}"_str).unwrap();
    EXPECT_EQ(first.first, u64(1));
    EXPECT_EQ(first.second, u64(2));
    EXPECT_EQ(first.expression, u64(17));
    auto explicit_first = rstd::json::decode<FactoryOrder>(R"({"first":0})"_str).unwrap();
    EXPECT_EQ(explicit_first.first, u64());
    EXPECT_EQ(explicit_first.second, u64(3));
}

TEST(SerdeDerive, TokenInputPreservesDuplicateFieldDiagnostics) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    const ref<str> keys[] = { "kind"_str, "data"_str, "name"_str };
    for (auto key : keys) {
        Vec<Token> tokens;
        tokens.push(Token::container(TokenKind::MapStart, usize(2)));
        for (int i = 0; i < 2; ++i) {
            tokens.push(Token::string(key));
            tokens.push(key == "kind"_str   ? Token::string("Idle"_str)
                        : key == "name"_str ? Token::string("wire"_str)
                                            : Token::null());
        }
        tokens.push(Token::marker(TokenKind::MapEnd));
        if (key == "name"_str) {
            auto result = rstd::serde::from_tokens<Record>(tokens.as_slice());
            ASSERT_TRUE(result.is_err());
            EXPECT_EQ(result.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::DuplicateField);
        } else {
            auto adjacent = rstd::serde::from_tokens<Adjacent>(tokens.as_slice());
            ASSERT_TRUE(adjacent.is_err());
            EXPECT_EQ(adjacent.as_ref().unwrap_err().kind(),
                      rstd::serde::ErrorKind::DuplicateField);
            if (key == "kind"_str) {
                auto internal = rstd::serde::from_tokens<Internal>(tokens.as_slice());
                ASSERT_TRUE(internal.is_err());
                EXPECT_EQ(internal.as_ref().unwrap_err().kind(),
                          rstd::serde::ErrorKind::DuplicateField);
            }
        }
    }
}
