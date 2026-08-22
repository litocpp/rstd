#include <rstd/test/gtest.hpp>
#include <rstd/enum.hpp>

import rstd.serde;
import rstd.json;
import rstd.toml;

using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace
{

struct Person {
    String    name;
    rstd::u64 age;
};

struct ImplicitSerde {
    template<typename Serializer>
    void serialize(Serializer&) const {}
};

struct HelperOnly {
    rstd::serde::RequiredField<String> value { "value"_str };
};

struct EncodeOnly {
    rstd::u64 value {};
};

struct DomainError {};

struct DomainValue {
    String value;
};

struct UnfinishedSequence {};

struct Blob {
    Vec<rstd::u8> bytes;
};

struct Identifier {
    rstd::u64 value;
};

class Event final {
    RSTD_ENUM(Event, (Idle), (Count, (rstd::u64 value;)))
};

struct FlexibleRecord {
    String               name;
    rstd::Option<String> note;
    rstd::u64            retries;
};

struct InvalidRecordSchema {};

enum class Mode : rstd::uint8_t
{
    Debug,
    Release,
};

} // namespace

template<>
struct rstd::Impl<rstd::serde::Serialize, Person> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const Person& value) ->
        typename Serializer::result_type {
        auto map = serializer.begin_map(rstd::usize(2));
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto output = rstd::move(map).unwrap_unchecked();
        auto name   = rstd::serde::field(output, "name"_str, value.name);
        if (name.is_err()) return rstd::Err(rstd::move(name).unwrap_err_unchecked());
        auto age = rstd::serde::field(output, "age"_str, value.age);
        if (age.is_err()) return rstd::Err(rstd::move(age).unwrap_err_unchecked());
        return output.end();
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, Person> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<Person, typename Deserializer::error_type> {
        auto map = deserializer.begin_map();
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto input = rstd::move(map).unwrap_unchecked();
        auto name  = rstd::serde::RequiredField<String>("name"_str);
        auto age   = rstd::serde::RequiredField<rstd::u64>("age"_str);
        for (;;) {
            auto key = input.template next_key<String>();
            if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err_unchecked());
            auto optional = rstd::move(key).unwrap_unchecked();
            if (optional.is_none()) break;
            auto key_value = rstd::move(optional).unwrap_unchecked();
            if (name.matches(key_value.as_str())) {
                auto result = name.assign(deserializer, input);
                if (result.is_err()) return rstd::Err(rstd::move(result).unwrap_err_unchecked());
            } else if (age.matches(key_value.as_str())) {
                auto result = age.assign(deserializer, input);
                if (result.is_err()) return rstd::Err(rstd::move(result).unwrap_err_unchecked());
            } else {
                return rstd::Err(deserializer.unknown_field(key_value.as_str()));
            }
        }
        auto end = input.end();
        if (end.is_err()) return rstd::Err(rstd::move(end).unwrap_err_unchecked());
        auto name_value = name.take(deserializer);
        if (name_value.is_err()) {
            return rstd::Err(rstd::move(name_value).unwrap_err_unchecked());
        }
        auto age_value = age.take(deserializer);
        if (age_value.is_err()) return rstd::Err(rstd::move(age_value).unwrap_err_unchecked());
        return rstd::Ok(Person { rstd::move(name_value).unwrap_unchecked(),
                                 rstd::move(age_value).unwrap_unchecked() });
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, EncodeOnly> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, const EncodeOnly& value) {
        return serializer.serialize_u64(value.value);
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, DomainValue> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<DomainValue, typename Deserializer::error_type> {
        auto value = rstd::serde::deserialize<String>(deserializer);
        if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
        if (value->as_str() != "accepted"_str) {
            return rstd::Err(deserializer.invalid_value_with_source(
                "domain value is not accepted"_str, DomainError {}));
        }
        return rstd::Ok(DomainValue { rstd::move(value).unwrap_unchecked() });
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, UnfinishedSequence> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const UnfinishedSequence&) ->
        typename Serializer::result_type {
        auto sequence = serializer.begin_sequence(rstd::usize());
        if (sequence.is_err()) return rstd::Err(rstd::move(sequence).unwrap_err_unchecked());
        return rstd::Ok(rstd::empty {});
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, Mode> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, Mode value) {
        return serializer.serialize_string(value == Mode::Debug ? "debug"_str : "release"_str);
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, Blob> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, const Blob& value) {
        return serializer.serialize_bytes(value.bytes.as_slice());
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, Blob> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<Blob, typename Deserializer::error_type> {
        auto bytes = deserializer.deserialize_bytes();
        if (bytes.is_err()) return rstd::Err(rstd::move(bytes).unwrap_err_unchecked());
        return rstd::Ok(Blob { rstd::move(bytes).unwrap_unchecked() });
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, Identifier> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, Identifier value) {
        return serializer.serialize_newtype("Identifier"_str, value.value);
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, Identifier> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<Identifier, typename Deserializer::error_type> {
        auto value = deserializer.template deserialize_newtype<rstd::u64>("Identifier"_str);
        if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
        return rstd::Ok(Identifier { rstd::move(value).unwrap_unchecked() });
    }
};

template<>
struct rstd::Impl<rstd::serde::Serialize, Event> {
    template<typename Serializer>
    static decltype(auto) serialize(Serializer& serializer, const Event& value) {
        if (value.is_Idle()) return serializer.serialize_unit_variant("Idle"_str);
        return serializer.serialize_newtype_variant("Count"_str, value.as_Count().value);
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, Event> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<Event, typename Deserializer::error_type> {
        auto started = deserializer.begin_enum();
        if (started.is_err()) return rstd::Err(rstd::move(started).unwrap_err_unchecked());
        auto value = rstd::move(started).unwrap_unchecked();
        if (value.variant() == "Idle"_str) {
            auto unit = value.unit();
            if (unit.is_err()) return rstd::Err(rstd::move(unit).unwrap_err_unchecked());
            return rstd::Ok(Event::Idle());
        }
        if (value.variant() == "Count"_str) {
            auto count = value.template value<rstd::u64>();
            if (count.is_err()) return rstd::Err(rstd::move(count).unwrap_err_unchecked());
            return rstd::Ok(Event::Count(rstd::move(count).unwrap_unchecked()));
        }
        return rstd::Err(deserializer.unknown_variant(value.variant()));
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, FlexibleRecord> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<FlexibleRecord, typename Deserializer::error_type> {
        const rstd::ref<rstd::str> aliases[] = { "legacy-name"_str };
        auto                       name      = rstd::serde::RequiredField<String>(
            "name"_str, rstd::slice<rstd::ref<rstd::str>>::from_raw_parts(aliases, rstd::usize(1)));
        auto note    = rstd::serde::OptionalField<String>("note"_str);
        auto retries = rstd::serde::DefaultedField<rstd::u64>("retries"_str, rstd::u64(3));
        auto result  = rstd::serde::deserialize_record(
            deserializer, rstd::serde::UnknownFieldPolicy::Ignore, name, note, retries);
        if (result.is_err()) return rstd::Err(rstd::move(result).unwrap_err_unchecked());
        auto required = name.take(deserializer);
        if (required.is_err()) return rstd::Err(rstd::move(required).unwrap_err_unchecked());
        return rstd::Ok(FlexibleRecord {
            rstd::move(required).unwrap_unchecked(), note.take(), retries.take() });
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, InvalidRecordSchema> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<InvalidRecordSchema, typename Deserializer::error_type> {
        const rstd::ref<rstd::str> aliases[] = { "second"_str };
        auto                       first     = rstd::serde::RequiredField<String>(
            "first"_str,
            rstd::slice<rstd::ref<rstd::str>>::from_raw_parts(aliases, rstd::usize(1)));
        auto second = rstd::serde::RequiredField<String>("second"_str);
        auto result = rstd::serde::deserialize_record(
            deserializer, rstd::serde::UnknownFieldPolicy::Reject, first, second);
        if (result.is_err()) return rstd::Err(rstd::move(result).unwrap_err_unchecked());
        return rstd::Ok(InvalidRecordSchema {});
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, Mode> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<Mode, typename Deserializer::error_type> {
        auto value = deserializer.deserialize_string();
        if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
        if (value->as_str() == "debug"_str) return rstd::Ok(Mode::Debug);
        if (value->as_str() == "release"_str) return rstd::Ok(Mode::Release);
        return rstd::Err(deserializer.invalid_value("unknown mode"_str));
    }
};

template<>
struct rstd::Impl<rstd::fmt::Display, DomainError> : rstd::ImplBase<DomainError> {
    auto fmt(rstd::fmt::Formatter& formatter) const -> bool {
        return formatter.write_str("domain conversion failed"_str);
    }
};

template<>
struct rstd::Impl<rstd::fmt::Debug, DomainError> : rstd::ImplBase<DomainError> {
    auto fmt(rstd::fmt::Formatter& formatter) const -> bool {
        return rstd::as<rstd::fmt::Display>(this->self()).fmt(formatter);
    }
};

template<>
struct rstd::Impl<rstd::error::Error, DomainError>
    : rstd::DefaultInImpl<rstd::error::Error, DomainError> {};

static_assert(rstd::serde::Serializable<Person>);
static_assert(rstd::serde::Deserializable<Person>);
static_assert(! rstd::serde::Serializable<ImplicitSerde>);
static_assert(! rstd::serde::Deserializable<ImplicitSerde>);
static_assert(! rstd::serde::Serializable<HelperOnly>);
static_assert(! rstd::serde::Deserializable<HelperOnly>);
static_assert(rstd::serde::Serializable<EncodeOnly>);
static_assert(! rstd::serde::Deserializable<EncodeOnly>);
static_assert(! rstd::serde::Serializable<DomainValue>);
static_assert(rstd::serde::Deserializable<DomainValue>);

TEST(Serde, ExplicitRecordTokenRoundtrip) {
    auto person = Person { String::make("Ada"_str), rstd::u64(37) };

    auto encoded = rstd::serde::to_tokens(person);
    ASSERT_TRUE(encoded.is_ok());
    auto tokens  = rstd::move(encoded).unwrap_unchecked();
    auto decoded = rstd::serde::from_tokens<Person>(tokens.as_slice());

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->name.as_str(), "Ada"_str);
    EXPECT_EQ(decoded->age, rstd::u64(37));
}

TEST(Serde, ContainerTokenRoundtrip) {
    auto values = Vec<rstd::Option<rstd::i64>>::make();
    values.push(rstd::Some(rstd::i64(-4)));
    values.push(rstd::None());
    values.push(rstd::Some(rstd::i64(9)));

    auto encoded = rstd::serde::to_tokens(values);
    ASSERT_TRUE(encoded.is_ok());
    auto tokens  = rstd::move(encoded).unwrap_unchecked();
    auto decoded = rstd::serde::from_tokens<Vec<rstd::Option<rstd::i64>>>(tokens.as_slice());

    ASSERT_TRUE(decoded.is_ok());
    ASSERT_EQ(decoded->len(), rstd::usize(3));
    EXPECT_EQ(*(*decoded)[rstd::usize()], rstd::i64(-4));
    EXPECT_TRUE((*decoded)[rstd::usize(1)].is_none());
    EXPECT_EQ(*(*decoded)[rstd::usize(2)], rstd::i64(9));
}

TEST(Serde, TupleTokenRoundtrip) {
    auto value = rstd::tuple<String, rstd::i64>(String::make("port"_str), rstd::i64(443));

    auto encoded = rstd::serde::to_tokens(value);
    ASSERT_TRUE(encoded.is_ok());
    auto tokens  = rstd::move(encoded).unwrap_unchecked();
    auto decoded = rstd::serde::from_tokens<rstd::tuple<String, rstd::i64>>(tokens.as_slice());

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->template get<0>().as_str(), "port"_str);
    EXPECT_EQ(decoded->template get<1>(), rstd::i64(443));
}

TEST(Serde, MapProtocolRejectsValueBeforeKey) {
    auto tokens     = Vec<rstd::serde::Token>::make();
    auto serializer = rstd::serde::TokenSerializer(tokens);
    auto map        = serializer.begin_map(rstd::usize(1));
    ASSERT_TRUE(map.is_ok());
    auto output = rstd::move(map).unwrap_unchecked();

    auto result = output.value(rstd::u64(1));

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);
}

TEST(Serde, MapProtocolRejectsMissingValues) {
    auto tokens     = Vec<rstd::serde::Token>::make();
    auto serializer = rstd::serde::TokenSerializer(tokens);
    auto map        = serializer.begin_map(rstd::usize(1));
    ASSERT_TRUE(map.is_ok());
    auto output = rstd::move(map).unwrap_unchecked();
    ASSERT_TRUE(output.key("name"_str).is_ok());

    auto second_key = output.key("other"_str);
    ASSERT_TRUE(second_key.is_err());
    EXPECT_EQ(second_key.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);

    auto end = output.end();
    ASSERT_TRUE(end.is_err());
    EXPECT_EQ(end.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);
}

TEST(Serde, MapAccessRejectsInvalidCallOrder) {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, rstd::usize(1)));
    tokens.push(rstd::serde::Token::string("name"_str));
    tokens.push(rstd::serde::Token::unsigned_integer(rstd::u64(1)));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));
    auto deserializer = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto map          = deserializer.begin_map();
    ASSERT_TRUE(map.is_ok());
    auto input = rstd::move(map).unwrap_unchecked();

    auto value_before_key = input.next_value<rstd::u64>();
    ASSERT_TRUE(value_before_key.is_err());
    EXPECT_EQ(value_before_key.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);

    auto key = input.next_key<String>();
    ASSERT_TRUE(key.is_ok());
    ASSERT_TRUE(key->is_some());

    auto second_key = input.next_key<String>();
    ASSERT_TRUE(second_key.is_err());
    EXPECT_EQ(second_key.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);

    auto end = input.end();
    ASSERT_TRUE(end.is_err());
    EXPECT_EQ(end.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);
}

