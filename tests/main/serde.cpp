#include <rstd/test/gtest.hpp>
#include <rstd/enum.hpp>

import rstd.serde;
import rstd.serde.token;
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
struct EmptyTaggedRecord {};
constexpr auto rstd_serde_definition(EmptyTaggedRecord*) {
    return rstd::serde::RecordDefinition<EmptyTaggedRecord,
                                         "EmptyTaggedRecord",
                                         rstd::serde::Direction::Both,
                                         rstd::serde::UnknownFieldPolicy::Ignore> {};
}

enum class Mode : rstd::uint8_t
{
    Debug,
    Release,
};

struct DerivedIdentifier {
    rstd::u64 value;
};
constexpr auto rstd_serde_definition(DerivedIdentifier*) {
    return rstd::serde::NewtypeDefinition<DerivedIdentifier,
                                          "DerivedIdentifier",
                                          rstd::serde::Direction::Both,
                                          &DerivedIdentifier::value> {};
}
enum class StrictMode
{
    Enabled,
    Disabled
};
constexpr auto rstd_serde_definition(StrictMode*) {
    using namespace rstd::serde;
    return StringEnumDefinition<StrictMode,
                                "StrictMode",
                                Direction::Both,
                                Variant<StrictMode::Enabled, "Enabled">,
                                Variant<StrictMode::Disabled, "Disabled">> {};
}
struct SeededRecord {
    bool enabled;
};
struct SeededRecordDefinition
    : rstd::serde::RecordDefinition<SeededRecord,
                                    "SeededRecord",
                                    rstd::serde::Direction::Deserialize,
                                    rstd::serde::UnknownFieldPolicy::Reject,
                                    rstd::serde::Member<&SeededRecord::enabled,
                                                        "enabled",
                                                        rstd::serde::FieldPolicy::Defaulted>> {
    template<class D>
    static auto deserialize(D& source) -> rstd::Result<SeededRecord, typename D::error_type> {
        return deserialize_with_defaults(source, SeededRecord { true });
    }
};
constexpr auto rstd_serde_definition(SeededRecord*) {
    return SeededRecordDefinition {};
}

class InternalEvent final {
    RSTD_ENUM(InternalEvent,
              (Idle),
              (Record, (String secret; rstd::u64 count; rstd::Option<String> note; bool enabled;)),
              (Map, (::alloc::collections::BTreeMap<String, rstd::u64> values;)),
              (Nested, (Blob data; Vec<DerivedIdentifier> items; Event event;)),
              (Scalar, (String value;)),
              (Conflict, (String type;)))
};
constexpr auto rstd_serde_definition(InternalEvent::Record_payload*) {
    using T = InternalEvent::Record_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Record",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::secret, "secret", FieldPolicy::Required, "password">,
                            Member<&T::count, "count">,
                            Member<&T::note, "note", FieldPolicy::Defaulted>,
                            Member<&T::enabled, "enabled", FieldPolicy::Defaulted>> {};
}
constexpr auto rstd_serde_definition(InternalEvent::Map_payload*) {
    using T = InternalEvent::Map_payload;
    return rstd::serde::NewtypeDefinition<T, "Map", rstd::serde::Direction::Both, &T::values> {};
}
struct NestedEventDefinition
    : rstd::serde::RecordDefinition<
          InternalEvent::Nested_payload,
          "Nested",
          rstd::serde::Direction::Both,
          rstd::serde::UnknownFieldPolicy::Reject,
          rstd::serde::Member<&InternalEvent::Nested_payload::data, "data">,
          rstd::serde::Member<&InternalEvent::Nested_payload::items, "items">,
          rstd::serde::Member<&InternalEvent::Nested_payload::event, "event">> {
    using T = InternalEvent::Nested_payload;
    template<class D>
    static auto deserialize(D& source) -> rstd::Result<T, typename D::error_type> {
        return deserialize_with_defaults(source, T { {}, {}, Event::Idle() });
    }
};
constexpr auto rstd_serde_definition(InternalEvent::Nested_payload*) {
    return NestedEventDefinition {};
}
constexpr auto rstd_serde_definition(InternalEvent::Scalar_payload*) {
    using T = InternalEvent::Scalar_payload;
    return rstd::serde::NewtypeDefinition<T, "Scalar", rstd::serde::Direction::Both, &T::value> {};
}
constexpr auto rstd_serde_definition(InternalEvent::Conflict_payload*) {
    using T = InternalEvent::Conflict_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Conflict",
                            Direction::Both,
                            UnknownFieldPolicy::Reject,
                            Member<&T::type, "type">> {};
}
constexpr auto rstd_serde_definition(InternalEvent*) {
    return rstd::serde::InternalVariantDefinition<InternalEvent,
                                                  "InternalEvent",
                                                  rstd::serde::Direction::Both,
                                                  "type",
                                                  rstd::serde::UnknownFieldPolicy::Ignore> {};
}

class AdjacentEvent final {
    RSTD_ENUM(AdjacentEvent, (Idle), (Record, (String secret;)))
};
constexpr auto rstd_serde_definition(AdjacentEvent::Record_payload*) {
    using T = AdjacentEvent::Record_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Record",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::secret, "secret">> {};
}
constexpr auto rstd_serde_definition(AdjacentEvent*) {
    return rstd::serde::AdjacentVariantDefinition<AdjacentEvent,
                                                  "AdjacentEvent",
                                                  rstd::serde::Direction::Both,
                                                  "type",
                                                  "message",
                                                  rstd::serde::UnknownFieldPolicy::Ignore> {};
}

