#include <rstd/test/gtest.hpp>

import rstd.toml;
import rstd;

using namespace rstd::prelude;
using namespace rstd::literals;

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