TEST(Serde, TokenSerializerRequiresCompoundEnd) {
    auto result = rstd::serde::to_tokens(UnfinishedSequence {});

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), rstd::serde::ErrorKind::Invariant);
}

TEST(Serde, ExplicitEnumRepresentationRoundtrip) {
    auto encoded = rstd::serde::to_tokens(Mode::Release);
    ASSERT_TRUE(encoded.is_ok());
    auto tokens  = rstd::move(encoded).unwrap_unchecked();
    auto decoded = rstd::serde::from_tokens<Mode>(tokens.as_slice());

    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(*decoded, Mode::Release);
}

TEST(Serde, UnitBytesAndNewtypeTokenRoundtrip) {
    auto unit = rstd::serde::to_tokens(rstd::empty {});
    ASSERT_TRUE(unit.is_ok());
    auto unit_tokens = rstd::move(unit).unwrap_unchecked();
    EXPECT_TRUE(rstd::serde::from_tokens<rstd::empty>(unit_tokens.as_slice()).is_ok());

    auto bytes = Vec<rstd::u8>::make();
    bytes.push(rstd::u8(1));
    bytes.push(rstd::u8(255));
    auto encoded_blob = rstd::serde::to_tokens(Blob { rstd::move(bytes) });
    ASSERT_TRUE(encoded_blob.is_ok());
    auto blob_tokens  = rstd::move(encoded_blob).unwrap_unchecked();
    auto decoded_blob = rstd::serde::from_tokens<Blob>(blob_tokens.as_slice());
    ASSERT_TRUE(decoded_blob.is_ok());
    ASSERT_EQ(decoded_blob->bytes.len(), rstd::usize(2));
    EXPECT_EQ(decoded_blob->bytes[rstd::usize()], rstd::u8(1));
    EXPECT_EQ(decoded_blob->bytes[rstd::usize(1)], rstd::u8(255));

    auto encoded_id = rstd::serde::to_tokens(Identifier { rstd::u64(42) });
    ASSERT_TRUE(encoded_id.is_ok());
    auto id_tokens  = rstd::move(encoded_id).unwrap_unchecked();
    auto decoded_id = rstd::serde::from_tokens<Identifier>(id_tokens.as_slice());
    ASSERT_TRUE(decoded_id.is_ok());
    EXPECT_EQ(decoded_id->value, rstd::u64(42));
}

