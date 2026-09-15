#include <rstd/test/gtest.hpp>

import rstd.toml;
import rstd.serde;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

struct KindObservation {
    rstd::serde::ValueKind kind;
    bool                   stable {};
    Option<String>         text;
    Vec<String>            items;
};

namespace rstd
{
template<>
struct Impl<serde::Deserialize, KindObservation> {
    template<typename D>
    static auto deserialize(D& decoder) -> Result<KindObservation, typename D::error_type> {
        auto result   = KindObservation { .kind = decoder.kind() };
        result.stable = result.kind == decoder.kind();
        if (result.kind == serde::ValueKind::String) {
            auto text = decoder.deserialize_string();
            if (text.is_err()) return Err(rstd::move(text).unwrap_err());
            result.text = Some(rstd::move(text).unwrap());
        } else if (result.kind == serde::ValueKind::Sequence) {
            auto items = serde::deserialize<Vec<String>>(decoder);
            if (items.is_err()) return Err(rstd::move(items).unwrap_err());
            result.items = rstd::move(items).unwrap();
        }
        return Ok(rstd::move(result));
    }
};
} // namespace rstd

TEST(TomlSerialize, DeserializerKindDoesNotConsumeValue) {
    auto parsed = rstd::toml::from_str(R"(
text = "value"
flag = false
count = -7
ratio = 1.25
items = ["one"]
record = { name = "nested" }
date = 2026-09-15
)"_str);
    ASSERT_TRUE(parsed.is_ok());
    using Kind = rstd::serde::ValueKind;
    struct Case {
        ref<str> key;
        Kind     kind;
    };
    const Case cases[] = {
        { "text"_str, Kind::String },         { "flag"_str, Kind::Boolean },
        { "count"_str, Kind::SignedInteger }, { "ratio"_str, Kind::Float },
        { "items"_str, Kind::Sequence },      { "record"_str, Kind::Map },
        { "date"_str, Kind::Extension },
    };
    for (const auto& item : cases) {
        auto observed = rstd::toml::decode_value<KindObservation>(**parsed->get(item.key));
        ASSERT_TRUE(observed.is_ok());
        EXPECT_EQ(observed->kind, item.kind);
        EXPECT_TRUE(observed->stable);
    }
    auto text = rstd::toml::decode_value<KindObservation>(**parsed->get("text"_str));
    ASSERT_TRUE(text.is_ok());
    ASSERT_TRUE(text->text.is_some());
    EXPECT_EQ(text->text->as_str(), "value"_str);
    auto items = rstd::toml::decode_value<KindObservation>(**parsed->get("items"_str));
    ASSERT_TRUE(items.is_ok());
    ASSERT_EQ(items->items.len(), usize(1));
    EXPECT_EQ(items->items[usize {}].as_str(), "one"_str);
}

TEST(TomlSerialize, RoundTripsCanonicalDocument) {
    auto parsed = rstd::toml::from_str(R"(
title = "demo"
enabled = true
count = -7
ratio = 1.25
when = 2026-08-14T03:04:05.123Z
values = [1, "two", { ready = true }]

[toolchain]
cxx = "clang++"

[[target]]
name = "first"
[target.metadata]
kind = "library"

[[target]]
name = "second"
)"_str);
    ASSERT_TRUE(parsed.is_ok());

    auto encoded = rstd::toml::to_string(*parsed);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_TRUE(encoded->as_str().contains("[toolchain]"_str));
    EXPECT_TRUE(encoded->as_str().contains("[[target]]"_str));
    EXPECT_TRUE(encoded->as_str().ends_with("\n"_str));

    auto reparsed = rstd::toml::from_str(encoded->as_str());
    ASSERT_TRUE(reparsed.is_ok());
    EXPECT_EQ(*reparsed, *parsed);

    auto repeated = rstd::toml::to_string(*reparsed);
    ASSERT_TRUE(repeated.is_ok());
    EXPECT_EQ(*repeated, *encoded);
}

TEST(TomlSerialize, EncodesKeysStringsScalarsAndInlineValues) {
    auto table = rstd::toml::Table::make();
    table.insert(String::make("quoted.key"_str),
                 rstd::toml::Value::String(String::make("line\n\x7f"_str)));
    table.insert(String::make("number"_str), rstd::toml::Value::Float(f64(3)));
    auto value   = rstd::toml::Value::Table(rstd::move(table));
    auto encoded = rstd::toml::to_value_string(value);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_TRUE(encoded->as_str().contains(R"("quoted.key" = "line\n\u007f")"_str));
    EXPECT_TRUE(encoded->as_str().contains("number = 3.0"_str));

    auto wrapped = rstd::format("value = {}\n", encoded->as_str());
    auto parsed  = rstd::toml::from_str(wrapped.as_str());
    ASSERT_TRUE(parsed.is_ok());
    EXPECT_EQ(**parsed->get("value"_str), value);
}

TEST(TomlSerialize, RejectsInvalidRootAndDateTime) {
    auto scalar = rstd::toml::Value::Integer(i64(1));
    EXPECT_TRUE(rstd::toml::to_string(scalar).is_err());

    auto invalid = rstd::toml::Value::LocalDate(
        rstd::toml::LocalDate { .year = uint16_t(2026), .month = uint8_t(2), .day = uint8_t(30) });
    EXPECT_TRUE(rstd::toml::to_value_string(invalid).is_err());

    auto empty = rstd::toml::Value::Table(rstd::toml::Table::make());
    auto text  = rstd::toml::to_string(empty);
    ASSERT_TRUE(text.is_ok());
    EXPECT_EQ(text->as_str(), "\n"_str);
}

TEST(TomlSerialize, EncodesDottedKeys) {
    auto key = rstd::toml::parse_key_path(R"(patch."https://example.com/a=b".path)"_str);
    ASSERT_TRUE(key.is_ok());
    EXPECT_EQ(rstd::toml::to_key_string(*key).as_str(),
              R"(patch."https://example.com/a=b".path)"_str);
}