class JsonAuthMethod final {
    RSTD_ENUM(JsonAuthMethod,
              (Username, (String username; String pw;)),
              (Phone, (String phone; String pw;)),
              (Email, (String email; String pw;)),
              (Qr, (String key;)))
};
constexpr auto rstd_serde_definition(JsonAuthMethod::Username_payload*) {
    using T = JsonAuthMethod::Username_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Username",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::username, "username">,
                            Member<&T::pw, "pw">> {};
}
constexpr auto rstd_serde_definition(JsonAuthMethod::Phone_payload*) {
    using T = JsonAuthMethod::Phone_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Phone",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::phone, "phone">,
                            Member<&T::pw, "pw">> {};
}
constexpr auto rstd_serde_definition(JsonAuthMethod::Email_payload*) {
    using T = JsonAuthMethod::Email_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Email",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::email, "email">,
                            Member<&T::pw, "pw">> {};
}
constexpr auto rstd_serde_definition(JsonAuthMethod::Qr_payload*) {
    using T = JsonAuthMethod::Qr_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Qr",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::key, "key">> {};
}
constexpr auto rstd_serde_definition(JsonAuthMethod*) {
    return rstd::serde::InternalVariantDefinition<JsonAuthMethod,
                                                  "AuthMethod",
                                                  rstd::serde::Direction::Both,
                                                  "type",
                                                  rstd::serde::UnknownFieldPolicy::Ignore> {};
}

class JsonAuthOutcome final {
    RSTD_ENUM(JsonAuthOutcome,
              (Ok),
              (Failed, (String message;)),
              (WrongPassword),
              (NoSuchUsername),
              (NoSuchEmail),
              (NoSuchPhone),
              (QrExpired),
              (QrWaitScan),
              (QrWaitComform, (String name; String avatar_url; String message;)))
};
constexpr auto rstd_serde_definition(JsonAuthOutcome::Failed_payload*) {
    using T = JsonAuthOutcome::Failed_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Failed",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::message, "message">> {};
}
constexpr auto rstd_serde_definition(JsonAuthOutcome::QrWaitComform_payload*) {
    using T = JsonAuthOutcome::QrWaitComform_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "QrWaitComform",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::name, "name">,
                            Member<&T::avatar_url, "avatar_url">,
                            Member<&T::message, "message">> {};
}
constexpr auto rstd_serde_definition(JsonAuthOutcome*) {
    return rstd::serde::AdjacentVariantDefinition<JsonAuthOutcome,
                                                  "AuthOutcome",
                                                  rstd::serde::Direction::Both,
                                                  "type",
                                                  "message",
                                                  rstd::serde::UnknownFieldPolicy::Ignore> {};
}
struct JsonAuthInput {
    String                       server_url;
    rstd::Option<JsonAuthMethod> method;
};
constexpr auto rstd_serde_definition(JsonAuthInput*) {
    using namespace rstd::serde;
    return RecordDefinition<JsonAuthInput,
                            "AuthInfo",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&JsonAuthInput::server_url, "server_url">,
                            Member<&JsonAuthInput::method, "method", FieldPolicy::Defaulted>> {};
}

class StrictJsonEvent final {
    RSTD_ENUM(StrictJsonEvent, (Idle), (Qr, (String key;)))
};
constexpr auto rstd_serde_definition(StrictJsonEvent::Qr_payload*) {
    using T = StrictJsonEvent::Qr_payload;
    using namespace rstd::serde;
    return RecordDefinition<T,
                            "Qr",
                            Direction::Both,
                            UnknownFieldPolicy::Ignore,
                            Member<&T::key, "key">> {};
}
constexpr auto rstd_serde_definition(StrictJsonEvent*) {
    return rstd::serde::InternalVariantDefinition<StrictJsonEvent,
                                                  "StrictJsonEvent",
                                                  rstd::serde::Direction::Both,
                                                  "type"> {};
}
class StrictJsonAdjacent final {
    RSTD_ENUM(StrictJsonAdjacent, (Ok))
};
constexpr auto rstd_serde_definition(StrictJsonAdjacent*) {
    return rstd::serde::AdjacentVariantDefinition<StrictJsonAdjacent,
                                                  "StrictJsonAdjacent",
                                                  rstd::serde::Direction::Both,
                                                  "type",
                                                  "message"> {};
}
class JsonObjectEvent final {
    RSTD_ENUM(JsonObjectEvent, (Object, (rstd::json::Value value;)))
};
constexpr auto rstd_serde_definition(JsonObjectEvent::Object_payload*) {
    using T = JsonObjectEvent::Object_payload;
    return rstd::serde::NewtypeDefinition<T, "Object", rstd::serde::Direction::Both, &T::value> {};
}
constexpr auto rstd_serde_definition(JsonObjectEvent*) {
    return rstd::serde::InternalVariantDefinition<JsonObjectEvent,
                                                  "JsonObjectEvent",
                                                  rstd::serde::Direction::Both,
                                                  "type"> {};
}

