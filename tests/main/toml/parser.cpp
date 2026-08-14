#include <rstd/test/gtest.hpp>

import rstd.toml;

using namespace rstd::prelude;
using namespace rstd::literals;
using rstd::toml::Value;

namespace
{

auto parse(ref<str> input) {
    return rstd::toml::from_str(input);
}

auto member(const Value& value, ref<str> key) -> const Value& {
    auto found = value.get(key);
    EXPECT_TRUE(found.is_some());
    return **found;
}

} // namespace

TEST(TomlParser, ParsesManifestShapedDocument) {
    auto result = parse(R"(
[package]
name = "demo-base"
version = "0.1.0"

[lib]
name = "demo.base"
sources = [
  "src/lib.cppm",
  "src/detail.cppm",
  "src/lib.cpp",
]

[dependencies]
support = { path = "../support", version = "0.2.0" }
)"_str);
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    const auto& package = member(document, "package"_str);
    EXPECT_EQ(*member(package, "name"_str).as_str(), rstd::ref<rstd::str>("demo-base"_str));

    const auto& library = member(document, "lib"_str);
    EXPECT_EQ(*member(library, "name"_str).as_str(), rstd::ref<rstd::str>("demo.base"_str));
    auto sources = member(library, "sources"_str).as_array();
    ASSERT_TRUE(sources.is_some());
    ASSERT_EQ((**sources).len(), usize(3));
    EXPECT_EQ(*(**sources)[usize(1)].as_str(), rstd::ref<rstd::str>("src/detail.cppm"_str));

    const auto& dependency = member(member(document, "dependencies"_str), "support"_str);
    EXPECT_EQ(*member(dependency, "path"_str).as_str(), rstd::ref<rstd::str>("../support"_str));
}

TEST(TomlParser, ParsesDottedKeysArraysOfTablesAndScalars) {
    auto result = parse(R"(
title = 'demo'
owner.name = "Tenon"
enabled = true
count = -42
ratio = 1.25e2
mask = 0xff
when = 2026-07-22T03:04:05.123456789Z

[[target]]
name = "first"

[[target]]
name = "second"
)"_str);
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    EXPECT_EQ(member(document, "count"_str).as_integer(), Some(i64(-42)));
    EXPECT_EQ(member(document, "ratio"_str).as_float(), Some(f64(125)));
    EXPECT_EQ(member(document, "mask"_str).as_integer(), Some(i64(255)));
    EXPECT_TRUE(member(document, "when"_str).is_offset_datetime());
    EXPECT_EQ(*member(member(document, "owner"_str), "name"_str).as_str(),
              rstd::ref<rstd::str>("Tenon"_str));

    auto targets = member(document, "target"_str).as_array();
    ASSERT_TRUE(targets.is_some());
    ASSERT_EQ((**targets).len(), usize(2));
    EXPECT_EQ(*member((**targets)[usize(1)], "name"_str).as_str(),
              rstd::ref<rstd::str>("second"_str));
}

TEST(TomlParser, DecodesStringsAndDateTimeKinds) {
    auto result = parse(R"(
escaped = "line\n\u4f60\u597d"
literal = 'C:\tools\tenon'
multiline = """
first \
  second
"""
date = 2026-07-22
time = 03:04:05.5
local = 2026-07-22 03:04:05
offset = 2026-07-22T03:04:05-07:30
)"_str);
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    EXPECT_EQ(*member(document, "escaped"_str).as_str(), rstd::ref<rstd::str>("line\n你好"_str));
    EXPECT_EQ(*member(document, "literal"_str).as_str(),
              rstd::ref<rstd::str>(R"(C:\tools\tenon)"_str));
    EXPECT_EQ(*member(document, "multiline"_str).as_str(),
              rstd::ref<rstd::str>("first second\n"_str));
    EXPECT_TRUE(member(document, "date"_str).is_local_date());
    EXPECT_TRUE(member(document, "time"_str).is_local_time());
    EXPECT_TRUE(member(document, "local"_str).is_local_datetime());
    EXPECT_TRUE(member(document, "offset"_str).is_offset_datetime());
}

