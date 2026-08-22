#include <rstd/test/gtest.hpp>
#include <string>

import rstd.json;

using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

namespace
{

struct JsonConfig {
    String               name;
    Vec<rstd::u64>       ports;
    rstd::Option<String> note;
};

auto text(const String& value) -> std::string {
    return { reinterpret_cast<const char*>(value.as_raw_ptr()), value.len().to_primitive() };
}

} // namespace

template<>
struct rstd::Impl<rstd::serde::Serialize, JsonConfig> {
    template<typename Serializer>
    static auto serialize(Serializer& serializer, const JsonConfig& value) ->
        typename Serializer::result_type {
        auto map = serializer.begin_map(rstd::usize(3));
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto output = rstd::move(map).unwrap_unchecked();
        auto name   = rstd::serde::field(output, "name"_str, value.name);
        if (name.is_err()) return rstd::Err(rstd::move(name).unwrap_err_unchecked());
        auto ports = rstd::serde::field(output, "ports"_str, value.ports);
        if (ports.is_err()) return rstd::Err(rstd::move(ports).unwrap_err_unchecked());
        auto note = rstd::serde::field(output, "note"_str, value.note);
        if (note.is_err()) return rstd::Err(rstd::move(note).unwrap_err_unchecked());
        return output.end();
    }
};

template<>
struct rstd::Impl<rstd::serde::Deserialize, JsonConfig> {
    template<typename Deserializer>
    static auto deserialize(Deserializer& deserializer)
        -> rstd::Result<JsonConfig, typename Deserializer::error_type> {
        auto map = deserializer.begin_map();
        if (map.is_err()) return rstd::Err(rstd::move(map).unwrap_err_unchecked());
        auto input = rstd::move(map).unwrap_unchecked();
        auto name  = rstd::Option<String>();
        auto ports = rstd::Option<Vec<rstd::u64>>();
        auto note  = rstd::Option<rstd::Option<String>>();
        for (;;) {
            auto key = input.template next_key<String>();
            if (key.is_err()) return rstd::Err(rstd::move(key).unwrap_err_unchecked());
            auto optional = rstd::move(key).unwrap_unchecked();
            if (optional.is_none()) break;
            auto key_value = rstd::move(optional).unwrap_unchecked();
            if (key_value.as_str() == "name"_str) {
                if (name.is_some()) return rstd::Err(deserializer.duplicate_field("name"_str));
                auto value = input.template next_value<String>();
                if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
                name = rstd::Some(rstd::move(value).unwrap_unchecked());
            } else if (key_value.as_str() == "ports"_str) {
                if (ports.is_some()) return rstd::Err(deserializer.duplicate_field("ports"_str));
                auto value = input.template next_value<Vec<rstd::u64>>();
                if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
                ports = rstd::Some(rstd::move(value).unwrap_unchecked());
            } else if (key_value.as_str() == "note"_str) {
                if (note.is_some()) return rstd::Err(deserializer.duplicate_field("note"_str));
                auto value = input.template next_value<rstd::Option<String>>();
                if (value.is_err()) return rstd::Err(rstd::move(value).unwrap_err_unchecked());
                note = rstd::Some(rstd::move(value).unwrap_unchecked());
            } else {
                return rstd::Err(deserializer.unknown_field(key_value.as_str()));
            }
        }
        auto end = input.end();
        if (end.is_err()) return rstd::Err(rstd::move(end).unwrap_err_unchecked());
        if (name.is_none()) return rstd::Err(deserializer.missing_field("name"_str));
        if (ports.is_none()) return rstd::Err(deserializer.missing_field("ports"_str));
        if (note.is_none()) note = rstd::Some(rstd::None<String>());
        return rstd::Ok(JsonConfig { rstd::move(name).unwrap_unchecked(),
                                     rstd::move(ports).unwrap_unchecked(),
                                     rstd::move(note).unwrap_unchecked() });
    }
};

TEST(JsonTyped, ValueAndTextUseTheSameExplicitImpl) {
    auto ports = Vec<rstd::u64>::make();
    ports.push(rstd::u64(80));
    ports.push(rstd::u64(443));
    auto config = JsonConfig { String::make("server"_str), rstd::move(ports), rstd::None() };

    auto value = rstd::json::to_value(config);
    ASSERT_TRUE(value.is_ok());
    auto decoded_value = rstd::json::decode_value<JsonConfig>(*value);
    ASSERT_TRUE(decoded_value.is_ok());
    EXPECT_EQ(decoded_value->name.as_str(), "server"_str);
    EXPECT_EQ(decoded_value->ports.len(), rstd::usize(2));

    auto encoded = rstd::json::encode(config);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_EQ(text(*encoded), R"({"name":"server","note":null,"ports":[80,443]})");
    auto decoded_text = rstd::json::decode<JsonConfig>(encoded->as_str());
    ASSERT_TRUE(decoded_text.is_ok());
    EXPECT_EQ(decoded_text->ports[rstd::usize(1)], rstd::u64(443));
}

TEST(JsonTyped, SeparatesSyntaxAndDataErrors) {
    auto syntax = rstd::json::decode<JsonConfig>("{"_str);
    ASSERT_TRUE(syntax.is_err());
    EXPECT_TRUE(syntax.unwrap_err().is_syntax());

    auto data =
        rstd::json::decode<JsonConfig>(R"({"name":"server","ports":[80,"https"],"note":null})"_str);
    ASSERT_TRUE(data.is_err());
    auto error = rstd::move(data).unwrap_err_unchecked();
    ASSERT_TRUE(error.is_data());
    auto path = error.data_error()->get().path().segments();
    ASSERT_EQ(path.len(), rstd::usize(2));
    EXPECT_EQ(path[rstd::usize()].name().unwrap(), "ports"_str);
    EXPECT_EQ(path[rstd::usize(1)].index(), rstd::usize(1));
}

TEST(JsonTyped, MissingAndUnknownFieldsAreOwnedByExplicitImpl) {
    auto missing = rstd::json::decode<JsonConfig>(R"({"ports":[]})"_str);
    ASSERT_TRUE(missing.is_err());
    EXPECT_EQ(missing.unwrap_err().data_error()->get().kind(),
              rstd::serde::ErrorKind::MissingField);

    auto unknown =
        rstd::json::decode<JsonConfig>(R"({"name":"server","ports":[],"extra":true})"_str);
    ASSERT_TRUE(unknown.is_err());
    EXPECT_EQ(unknown.unwrap_err().data_error()->get().kind(),
              rstd::serde::ErrorKind::UnknownField);
}