struct TaggedDescription {
    bool internal {};
    bool adjacent {};
    int  count {};

    template<typename D>
    void variants() {
        if constexpr (requires { D::tag; }) {
            internal = D::tag.view() == "type"_str;
            if constexpr (requires { D::content; }) {
                internal = false;
                adjacent = D::content.view() == "message"_str;
            }
        }
        D::variants([&]<typename>() {
            ++count;
        });
    }
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

TEST(Serde, DerivedNewtypesRemainTransparent) {
    auto value = rstd::json::decode<DerivedIdentifier>("42"_str).unwrap();
    EXPECT_EQ(value.value, rstd::u64(42));
    EXPECT_EQ(rstd::json::to_value(value).unwrap().as_u64().unwrap(), rstd::u64(42));
}

TEST(Serde, StringEnumsRejectExternallyTaggedObjects) {
    EXPECT_EQ(rstd::json::decode<StrictMode>(R"("Enabled")"_str).unwrap(), StrictMode::Enabled);
    EXPECT_TRUE(rstd::json::decode<StrictMode>(R"({"Enabled":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<StrictMode>(R"("enabled")"_str).is_err());
    EXPECT_EQ(rstd::json::to_value(StrictMode::Disabled).unwrap().as_str().unwrap(),
              "Disabled"_str);
}

TEST(Serde, RecordDefaultsDoNotTreatNullAsMissing) {
    EXPECT_TRUE(rstd::json::decode<SeededRecord>("{}"_str).unwrap().enabled);
    EXPECT_FALSE(rstd::json::decode<SeededRecord>("{\"enabled\":false}"_str).unwrap().enabled);
    EXPECT_TRUE(rstd::json::decode<SeededRecord>("{\"enabled\":null}"_str).is_err());
}

TEST(Serde, InternalVariantUsesRecordAndVariantMetadata) {
    auto original =
        InternalEvent::Record(String::make("sensitive"_str), rstd::u64::MAX, rstd::None(), true);
    auto tokens = rstd::serde::to_tokens(original).unwrap();
    EXPECT_EQ(tokens[rstd::usize()].kind(), rstd::serde::TokenKind::MapStart);
    EXPECT_EQ(tokens[rstd::usize()].len(), rstd::usize(5));
    EXPECT_EQ(tokens[rstd::usize(1)].as_str(), "type"_str);
    EXPECT_EQ(tokens[rstd::usize(2)].as_str(), "Record"_str);
    auto result = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice());
    ASSERT_TRUE(result.is_ok());
    ASSERT_TRUE(result->is_Record());
    EXPECT_EQ(result->as_Record().secret.as_str(), "sensitive"_str);
    EXPECT_EQ(result->as_Record().count, rstd::u64::MAX);
    EXPECT_TRUE(result->as_Record().note.is_none());
    EXPECT_TRUE(result->as_Record().enabled);

    auto json = rstd::json::to_value(original).unwrap();
    EXPECT_EQ((*json.get("type"_str))->as_str().unwrap(), "Record"_str);
    EXPECT_EQ((*json.get("count"_str))->as_u64().unwrap(), rstd::u64::MAX);
}

TEST(Serde, InternalVariantAcceptsTagLastAndReusesRecordDefaults) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto tokens = Vec<Token>::make();
    tokens.push(Token::container(TokenKind::MapStart, rstd::usize(5)));
    tokens.push(Token::string("password"_str));
    tokens.push(Token::string("sensitive"_str));
    tokens.push(Token::string("count"_str));
    tokens.push(Token::unsigned_integer(rstd::u64::MAX));
    tokens.push(Token::string("ignored"_str));
    tokens.push(Token::container(TokenKind::SequenceStart, rstd::usize(1)));
    tokens.push(Token::container(TokenKind::MapStart, rstd::usize(1)));
    tokens.push(Token::string("nested"_str));
    tokens.push(Token::bytes(rstd::slice<rstd::u8>()));
    tokens.push(Token::marker(TokenKind::MapEnd));
    tokens.push(Token::marker(TokenKind::SequenceEnd));
    tokens.push(Token::string("note"_str));
    tokens.push(Token::null());
    tokens.push(Token::string("type"_str));
    tokens.push(Token::string("Record"_str));
    tokens.push(Token::marker(TokenKind::MapEnd));
    auto decoded = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice()).unwrap();
    ASSERT_TRUE(decoded.is_Record());
    EXPECT_EQ(decoded.as_Record().secret.as_str(), "sensitive"_str);
    EXPECT_EQ(decoded.as_Record().count, rstd::u64::MAX);
    EXPECT_TRUE(decoded.as_Record().note.is_none());
    EXPECT_FALSE(decoded.as_Record().enabled);
}

TEST(Serde, InternalVariantSupportsMapNewtypesAndRejectsInvalidPayloadShapes) {
    auto map = ::alloc::collections::BTreeMap<String, rstd::u64>::make();
    map.insert(String::make("count"_str), rstd::u64::MAX);
    auto original = InternalEvent::Map(rstd::move(map));
    auto tokens   = rstd::serde::to_tokens(original).unwrap();
    auto decoded  = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice()).unwrap();
    ASSERT_TRUE(decoded.is_Map());
    EXPECT_EQ(*decoded.as_Map().values.get("count"_str).unwrap(), rstd::u64::MAX);
    auto scalar = rstd::serde::to_tokens(InternalEvent::Scalar(String::make("sensitive"_str)));
    ASSERT_TRUE(scalar.is_err());
    EXPECT_EQ(scalar.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::Unsupported);
    EXPECT_TRUE(
        rstd::serde::to_tokens(InternalEvent::Conflict(String::make("sensitive"_str))).is_err());
}