TEST(TomlParser, SupportsToml11ScalarAndContainerSyntax) {
    auto result = parse(R"TOML(
escaped = "\e\x41"
exponent = 3e0
hex = 0xDEADBEEF
time = 03:04
local = 2026-07-22T03:04
quotes = """two quotes: """""
inline = {
  # comment
  name = "demo",
}
)TOML"_str);
    ASSERT_TRUE(result.is_ok());
    auto document = result.unwrap();

    EXPECT_EQ(*member(document, "escaped"_str).as_str(),
              rstd::ref<rstd::str>("\x1b"_str
                                   "A"_str));
    EXPECT_EQ(member(document, "exponent"_str).as_float(), Some(f64(3)));
    EXPECT_EQ(member(document, "hex"_str).as_integer(), Some(i64(3735928559)));
    auto time = member(document, "time"_str).as_local_time();
    ASSERT_TRUE(time.is_some());
    EXPECT_EQ(time->second, rstd::uint8_t {});
    EXPECT_TRUE(member(document, "local"_str).is_local_datetime());
    EXPECT_EQ(*member(document, "quotes"_str).as_str(),
              rstd::ref<rstd::str>("two quotes: \"\""_str));
    EXPECT_EQ(*member(member(document, "inline"_str), "name"_str).as_str(),
              rstd::ref<rstd::str>("demo"_str));
}

TEST(TomlParser, RejectsDuplicateAndMalformedInput) {
    EXPECT_TRUE(parse("name = 1\nname = 2\n"_str).is_err());
    EXPECT_TRUE(parse("[package]\n[package]\n"_str).is_err());
    EXPECT_TRUE(parse("value = 01\n"_str).is_err());
    EXPECT_TRUE(parse("value = [1 2]\n"_str).is_err());
    EXPECT_TRUE(parse("value = \"\\q\"\n"_str).is_err());
    EXPECT_TRUE(parse("value = +0x1\n"_str).is_err());
    EXPECT_TRUE(parse("value = 1e_2\n"_str).is_err());
    EXPECT_TRUE(parse("value = \"\x7f\"\n"_str).is_err());
    EXPECT_TRUE(parse("value = 1 # \x7f\n"_str).is_err());
    EXPECT_TRUE(parse("value = \"\"\"six quotes: \"\"\"\"\"\"\n"_str).is_err());

    auto trailing = parse("name = 1 unexpected\n"_str);
    ASSERT_TRUE(trailing.is_err());
    EXPECT_TRUE(trailing.unwrap_err().is_syntax());
    EXPECT_EQ(trailing.unwrap_err().line(), usize(1));
}

TEST(TomlParser, SliceAndFromStrTraitReuseDecoder) {
    auto from_slice = rstd::toml::from_slice("x = 1\n"_bytes);
    auto from_trait = rstd::from_str<Value>("x = 1\n"_str);

    ASSERT_TRUE(from_slice.is_ok());
    ASSERT_TRUE(from_trait.is_ok());
    EXPECT_EQ(*from_slice, *from_trait);

    rstd::byte invalid[] = { rstd::byte { 0xff } };
    auto       invalid_result =
        rstd::toml::from_slice(rstd::slice<rstd::u8>::from_raw_parts(invalid, usize(1)));
    ASSERT_TRUE(invalid_result.is_err());
    EXPECT_TRUE(invalid_result.unwrap_err().is_syntax());
}

TEST(TomlParser, TracksImplicitDottedAndArrayTableLifecycle) {
    auto result = parse(R"(
fruit.apple.color = "red"
fruit.apple.taste.sweet = true

[fruit.apple.texture]
smooth = true

[service.api.http]
port = 8080

[service]
enabled = true

[[products]]
name = "hammer"
[products.meta]
kind = "tool"
[[products.tags]]
name = "metal"

[[products]]
name = "nail"
[[products.tags]]
name = "small"
)"_str);
    ASSERT_TRUE(result.is_ok());
    auto document = result.unwrap();

    const auto& service = member(document, "service"_str);
    EXPECT_EQ(member(service, "enabled"_str).as_bool(), Some(true));
    EXPECT_EQ(member(member(member(service, "api"_str), "http"_str), "port"_str).as_integer(),
              Some(i64(8080)));

    const auto& apple = member(member(document, "fruit"_str), "apple"_str);
    EXPECT_EQ(*member(apple, "color"_str).as_str(), rstd::ref<rstd::str>("red"_str));
    EXPECT_EQ(member(member(apple, "taste"_str), "sweet"_str).as_bool(), Some(true));
    EXPECT_EQ(member(member(apple, "texture"_str), "smooth"_str).as_bool(), Some(true));

    auto products = member(document, "products"_str).as_array();
    ASSERT_TRUE(products.is_some());
    ASSERT_EQ((**products).len(), usize(2));
    EXPECT_EQ(*member((**products)[usize(1)], "name"_str).as_str(),
              rstd::ref<rstd::str>("nail"_str));
    auto tags = member((**products)[usize(1)], "tags"_str).as_array();
    ASSERT_TRUE(tags.is_some());
    ASSERT_EQ((**tags).len(), usize(1));
    EXPECT_EQ(*member((**tags)[usize()], "name"_str).as_str(), rstd::ref<rstd::str>("small"_str));
}