TEST(Serde, EnumAccessDistinguishesUnitAndNewtypeVariants) {
    auto idle = rstd::serde::to_tokens(Event::Idle());
    ASSERT_TRUE(idle.is_ok());
    auto idle_tokens = rstd::move(idle).unwrap_unchecked();
    auto idle_value  = rstd::serde::from_tokens<Event>(idle_tokens.as_slice());
    ASSERT_TRUE(idle_value.is_ok());
    EXPECT_TRUE(idle_value->is_Idle());

    auto count = rstd::serde::to_tokens(Event::Count(rstd::u64(7)));
    ASSERT_TRUE(count.is_ok());
    auto count_tokens = rstd::move(count).unwrap_unchecked();
    auto count_value  = rstd::serde::from_tokens<Event>(count_tokens.as_slice());
    ASSERT_TRUE(count_value.is_ok());
    ASSERT_TRUE(count_value->is_Count());
    EXPECT_EQ(count_value->as_Count().value, rstd::u64(7));
}

TEST(Serde, RecordPolicySupportsAliasOptionalDefaultAndIgnoredFields) {
    auto decoded = rstd::json::decode<FlexibleRecord>(
        R"({"legacy-name":"worker","future":{"nested":[1,2]}})"_str);
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->name.as_str(), "worker"_str);
    EXPECT_TRUE(decoded->note.is_none());
    EXPECT_EQ(decoded->retries, rstd::u64(3));

    auto present =
        rstd::json::decode<FlexibleRecord>(R"({"name":"worker","note":"ready","retries":5})"_str);
    ASSERT_TRUE(present.is_ok());
    ASSERT_TRUE(present->note.is_some());
    EXPECT_EQ(present->note->as_str(), "ready"_str);
    EXPECT_EQ(present->retries, rstd::u64(5));
}