TEST(Serde, InternalVariantRejectsMissingNullDuplicateAndUnknownTags) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto missing = Vec<Token>::make();
    missing.push(Token::container(TokenKind::MapStart, rstd::usize()));
    missing.push(Token::marker(TokenKind::MapEnd));
    auto result = rstd::serde::from_tokens<InternalEvent>(missing.as_slice());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::MissingField);

    const bool flags[] = { false, true };
    for (auto null : flags) {
        auto tokens = Vec<Token>::make();
        tokens.push(Token::container(TokenKind::MapStart, rstd::usize(1)));
        tokens.push(Token::string("type"_str));
        tokens.push(null ? Token::null() : Token::string("credential-in-tag"_str));
        tokens.push(Token::marker(TokenKind::MapEnd));
        auto decoded = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice());
        ASSERT_TRUE(decoded.is_err());
        EXPECT_EQ(decoded.as_ref().unwrap_err().kind(),
                  null ? rstd::serde::ErrorKind::TypeMismatch
                       : rstd::serde::ErrorKind::UnknownVariant);
        EXPECT_TRUE(rstd::format("{}", decoded.as_ref().unwrap_err())
                        .as_str()
                        .find("credential-in-tag"_str)
                        .is_none());
    }
    auto duplicate = Vec<Token>::make();
    duplicate.push(Token::container(TokenKind::MapStart, rstd::usize(2)));
    for (int index = 0; index < 2; ++index) {
        duplicate.push(Token::string("type"_str));
        duplicate.push(Token::string("Idle"_str));
    }
    duplicate.push(Token::marker(TokenKind::MapEnd));
    auto duplicated = rstd::serde::from_tokens<InternalEvent>(duplicate.as_slice());
    ASSERT_TRUE(duplicated.is_err());
    EXPECT_EQ(duplicated.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::DuplicateField);
}

TEST(Serde, InternalVariantUnitHonorsUnknownFieldPolicyAndSingleConsumption) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto tokens = Vec<Token>::make();
    tokens.push(Token::container(TokenKind::MapStart, rstd::usize(2)));
    tokens.push(Token::string("extra"_str));
    tokens.push(Token::null());
    tokens.push(Token::string("type"_str));
    tokens.push(Token::string("Idle"_str));
    tokens.push(Token::marker(TokenKind::MapEnd));
    EXPECT_TRUE(rstd::serde::from_tokens<InternalEvent>(tokens.as_slice()).unwrap().is_Idle());
    auto strict   = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto rejected = strict.begin_internal_enum("type"_str, rstd::serde::UnknownFieldPolicy::Reject)
                        .unwrap()
                        .unit();
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::UnknownField);
    auto source = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto access =
        source.begin_internal_enum("type"_str, rstd::serde::UnknownFieldPolicy::Ignore).unwrap();
    EXPECT_TRUE(access.unit().is_ok());
    EXPECT_TRUE(access.unit().is_err());
}

TEST(Serde, AdjacentVariantRoundtripsAndAcceptsContentBeforeTag) {
    auto value   = AdjacentEvent::Record(String::make("sensitive"_str));
    auto tokens  = rstd::serde::to_tokens(value).unwrap();
    auto decoded = rstd::serde::from_tokens<AdjacentEvent>(tokens.as_slice()).unwrap();
    ASSERT_TRUE(decoded.is_Record());
    EXPECT_EQ(decoded.as_Record().secret.as_str(), "sensitive"_str);

    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto reversed = Vec<Token>::make();
    reversed.push(Token::container(TokenKind::MapStart, rstd::usize(3)));
    reversed.push(Token::string("message"_str));
    reversed.push(Token::container(TokenKind::MapStart, rstd::usize(1)));
    reversed.push(Token::string("secret"_str));
    reversed.push(Token::string("sensitive"_str));
    reversed.push(Token::marker(TokenKind::MapEnd));
    reversed.push(Token::string("unknown"_str));
    reversed.push(Token::null());
    reversed.push(Token::string("type"_str));
    reversed.push(Token::string("Record"_str));
    reversed.push(Token::marker(TokenKind::MapEnd));
    EXPECT_TRUE(rstd::serde::from_tokens<AdjacentEvent>(reversed.as_slice()).is_ok());
    auto source = rstd::serde::TokenDeserializer(reversed.as_slice());
    auto strict = source.begin_adjacent_enum(
        "type"_str, "message"_str, rstd::serde::UnknownFieldPolicy::Reject);
    ASSERT_TRUE(strict.is_err());
    EXPECT_EQ(strict.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::UnknownField);
}

