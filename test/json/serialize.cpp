#include <gtest/gtest.h>

import rstd.json;

using namespace rstd;
using namespace rstd::json;

namespace
{

auto text(const ::alloc::string::String& value) -> std::string {
    return { reinterpret_cast<const char*>(value.as_raw_ptr()), value.len() };
}

} // namespace

TEST(JsonSerialize, WritesCompactSortedJson) {
    auto value = from_str(R"({"z":[true,null,"line\n"],"a":1.0})").unwrap();
    EXPECT_EQ(text(to_string(value)), R"({"a":1.0,"z":[true,null,"line\n"]})");
}

TEST(JsonSerialize, WritesPrettyJsonWithRequestedIndent) {
    auto value  = from_str(R"({"z":[true,null],"a":1})").unwrap();
    auto output = to_string(value, FormatOptions { .pretty = true, .indent = 4 });
    EXPECT_EQ(text(output),
              "{\n"
              "    \"a\": 1,\n"
              "    \"z\": [\n"
              "        true,\n"
              "        null\n"
              "    ]\n"
              "}");
}

TEST(JsonSerialize, EscapesStringsAndPreservesUtf8) {
    auto value =
        from_str(R"({"text":"quote:\" slash:/ backslash:\\ controls:\b\f\n\r\t\u0001 utf8:雪"})")
            .unwrap();
    EXPECT_EQ(text(to_string(value)),
              R"({"text":"quote:\" slash:/ backslash:\\ controls:\b\f\n\r\t\u0001 utf8:雪"})");
}

TEST(JsonSerialize, FormatsNumberKindsAndRoundTrips) {
    auto value =
        from_str(R"([0,-1,18446744073709551615,-0,1.0,1.25,5e-324,1.7976931348623157e308])")
            .unwrap();
    auto output = to_string(value);
    EXPECT_EQ(text(output),
              R"([0,-1,18446744073709551615,-0.0,1.0,1.25,5e-324,1.7976931348623157e+308])");
    EXPECT_EQ(from_str(output.as_str()).unwrap(), value);
}

TEST(JsonSerialize, DisplayUsesTheSameEmitter) {
    auto value = from_str(R"({"a":[1]})").unwrap();
    EXPECT_EQ(text(rstd::format("{}", value)), R"({"a":[1]})");
    EXPECT_EQ(text(rstd::format("{:#}", value)), "{\n  \"a\": [\n    1\n  ]\n}");
}