TEST(Serde, TokenIgnoredValueConsumesNestedCompounds) {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, rstd::usize(2)));
    tokens.push(rstd::serde::Token::string("legacy-name"_str));
    tokens.push(rstd::serde::Token::string("worker"_str));
    tokens.push(rstd::serde::Token::string("future"_str));
    tokens.push(
        rstd::serde::Token::container(rstd::serde::TokenKind::SequenceStart, rstd::usize(1)));
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, rstd::usize(1)));
    tokens.push(rstd::serde::Token::string("nested"_str));
    tokens.push(rstd::serde::Token::unsigned_integer(rstd::u64(1)));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::SequenceEnd));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));

    auto decoded = rstd::serde::from_tokens<FlexibleRecord>(tokens.as_slice());
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(decoded->name.as_str(), "worker"_str);
    EXPECT_EQ(decoded->retries, rstd::u64(3));
}

TEST(Serde, RecordPolicyRejectsOverlappingAliases) {
    auto decoded = rstd::json::decode<InvalidRecordSchema>("{}"_str);
    ASSERT_TRUE(decoded.is_err());
    auto error = rstd::move(decoded).unwrap_err_unchecked();
    ASSERT_TRUE(error.data_error().is_some());
    EXPECT_EQ(error.data_error()->get().kind(), rstd::serde::ErrorKind::Invariant);
}

