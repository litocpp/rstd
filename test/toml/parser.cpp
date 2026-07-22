#include <gtest/gtest.h>

import rstd.toml;

using namespace rstd::prelude;
using rstd::toml::Value;

namespace
{

auto parse(const char* input) {
    return rstd::toml::from_str(rstd::ref<rstd::str>(input));
}

auto member(const Value& value, const char* key) -> const Value& {
    auto found = value.get(rstd::ref<rstd::str>(key));
    EXPECT_TRUE(found.is_some()) << key;
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
)");
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    const auto& package = member(document, "package");
    EXPECT_EQ(*member(package, "name").as_str(), rstd::ref<rstd::str>("demo-base"));

    const auto& library = member(document, "lib");
    EXPECT_EQ(*member(library, "name").as_str(), rstd::ref<rstd::str>("demo.base"));
    auto sources = member(library, "sources").as_array();
    ASSERT_TRUE(sources.is_some());
    ASSERT_EQ((**sources).len(), usize(3));
    EXPECT_EQ(*(**sources)[usize(1)].as_str(), rstd::ref<rstd::str>("src/detail.cppm"));

    const auto& dependency = member(member(document, "dependencies"), "support");
    EXPECT_EQ(*member(dependency, "path").as_str(), rstd::ref<rstd::str>("../support"));
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
)");
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    EXPECT_EQ(member(document, "count").as_integer(), Some(i64(-42)));
    EXPECT_EQ(member(document, "ratio").as_float(), Some(f64(125)));
    EXPECT_EQ(member(document, "mask").as_integer(), Some(i64(255)));
    EXPECT_TRUE(member(document, "when").is_offset_datetime());
    EXPECT_EQ(*member(member(document, "owner"), "name").as_str(), rstd::ref<rstd::str>("Tenon"));

    auto targets = member(document, "target").as_array();
    ASSERT_TRUE(targets.is_some());
    ASSERT_EQ((**targets).len(), usize(2));
    EXPECT_EQ(*member((**targets)[usize(1)], "name").as_str(), rstd::ref<rstd::str>("second"));
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
)");
    if (result.is_err()) {
        auto parse_error = result.unwrap_err();
        FAIL() << "line " << parse_error.line().to_primitive() << " column "
               << parse_error.column().to_primitive() << " byte "
               << parse_error.offset().to_primitive();
    }
    auto document = result.unwrap();

    EXPECT_EQ(*member(document, "escaped").as_str(), rstd::ref<rstd::str>("line\n你好"));
    EXPECT_EQ(*member(document, "literal").as_str(), rstd::ref<rstd::str>(R"(C:\tools\tenon)"));
    EXPECT_EQ(*member(document, "multiline").as_str(), rstd::ref<rstd::str>("first second\n"));
    EXPECT_TRUE(member(document, "date").is_local_date());
    EXPECT_TRUE(member(document, "time").is_local_time());
    EXPECT_TRUE(member(document, "local").is_local_datetime());
    EXPECT_TRUE(member(document, "offset").is_offset_datetime());
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
)TOML");
    ASSERT_TRUE(result.is_ok());
    auto document = result.unwrap();

    EXPECT_EQ(*member(document, "escaped").as_str(),
              rstd::ref<rstd::str>("\x1b"
                                   "A"));
    EXPECT_EQ(member(document, "exponent").as_float(), Some(f64(3)));
    EXPECT_EQ(member(document, "hex").as_integer(), Some(i64(3735928559)));
    auto time = member(document, "time").as_local_time();
    ASSERT_TRUE(time.is_some());
    EXPECT_EQ(time->second, rstd::uint8_t {});
    EXPECT_TRUE(member(document, "local").is_local_datetime());
    EXPECT_EQ(*member(document, "quotes").as_str(), rstd::ref<rstd::str>("two quotes: \"\""));
    EXPECT_EQ(*member(member(document, "inline"), "name").as_str(), rstd::ref<rstd::str>("demo"));
}

TEST(TomlParser, RejectsDuplicateAndMalformedInput) {
    EXPECT_TRUE(parse("name = 1\nname = 2\n").is_err());
    EXPECT_TRUE(parse("[package]\n[package]\n").is_err());
    EXPECT_TRUE(parse("value = 01\n").is_err());
    EXPECT_TRUE(parse("value = [1 2]\n").is_err());
    EXPECT_TRUE(parse("value = \"\\q\"\n").is_err());
    EXPECT_TRUE(parse("value = +0x1\n").is_err());
    EXPECT_TRUE(parse("value = 1e_2\n").is_err());
    EXPECT_TRUE(parse("value = \"\x7f\"\n").is_err());
    EXPECT_TRUE(parse("value = 1 # \x7f\n").is_err());
    EXPECT_TRUE(parse("value = \"\"\"six quotes: \"\"\"\"\"\"\n").is_err());

    auto trailing = parse("name = 1 unexpected\n");
    ASSERT_TRUE(trailing.is_err());
    EXPECT_TRUE(trailing.unwrap_err().is_syntax());
    EXPECT_EQ(trailing.unwrap_err().line(), usize(1));
}