TEST(Serde, AdjacentVariantUnitAcceptsMissingOrNullButNotMapContent) {
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto absent = rstd::serde::to_tokens(AdjacentEvent::Idle()).unwrap();
    EXPECT_TRUE(rstd::serde::from_tokens<AdjacentEvent>(absent.as_slice()).unwrap().is_Idle());
    auto source = rstd::serde::TokenDeserializer(absent.as_slice());
    auto access =
        source
            .begin_adjacent_enum("type"_str, "message"_str, rstd::serde::UnknownFieldPolicy::Ignore)
            .unwrap();
    EXPECT_TRUE(access.unit().is_ok());
    EXPECT_TRUE(access.value<bool>().is_err());
    const bool flags[] = { false, true };
    for (auto valid : flags) {
        auto tokens = Vec<Token>::make();
        tokens.push(Token::container(TokenKind::MapStart, rstd::usize(2)));
        tokens.push(Token::string("type"_str));
        tokens.push(Token::string("Idle"_str));
        tokens.push(Token::string("message"_str));
        if (valid)
            tokens.push(Token::null());
        else {
            tokens.push(Token::container(TokenKind::MapStart, rstd::usize()));
            tokens.push(Token::marker(TokenKind::MapEnd));
        }
        tokens.push(Token::marker(TokenKind::MapEnd));
        EXPECT_EQ(rstd::serde::from_tokens<AdjacentEvent>(tokens.as_slice()).is_ok(), valid);
    }
}

TEST(Serde, InternalVariantPreservesNestedBytesSequencesAndEnums) {
    auto bytes = Vec<rstd::u8>::make();
    bytes.push(rstd::u8());
    bytes.push(rstd::u8(255));
    auto items = Vec<DerivedIdentifier>::make();
    items.push(DerivedIdentifier { rstd::u64::MAX });
    auto original = InternalEvent::Nested(
        Blob { rstd::move(bytes) }, rstd::move(items), Event::Count(rstd::u64(42)));
    auto tokens = rstd::serde::to_tokens(original).unwrap();
    auto result = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice()).unwrap();
    ASSERT_TRUE(result.is_Nested());
    EXPECT_EQ(result.as_Nested().data.bytes.len(), rstd::usize(2));
    EXPECT_EQ(result.as_Nested().data.bytes[rstd::usize(1)], rstd::u8(255));
    EXPECT_EQ(result.as_Nested().items[rstd::usize()].value, rstd::u64::MAX);
    ASSERT_TRUE(result.as_Nested().event.is_Count());
    EXPECT_EQ(result.as_Nested().event.as_Count().value, rstd::u64(42));
}

TEST(Serde, InternalVariantUsesPayloadMissingNullDuplicateAndUnknownFieldRules) {
    struct Case {
        rstd::ref<rstd::str>   input;
        rstd::serde::ErrorKind error;
    };
    using rstd::serde::ErrorKind;
    const Case cases[] = {
        { R"({"type":"Record","secret":"credential-marker"})"_str, ErrorKind::MissingField },
        { R"({"type":"Record","secret":null,"count":1})"_str, ErrorKind::TypeMismatch },
        { R"({"type":"Record","secret":"credential-marker","count":1,"enabled":null})"_str,
          ErrorKind::TypeMismatch },
        { R"({"type":"Nested","secret":"credential-marker"})"_str, ErrorKind::UnknownField },
    };
    for (const auto& test : cases) {
        auto tokens = rstd::serde::to_tokens(rstd::json::from_str(test.input).unwrap()).unwrap();
        auto result = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice());
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.as_ref().unwrap_err().kind(), test.error);
        EXPECT_FALSE(rstd::format("{}", result.as_ref().unwrap_err())
                         .as_str()
                         .contains("credential-marker"_str));
    }
    using rstd::serde::Token;
    using rstd::serde::TokenKind;
    auto tokens = Vec<Token>::make();
    tokens.push(Token::container(TokenKind::MapStart, rstd::usize(4)));
    tokens.push(Token::string("type"_str));
    tokens.push(Token::string("Record"_str));
    tokens.push(Token::string("secret"_str));
    tokens.push(Token::string("credential-marker"_str));
    tokens.push(Token::string("password"_str));
    tokens.push(Token::string("credential-marker"_str));
    tokens.push(Token::string("count"_str));
    tokens.push(Token::unsigned_integer(rstd::u64(1)));
    tokens.push(Token::marker(TokenKind::MapEnd));
    auto duplicate = rstd::serde::from_tokens<InternalEvent>(tokens.as_slice());
    ASSERT_TRUE(duplicate.is_err());
    EXPECT_EQ(duplicate.as_ref().unwrap_err().kind(), ErrorKind::DuplicateField);
}