TEST(Serde, JsonAndTomlShareBytesNewtypeAndEnumContracts) {
    auto bytes = Vec<rstd::u8>::make();
    bytes.push(rstd::u8(1));
    bytes.push(rstd::u8(255));
    auto blob = Blob { rstd::move(bytes) };

    auto json_blob = rstd::json::to_value(blob);
    ASSERT_TRUE(json_blob.is_ok());
    auto decoded_json_blob = rstd::json::decode_value<Blob>(*json_blob);
    ASSERT_TRUE(decoded_json_blob.is_ok());
    EXPECT_EQ(decoded_json_blob->bytes[rstd::usize(1)], rstd::u8(255));

    auto toml_blob = rstd::toml::to_value(blob);
    ASSERT_TRUE(toml_blob.is_ok());
    auto decoded_toml_blob = rstd::toml::decode_value<Blob>(*toml_blob);
    ASSERT_TRUE(decoded_toml_blob.is_ok());
    EXPECT_EQ(decoded_toml_blob->bytes[rstd::usize(1)], rstd::u8(255));

    auto json_id = rstd::json::to_value(Identifier { rstd::u64(9) });
    ASSERT_TRUE(json_id.is_ok());
    auto decoded_json_id = rstd::json::decode_value<Identifier>(*json_id);
    ASSERT_TRUE(decoded_json_id.is_ok());
    EXPECT_EQ(decoded_json_id->value, rstd::u64(9));

    auto json_event = rstd::json::to_value(Event::Count(rstd::u64(11)));
    ASSERT_TRUE(json_event.is_ok());
    auto decoded_json_event = rstd::json::decode_value<Event>(*json_event);
    ASSERT_TRUE(decoded_json_event.is_ok());
    ASSERT_TRUE(decoded_json_event->is_Count());
    EXPECT_EQ(decoded_json_event->as_Count().value, rstd::u64(11));

    auto toml_event = rstd::toml::to_value(Event::Idle());
    ASSERT_TRUE(toml_event.is_ok());
    auto decoded_toml_event = rstd::toml::decode_value<Event>(*toml_event);
    ASSERT_TRUE(decoded_toml_event.is_ok());
    EXPECT_TRUE(decoded_toml_event->is_Idle());
}