TEST(TomlParser, RejectsSealedAndRedefinedTableKinds) {
    EXPECT_TRUE(parse("fruit.apple.color = \"red\"\n[fruit.apple]\n"_str).is_err());
    EXPECT_TRUE(parse("[product]\ntype = { name = \"Nail\" }\ntype.edible = false\n"_str).is_err());
    EXPECT_TRUE(parse("[product]\ntype.name = \"Nail\"\ntype = { edible = false }\n"_str).is_err());
    EXPECT_TRUE(parse("[[fruits]]\n[fruits]\n"_str).is_err());
    EXPECT_TRUE(parse("[fruits]\n[[fruits]]\n"_str).is_err());
    EXPECT_TRUE(parse("fruits = []\n[[fruits]]\n"_str).is_err());
    EXPECT_TRUE(parse("[fruit.physical]\ncolor = \"red\"\n[[fruit]]\n"_str).is_err());
    EXPECT_TRUE(parse("[[fruits]]\n[[fruits.tags]]\n[fruits.tags]\n"_str).is_err());
    EXPECT_TRUE(parse("[a.b.c]\nvalue = 1\n[a]\nb.c.other = 2\n"_str).is_err());
    EXPECT_TRUE(parse("[[a.b]]\nvalue = 1\n[a]\nb.other = 2\n"_str).is_err());
}

TEST(TomlParser, EnforcesInputValueAndDepthLimits) {
    auto input_options            = rstd::toml::ParseOptions {};
    input_options.max_input_bytes = usize(3);
    auto input                    = rstd::toml::from_str("key = 1\n"_str, input_options);
    ASSERT_TRUE(input.is_err());
    EXPECT_TRUE(input.unwrap_err().is_limit());

    auto value_options       = rstd::toml::ParseOptions {};
    value_options.max_values = usize(2);
    auto values              = rstd::toml::from_str("values = [1, 2]\n"_str, value_options);
    ASSERT_TRUE(values.is_err());
    EXPECT_TRUE(values.unwrap_err().is_limit());

    auto depth_options      = rstd::toml::ParseOptions {};
    depth_options.max_depth = u8(1);
    auto depth              = rstd::toml::from_str("values = [[]]\n"_str, depth_options);
    ASSERT_TRUE(depth.is_err());
    EXPECT_TRUE(depth.unwrap_err().is_limit());
}

TEST(TomlParser, ParsesStandaloneKeysValuesAndAssignments) {
    auto key =
        rstd::toml::parse_key_path(R"(patch."https://example.com/source?left=right".path)"_str);
    ASSERT_TRUE(key.is_ok());
    ASSERT_EQ(key->len(), usize(3));
    EXPECT_EQ((*key)[usize()].as_str(), "patch"_str);
    EXPECT_EQ((*key)[usize(1)].as_str(), "https://example.com/source?left=right"_str);
    EXPECT_EQ((*key)[usize(2)].as_str(), "path"_str);

    auto value = rstd::toml::parse_value(R"([true, 7, { name = "demo" }])"_str);
    ASSERT_TRUE(value.is_ok());
    ASSERT_TRUE(value->is_array());
    EXPECT_EQ((**value->as_array()).len(), usize(3));

    auto assignment = rstd::toml::parse_assignment(
        R"(patch."https://example.com/source?left=right".path = "../source")"_str);
    ASSERT_TRUE(assignment.is_ok());
    ASSERT_EQ(assignment->key.len(), usize(3));
    EXPECT_EQ(assignment->key[usize(1)].as_str(), "https://example.com/source?left=right"_str);
    EXPECT_EQ(assignment->value.as_str(), Some("../source"_str));

    auto assignment_text = rstd::toml::parse_assignment_text(
        R"(patch."https://example.com/source?left=right".path = ../source)"_str);
    ASSERT_TRUE(assignment_text.is_ok());
    EXPECT_EQ(assignment_text->key[usize(1)].as_str(), "https://example.com/source?left=right"_str);
    EXPECT_EQ(assignment_text->value.as_str(), "../source"_str);

    EXPECT_TRUE(rstd::toml::parse_key_path("key trailing"_str).is_err());
    EXPECT_TRUE(rstd::toml::parse_value("true false"_str).is_err());
    EXPECT_TRUE(rstd::toml::parse_assignment("key = 1\nother = 2"_str).is_err());
    EXPECT_TRUE(rstd::toml::parse_assignment_text("key = "_str).is_err());
}