TEST(Serde, AdjacentVariantRejectsMissingNullAndDuplicateContentWithoutLeakingValues) {
    using rstd::serde::ErrorKind;
    struct Case {
        rstd::ref<rstd::str> input;
        ErrorKind            error;
    };
    const Case cases[] = {
        { R"({})"_str, ErrorKind::MissingField },
        { R"({"type":"credential-marker"})"_str, ErrorKind::UnknownVariant },
        { R"({"type":"Record"})"_str, ErrorKind::MissingField },
        { R"({"type":"Record","message":null})"_str, ErrorKind::TypeMismatch },
        { R"({"type":"Record","message":{"secret":null}})"_str, ErrorKind::TypeMismatch },
        { R"({"type":"Record","message":{}})"_str, ErrorKind::MissingField },
    };
    for (const auto& test : cases) {
        auto tokens = rstd::serde::to_tokens(rstd::json::from_str(test.input).unwrap()).unwrap();
        auto result = rstd::serde::from_tokens<AdjacentEvent>(tokens.as_slice());
        ASSERT_TRUE(result.is_err());
        EXPECT_EQ(result.as_ref().unwrap_err().kind(), test.error);
        EXPECT_FALSE(rstd::format("{}", result.as_ref().unwrap_err())
                         .as_str()
                         .contains("credential-marker"_str));
    }
    auto tokens = rstd::serde::to_tokens(AdjacentEvent::Idle()).unwrap();
    (void)tokens.pop();
    for (int index = 0; index < 2; ++index) {
        tokens.push(rstd::serde::Token::string("message"_str));
        tokens.push(rstd::serde::Token::null());
    }
    tokens.push(rstd::serde::Token::marker(rstd::serde::TokenKind::MapEnd));
    auto result = rstd::serde::from_tokens<AdjacentEvent>(tokens.as_slice());
    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.as_ref().unwrap_err().kind(), ErrorKind::DuplicateField);
}

TEST(Serde, TaggedVariantDescriptionsRetainRepresentationMetadata) {
    auto internal = TaggedDescription {};
    rstd::serde::describe<InternalEvent>(internal);
    EXPECT_TRUE(internal.internal);
    EXPECT_FALSE(internal.adjacent);
    EXPECT_EQ(internal.count, 6);
    auto adjacent = TaggedDescription {};
    rstd::serde::describe<AdjacentEvent>(adjacent);
    EXPECT_FALSE(adjacent.internal);
    EXPECT_TRUE(adjacent.adjacent);
    EXPECT_EQ(adjacent.count, 2);
}

TEST(Serde, AdjacentVariantKeepsContentAndNestedFieldPaths) {
    const rstd::ref<rstd::str> inputs[] = {
        R"({"type":"Record"})"_str,
        R"({"type":"Record","message":{"secret":null}})"_str,
    };
    for (rstd::usize index; index < rstd::usize(2); ++index) {
        auto tokens =
            rstd::serde::to_tokens(rstd::json::from_str(inputs[index.to_primitive()]).unwrap())
                .unwrap();
        auto result = rstd::serde::from_tokens<AdjacentEvent>(tokens.as_slice());
        ASSERT_TRUE(result.is_err());
        auto path = result.as_ref().unwrap_err().path().segments();
        ASSERT_EQ(path.len(), index + rstd::usize(1));
        EXPECT_EQ(path[rstd::usize()].name().unwrap(), "message"_str);
        if (index != rstd::usize()) EXPECT_EQ(path[rstd::usize(1)].name().unwrap(), "secret"_str);
    }
}

TEST(Serde, TaggedVariantSharesExternalVariantDispatch) {
    using Definition = rstd::serde::
        VariantDefinition<InternalEvent, "ExternalEvent", rstd::serde::Direction::Both>;
    auto tokens   = Vec<rstd::serde::Token>::make();
    auto sink     = rstd::serde::TokenSerializer(tokens);
    auto original = InternalEvent::Record(
        String::make("credential-marker"_str), rstd::u64::MAX, rstd::None(), false);
    ASSERT_TRUE(Definition::serialize(sink, original).is_ok());
    auto source  = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto decoded = Definition::deserialize(source).unwrap();
    ASSERT_TRUE(decoded.is_Record());
    EXPECT_EQ(decoded.as_Record().secret.as_str(), "credential-marker"_str);
    EXPECT_EQ(decoded.as_Record().count, rstd::u64::MAX);
    EXPECT_EQ(source.position(), tokens.len());
}

TEST(Serde, InternalVariantRejectPolicyReachesPayloadRecords) {
    using Definition =
        rstd::serde::InternalVariantDefinition<InternalEvent,
                                               "StrictEvent",
                                               rstd::serde::Direction::Both,
                                               "type",
                                               rstd::serde::UnknownFieldPolicy::Reject>;
    auto tokens =
        rstd::serde::to_tokens(
            rstd::json::from_str(R"({"type":"Record","secret":"credential-marker","count":1})"_str)
                .unwrap())
            .unwrap();
    auto source  = rstd::serde::TokenDeserializer(tokens.as_slice());
    auto decoded = Definition::deserialize(source).unwrap();
    ASSERT_TRUE(decoded.is_Record());
    EXPECT_TRUE(decoded.as_Record().note.is_none());
    EXPECT_FALSE(decoded.as_Record().enabled);

    auto extra =
        rstd::serde::to_tokens(
            rstd::json::from_str(
                R"({"type":"Record","secret":"credential-marker","count":1,"extra":null})"_str)
                .unwrap())
            .unwrap();
    auto strict   = rstd::serde::TokenDeserializer(extra.as_slice());
    auto rejected = Definition::deserialize(strict);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::UnknownField);

    auto empty = rstd::serde::TokenDeserializer(extra.as_slice());
    auto access =
        empty.begin_internal_enum("type"_str, rstd::serde::UnknownFieldPolicy::Reject).unwrap();
    auto empty_record = access.value<EmptyTaggedRecord>();
    ASSERT_TRUE(empty_record.is_err());
    EXPECT_EQ(empty_record.as_ref().unwrap_err().kind(), rstd::serde::ErrorKind::UnknownField);
}