TEST(TomlParser, SliceAndFromStrTraitReuseDecoder) {
    const rstd::u8 input[] = { u8('x'), u8(' '), u8('='), u8(' '), u8('1'), u8('\n') };
    auto           from_slice =
        rstd::toml::from_slice(rstd::slice<rstd::u8>::from_raw_parts(input, usize(sizeof(input))));
    auto from_trait = rstd::from_str<Value>("x = 1\n");

    ASSERT_TRUE(from_slice.is_ok());
    ASSERT_TRUE(from_trait.is_ok());
    EXPECT_EQ(*from_slice, *from_trait);

    const rstd::u8 invalid[]      = { u8(0xff) };
    auto           invalid_result = rstd::toml::from_slice(
        rstd::slice<rstd::u8>::from_raw_parts(invalid, usize(sizeof(invalid))));
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
)");
    ASSERT_TRUE(result.is_ok());
    auto document = result.unwrap();

    const auto& service = member(document, "service");
    EXPECT_EQ(member(service, "enabled").as_bool(), Some(true));
    EXPECT_EQ(member(member(member(service, "api"), "http"), "port").as_integer(), Some(i64(8080)));

    const auto& apple = member(member(document, "fruit"), "apple");
    EXPECT_EQ(*member(apple, "color").as_str(), rstd::ref<rstd::str>("red"));
    EXPECT_EQ(member(member(apple, "taste"), "sweet").as_bool(), Some(true));
    EXPECT_EQ(member(member(apple, "texture"), "smooth").as_bool(), Some(true));

    auto products = member(document, "products").as_array();
    ASSERT_TRUE(products.is_some());
    ASSERT_EQ((**products).len(), usize(2));
    EXPECT_EQ(*member((**products)[usize(1)], "name").as_str(), rstd::ref<rstd::str>("nail"));
    auto tags = member((**products)[usize(1)], "tags").as_array();
    ASSERT_TRUE(tags.is_some());
    ASSERT_EQ((**tags).len(), usize(1));
    EXPECT_EQ(*member((**tags)[usize()], "name").as_str(), rstd::ref<rstd::str>("small"));
}

TEST(TomlParser, RejectsSealedAndRedefinedTableKinds) {
    EXPECT_TRUE(parse("fruit.apple.color = \"red\"\n[fruit.apple]\n").is_err());
    EXPECT_TRUE(parse("[product]\ntype = { name = \"Nail\" }\ntype.edible = false\n").is_err());
    EXPECT_TRUE(parse("[product]\ntype.name = \"Nail\"\ntype = { edible = false }\n").is_err());
    EXPECT_TRUE(parse("[[fruits]]\n[fruits]\n").is_err());
    EXPECT_TRUE(parse("[fruits]\n[[fruits]]\n").is_err());
    EXPECT_TRUE(parse("fruits = []\n[[fruits]]\n").is_err());
    EXPECT_TRUE(parse("[fruit.physical]\ncolor = \"red\"\n[[fruit]]\n").is_err());
    EXPECT_TRUE(parse("[[fruits]]\n[[fruits.tags]]\n[fruits.tags]\n").is_err());
    EXPECT_TRUE(parse("[a.b.c]\nvalue = 1\n[a]\nb.c.other = 2\n").is_err());
    EXPECT_TRUE(parse("[[a.b]]\nvalue = 1\n[a]\nb.other = 2\n").is_err());
}

TEST(TomlParser, EnforcesInputValueAndDepthLimits) {
    auto input_options            = rstd::toml::ParseOptions {};
    input_options.max_input_bytes = usize(3);
    auto input                    = rstd::toml::from_str("key = 1\n", input_options);
    ASSERT_TRUE(input.is_err());
    EXPECT_TRUE(input.unwrap_err().is_limit());

    auto value_options       = rstd::toml::ParseOptions {};
    value_options.max_values = usize(2);
    auto values              = rstd::toml::from_str("values = [1, 2]\n", value_options);
    ASSERT_TRUE(values.is_err());
    EXPECT_TRUE(values.unwrap_err().is_limit());

    auto depth_options      = rstd::toml::ParseOptions {};
    depth_options.max_depth = u8(1);
    auto depth              = rstd::toml::from_str("values = [[]]\n", depth_options);
    ASSERT_TRUE(depth.is_err());
    EXPECT_TRUE(depth.unwrap_err().is_limit());
}