TEST(Serde, JsonSupportsUnitWhileTomlRejectsItExplicitly) {
    auto json_unit = rstd::json::to_value(rstd::empty {});
    ASSERT_TRUE(json_unit.is_ok());
    EXPECT_TRUE(rstd::json::decode_value<rstd::empty>(*json_unit).is_ok());

    auto toml_unit = rstd::toml::to_value(rstd::empty {});
    ASSERT_TRUE(toml_unit.is_err());
    EXPECT_EQ(toml_unit.unwrap_err().kind(), rstd::serde::ErrorKind::Unsupported);
}

TEST(Serde, EnumErrorsCarryVariantPaths) {
    auto payload = rstd::json::decode<Event>(R"({"Count":"many"})"_str);
    ASSERT_TRUE(payload.is_err());
    auto payload_result = rstd::move(payload).unwrap_err_unchecked();
    auto payload_error  = payload_result.data_error();
    ASSERT_TRUE(payload_error.is_some());
    ASSERT_EQ(payload_error->get().path().segments().len(), rstd::usize(1));
    EXPECT_EQ(payload_error->get().path().segments()[rstd::usize()].kind(),
              rstd::serde::PathSegmentKind::Variant);
    EXPECT_EQ(*payload_error->get().path().segments()[rstd::usize()].name(), "Count"_str);

    auto variant = rstd::json::decode<Event>(R"("Future")"_str);
    ASSERT_TRUE(variant.is_err());
    auto variant_result = rstd::move(variant).unwrap_err_unchecked();
    auto variant_error  = variant_result.data_error();
    ASSERT_TRUE(variant_error.is_some());
    EXPECT_EQ(variant_error->get().kind(), rstd::serde::ErrorKind::UnknownVariant);
    ASSERT_EQ(variant_error->get().path().segments().len(), rstd::usize(1));
    EXPECT_EQ(*variant_error->get().path().segments()[rstd::usize()].name(), "Future"_str);
}

TEST(Serde, DomainConversionErrorRemainsADataSource) {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::string("rejected"_str));

    auto result = rstd::serde::from_tokens<DomainValue>(tokens.as_slice());

    ASSERT_TRUE(result.is_err());
    auto error  = rstd::move(result).unwrap_err_unchecked();
    auto source = error.source();
    ASSERT_TRUE(source.is_some());
    EXPECT_TRUE(rstd::error::is<DomainError>(*source));
}

TEST(Serde, RecordHelperRejectsDuplicateField) {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, rstd::usize(3)));
    tokens.push(rstd::serde::Token::string("name"_str));
    tokens.push(rstd::serde::Token::string("Ada"_str));
    tokens.push(rstd::serde::Token::string("name"_str));
    tokens.push(rstd::serde::Token::string("Grace"_str));
    tokens.push(rstd::serde::Token::string("age"_str));
    tokens.push(rstd::serde::Token::unsigned_integer(rstd::u64(37)));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));

    auto result = rstd::serde::from_tokens<Person>(tokens.as_slice());

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), rstd::serde::ErrorKind::DuplicateField);
}

TEST(Serde, NestedTypeErrorCarriesFieldPath) {
    auto tokens = Vec<rstd::serde::Token>::make();
    tokens.push(rstd::serde::Token::container(rstd::serde::TokenKind::MapStart, rstd::usize(2)));
    tokens.push(rstd::serde::Token::string("name"_str));
    tokens.push(rstd::serde::Token::string("Ada"_str));
    tokens.push(rstd::serde::Token::string("age"_str));
    tokens.push(rstd::serde::Token::string("old"_str));
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));

    auto result = rstd::serde::from_tokens<Person>(tokens.as_slice());

    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err_unchecked();
    ASSERT_EQ(error.path().segments().len(), rstd::usize(1));
    EXPECT_EQ(error.path().segments()[rstd::usize()].kind(), rstd::serde::PathSegmentKind::MapKey);
    ASSERT_TRUE(error.path().segments()[rstd::usize()].name().is_some());
    EXPECT_EQ(*error.path().segments()[rstd::usize()].name(), "age"_str);
}