TEST(SerdeJsonTagged, AuthMethodsDecodeThroughThePublicJsonEntryPoints) {
    const rstd::ref<rstd::str> inputs[] = {
        R"({"type":"Username","username":"alice","pw":"credential-marker"})"_str,
        R"({"pw":"credential-marker","phone":"123","type":"Phone"})"_str,
        R"({"email":"a@example.com","type":"Email","pw":"credential-marker"})"_str,
        R"({"key":"qr-key","type":"Qr"})"_str,
    };
    for (auto input : inputs) {
        auto decoded = rstd::json::decode<JsonAuthMethod>(input);
        ASSERT_TRUE(decoded.is_ok());
        auto encoded  = rstd::json::to_value(*decoded).unwrap();
        auto original = rstd::json::from_str(input).unwrap();
        EXPECT_EQ(rstd::json::to_string(encoded), rstd::json::to_string(original));
        EXPECT_TRUE(rstd::json::decode_value<JsonAuthMethod>(original).is_ok());
    }
    auto extra =
        rstd::json::decode<JsonAuthMethod>(
            R"({"type":"Username","username":"alice","pw":"credential-marker","extra":{"nested":[null,1]}})"_str)
            .unwrap();
    EXPECT_EQ(extra.as_Username().username.as_str(), "alice"_str);
    EXPECT_EQ(extra.as_Username().pw.as_str(), "credential-marker"_str);
    EXPECT_TRUE(
        rstd::json::decode<JsonAuthMethod>(R"({"type":"Username","username":"","pw":""})"_str)
            .is_ok());
}

