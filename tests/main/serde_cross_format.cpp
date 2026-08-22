#include <rstd/test/gtest.hpp>

import rstd.json;
import rstd.toml;
import rstd.tests.serde_fixture;

using namespace rstd::literals;
using ::alloc::string::String;
using ::alloc::vec::Vec;

TEST(SerdeCrossFormat, ExplicitImplDrivesJsonAndToml) {
    auto ports = Vec<rstd::u64>::make();
    ports.push(rstd::u64(80));
    ports.push(rstd::u64(443));
    auto value = rstd::tests::WireConfig { String::make("server"_str), rstd::move(ports) };

    auto json = rstd::json::encode(value);
    ASSERT_TRUE(json.is_ok());
    auto json_value = rstd::json::decode<rstd::tests::WireConfig>(json->as_str());
    ASSERT_TRUE(json_value.is_ok());
    EXPECT_EQ(json_value->ports[rstd::usize(1)], rstd::u64(443));

    auto toml = rstd::toml::encode(value);
    ASSERT_TRUE(toml.is_ok());
    EXPECT_TRUE(toml->as_str().contains("name = \"server\""_str));
    EXPECT_TRUE(toml->as_str().contains("ports = [80, 443]"_str));
    auto toml_value = rstd::toml::decode<rstd::tests::WireConfig>(toml->as_str());
    ASSERT_TRUE(toml_value.is_ok());
    EXPECT_EQ(toml_value->name.as_str(), "server"_str);
}

TEST(SerdeCrossFormat, TomlDataErrorKeepsNestedPath) {
    auto result = rstd::toml::decode<rstd::tests::WireConfig>(
        "name = \"server\"\nports = [80, \"https\"]\n"_str);

    ASSERT_TRUE(result.is_err());
    auto error = rstd::move(result).unwrap_err_unchecked();
    ASSERT_TRUE(error.is_data());
    auto path = error.data_error()->get().path().segments();
    ASSERT_EQ(path.len(), rstd::usize(2));
    EXPECT_EQ(path[rstd::usize()].name().unwrap(), "ports"_str);
    EXPECT_EQ(path[rstd::usize(1)].index(), rstd::usize(1));
}

TEST(SerdeCrossFormat, ValueDecodePreservesCallerRootPath) {
    auto json = rstd::json::from_str("{\"name\":\"server\",\"ports\":[80,\"https\"]}"_str);
    ASSERT_TRUE(json.is_ok());
    auto json_result = rstd::json::decode_value<rstd::tests::WireConfig>(
        *json, rstd::serde::DataPath().with_field("catalog"_str));
    ASSERT_TRUE(json_result.is_err());
    auto json_error = rstd::move(json_result).unwrap_err_unchecked();
    auto json_path  = json_error.path().segments();
    ASSERT_EQ(json_path.len(), rstd::usize(3));
    EXPECT_EQ(json_path[rstd::usize()].name().unwrap(), "catalog"_str);
    EXPECT_EQ(json_path[rstd::usize(1)].name().unwrap(), "ports"_str);
    EXPECT_EQ(json_path[rstd::usize(2)].index(), rstd::usize(1));

    auto toml = rstd::toml::from_str("name = \"server\"\nports = [80, \"https\"]\n"_str);
    ASSERT_TRUE(toml.is_ok());
    auto toml_result = rstd::toml::decode_value<rstd::tests::WireConfig>(
        *toml, rstd::serde::DataPath().with_field("catalog"_str));
    ASSERT_TRUE(toml_result.is_err());
    auto toml_error = rstd::move(toml_result).unwrap_err_unchecked();
    auto toml_path  = toml_error.path().segments();
    ASSERT_EQ(toml_path.len(), rstd::usize(3));
    EXPECT_EQ(toml_path[rstd::usize()].name().unwrap(), "catalog"_str);
    EXPECT_EQ(toml_path[rstd::usize(1)].name().unwrap(), "ports"_str);
    EXPECT_EQ(toml_path[rstd::usize(2)].index(), rstd::usize(1));
}

TEST(SerdeCrossFormat, TomlRejectsUnrepresentableUnsignedInteger) {
    auto value  = rstd::u64::MAX;
    auto result = rstd::toml::to_value(value);

    ASSERT_TRUE(result.is_err());
    EXPECT_EQ(result.unwrap_err().kind(), rstd::serde::ErrorKind::InvalidValue);
}

TEST(SerdeCrossFormat, TomlDateUsesAnExplicitFormatExtension) {
    auto date = rstd::toml::LocalDate { .year = 2026, .month = 8, .day = 22 };

    auto encoded = rstd::toml::to_value(date);
    ASSERT_TRUE(encoded.is_ok());
    EXPECT_TRUE(encoded->is_local_date());
    auto decoded = rstd::toml::decode_value<rstd::toml::LocalDate>(*encoded);
    ASSERT_TRUE(decoded.is_ok());
    EXPECT_EQ(*decoded, date);

    auto json = rstd::json::to_value(date);
    ASSERT_TRUE(json.is_err());
    EXPECT_EQ(json.unwrap_err().kind(), rstd::serde::ErrorKind::Unsupported);
}