TEST(SerdeJsonTagged, AuthInputReusesTheNestedMethodAndOptionalDefaults) {
    auto decoded =
        rstd::json::decode<JsonAuthInput>(
            R"({"server_url":"https://server","method":{"type":"Username","username":"alice","pw":"credential-marker"}})"_str)
            .unwrap();
    ASSERT_TRUE(decoded.method.is_some());
    EXPECT_EQ(decoded.method->as_Username().username.as_str(), "alice"_str);
    auto encoded = rstd::json::to_value(decoded).unwrap();
    EXPECT_EQ((*encoded.get("server_url"_str))->as_str().unwrap(), "https://server"_str);
    const rstd::ref<rstd::str> optional[] = {
        R"({"server_url":"https://server"})"_str,
        R"({"server_url":"https://server","method":null})"_str,
    };
    for (auto input : optional)
        EXPECT_TRUE(rstd::json::decode<JsonAuthInput>(input).unwrap().method.is_none());
    EXPECT_TRUE(rstd::json::decode<JsonAuthInput>(R"({"method":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<JsonAuthInput>(R"({"server_url":null})"_str).is_err());
}

TEST(SerdeJsonTagged, AuthOutcomesDecodeThroughThePublicJsonEntryPoints) {
    const rstd::ref<rstd::str> inputs[] = {
        R"({"type":"Ok"})"_str,
        R"({"message":{"message":"denied"},"type":"Failed"})"_str,
        R"({"type":"WrongPassword"})"_str,
        R"({"type":"NoSuchUsername"})"_str,
        R"({"type":"NoSuchEmail"})"_str,
        R"({"type":"NoSuchPhone"})"_str,
        R"({"type":"QrExpired"})"_str,
        R"({"type":"QrWaitScan"})"_str,
        R"({"type":"QrWaitComform","message":{"name":"alice","avatar_url":"avatar","message":"confirm"}})"_str,
    };
    for (auto input : inputs) {
        auto decoded = rstd::json::decode<JsonAuthOutcome>(input);
        ASSERT_TRUE(decoded.is_ok());
        auto encoded  = rstd::json::to_value(*decoded).unwrap();
        auto original = rstd::json::from_str(input).unwrap();
        EXPECT_EQ(rstd::json::to_string(encoded), rstd::json::to_string(original));
        EXPECT_TRUE(rstd::json::decode_value<JsonAuthOutcome>(original).is_ok());
    }
    EXPECT_TRUE(rstd::json::decode<JsonAuthOutcome>(
                    R"({"type":"Ok","message":null,"extra":{"nested":[null]}})"_str)
                    .unwrap()
                    .is_Ok());
    auto extra =
        rstd::json::decode<JsonAuthOutcome>(
            R"({"type":"Failed","message":{"message":"denied","extra":true},"extra":null})"_str)
            .unwrap();
    EXPECT_EQ(extra.as_Failed().message.as_str(), "denied"_str);
}

TEST(SerdeJsonTagged, InvalidAuthFieldsDoNotDefaultNullOrLeakCredentials) {
    const rstd::ref<rstd::str> methods[] = {
        R"({"Username":{"username":"alice","pw":"credential-marker"}})"_str,
        R"({"type":"credential-marker"})"_str,
        R"({"type":null})"_str,
        R"({"type":2})"_str,
        R"({"type":"Username","username":"alice"})"_str,
        R"({"type":"Username","username":null,"pw":"credential-marker"})"_str,
        R"({"type":"Phone","phone":"123","pw":null})"_str,
        R"({"type":"Email","pw":"credential-marker"})"_str,
        R"({"type":"Qr","key":null})"_str,
        R"(null)"_str,
    };
    for (auto input : methods) {
        auto result = rstd::json::decode<JsonAuthMethod>(input);
        ASSERT_TRUE(result.is_err());
        EXPECT_FALSE(rstd::format("{}", result.as_ref().unwrap_err())
                         .as_str()
                         .contains("credential-marker"_str));
    }
    const rstd::ref<rstd::str> outcomes[] = {
        R"({})"_str,
        R"({"type":"credential-marker"})"_str,
        R"({"type":null})"_str,
        R"({"type":"Failed"})"_str,
        R"({"type":"Failed","message":null})"_str,
        R"({"type":"Failed","message":"credential-marker"})"_str,
        R"({"type":"Failed","message":{}})"_str,
        R"({"type":"Failed","message":{"message":null}})"_str,
        R"({"type":"QrWaitComform","message":{"name":"alice","message":"credential-marker"}})"_str,
        R"({"type":"Ok","message":{}})"_str,
        R"(null)"_str,
    };
    for (auto input : outcomes) {
        auto result = rstd::json::decode<JsonAuthOutcome>(input);
        ASSERT_TRUE(result.is_err());
        EXPECT_FALSE(rstd::format("{}", result.as_ref().unwrap_err())
                         .as_str()
                         .contains("credential-marker"_str));
    }
}

TEST(SerdeJsonTagged, InternalPayloadViewsPreserveOriginalDataAndPrecision) {
    auto input =
        rstd::json::from_str(R"({"type":"Map","count":18446744073709551615})"_str).unwrap();
    auto decoded = rstd::json::decode_value<InternalEvent>(input).unwrap();
    ASSERT_TRUE(decoded.is_Map());
    EXPECT_EQ(decoded.as_Map().values.len(), rstd::usize(1));
    EXPECT_EQ(*decoded.as_Map().values.get("count"_str).unwrap(), rstd::u64::MAX);
    EXPECT_TRUE(input.get("type"_str).is_some());
    EXPECT_TRUE(rstd::json::decode_value<InternalEvent>(input).is_ok());

    auto object =
        rstd::json::from_str(
            R"({"type":"Object","count":18446744073709551615,"nested":{"type":"Qr"}})"_str)
            .unwrap();
    auto raw = rstd::json::decode_value<JsonObjectEvent>(object).unwrap();
    EXPECT_TRUE(raw.as_Object().value.get("type"_str).is_none());
    EXPECT_EQ((*raw.as_Object().value.get("count"_str))->as_u64().unwrap(), rstd::u64::MAX);
    EXPECT_TRUE((*raw.as_Object().value.get("nested"_str))->get("type"_str).is_some());
    EXPECT_TRUE(object.get("type"_str).is_some());
}

TEST(SerdeJsonTagged, StrictPoliciesRejectOnlyUnknownFields) {
    EXPECT_TRUE(rstd::json::decode<StrictJsonEvent>(R"({"type":"Idle"})"_str).is_ok());
    EXPECT_TRUE(rstd::json::decode<StrictJsonEvent>(R"({"type":"Qr","key":"key"})"_str).is_ok());
    EXPECT_TRUE(
        rstd::json::decode<StrictJsonEvent>(R"({"type":"Idle","extra":null})"_str).is_err());
    auto rejected = rstd::json::decode<StrictJsonEvent>(
        R"({"type":"Qr","key":"credential-marker","extra":null})"_str);
    ASSERT_TRUE(rejected.is_err());
    EXPECT_EQ(rejected.as_ref().unwrap_err().data_error()->get().kind(),
              rstd::serde::ErrorKind::UnknownField);
    EXPECT_TRUE(
        rstd::json::decode<StrictJsonAdjacent>(R"({"type":"Ok","message":null})"_str).is_ok());
    EXPECT_TRUE(
        rstd::json::decode<StrictJsonAdjacent>(R"({"type":"Ok","extra":null})"_str).is_err());
    EXPECT_TRUE(rstd::json::decode<InternalEvent>(R"({"type":"Idle","extra":null})"_str).is_ok());
}

TEST(SerdeJsonTagged, AuthFieldErrorsPreserveNestedPaths) {
    auto internal = rstd::json::decode<JsonAuthInput>(
        R"({"server_url":"server","method":{"type":"Username","username":"alice","pw":null}})"_str);
    ASSERT_TRUE(internal.is_err());
    auto method_path = internal.as_ref().unwrap_err().data_error()->get().path().segments();
    ASSERT_EQ(method_path.len(), rstd::usize(2));
    EXPECT_EQ(method_path[rstd::usize()].name().unwrap(), "method"_str);
    EXPECT_EQ(method_path[rstd::usize(1)].name().unwrap(), "pw"_str);
    auto adjacent =
        rstd::json::decode<JsonAuthOutcome>(R"({"type":"Failed","message":{"message":null}})"_str);
    ASSERT_TRUE(adjacent.is_err());
    auto message_path = adjacent.as_ref().unwrap_err().data_error()->get().path().segments();
    ASSERT_EQ(message_path.len(), rstd::usize(2));
    EXPECT_EQ(message_path[rstd::usize()].name().unwrap(), "message"_str);
    EXPECT_EQ(message_path[rstd::usize(1)].name().unwrap(), "message"_str);
}
